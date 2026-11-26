/*
 * anybind: modern C++ bindings for every Node-API runtime.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifndef NAPI_VERSION
#define NAPI_VERSION 8
#endif
#include <node_api.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#define ANYBIND_VERSION_MAJOR 0
#define ANYBIND_VERSION_MINOR 0
#define ANYBIND_VERSION_PATCH 1

namespace anybind {

/**
 * Thrown when a JavaScript exception is already pending.
 * Unwinds back to the JS boundary without raising a second error.
 */
class error_already_set : public std::exception {
 public:
  const char* what() const noexcept override { return "a JavaScript exception is pending"; }
};

/** Throw this from bound code to raise a JavaScript TypeError. */
class type_error : public std::exception {
 public:
  explicit type_error(std::string message) : message_(std::move(message)) {}
  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

/** Named argument paired with a default value (created via `arg("x") = value`). */
template <typename T>
struct arg_v {
  const char* name;
  T value;
};

/** Names a function parameter in `module_::def`, optionally with a default value. */
struct arg {
  const char* name;

  constexpr explicit arg(const char* name) : name(name) {}

  template <typename T>
  arg_v<std::decay_t<T>> operator=(T&& value) const {
    return arg_v<std::decay_t<T>>{name, std::forward<T>(value)};
  }
};

namespace literals {
inline arg operator""_a(const char* name, std::size_t) { return arg(name); }
}  // namespace literals

class module_;

namespace detail {

template <typename T>
using intrinsic_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename... T>
struct type_list {};

inline void check(napi_env env, napi_status status) {
  if (status == napi_ok) return;
  // Must be read before any other Node-API call overwrites it.
  const napi_extended_error_info* info = nullptr;
  napi_get_last_error_info(env, &info);
  std::string message = info && info->error_message ? info->error_message : "Node-API call failed";
  bool pending = false;
  napi_is_exception_pending(env, &pending);
  if (pending) throw error_already_set();
  throw std::runtime_error(message);
}

inline void expect_type(napi_env env, napi_value value, napi_valuetype expected,
                        std::string_view ts_name) {
  napi_valuetype actual;
  check(env, napi_typeof(env, value, &actual));
  if (actual != expected) throw type_error("expected " + std::string(ts_name));
}

/* === Type casters: C++ <-> JS conversion plus TypeScript names for stubs === */

template <typename T, typename SFINAE = void>
struct type_caster {
  static_assert(sizeof(T) == 0, "anybind: no type_caster is defined for this type");
};

template <>
struct type_caster<bool> {
  static constexpr std::string_view ts_name = "boolean";
  static bool from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_boolean, ts_name);
    bool result;
    check(env, napi_get_value_bool(env, value, &result));
    return result;
  }
  static napi_value to_js(napi_env env, bool value) {
    napi_value result;
    check(env, napi_get_boolean(env, value, &result));
    return result;
  }
};

template <typename T>
struct type_caster<T, std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>> {
  static constexpr std::string_view ts_name = "number";
  static T from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_number, ts_name);
    double d;
    check(env, napi_get_value_double(env, value, &d));
    if constexpr (std::is_integral_v<T>) {
      // Valid range is [lo, hi); NaN fails every comparison.
      constexpr int digits = std::numeric_limits<T>::digits;
      const double hi = std::ldexp(1.0, digits);
      const double lo = std::is_signed_v<T> ? -hi : 0.0;
      if (!(d >= lo && d < hi) || std::trunc(d) != d) {
        throw type_error("expected an integer in the range of the C++ parameter type");
      }
    } else if constexpr (static_cast<long double>(std::numeric_limits<T>::max()) <
                         static_cast<long double>(std::numeric_limits<double>::max())) {
      // Narrowing a finite double outside T's range is undefined behavior.
      // Rounding within range is fine; inf and NaN convert as-is.
      if (std::isfinite(d) && std::fabs(d) > static_cast<double>(std::numeric_limits<T>::max())) {
        throw type_error("expected a number in the range of the C++ parameter type");
      }
    }
    return static_cast<T>(d);
  }
  static napi_value to_js(napi_env env, T value) {
    napi_value result;
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T> && sizeof(T) <= 4) {
      check(env, napi_create_int32(env, static_cast<int32_t>(value), &result));
    } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= 4) {
      check(env, napi_create_uint32(env, static_cast<uint32_t>(value), &result));
    } else {
      // TODO: map 64-bit integers outside +-2^53 to bigint.
      check(env, napi_create_double(env, static_cast<double>(value), &result));
    }
    return result;
  }
};

