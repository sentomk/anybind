/*
 * anybind: modern C++ bindings for every Node-API runtime.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifndef NAPI_VERSION
#define NAPI_VERSION 8
#endif
#include <node_api.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
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
inline arg operator""_a(const char* name, size_t) { return arg(name); }
}  // namespace literals

/* === TypeScript type descriptors === */

namespace ts {

struct void_ {};
struct boolean {};
struct number {};
struct bigint {};
struct string {};
struct null {};
struct undefined {};
struct unknown {};
struct never {};

/**
 * Literal type: supports `bool`, integral types, and `const char*` pointing to
 * a namespace-scope `constexpr char[]` (C++17 NTTP).
 */
template <auto V>
struct literal {
  static constexpr auto value = V;
};

template <typename T>
struct array {};

template <typename... Ts>
struct tuple {};

template <typename... Ts>
struct union_ {};

template <typename K, typename V>
struct record {};

template <const char* Name, typename Type, bool Optional = false>
struct field {
  static constexpr const char* name = Name;
  using type = Type;
  static constexpr bool optional = Optional;
};

template <typename... Fields>
struct object {};

template <typename Sig>
struct function;

template <typename R, typename... Args>
struct function<R(Args...)> {};

/** Placeholder for a bound C++ class `T`; resolved via the runtime registry. */
template <typename T>
struct bound {};

/** Type whose TypeScript representation differs in parameter vs return position. */
template <typename In, typename Out>
struct io {};

}  // namespace ts

class module_;

namespace detail {

template <typename T>
using intrinsic_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename... T>
struct type_list {};

template <typename T>
struct type_tag {
  using type = T;
};

template <typename T>
inline const void* type_id() noexcept {
  static const char tag = 0;
  return &tag;
}

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

/* === Type casters: C++ <-> JS conversion plus TypeScript descriptors === */

template <typename T, typename SFINAE = void>
struct type_caster {
  static_assert(sizeof(T) == 0, "anybind: no type_caster is defined for this type");
};

template <typename T, typename = void>
struct ts_of_impl {
  using type = typename type_caster<intrinsic_t<T>>::ts;
};

template <typename T>
struct ts_of_impl<T, std::enable_if_t<std::is_void_v<T>>> {
  using type = anybind::ts::void_;
};

template <typename T>
using ts_of = typename ts_of_impl<T>::type;

template <>
struct type_caster<bool> {
  using ts = anybind::ts::boolean;
  static bool from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_boolean, "boolean");
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
  using ts = anybind::ts::number;
  static T from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_number, "number");
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
  using ts = anybind::ts::string;
  static std::string from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_string, "string");
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

/* === TypeScript runtime serializer === */

enum class ts_dir { in, out };

constexpr ts_dir flip_dir(ts_dir d) noexcept { return d == ts_dir::in ? ts_dir::out : ts_dir::in; }

struct ts_context {
  std::string (*resolve_bound)(void* user_data, const void* id) = nullptr;
  void* user_data = nullptr;
};

template <typename Node, ts_dir Dir>
struct ts_resolve {
  using type = Node;
};

template <typename In, typename Out>
struct ts_resolve<anybind::ts::io<In, Out>, ts_dir::in> : ts_resolve<In, ts_dir::in> {};

template <typename In, typename Out>
struct ts_resolve<anybind::ts::io<In, Out>, ts_dir::out> : ts_resolve<Out, ts_dir::out> {};

template <typename T, ts_dir Dir>
struct ts_resolve<anybind::ts::union_<T>, Dir> : ts_resolve<T, Dir> {};

template <typename Node, ts_dir Dir>
using ts_resolve_t = typename ts_resolve<Node, Dir>::type;

/*
 * TypeScript type operator precedence (higher binds tighter):
 *   0: function  `(...) => R`
 *   1: union     `A | B`
 *   2: primary   primitives, literals, arrays, tuples, records, objects, bound classes
 */
template <typename Node>
struct ts_prec : std::integral_constant<int, 2> {};

template <typename R, typename... Args>
struct ts_prec<anybind::ts::function<R(Args...)>> : std::integral_constant<int, 0> {};

template <typename... Ts>
struct ts_prec<anybind::ts::union_<Ts...>>
    : std::integral_constant<int, (sizeof...(Ts) > 1 ? 1 : 2)> {};

inline void ts_append_quoted(std::string& out, std::string_view s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  out += '"';
}

inline bool ts_is_ident(std::string_view s) {
  if (s.empty()) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
    const bool digit = c >= '0' && c <= '9';
    if (i == 0 ? !letter : !(letter || digit)) return false;
  }
  return true;
}

template <typename Node>
struct ts_serializer {
  static_assert(sizeof(Node) == 0, "anybind: unsupported TypeScript type descriptor");
};

template <typename Node, ts_dir Dir, int MinPrec>
void ts_write_wrapped(std::string& out, const ts_context& ctx) {
  using resolved = ts_resolve_t<Node, Dir>;
  constexpr bool parens = ts_prec<resolved>::value < MinPrec;
  if constexpr (parens) out += '(';
  ts_serializer<resolved>::template write<Dir>(out, ctx);
  if constexpr (parens) out += ')';
}

#define ANYBIND_TS_PRIMITIVE(node_type, text)                \
  template <>                                                \
  struct ts_serializer<anybind::ts::node_type> {             \
    template <ts_dir>                                        \
    static void write(std::string& out, const ts_context&) { \
      out += text;                                           \
    }                                                        \
  }

ANYBIND_TS_PRIMITIVE(void_, "void");
ANYBIND_TS_PRIMITIVE(boolean, "boolean");
ANYBIND_TS_PRIMITIVE(number, "number");
ANYBIND_TS_PRIMITIVE(bigint, "bigint");
ANYBIND_TS_PRIMITIVE(string, "string");
ANYBIND_TS_PRIMITIVE(null, "null");
ANYBIND_TS_PRIMITIVE(undefined, "undefined");
ANYBIND_TS_PRIMITIVE(unknown, "unknown");
ANYBIND_TS_PRIMITIVE(never, "never");

#undef ANYBIND_TS_PRIMITIVE

template <auto V>
struct ts_serializer<anybind::ts::literal<V>> {
  template <ts_dir>
  static void write(std::string& out, const ts_context&) {
    using T = std::decay_t<decltype(V)>;
    if constexpr (std::is_same_v<T, bool>) {
      out += V ? "true" : "false";
    } else if constexpr (std::is_integral_v<T>) {
      out += std::to_string(V);
    } else if constexpr (std::is_same_v<T, const char*>) {
      // Copy into a local pointer to avoid GCC -Waddress on static arrays.
      const char* s = V;
      ts_append_quoted(out, s ? std::string_view(s) : std::string_view{});
    } else {
      static_assert(sizeof(T) == 0, "anybind::ts::literal: unsupported literal type");
    }
  }
};

template <typename T>
struct ts_serializer<anybind::ts::array<T>> {
  template <ts_dir Dir>
  static void write(std::string& out, const ts_context& ctx) {
    ts_write_wrapped<T, Dir, 2>(out, ctx);
    out += "[]";
  }
};

template <typename... Ts>
struct ts_serializer<anybind::ts::tuple<Ts...>> {
  template <ts_dir Dir>
  static void write(std::string& out, [[maybe_unused]] const ts_context& ctx) {
    out += '[';
    if constexpr (sizeof...(Ts) > 0) {
      size_t i = 0;
      auto append_elem = [&](auto tag) {
        using Elem = typename decltype(tag)::type;
        if (i++ > 0) out += ", ";
        ts_write_wrapped<Elem, Dir, 0>(out, ctx);
      };
      (append_elem(type_tag<Ts>{}), ...);
    }
    out += ']';
  }
};

template <typename... Ts>
struct ts_serializer<anybind::ts::union_<Ts...>> {
  template <ts_dir Dir>
  static void write(std::string& out, [[maybe_unused]] const ts_context& ctx) {
    if constexpr (sizeof...(Ts) == 0) {
      out += "never";
    } else {
      size_t i = 0;
      auto append_member = [&](auto tag) {
        using Member = typename decltype(tag)::type;
        if (i++ > 0) out += " | ";
        ts_write_wrapped<Member, Dir, 1>(out, ctx);
      };
      (append_member(type_tag<Ts>{}), ...);
    }
  }
};

template <typename K, typename V>
struct ts_serializer<anybind::ts::record<K, V>> {
  template <ts_dir Dir>
  static void write(std::string& out, const ts_context& ctx) {
    out += "Record<";
    ts_write_wrapped<K, Dir, 0>(out, ctx);
    out += ", ";
    ts_write_wrapped<V, Dir, 0>(out, ctx);
    out += '>';
  }
};