template <>
struct type_caster<std::string> {
  static constexpr std::string_view ts_name = "string";
  static std::string from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_string, ts_name);
    size_t length = 0;
    check(env, napi_get_value_string_utf8(env, value, nullptr, 0, &length));
    std::string result(length, '\0');
    check(env, napi_get_value_string_utf8(env, value, result.data(), length + 1, &length));
    return result;
  }
  static napi_value to_js(napi_env env, const std::string& value) {
    napi_value result;
    check(env, napi_create_string_utf8(env, value.data(), value.size(), &result));
    return result;
  }
};

template <typename T>
constexpr std::string_view ts_name_of() {
  if constexpr (std::is_void_v<T>) {
    return "void";
  } else {
    return type_caster<intrinsic_t<T>>::ts_name;
  }
}

/* === Callable introspection === */

template <typename F>
struct function_traits : function_traits<decltype(&F::operator())> {};

template <typename R, typename... A>
struct function_traits<R (*)(A...)> {
  using return_type = R;
  using args = type_list<A...>;
  static constexpr size_t arity = sizeof...(A);
};
template <typename R, typename... A>
struct function_traits<R (*)(A...) noexcept> : function_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct function_traits<R (C::*)(A...)> : function_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct function_traits<R (C::*)(A...) const> : function_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct function_traits<R (C::*)(A...) noexcept> : function_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct function_traits<R (C::*)(A...) const noexcept> : function_traits<R (*)(A...)> {};

/* === Argument specs === */

template <typename T>
struct is_arg : std::false_type {};
template <>
struct is_arg<arg> : std::true_type {};
template <typename T>
struct is_arg<arg_v<T>> : std::true_type {};

template <typename T>
struct arg_slot {
  std::string name;
  std::optional<T> default_value;
};

/*
 * Whether arithmetic value `v` survives conversion to `T` without being
 * truncated or overflowing. Every comparison is ordered so that no step of
 * the check itself performs an out-of-range (undefined) conversion.
 */
template <typename T, typename V>
bool arithmetic_value_fits(V v) {
  using LT = std::numeric_limits<T>;
  using LV = std::numeric_limits<V>;
  if constexpr (std::is_integral_v<T> && std::is_integral_v<V>) {
    // Ranges that trivially fit are skipped to avoid always-true comparisons.
    if constexpr (std::is_signed_v<V> && !std::is_signed_v<T>) {
      if (v < 0) return false;
      if constexpr (LV::digits <= LT::digits) {
        return true;
      } else {
        return static_cast<std::make_unsigned_t<V>>(v) <= LT::max();
      }
    } else if constexpr (LV::digits <= LT::digits) {
      return true;
    } else if constexpr (!std::is_signed_v<V> && std::is_signed_v<T>) {
      return v <= static_cast<std::make_unsigned_t<T>>(LT::max());
    } else if constexpr (std::is_signed_v<V>) {
      return v >= LT::min() && v <= LT::max();
    } else {
      return v <= LT::max();
    }
  } else if constexpr (std::is_integral_v<T>) {
    // Floating-point value into an integer: must be integral and in [lo, hi).
    const double d = static_cast<double>(v);
    const double hi = std::ldexp(1.0, LT::digits);
    const double lo = std::is_signed_v<T> ? -hi : 0.0;
    return d >= lo && d < hi && std::trunc(d) == d;
  } else if constexpr (std::is_integral_v<V>) {
    // Integer into a floating-point type: must be exactly representable.
    // Every integer with at most LT::digits significant bits is exact.
    if constexpr (LV::digits <= LT::digits) {
      return true;
    } else {
      using wide = std::conditional_t<std::is_signed_v<V>, std::intmax_t, std::uintmax_t>;
      const wide limit = wide{1} << LT::digits;  // LT::digits < LV::digits, so no overflow
      const wide w = static_cast<wide>(v);
      if constexpr (std::is_signed_v<V>) {
        return w >= -limit && w <= limit;
      } else {
        return w <= limit;
      }
    }
  } else {
    // Floating-point into floating-point: rounding is fine, overflow is not.
    if (!std::isfinite(v)) return true;
    return std::fabs(static_cast<long double>(v)) <= static_cast<long double>(LT::max());
  }
}