template <typename... Fields>
struct ts_serializer<anybind::ts::object<Fields...>> {
  template <ts_dir Dir>
  static void write(std::string& out, [[maybe_unused]] const ts_context& ctx) {
    if constexpr (sizeof...(Fields) == 0) {
      out += "{}";
    } else {
      out += "{ ";
      size_t i = 0;
      auto append_field = [&](auto tag) {
        using F = typename decltype(tag)::type;
        if (i++ > 0) out += "; ";
        const char* raw_name = F::name;
        const std::string_view name = raw_name ? std::string_view(raw_name) : std::string_view{};
        if (ts_is_ident(name)) {
          out += name;
        } else {
          ts_append_quoted(out, name);
        }
        out += F::optional ? "?: " : ": ";
        ts_write_wrapped<typename F::type, Dir, 0>(out, ctx);
      };
      (append_field(type_tag<Fields>{}), ...);
      out += " }";
    }
  }
};

template <typename R, typename... Args>
struct ts_serializer<anybind::ts::function<R(Args...)>> {
  template <ts_dir Dir>
  static void write(std::string& out, const ts_context& ctx) {
    out += '(';
    if constexpr (sizeof...(Args) > 0) {
      size_t i = 0;
      auto append_arg = [&](auto tag) {
        using ArgNode = typename decltype(tag)::type;
        if (i > 0) out += ", ";
        out += "arg";
        out += std::to_string(i++);
        out += ": ";
        ts_write_wrapped<ArgNode, flip_dir(Dir), 0>(out, ctx);
      };
      (append_arg(type_tag<Args>{}), ...);
    }
    out += ") => ";
    ts_write_wrapped<R, Dir, 0>(out, ctx);
  }
};

template <typename T>
struct ts_serializer<anybind::ts::bound<T>> {
  template <ts_dir>
  static void write(std::string& out, const ts_context& ctx) {
    if (ctx.resolve_bound) {
      out += ctx.resolve_bound(ctx.user_data, type_id<intrinsic_t<T>>());
    } else {
      out += "unknown";
    }
  }
};

template <typename Node>
std::string ts_format(ts_dir dir = ts_dir::out, const ts_context& ctx = {}) {
  std::string out;
  if (dir == ts_dir::in) {
    ts_write_wrapped<Node, ts_dir::in, 0>(out, ctx);
  } else {
    ts_write_wrapped<Node, ts_dir::out, 0>(out, ctx);
  }
  return out;
}

/* === Per-environment type registry === */

struct type_info {
  const void* id = nullptr;
  std::string name;
  napi_type_tag tag{0, 0};
  napi_ref ctor_ref = nullptr;
};

/*
 * Per-napi_env registry stored via `napi_set_instance_data`, so loading the
 * same addon across worker threads or multiple contexts stays isolated and
 * thread-safe, and every `napi_ref` is released when its environment exits.
 */
class type_registry {
 public:
  type_registry() : env_salt_(next_env_salt()) {}

  type_registry(const type_registry&) = delete;
  type_registry& operator=(const type_registry&) = delete;

  static type_registry& get(napi_env env) {
    void* raw = nullptr;
    check(env, napi_get_instance_data(env, &raw));
    if (raw != nullptr) return *static_cast<type_registry*>(raw);

    auto* reg = new type_registry();
    napi_status status = napi_set_instance_data(env, reg, &type_registry::finalize, nullptr);
    if (status != napi_ok) {
      delete reg;
      check(env, status);
    }
    return *reg;
  }

  static type_registry* try_get(napi_env env) noexcept {
    void* raw = nullptr;
    if (napi_get_instance_data(env, &raw) != napi_ok) return nullptr;
    return static_cast<type_registry*>(raw);
  }

  /* Takes ownership of `ctor_ref` (if non-null), including on error paths. */
  type_info& register_type(napi_env env, const void* id, std::string name,
                           napi_ref ctor_ref = nullptr) {
    if (types_.find(id) != types_.end()) {
      if (ctor_ref != nullptr) napi_delete_reference(env, ctor_ref);
      throw std::runtime_error("anybind: type '" + name +
                               "' is already registered in this environment");
    }
    try {
      auto info = std::make_unique<type_info>();
      info->id = id;
      info->name = std::move(name);
      info->tag = napi_type_tag{env_salt_, ++next_tag_id_};
      auto [it, inserted] = types_.emplace(id, std::move(info));
      (void)inserted;
      // Transfer `ctor_ref` only after map insertion can no longer throw.
      it->second->ctor_ref = ctor_ref;
      return *it->second;
    } catch (...) {
      if (ctor_ref != nullptr) napi_delete_reference(env, ctor_ref);
      throw;
    }
  }

  template <typename T>
  type_info& register_type(napi_env env, std::string name, napi_ref ctor_ref = nullptr) {
    return register_type(env, type_id<intrinsic_t<T>>(), std::move(name), ctor_ref);
  }