template <typename T, typename V>
void check_default_value([[maybe_unused]] const char* name, [[maybe_unused]] const V& value) {
  if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<V>) {
    if (!arithmetic_value_fits<T>(value)) {
      throw std::invalid_argument(std::string("anybind::arg(\"") + (name ? name : "") +
                                  "\"): default value does not fit the parameter type");
    }
  }
}

template <typename T>
arg_slot<T> make_arg_slot(const arg& a) {
  return arg_slot<T>{a.name ? a.name : "", std::nullopt};
}

template <typename T, typename V>
arg_slot<T> make_arg_slot(arg_v<V>&& a) {
  static_assert(std::is_convertible_v<V&&, T>,
                "anybind::arg: default value is not convertible to the parameter type");
  check_default_value<T>(a.name, a.value);
  return arg_slot<T>{a.name ? a.name : "", std::optional<T>(std::in_place, std::move(a.value))};
}

template <typename T, typename V>
arg_slot<T> make_arg_slot(const arg_v<V>& a) {
  static_assert(std::is_convertible_v<const V&, T>,
                "anybind::arg: default value is not convertible to the parameter type");
  check_default_value<T>(a.name, a.value);
  return arg_slot<T>{a.name ? a.name : "", std::optional<T>(std::in_place, a.value)};
}

/* Overload 1: no arg() specs passed (backward compatible). */
template <typename... A>
std::tuple<arg_slot<intrinsic_t<A>>...> make_arg_slots(type_list<A...>) {
  return {};
}

/* Overload 2: one arg() spec per parameter. */
template <typename FirstA, typename... RestA, typename FirstExtra, typename... RestExtra>
std::tuple<arg_slot<intrinsic_t<FirstA>>, arg_slot<intrinsic_t<RestA>>...> make_arg_slots(
    type_list<FirstA, RestA...>, FirstExtra&& first, RestExtra&&... rest) {
  return {make_arg_slot<intrinsic_t<FirstA>>(std::forward<FirstExtra>(first)),
          make_arg_slot<intrinsic_t<RestA>>(std::forward<RestExtra>(rest))...};
}

template <typename List>
struct slots_for_list;

template <typename... A>
struct slots_for_list<type_list<A...>> {
  using type = std::tuple<arg_slot<intrinsic_t<A>>...>;
};

template <typename SlotsTuple, size_t... I>
size_t compute_min_args([[maybe_unused]] const SlotsTuple& slots, std::index_sequence<I...>) {
  size_t min_args = 0;
  [[maybe_unused]] auto check_one = [&](size_t idx, const auto& slot) {
    if (!slot.default_value.has_value()) min_args = idx + 1;
  };
  (check_one(I, std::get<I>(slots)), ...);
  return min_args;
}

/* === Calling C++ from JS === */

/* Runs `fn` and translates any C++ exception into a pending JS exception. */
template <typename Fn>
napi_value guard(napi_env env, Fn&& fn) noexcept {
  try {
    return fn();
  } catch (const error_already_set&) {
    // A JS exception is already pending; let it propagate.
  } catch (const type_error& e) {
    napi_throw_type_error(env, nullptr, e.what());
  } catch (const std::exception& e) {
    napi_throw_error(env, nullptr, e.what());
  } catch (...) {
    napi_throw_error(env, nullptr, "unknown C++ exception");
  }
  return nullptr;
}