  const type_info* find(const void* id) const noexcept {
    auto it = types_.find(id);
    return it == types_.end() ? nullptr : it->second.get();
  }

  template <typename T>
  const type_info* find() const noexcept {
    return find(type_id<intrinsic_t<T>>());
  }

  size_t size() const noexcept { return types_.size(); }

 private:
  static uint64_t next_env_salt() noexcept {
    static std::atomic<uint64_t> counter{0};
    return 0x616e7962696e6400ULL ^ (counter.fetch_add(1, std::memory_order_relaxed) + 1);
  }

  void cleanup(napi_env env) noexcept {
    for (auto& [id, info] : types_) {
      (void)id;
      if (info && info->ctor_ref != nullptr) {
        napi_delete_reference(env, info->ctor_ref);
        info->ctor_ref = nullptr;
      }
    }
    types_.clear();
  }

  static void finalize(napi_env env, void* data, void*) noexcept {
    auto* reg = static_cast<type_registry*>(data);
    reg->cleanup(env);
    delete reg;
  }

  uint64_t env_salt_;
  uint64_t next_tag_id_ = 0;
  std::unordered_map<const void*, std::unique_ptr<type_info>> types_;
};

inline std::string resolve_bound_type(void* user_data, const void* id) {
  auto* reg = static_cast<const type_registry*>(user_data);
  if (reg != nullptr) {
    if (const type_info* info = reg->find(id)) return info->name;
  }
  return "unknown";
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
        using UV = std::make_unsigned_t<V>;
        return static_cast<UV>(v) <= static_cast<UV>(LT::max());
      }
    } else if constexpr (LV::digits <= LT::digits) {
      return true;
    } else if constexpr (!std::is_signed_v<V> && std::is_signed_v<T>) {
      return v <= static_cast<std::make_unsigned_t<T>>(LT::max());
    } else if constexpr (std::is_signed_v<V>) {
      // T is narrower than V here, so its limits are representable as V.
      return v >= static_cast<V>(LT::min()) && v <= static_cast<V>(LT::max());
    } else {
      return v <= static_cast<V>(LT::max());
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
      using wide = std::conditional_t<std::is_signed_v<V>, intmax_t, uintmax_t>;
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
  // Explicit: arithmetic narrowing was range-checked above.
  return arg_slot<T>{a.name ? a.name : "", std::optional<T>(static_cast<T>(std::move(a.value)))};
}

template <typename T, typename V>
arg_slot<T> make_arg_slot(const arg_v<V>& a) {
  static_assert(std::is_convertible_v<const V&, T>,
                "anybind::arg: default value is not convertible to the parameter type");
  check_default_value<T>(a.name, a.value);
  return arg_slot<T>{a.name ? a.name : "", std::optional<T>(static_cast<T>(a.value))};
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
                         std::index_sequence<I...>, const ts_context& ctx = {}) {
  std::string s = "export function ";
  s += name;
  s += '(';
  [[maybe_unused]] auto append_param = [&](size_t i, const auto& slot, auto tag) {
    using ParamT = typename decltype(tag)::type;
    if (i > 0) s += ", ";
    s += ts_param_name(slot.name, i);
    if (i >= min_args) {
      // Trailing optional parameter.
      s += "?: ";
      ts_write_wrapped<ts_of<ParamT>, ts_dir::in, 0>(s, ctx);
    } else if (slot.default_value.has_value()) {
      // Non-trailing default: TS forbids `?` before a required parameter.
      s += ": ";
      ts_write_wrapped<anybind::ts::union_<ts_of<ParamT>, anybind::ts::undefined>, ts_dir::in, 0>(
          s, ctx);
    } else {
      s += ": ";
      ts_write_wrapped<ts_of<ParamT>, ts_dir::in, 0>(s, ctx);
    }
  };
  (append_param(I, std::get<I>(slots), type_tag<A>{}), ...);
  s += "): ";
  ts_write_wrapped<ts_of<R>, ts_dir::out, 0>(s, ctx);
  s += ";\n";
  return s;
}

}  // namespace detail

/** TypeScript type descriptor for C++ type `T`. */
using detail::ts_of;

/** Handle to the module being initialized inside ANYBIND_MODULE. */
class module_ {
 public:
  module_(napi_env env, napi_value exports)
      : env_(env), exports_(exports), registry_(&detail::type_registry::get(env)) {}

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
    detail::ts_context ts_ctx{&detail::resolve_bound_type, registry_};
    std::string sig = detail::ts_signature<typename traits::return_type>(
        name, typename traits::args{}, slots, min_args, indices{}, ts_ctx);

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
  detail::type_registry* registry_;
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