template <typename T>
T load_arg(napi_env env, napi_value* argv, size_t argc, size_t index, const arg_slot<T>& slot) {
  if (slot.default_value.has_value()) {
    // Missing trailing argument or explicit `undefined` falls back to default.
    if (index >= argc) return *slot.default_value;
    napi_valuetype actual;
    check(env, napi_typeof(env, argv[index], &actual));
    if (actual == napi_undefined) return *slot.default_value;
  }
  try {
    return type_caster<T>::from_js(env, argv[index]);
  } catch (const type_error& e) {
    std::string prefix = "argument " + std::to_string(index + 1);
    if (!slot.name.empty()) prefix += " (" + slot.name + ")";
    throw type_error(prefix + ": " + e.what());
  }
}

template <typename R, typename F, typename SlotsTuple, typename... A, size_t... I>
napi_value call(napi_env env, F& f, [[maybe_unused]] napi_value* argv, [[maybe_unused]] size_t argc,
                [[maybe_unused]] const SlotsTuple& slots, type_list<A...>,
                std::index_sequence<I...>) {
  // Braced initialization guarantees left-to-right argument conversion.
  [[maybe_unused]] std::tuple<intrinsic_t<A>...> values{
      load_arg<intrinsic_t<A>>(env, argv, argc, I, std::get<I>(slots))...};
  if constexpr (std::is_void_v<R>) {
    f(std::get<I>(values)...);
    napi_value undefined;
    check(env, napi_get_undefined(env, &undefined));
    return undefined;
  } else {
    return type_caster<intrinsic_t<R>>::to_js(env, f(std::get<I>(values)...));
  }
}

template <typename Func>
struct function_record {
  using traits = function_traits<Func>;
  using slots_t = typename slots_for_list<typename traits::args>::type;

  Func f;
  slots_t slots;
  size_t min_args;

  static napi_value invoke(napi_env env, napi_callback_info info) {
    return guard(env, [&]() -> napi_value {
      constexpr size_t arity = traits::arity;
      size_t argc = arity;
      napi_value argv[arity > 0 ? arity : 1];
      void* data = nullptr;
      check(env, napi_get_cb_info(env, info, &argc, argv, nullptr, &data));
      auto* record = static_cast<function_record*>(data);
      if (argc < record->min_args || argc > arity) {
        std::string expected = record->min_args == arity ? std::to_string(arity)
                                                         : std::to_string(record->min_args) +
                                                               " to " + std::to_string(arity);
        throw type_error("expected " + expected + " argument(s), got " + std::to_string(argc));
      }
      return call<typename traits::return_type>(env, record->f, argv, argc, record->slots,
                                                typename traits::args{},
                                                std::make_index_sequence<arity>{});
    });
  }

  static void finalize(napi_env, void* data, void*) { delete static_cast<function_record*>(data); }
};

/*
 * Turns a C++-side parameter name into a valid TypeScript parameter name.
 * Only the generated declarations are affected; JS calls are positional.
 */
inline std::string ts_param_name(std::string_view name, size_t index) {
  if (name.empty()) return "arg" + std::to_string(index);
  std::string out;
  out.reserve(name.size() + 1);
  for (char c : name) {
    const auto u = static_cast<unsigned char>(c);
    // Bytes >= 0x80 are kept so UTF-8 identifiers survive unchanged.
    const bool ok = u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '$';
    out += ok ? c : '_';
  }
  if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
  // Words TypeScript rejects as parameter names, plus `this`, which TS would
  // silently treat as a `this` parameter and drop from the arity.
  static constexpr std::string_view reserved[] = {
      "break",  "case",     "catch",  "class",  "const",  "continue",   "debugger", "default",
      "delete", "do",       "else",   "enum",   "export", "extends",    "false",    "finally",
      "for",    "function", "if",     "import", "in",     "instanceof", "new",      "null",
      "return", "super",    "switch", "this",   "throw",  "true",       "try",      "typeof",
      "var",    "void",     "while",  "with"};
  for (std::string_view word : reserved) {
    if (out == word) {
      out += '_';
      break;
    }
  }
  return out;
}

template <typename R, typename... A, typename SlotsTuple, size_t... I>
std::string ts_signature(const char* name, type_list<A...>,
                         [[maybe_unused]] const SlotsTuple& slots, [[maybe_unused]] size_t min_args,
                         std::index_sequence<I...>) {
  std::string s = "export function ";
  s += name;
  s += '(';
  [[maybe_unused]] auto append_param = [&](size_t i, const auto& slot, std::string_view type_name) {
    if (i > 0) s += ", ";
    s += ts_param_name(slot.name, i);
    if (i >= min_args) {
      // Trailing optional parameter.
      s += "?: ";
      s += type_name;
    } else if (slot.default_value.has_value()) {
      // Non-trailing default: TS forbids `?` before a required parameter.
      s += ": ";
      s += type_name;
      s += " | undefined";
    } else {
      s += ": ";
      s += type_name;
    }
  };
  (append_param(I, std::get<I>(slots), ts_name_of<A>()), ...);
  s += "): ";
  s += ts_name_of<R>();
  s += ";\n";
  return s;
}

}  // namespace detail

/** Handle to the module being initialized inside ANYBIND_MODULE. */
class module_ {
 public:
  module_(napi_env env, napi_value exports) : env_(env), exports_(exports) {}

  /** Binds a free function, function pointer or (capturing) lambda. */
  template <typename Func, typename... Extra>
  module_& def(const char* name, Func&& f, Extra&&... extra) {
    using F = std::decay_t<Func>;
    using record_t = detail::function_record<F>;
    using traits = detail::function_traits<F>;
    using indices = std::make_index_sequence<traits::arity>;

    static_assert((detail::is_arg<std::decay_t<Extra>>::value && ...),
                  "anybind::def: extra arguments must be anybind::arg");
    static_assert(sizeof...(Extra) == 0 || sizeof...(Extra) == traits::arity,
                  "anybind::def: number of anybind::arg specs must match function arity");

    auto slots = detail::make_arg_slots(typename traits::args{}, std::forward<Extra>(extra)...);
    size_t min_args = detail::compute_min_args(slots, indices{});
    std::string sig = detail::ts_signature<typename traits::return_type>(
        name, typename traits::args{}, slots, min_args, indices{});

    auto* record = new record_t{std::forward<Func>(f), std::move(slots), min_args};
    napi_value fn;
    napi_status status =
        napi_create_function(env_, name, NAPI_AUTO_LENGTH, &record_t::invoke, record, &fn);
    if (status == napi_ok) {
      status = napi_add_finalizer(env_, fn, record, &record_t::finalize, nullptr, nullptr);
    }
    if (status != napi_ok) {
      delete record;
      detail::check(env_, status);
    }
    detail::check(env_, napi_set_named_property(env_, exports_, name, fn));
    dts_ += sig;
    return *this;
  }

  napi_env env() const { return env_; }
  napi_value exports() const { return exports_; }

  /** TypeScript declarations for everything bound so far. */
  const std::string& dts() const { return dts_; }

 private:
  napi_env env_;
  napi_value exports_;
  std::string dts_;
};

namespace detail {

inline napi_value init_module(napi_env env, napi_value exports, void (*init)(module_&)) {
  return guard(env, [&]() -> napi_value {
    module_ m(env, exports);
    init(m);
    // Hidden (non-enumerable) property read by `anybind-stubgen`.
    napi_value dts;
    check(env, napi_create_string_utf8(env, m.dts().data(), m.dts().size(), &dts));
    napi_property_descriptor desc{"__anybind_dts__", nullptr, nullptr,      nullptr,
                                  nullptr,           dts,     napi_default, nullptr};
    check(env, napi_define_properties(env, exports, 1, &desc));
    return exports;
  });
}

}  // namespace detail
}  // namespace anybind

/**
 * Defines the entry point of an anybind extension.
 *
 *  ANYBIND_MODULE(my_addon, m) {
 *    m.def("add", [](int a, int b) { return a + b; });
 *  }
 */
#define ANYBIND_MODULE(name, variable)                                         \
  static void anybind_init_##name(::anybind::module_&);                        \
  NAPI_MODULE_INIT() {                                                         \
    return ::anybind::detail::init_module(env, exports, &anybind_init_##name); \
  }                                                                            \
  static void anybind_init_##name(::anybind::module_& variable)
