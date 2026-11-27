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

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

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

/** Rest parameter inside `ts::function`, rendered as `...args: T[]`. */
template <typename T>
struct rest {};

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

/**
 * A 64-bit integer that converts to and from a JavaScript `bigint`, keeping
 * full precision. Plain 64-bit integers convert to `number` instead.
 *
 *  m.def("hash", [](const std::string& s) { return anybind::bigint<uint64_t>(xxh64(s)); });
 */
template <typename T>
struct bigint {
  static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool> && sizeof(T) == 8,
                "anybind::bigint<T>: T must be a 64-bit integer type such as int64_t or uint64_t");

  T value{};

  constexpr bigint() noexcept = default;
  constexpr bigint(T v) noexcept : value(v) {}
  constexpr operator T() const noexcept { return value; }
};

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
struct is_std_optional : std::false_type {};

template <typename T>
struct is_std_optional<std::optional<T>> : std::true_type {};

/*
 * Unique address per type. The tag must stay mutable: identical read-only
 * objects may be folded into one by MSVC `/OPT:ICF` or lld `--icf=all`
 * (Release defaults), which would give every type the same ID.
 */
template <typename T>
inline const void* type_id() noexcept {
  static char tag;
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
struct type_caster<T, std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool> &&
                                       (!std::is_integral_v<T> || sizeof(T) <= 4)>> {
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
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
      check(env, napi_create_int32(env, static_cast<int32_t>(value), &result));
    } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
      check(env, napi_create_uint32(env, static_cast<uint32_t>(value), &result));
    } else {
      check(env, napi_create_double(env, static_cast<double>(value), &result));
    }
    return result;
  }
};

/*
 * Reads a JS number or bigint into a 64-bit integer. Numbers must be safe
 * integers so that no precision is lost on the way in.
 */
template <typename T>
T int64_from_js(napi_env env, napi_value value) {
  napi_valuetype actual;
  check(env, napi_typeof(env, value, &actual));
  if (actual == napi_bigint) {
    bool lossless = false;
    if constexpr (std::is_signed_v<T>) {
      int64_t result = 0;
      check(env, napi_get_value_bigint_int64(env, value, &result, &lossless));
      if (!lossless) {
        throw type_error("expected a 64-bit signed integer (bigint out of range)");
      }
      return static_cast<T>(result);
    } else {
      uint64_t result = 0;
      check(env, napi_get_value_bigint_uint64(env, value, &result, &lossless));
      if (!lossless) {
        throw type_error("expected a 64-bit unsigned integer (bigint out of range)");
      }
      return static_cast<T>(result);
    }
  }
  if (actual == napi_number) {
    double d = 0.0;
    check(env, napi_get_value_double(env, value, &d));
    // Only JS safe integers [-(2^53 - 1), 2^53 - 1] convert losslessly from number.
    constexpr double max_safe = 9007199254740991.0;
    const double lo = std::is_signed_v<T> ? -max_safe : 0.0;
    if (!(d >= lo && d <= max_safe) || std::trunc(d) != d) {
      throw type_error("expected a safe integer or bigint in the range of the C++ parameter type");
    }
    return static_cast<T>(d);
  }
  throw type_error("expected number or bigint");
}

/*
 * Plain 64-bit integers (`int64_t`, `uint64_t`, `size_t`, `ptrdiff_t`, and
 * `long` where it is 64-bit) map to `number`, so the generated `.d.ts` is the
 * same on every platform. Parameters also accept a bigint at run time.
 * Returning a value outside the safe integer range throws instead of losing
 * precision; `anybind::bigint<T>` returns a JS bigint instead.
 */
template <typename T>
struct type_caster<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                                       sizeof(T) == 8>> {
  using ts = anybind::ts::number;
  static T from_js(napi_env env, napi_value value) { return int64_from_js<T>(env, value); }
  static napi_value to_js(napi_env env, T value) {
    constexpr T max_safe = static_cast<T>(9007199254740991LL);
    bool safe = value <= max_safe;
    if constexpr (std::is_signed_v<T>) safe = safe && value >= -max_safe;
    if (!safe) {
      throw std::range_error(
          "64-bit integer is outside the safe JavaScript number range; return "
          "anybind::bigint<T> to get a bigint");
    }
    napi_value result = nullptr;
    check(env, napi_create_double(env, static_cast<double>(value), &result));
    return result;
  }
};

template <typename T>
struct type_caster<anybind::bigint<T>> {
  using ts = anybind::ts::io<anybind::ts::union_<anybind::ts::number, anybind::ts::bigint>,
                             anybind::ts::bigint>;
  static anybind::bigint<T> from_js(napi_env env, napi_value value) {
    return anybind::bigint<T>(int64_from_js<T>(env, value));
  }
  static napi_value to_js(napi_env env, anybind::bigint<T> value) {
    napi_value result = nullptr;
    if constexpr (std::is_signed_v<T>) {
      check(env, napi_create_bigint_int64(env, static_cast<int64_t>(value.value), &result));
    } else {
      check(env, napi_create_bigint_uint64(env, static_cast<uint64_t>(value.value), &result));
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

/*
 * Maps a parameter's intrinsic C++ type to the owning type stored in `arg_slot`
 * and `call`'s argument tuple. Non-owning views such as `std::string_view`
 * store an owning `std::string` for the duration of the call.
 */
template <typename T, typename = void>
struct caster_storage {
  using type = T;
  static T store(T v) { return v; }
  static T& unwrap(T& v) noexcept { return v; }
};

template <>
struct caster_storage<std::string_view> {
  using type = std::string;
  static std::string store(std::string_view v) { return std::string(v); }
  static std::string_view unwrap(const std::string& v) noexcept { return v; }
};

template <typename T>
struct caster_storage<std::optional<T>,
                      std::enable_if_t<!std::is_same_v<
                          typename caster_storage<intrinsic_t<T>>::type, intrinsic_t<T>>>> {
  using inner = caster_storage<intrinsic_t<T>>;
  using type = std::optional<typename inner::type>;
  static type store(std::optional<T> v) {
    if (!v.has_value()) return std::nullopt;
    return type(inner::store(*v));
  }
  static std::optional<T> unwrap(type& v) {
    if (!v.has_value()) return std::nullopt;
    return std::optional<T>(inner::unwrap(*v));
  }
};

template <typename T>
using storage_t = typename caster_storage<intrinsic_t<T>>::type;

/*
 * Whether `T` holds a non-owning view (`std::string_view`) anywhere inside it.
 * `caster_storage` only keeps the viewed data alive for a top-level parameter,
 * so converting JS into a view nested in a container, or returning one from
 * `value::as<T>()`, would leave it pointing at a destroyed temporary.
 */
template <typename T>
struct contains_view : std::false_type {};

template <>
struct contains_view<std::string_view> : std::true_type {};

template <typename T>
struct contains_view<std::optional<T>> : contains_view<intrinsic_t<T>> {};

template <typename T, typename Alloc>
struct contains_view<std::vector<T, Alloc>> : contains_view<intrinsic_t<T>> {};

template <typename T, size_t N>
struct contains_view<std::array<T, N>> : contains_view<intrinsic_t<T>> {};

template <typename K, typename V, typename Compare, typename Alloc>
struct contains_view<std::map<K, V, Compare, Alloc>>
    : std::disjunction<contains_view<intrinsic_t<K>>, contains_view<intrinsic_t<V>>> {};

template <typename K, typename V, typename Hash, typename KeyEqual, typename Alloc>
struct contains_view<std::unordered_map<K, V, Hash, KeyEqual, Alloc>>
    : std::disjunction<contains_view<intrinsic_t<K>>, contains_view<intrinsic_t<V>>> {};

template <typename... Ts>
struct contains_view<std::variant<Ts...>> : std::disjunction<contains_view<intrinsic_t<Ts>>...> {};

template <typename T>
inline constexpr bool contains_view_v = contains_view<intrinsic_t<T>>::value;

#define ANYBIND_DETAIL_NO_NESTED_VIEW(Elem)                                                 \
  static_assert(!::anybind::detail::contains_view_v<Elem>,                                 \
                "anybind: cannot convert a JS value into a std::string_view nested inside " \
                "a container, variant, or struct field; the view would dangle. Use "        \
                "std::string instead.")

template <>
struct type_caster<std::string_view> {
  using ts = anybind::ts::string;
  static std::string from_js(napi_env env, napi_value value) {
    return type_caster<std::string>::from_js(env, value);
  }
  static napi_value to_js(napi_env env, std::string_view value) {
    napi_value result = nullptr;
    const char* ptr = value.empty() ? "" : value.data();
    check(env, napi_create_string_utf8(env, ptr, value.size(), &result));
    return result;
  }
};

template <typename T>
struct type_caster<std::optional<T>> {
  using ts = anybind::ts::union_<ts_of<T>, anybind::ts::undefined>;
  using result_t = storage_t<std::optional<T>>;
  static result_t from_js(napi_env env, napi_value value) {
    napi_valuetype actual;
    check(env, napi_typeof(env, value, &actual));
    if (actual == napi_undefined) return std::nullopt;
    return result_t(type_caster<intrinsic_t<T>>::from_js(env, value));
  }
  static napi_value to_js(napi_env env, const std::optional<T>& value) {
    if (!value.has_value()) {
      napi_value u = nullptr;
      check(env, napi_get_undefined(env, &u));
      return u;
    }
    return type_caster<intrinsic_t<T>>::to_js(env, *value);
  }
};

template <typename T>
struct type_caster<std::vector<T>> {
  using elem_t = intrinsic_t<T>;
  using ts = anybind::ts::array<ts_of<elem_t>>;
  static std::vector<T> from_js(napi_env env, napi_value value) {
    ANYBIND_DETAIL_NO_NESTED_VIEW(elem_t);
    bool is_arr = false;
    check(env, napi_is_array(env, value, &is_arr));
    if (!is_arr) throw type_error("expected array");
    uint32_t len = 0;
    check(env, napi_get_array_length(env, value, &len));
    std::vector<T> result;
    result.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
      napi_value elem = nullptr;
      check(env, napi_get_element(env, value, i, &elem));
      try {
        result.push_back(type_caster<elem_t>::from_js(env, elem));
      } catch (const type_error& e) {
        throw type_error("element " + std::to_string(i) + ": " + e.what());
      }
    }
    return result;
  }
  static napi_value to_js(napi_env env, const std::vector<T>& value) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) {
      throw std::length_error("anybind: vector exceeds JavaScript array length limit");
    }
    napi_value arr = nullptr;
    check(env, napi_create_array_with_length(env, value.size(), &arr));
    uint32_t i = 0;
    // Use forwarding reference so `std::vector<bool>` proxies do not warn.
    for (auto&& elem : value) {
      napi_value js_elem = type_caster<elem_t>::to_js(env, elem);
      check(env, napi_set_element(env, arr, i++, js_elem));
    }
    return arr;
  }
};

template <typename Node, typename Seq>
struct ts_repeat_tuple;

template <typename Node, size_t... I>
struct ts_repeat_tuple<Node, std::index_sequence<I...>> {
  template <size_t>
  using always = Node;
  using type = anybind::ts::tuple<always<I>...>;
};

template <typename Node, size_t N, bool AsTuple = (N <= 16)>
struct ts_fixed_array {
  using type = anybind::ts::array<Node>;
};

template <typename Node, size_t N>
struct ts_fixed_array<Node, N, true> {
  using type = typename ts_repeat_tuple<Node, std::make_index_sequence<N>>::type;
};

template <typename T, size_t N>
struct type_caster<std::array<T, N>> {
  using elem_t = intrinsic_t<T>;
  using ts = typename ts_fixed_array<ts_of<elem_t>, N>::type;
  static std::array<T, N> from_js(napi_env env, napi_value value) {
    ANYBIND_DETAIL_NO_NESTED_VIEW(elem_t);
    bool is_arr = false;
    check(env, napi_is_array(env, value, &is_arr));
    if (!is_arr) throw type_error("expected array");
    uint32_t len = 0;
    check(env, napi_get_array_length(env, value, &len));
    if (len != N) {
      throw type_error("expected array of length " + std::to_string(N) + ", got " +
                       std::to_string(len));
    }
    std::array<T, N> result{};
    if constexpr (N > 0) {
      for (size_t i = 0; i < N; ++i) {
        napi_value elem = nullptr;
        check(env, napi_get_element(env, value, static_cast<uint32_t>(i), &elem));
        try {
          result[i] = type_caster<elem_t>::from_js(env, elem);
        } catch (const type_error& e) {
          throw type_error("element " + std::to_string(i) + ": " + e.what());
        }
      }
    }
    return result;
  }
  static napi_value to_js(napi_env env, const std::array<T, N>& value) {
    napi_value arr = nullptr;
    check(env, napi_create_array_with_length(env, N, &arr));
    if constexpr (N > 0) {
      for (size_t i = 0; i < N; ++i) {
        napi_value js_elem = type_caster<elem_t>::to_js(env, value[i]);
        check(env, napi_set_element(env, arr, static_cast<uint32_t>(i), js_elem));
      }
    }
    return arr;
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

/*
 * ECMAScript reserved words, which TypeScript rejects as function and parameter
 * names. `this` is included because TS would treat a parameter named `this` as
 * a `this` parameter and drop it from the arity.
 */
inline bool ts_is_reserved_word(std::string_view s) {
  static constexpr std::string_view reserved[] = {
      "break",  "case",     "catch",  "class",  "const",  "continue",   "debugger", "default",
      "delete", "do",       "else",   "enum",   "export", "extends",    "false",    "finally",
      "for",    "function", "if",     "import", "in",     "instanceof", "new",      "null",
      "return", "super",    "switch", "this",   "throw",  "true",       "try",      "typeof",
      "var",    "void",     "while",  "with"};
  for (std::string_view word : reserved) {
    if (s == word) return true;
  }
  return false;
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

template <typename Node, ts_dir Dir>
struct ts_union_collector {
  static void collect(std::vector<std::string>& parts, const ts_context& ctx) {
    std::string part;
    ts_write_wrapped<Node, Dir, 1>(part, ctx);
    for (const auto& existing : parts) {
      if (existing == part) return;
    }
    parts.push_back(std::move(part));
  }
};

template <ts_dir Dir, typename... Ts>
struct ts_union_collector<anybind::ts::union_<Ts...>, Dir> {
  static void collect(std::vector<std::string>& parts, [[maybe_unused]] const ts_context& ctx) {
    (ts_union_collector<ts_resolve_t<Ts, Dir>, Dir>::collect(parts, ctx), ...);
  }
};

template <typename... Ts>
struct ts_serializer<anybind::ts::union_<Ts...>> {
  template <ts_dir Dir>
  static void write(std::string& out, [[maybe_unused]] const ts_context& ctx) {
    if constexpr (sizeof...(Ts) == 0) {
      out += "never";
    } else {
      std::vector<std::string> parts;
      parts.reserve(sizeof...(Ts));
      (ts_union_collector<ts_resolve_t<Ts, Dir>, Dir>::collect(parts, ctx), ...);
      if (parts.empty()) {
        out += "never";
        return;
      }
      for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += " | ";
        out += parts[i];
      }
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

template <typename T>
struct is_ts_rest : std::false_type {};

template <typename T>
struct is_ts_rest<anybind::ts::rest<T>> : std::true_type {};

template <typename T>
struct ts_rest_elem {
  using type = T;
};

template <typename T>
struct ts_rest_elem<anybind::ts::rest<T>> {
  using type = T;
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
        if constexpr (is_ts_rest<ArgNode>::value) {
          out += "...args: ";
          ts_write_wrapped<typename ts_rest_elem<ArgNode>::type, flip_dir(Dir), 2>(out, ctx);
          out += "[]";
          ++i;
        } else {
          out += "arg";
          out += std::to_string(i++);
          out += ": ";
          ts_write_wrapped<ArgNode, flip_dir(Dir), 0>(out, ctx);
        }
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

/* === Map and variant casters === */

/*
 * Whether `key`, looked up through the prototype chain of `obj`, belongs to the
 * intrinsic `Object.prototype` (built-ins such as `constructor`, `toString` or
 * the `__proto__` accessor) rather than to `obj` or one of its own prototypes.
 */
inline bool inherited_from_object_prototype(napi_env env, napi_value obj, napi_value key) {
  // The prototype of a fresh object is the intrinsic `Object.prototype`, even if
  // the global `Object` has been replaced.
  napi_value probe = nullptr;
  check(env, napi_create_object(env, &probe));
  napi_value object_proto = nullptr;
  check(env, napi_get_prototype(env, probe, &object_proto));
  napi_value current = nullptr;
  check(env, napi_get_prototype(env, obj, &current));
  // Bounded so a Proxy whose `getPrototypeOf` keeps returning new objects cannot loop forever.
  for (int depth = 0; depth < 1024; ++depth) {
    napi_valuetype type = napi_undefined;
    check(env, napi_typeof(env, current, &type));
    if (type != napi_object && type != napi_function) return false;
    bool own = false;
    check(env, napi_has_own_property(env, current, key, &own));
    if (own) {
      bool same = false;
      check(env, napi_strict_equals(env, current, object_proto, &same));
      return same;
    }
    check(env, napi_get_prototype(env, current, &current));
  }
  return false;
}

inline void expect_plain_object(napi_env env, napi_value value) {
  expect_type(env, value, napi_object, "object");
  bool is_arr = false;
  check(env, napi_is_array(env, value, &is_arr));
  if (is_arr) throw type_error("expected object");
}

/*
 * Defines `obj[key] = value` as an own, writable, enumerable, configurable
 * data property. Unlike assignment (`napi_set_property`), a `__proto__` key
 * creates a property instead of replacing the prototype, and inherited
 * setters are never invoked.
 */
inline void define_own_property(napi_env env, napi_value obj, napi_value key, napi_value value) {
  napi_property_descriptor desc{};
  desc.name = key;
  desc.value = value;
  desc.attributes =
      static_cast<napi_property_attributes>(napi_writable | napi_enumerable | napi_configurable);
  check(env, napi_define_properties(env, obj, 1, &desc));
}

inline void define_own_property(napi_env env, napi_value obj, const char* key, napi_value value) {
  napi_property_descriptor desc{};
  desc.utf8name = key;
  desc.value = value;
  desc.attributes =
      static_cast<napi_property_attributes>(napi_writable | napi_enumerable | napi_configurable);
  check(env, napi_define_properties(env, obj, 1, &desc));
}

template <typename Map, typename V>
Map map_from_js(napi_env env, napi_value value) {
  using elem_t = intrinsic_t<V>;
  ANYBIND_DETAIL_NO_NESTED_VIEW(elem_t);
  expect_plain_object(env, value);
  napi_value keys = nullptr;
  check(env, napi_get_all_property_names(
                 env, value, napi_key_own_only,
                 static_cast<napi_key_filter>(napi_key_enumerable | napi_key_skip_symbols),
                 napi_key_numbers_to_strings, &keys));
  uint32_t len = 0;
  check(env, napi_get_array_length(env, keys, &len));
  Map result;
  for (uint32_t i = 0; i < len; ++i) {
    napi_value js_key = nullptr;
    check(env, napi_get_element(env, keys, i, &js_key));
    std::string key = type_caster<std::string>::from_js(env, js_key);
    napi_value js_val = nullptr;
    check(env, napi_get_property(env, value, js_key, &js_val));
    // Convert first so `key` is still intact for the error message.
    elem_t elem = [&]() -> elem_t {
      try {
        return type_caster<elem_t>::from_js(env, js_val);
      } catch (const type_error& e) {
        throw type_error("property '" + key + "': " + e.what());
      }
    }();
    result.emplace(std::move(key), std::move(elem));
  }
  return result;
}

template <typename Map, typename V>
napi_value map_to_js(napi_env env, const Map& value) {
  using elem_t = intrinsic_t<V>;
  napi_value obj = nullptr;
  check(env, napi_create_object(env, &obj));
  for (const auto& [k, v] : value) {
    napi_value js_key = nullptr;
    check(env, napi_create_string_utf8(env, k.data(), k.size(), &js_key));
    napi_value js_val = type_caster<elem_t>::to_js(env, v);
    define_own_property(env, obj, js_key, js_val);
  }
  return obj;
}

template <typename V, typename Compare, typename Alloc>
struct type_caster<std::map<std::string, V, Compare, Alloc>> {
  using map_t = std::map<std::string, V, Compare, Alloc>;
  using ts = anybind::ts::record<anybind::ts::string, ts_of<V>>;
  static map_t from_js(napi_env env, napi_value value) { return map_from_js<map_t, V>(env, value); }
  static napi_value to_js(napi_env env, const map_t& value) {
    return map_to_js<map_t, V>(env, value);
  }
};

template <typename V, typename Hash, typename KeyEqual, typename Alloc>
struct type_caster<std::unordered_map<std::string, V, Hash, KeyEqual, Alloc>> {
  using map_t = std::unordered_map<std::string, V, Hash, KeyEqual, Alloc>;
  using ts = anybind::ts::record<anybind::ts::string, ts_of<V>>;
  static map_t from_js(napi_env env, napi_value value) { return map_from_js<map_t, V>(env, value); }
  static napi_value to_js(napi_env env, const map_t& value) {
    return map_to_js<map_t, V>(env, value);
  }
};

template <>
struct type_caster<std::monostate> {
  using ts = anybind::ts::null;
  static std::monostate from_js(napi_env env, napi_value value) {
    expect_type(env, value, napi_null, "null");
    return std::monostate{};
  }
  static napi_value to_js(napi_env env, std::monostate) {
    napi_value n = nullptr;
    check(env, napi_get_null(env, &n));
    return n;
  }
};

template <typename... Ts>
struct type_caster<std::variant<Ts...>> {
  using variant_t = std::variant<Ts...>;
  using ts = anybind::ts::union_<ts_of<Ts>...>;

  template <size_t I>
  static bool try_alternative(napi_env env, napi_value value, std::optional<variant_t>& out) {
    if (out.has_value()) return true;
    using Alt = std::variant_alternative_t<I, variant_t>;
    try {
      out.emplace(std::in_place_index<I>, type_caster<intrinsic_t<Alt>>::from_js(env, value));
      return true;
    } catch (const type_error&) {
      return false;
    }
  }

  template <size_t... I>
  static variant_t from_js_impl(napi_env env, napi_value value, std::index_sequence<I...>) {
    std::optional<variant_t> out;
    (void)(try_alternative<I>(env, value, out) || ...);
    if (!out.has_value()) {
      throw type_error("expected " + ts_format<ts>(ts_dir::in));
    }
    return std::move(*out);
  }

  static variant_t from_js(napi_env env, napi_value value) {
    // Unlike a top-level parameter, a view alternative has no owning storage.
    ANYBIND_DETAIL_NO_NESTED_VIEW(variant_t);
    return from_js_impl(env, value, std::index_sequence_for<Ts...>{});
  }

  static napi_value to_js(napi_env env, const variant_t& value) {
    if (value.valueless_by_exception()) {
      throw std::runtime_error("anybind: variant is valueless by exception");
    }
    return std::visit(
        [env](const auto& alt) -> napi_value {
          return type_caster<intrinsic_t<decltype(alt)>>::to_js(env, alt);
        },
        value);
  }
};

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
 *
 * The addon's instance data is reserved for this registry. Its pointer is only
 * trusted if it matches the registry this module created for the environment,
 * so instance data replaced by user code is reported instead of misused.
 * Per-environment user state goes through `anybind::env_data<T>()`.
 */
class type_registry {
 public:
  type_registry() : env_salt_(next_env_salt()) {}

  type_registry(const type_registry&) = delete;
  type_registry& operator=(const type_registry&) = delete;

  static type_registry& get(napi_env env) {
    void* raw = nullptr;
    check(env, napi_get_instance_data(env, &raw));
    auto& owned = owned_registries();
    if (raw != nullptr) {
      auto it = owned.find(env);
      if (it != owned.end() && it->second == raw) return *it->second;
      throw std::runtime_error(
          "anybind: this addon's Node-API instance data was replaced by "
          "napi_set_instance_data(), which anybind reserves; store per-environment "
          "state with anybind::env_data<T>() instead");
    }

    auto reg = std::make_unique<type_registry>();
    owned[env] = reg.get();
    napi_status status = napi_set_instance_data(env, reg.get(), &type_registry::finalize, nullptr);
    if (status != napi_ok) {
      owned.erase(env);
      check(env, status);
    }
    return *reg.release();
  }

  static type_registry* try_get(napi_env env) noexcept {
    void* raw = nullptr;
    if (napi_get_instance_data(env, &raw) != napi_ok || raw == nullptr) return nullptr;
    auto& owned = owned_registries();
    auto it = owned.find(env);
    return it != owned.end() && it->second == raw ? it->second : nullptr;
  }

  /* Per-environment user state backing `anybind::env_data<T>()`. */
  template <typename T>
  T& user_data() {
    const void* id = type_id<T>();
    for (auto& [key, ptr] : user_data_) {
      if (key == id) return *static_cast<T*>(ptr.get());
    }
    user_data_.reserve(user_data_.size() + 1);
    std::unique_ptr<void, void (*)(void*)> ptr(new T(),
                                               [](void* p) { delete static_cast<T*>(p); });
    T& data = *static_cast<T*>(ptr.get());
    user_data_.emplace_back(id, std::move(ptr));
    return data;
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

  /*
   * Registries this module created, per environment. Every environment runs on
   * a single JS thread, so a thread-local map needs no locking.
   */
  static std::unordered_map<napi_env, type_registry*>& owned_registries() noexcept {
    thread_local std::unordered_map<napi_env, type_registry*> owned;
    return owned;
  }

  void cleanup(napi_env env) noexcept {
    // User state may hold `ref<T>` handles, so release it while the env is usable,
    // newest first.
    while (!user_data_.empty()) user_data_.pop_back();
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
    auto& owned = owned_registries();
    auto it = owned.find(env);
    if (it != owned.end() && it->second == reg) owned.erase(it);
    reg->cleanup(env);
    delete reg;
  }

  uint64_t env_salt_;
  uint64_t next_tag_id_ = 0;
  std::unordered_map<const void*, std::unique_ptr<type_info>> types_;
  std::vector<std::pair<const void*, std::unique_ptr<void, void (*)(void*)>>> user_data_;
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

template <typename A>
arg_slot<storage_t<A>> make_arg_slot(const arg& a) {
  return arg_slot<storage_t<A>>{a.name ? a.name : "", std::nullopt};
}

template <typename A, typename V>
arg_slot<storage_t<A>> make_arg_slot(arg_v<V>&& a) {
  using T = intrinsic_t<A>;
  using S = storage_t<A>;
  static_assert(std::is_convertible_v<V&&, T>,
                "anybind::arg: default value is not convertible to the parameter type");
  check_default_value<T>(a.name, a.value);
  // Explicit: arithmetic narrowing was range-checked above.
  return arg_slot<S>{a.name ? a.name : "",
                     std::optional<S>(caster_storage<T>::store(static_cast<T>(std::move(a.value))))};
}

template <typename A, typename V>
arg_slot<storage_t<A>> make_arg_slot(const arg_v<V>& a) {
  using T = intrinsic_t<A>;
  using S = storage_t<A>;
  static_assert(std::is_convertible_v<const V&, T>,
                "anybind::arg: default value is not convertible to the parameter type");
  check_default_value<T>(a.name, a.value);
  return arg_slot<S>{a.name ? a.name : "",
                     std::optional<S>(caster_storage<T>::store(static_cast<T>(a.value)))};
}

/* Overload 1: no arg() specs passed (backward compatible). */
template <typename... A>
std::tuple<arg_slot<storage_t<A>>...> make_arg_slots(type_list<A...>) {
  return {};
}

/* Overload 2: one arg() spec per parameter. */
template <typename FirstA, typename... RestA, typename FirstExtra, typename... RestExtra>
std::tuple<arg_slot<storage_t<FirstA>>, arg_slot<storage_t<RestA>>...> make_arg_slots(
    type_list<FirstA, RestA...>, FirstExtra&& first, RestExtra&&... rest) {
  return {make_arg_slot<FirstA>(std::forward<FirstExtra>(first)),
          make_arg_slot<RestA>(std::forward<RestExtra>(rest))...};
}

template <typename List>
struct slots_for_list;

template <typename... A>
struct slots_for_list<type_list<A...>> {
  using type = std::tuple<arg_slot<storage_t<A>>...>;
};

template <typename SlotsTuple, size_t... I>
size_t compute_min_args([[maybe_unused]] const SlotsTuple& slots, std::index_sequence<I...>) {
  size_t min_args = 0;
  // A trailing parameter may be omitted if it has a default or is `std::optional`.
  [[maybe_unused]] auto check_one = [&](size_t idx, const auto& slot) {
    using slot_t = intrinsic_t<decltype(slot)>;
    using value_t = typename decltype(slot_t::default_value)::value_type;
    if (!slot.default_value.has_value() && !is_std_optional<value_t>::value) min_args = idx + 1;
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
storage_t<T> load_arg(napi_env env, napi_value* argv, size_t argc, size_t index,
                      const arg_slot<storage_t<T>>& slot) {
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

/*
 * Hands a converted argument to the bound function. Each stored value is used
 * exactly once, so by-value and rvalue-reference parameters receive it by
 * move instead of a copy; lvalue-reference parameters bind to the stored
 * value. View types (`std::string_view`) are rebuilt from their owning
 * storage by `caster_storage::unwrap` and returned by value.
 */
template <typename A, typename S>
decltype(auto) forward_arg(S& stored) {
  using T = intrinsic_t<A>;
  if constexpr (std::is_lvalue_reference_v<A> || !std::is_same_v<S, T>) {
    return caster_storage<T>::unwrap(stored);
  } else {
    return std::move(stored);
  }
}

/*
 * When the bound function always throws and is inlined (Release builds), the
 * code after the call is unreachable. MSVC reports that as C4702, which `/WX`
 * would turn into an error in user code, so it is silenced for `call` only.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
template <typename R, typename F, typename SlotsTuple, typename... A, size_t... I>
napi_value call(napi_env env, F& f, [[maybe_unused]] napi_value* argv, [[maybe_unused]] size_t argc,
                [[maybe_unused]] const SlotsTuple& slots, type_list<A...>,
                std::index_sequence<I...>) {
  // Braced initialization guarantees left-to-right argument conversion.
  [[maybe_unused]] std::tuple<storage_t<A>...> values{
      load_arg<intrinsic_t<A>>(env, argv, argc, I, std::get<I>(slots))...};
  if constexpr (std::is_void_v<R>) {
    f(forward_arg<A>(std::get<I>(values))...);
    napi_value undefined;
    check(env, napi_get_undefined(env, &undefined));
    return undefined;
  } else {
    return type_caster<intrinsic_t<R>>::to_js(env, f(forward_arg<A>(std::get<I>(values))...));
  }
}
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

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
  if (ts_is_reserved_word(out)) out += '_';
  return out;
}

template <typename Node, typename Target>
struct ts_contains : std::is_same<Node, Target> {};

template <typename Target, typename... Ts>
struct ts_contains<anybind::ts::union_<Ts...>, Target>
    : std::disjunction<ts_contains<Ts, Target>...> {};

template <typename R, typename... A, typename SlotsTuple, size_t... I>
std::string ts_signature(std::string_view head, type_list<A...>,
                         [[maybe_unused]] const SlotsTuple& slots, [[maybe_unused]] size_t min_args,
                         std::index_sequence<I...>, const ts_context& ctx = {}) {
  std::string s(head);
  s += '(';
  // TypeScript rejects duplicate parameter names, which can come from repeated
  // `arg` names or from different names that sanitize to the same identifier.
  [[maybe_unused]] std::vector<std::string> used;
  [[maybe_unused]] auto append_param = [&](size_t i, const auto& slot, auto tag) {
    using ParamT = typename decltype(tag)::type;
    if (i > 0) s += ", ";
    const std::string base = ts_param_name(slot.name, i);
    std::string param = base;
    for (size_t n = 2; std::find(used.begin(), used.end(), param) != used.end(); ++n) {
      param = base + "_" + std::to_string(n);
    }
    s += param;
    used.push_back(std::move(param));
    if (i >= min_args) {
      // Trailing optional parameter.
      s += "?: ";
      ts_write_wrapped<ts_of<ParamT>, ts_dir::in, 0>(s, ctx);
    } else if (slot.default_value.has_value()) {
      // Non-trailing default: TS forbids `?` before a required parameter.
      s += ": ";
      if constexpr (ts_contains<ts_of<ParamT>, anybind::ts::undefined>::value) {
        ts_write_wrapped<ts_of<ParamT>, ts_dir::in, 0>(s, ctx);
      } else {
        ts_write_wrapped<anybind::ts::union_<ts_of<ParamT>, anybind::ts::undefined>, ts_dir::in, 0>(
            s, ctx);
      }
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

template <typename T>
napi_value to_js_value(napi_env env, T&& v) {
  using D = std::decay_t<T>;
  if constexpr (std::is_same_v<D, const char*> || std::is_same_v<D, char*>) {
    const char* s = v;
    napi_value out = nullptr;
    check(env, napi_create_string_utf8(env, s ? s : "", s ? NAPI_AUTO_LENGTH : 0, &out));
    return out;
  } else if constexpr (std::is_same_v<D, std::string_view>) {
    napi_value out = nullptr;
    check(env, napi_create_string_utf8(env, v.data(), v.size(), &out));
    return out;
  } else {
    return type_caster<intrinsic_t<T>>::to_js(env, std::forward<T>(v));
  }
}

}  // namespace detail

/** TypeScript type descriptor for C++ type `T`. */
using detail::ts_of;

/**
 * Per-environment instance of `T`: default-constructed on first use in `env`
 * and destroyed when that environment shuts down (e.g. when a worker thread
 * exits). Each worker thread or context gets its own instance.
 *
 * Use this instead of `napi_set_instance_data`, which anybind reserves for its
 * own registry. Only call it on the JS thread that owns `env`.
 *
 *  struct Cache { std::map<std::string, int> hits; };
 *  ANYBIND_MODULE(m_, m) {
 *    m.def("hit", [env = m.env()](const std::string& k) {
 *      return ++anybind::env_data<Cache>(env).hits[k];
 *    });
 *  }
 */
template <typename T>
T& env_data(napi_env env) {
  static_assert(std::is_default_constructible_v<T>,
                "anybind::env_data<T>: T must be default-constructible");
  return detail::type_registry::get(env).user_data<T>();
}

/* === Scoped JS handle types === */

/**
 * Non-owning handle to a JavaScript value valid within the current Node-API
 * callback scope. Use `ref<T>` to keep a value alive across calls.
 */
class value {
 public:
  value() noexcept = default;
  value(napi_env env, napi_value raw) noexcept : env_(env), raw_(raw) {}

  explicit operator bool() const noexcept { return env_ != nullptr && raw_ != nullptr; }
  napi_env env() const noexcept { return env_; }
  napi_value raw() const noexcept { return raw_; }

  napi_valuetype type() const {
    require_handle();
    napi_valuetype t;
    detail::check(env_, napi_typeof(env_, raw_, &t));
    return t;
  }

  bool is_undefined() const { return type() == napi_undefined; }
  bool is_null() const { return type() == napi_null; }
  bool is_null_or_undefined() const {
    napi_valuetype t = type();
    return t == napi_null || t == napi_undefined;
  }
  bool is_boolean() const { return type() == napi_boolean; }
  bool is_number() const { return type() == napi_number; }
  bool is_string() const { return type() == napi_string; }
  bool is_symbol() const { return type() == napi_symbol; }
  bool is_object() const { return type() == napi_object; }
  bool is_function() const { return type() == napi_function; }
  bool is_bigint() const { return type() == napi_bigint; }

  bool is_array() const {
    require_handle();
    bool result = false;
    detail::check(env_, napi_is_array(env_, raw_, &result));
    return result;
  }

  bool strict_equals(const value& other) const {
    require_handle();
    other.require_handle();
    if (env_ != other.env_) return false;
    bool eq = false;
    detail::check(env_, napi_strict_equals(env_, raw_, other.raw_, &eq));
    return eq;
  }

  /**
   * Converts this JS value to C++ type `T` using its `type_caster`. `T` must
   * own its data: a returned `std::string_view` would outlive its storage.
   */
  template <typename T>
  T as() const {
    static_assert(!detail::contains_view_v<T>,
                  "anybind::value::as<T>: T must not be or contain std::string_view, which "
                  "would dangle; use std::string instead.");
    require_handle();
    return detail::type_caster<detail::intrinsic_t<T>>::from_js(env_, raw_);
  }

  /** Converts a C++ value to a scoped JS `value` in `env`. */
  template <typename T>
  static value from(napi_env env, T&& v) {
    return value(env, detail::to_js_value(env, std::forward<T>(v)));
  }

  static value undefined(napi_env env) {
    napi_value u = nullptr;
    detail::check(env, napi_get_undefined(env, &u));
    return value(env, u);
  }

  static value null(napi_env env) {
    napi_value n = nullptr;
    detail::check(env, napi_get_null(env, &n));
    return value(env, n);
  }

 protected:
  void require_handle() const {
    if (env_ == nullptr || raw_ == nullptr) {
      throw std::runtime_error("anybind: attempted to use an empty JS handle");
    }
  }

  napi_env env_ = nullptr;
  napi_value raw_ = nullptr;
};

/** Scoped handle to a JavaScript object, with property access helpers. */
class object : public value {
 public:
  object() noexcept = default;
  object(napi_env env, napi_value raw) noexcept : value(env, raw) {}

  /** Creates a new empty plain JavaScript object `{}` in `env`. */
  static object create(napi_env env) {
    napi_value obj = nullptr;
    detail::check(env, napi_create_object(env, &obj));
    return object(env, obj);
  }

  bool has(std::string_view key) const {
    require_handle();
    bool result = false;
    detail::check(env_, napi_has_property(env_, raw_, make_key(env_, key), &result));
    return result;
  }

  bool has_own(std::string_view key) const {
    require_handle();
    bool result = false;
    detail::check(env_, napi_has_own_property(env_, raw_, make_key(env_, key), &result));
    return result;
  }

  value get(std::string_view key) const {
    require_handle();
    napi_value result = nullptr;
    detail::check(env_, napi_get_property(env_, raw_, make_key(env_, key), &result));
    return value(env_, result);
  }

  template <typename T>
  T get(std::string_view key) const {
    return get(key).as<T>();
  }

  template <typename T>
  void set(std::string_view key, T&& val) const {
    require_handle();
    napi_value js_val = detail::to_js_value(env_, std::forward<T>(val));
    detail::check(env_, napi_set_property(env_, raw_, make_key(env_, key), js_val));
  }

  bool del(std::string_view key) const {
    require_handle();
    bool deleted = false;
    detail::check(env_, napi_delete_property(env_, raw_, make_key(env_, key), &deleted));
    return deleted;
  }

  /** Returns own enumerable string keys, matching `Object.keys(obj)`. */
  std::vector<std::string> keys() const {
    require_handle();
    napi_value arr = nullptr;
    detail::check(env_,
                  napi_get_all_property_names(
                      env_, raw_, napi_key_own_only,
                      static_cast<napi_key_filter>(napi_key_enumerable | napi_key_skip_symbols),
                      napi_key_numbers_to_strings, &arr));
    uint32_t len = 0;
    detail::check(env_, napi_get_array_length(env_, arr, &len));
    std::vector<std::string> out;
    out.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
      napi_value elem = nullptr;
      detail::check(env_, napi_get_element(env_, arr, i, &elem));
      out.push_back(detail::type_caster<std::string>::from_js(env_, elem));
    }
    return out;
  }

 private:
  static napi_value make_key(napi_env env, std::string_view key) {
    napi_value js_key = nullptr;
    detail::check(env, napi_create_string_utf8(env, key.data(), key.size(), &js_key));
    return js_key;
  }
};

/** Scoped handle to a JavaScript function callable from C++. */
class function : public value {
 public:
  function() noexcept = default;
  function(napi_env env, napi_value raw) noexcept : value(env, raw) {}

  /** Invokes this JS function with `recv` as `this` and converted C++ arguments. */
  template <typename... Args>
  value call_with_this(const value& recv, Args&&... args) const {
    require_handle();
    if (!recv || recv.env() != env_) {
      throw std::runtime_error("anybind: invalid receiver handle for function call");
    }
    napi_value result = nullptr;
    if constexpr (sizeof...(Args) == 0) {
      detail::check(env_, napi_call_function(env_, recv.raw(), raw_, 0, nullptr, &result));
    } else {
      // Braced initialization guarantees left-to-right argument conversion.
      napi_value argv[sizeof...(Args)]{detail::to_js_value(env_, std::forward<Args>(args))...};
      detail::check(env_,
                    napi_call_function(env_, recv.raw(), raw_, sizeof...(Args), argv, &result));
    }
    return value(env_, result);
  }

  /** Invokes this JS function with `undefined` as `this`. */
  template <typename... Args>
  value call(Args&&... args) const {
    require_handle();
    return call_with_this(value::undefined(env_), std::forward<Args>(args)...);
  }

  template <typename... Args>
  value operator()(Args&&... args) const {
    return call(std::forward<Args>(args)...);
  }
};

/**
 * Persistent RAII handle (`napi_ref`) that keeps a JS handle (`value`,
 * `object`, or `function`) valid across callback scopes.
 *
 * Move-only to keep ownership of the underlying `napi_ref` unambiguous; call
 * `clone()` to create an independent reference. Under Node-API 8, persistent
 * references support objects and functions (or a `value` holding one).
 */
template <typename T = value>
class ref {
  static_assert(std::is_base_of_v<value, T>,
                "anybind::ref<T>: T must be anybind::value, object, or function");

 public:
  ref() noexcept = default;

  explicit ref(const T& handle, uint32_t initial_refcount = 1) {
    if (!handle) {
      throw std::invalid_argument("anybind::ref: cannot reference an empty JS handle");
    }
    napi_env env = handle.env();
    napi_ref raw_ref = nullptr;
    detail::check(env, napi_create_reference(env, handle.raw(), initial_refcount, &raw_ref));
    env_ = env;
    ref_ = raw_ref;
  }

  /** Creates a strong persistent reference (`refcount = 1`) preventing GC. */
  static ref strong(const T& handle) { return ref(handle, 1); }

  /** Creates a weak reference (`refcount = 0`); `get()` returns `std::nullopt` once collected. */
  static ref weak(const T& handle) { return ref(handle, 0); }

  ref(const ref&) = delete;
  ref& operator=(const ref&) = delete;

  ref(ref&& other) noexcept : env_(other.env_), ref_(other.ref_) {
    other.env_ = nullptr;
    other.ref_ = nullptr;
  }

  ref& operator=(ref&& other) noexcept {
    if (this != &other) {
      reset();
      env_ = other.env_;
      ref_ = other.ref_;
      other.env_ = nullptr;
      other.ref_ = nullptr;
    }
    return *this;
  }

  ~ref() noexcept { reset(); }

  explicit operator bool() const noexcept { return ref_ != nullptr; }
  napi_env env() const noexcept { return env_; }
  napi_ref raw() const noexcept { return ref_; }

  /** Releases the underlying `napi_ref`, leaving this handle empty. */
  void reset() noexcept {
    if (ref_ != nullptr) {
      napi_delete_reference(env_, ref_);
      env_ = nullptr;
      ref_ = nullptr;
    }
  }

  /**
   * Returns a scoped `T` handle if this reference is non-empty and still alive,
   * or `std::nullopt` if empty or garbage-collected (for weak references).
   */
  std::optional<T> get() const {
    if (ref_ == nullptr) return std::nullopt;
    napi_value val = nullptr;
    detail::check(env_, napi_get_reference_value(env_, ref_, &val));
    if (val == nullptr) return std::nullopt;
    return T(env_, val);
  }

  /** Increments the reference count and returns the new count. */
  uint32_t ref_count_inc() {
    require_ref();
    uint32_t count = 0;
    detail::check(env_, napi_reference_ref(env_, ref_, &count));
    return count;
  }

  /** Decrements the reference count and returns the new count. */
  uint32_t ref_count_dec() {
    require_ref();
    uint32_t count = 0;
    detail::check(env_, napi_reference_unref(env_, ref_, &count));
    return count;
  }

  /** Creates a new independent `ref<T>` pointing to the same JS value. */
  ref clone(uint32_t initial_refcount = 1) const {
    auto current = get();
    if (!current) {
      throw std::runtime_error("anybind::ref::clone: reference is empty or expired");
    }
    return ref(*current, initial_refcount);
  }

 private:
  void require_ref() const {
    if (env_ == nullptr || ref_ == nullptr) {
      throw std::runtime_error("anybind::ref: attempted to use an empty reference");
    }
  }

  napi_env env_ = nullptr;
  napi_ref ref_ = nullptr;
};

namespace detail {

template <typename T>
struct unwrap_optional {
  using type = T;
};

template <typename T>
struct unwrap_optional<std::optional<T>> {
  using type = T;
};

}  // namespace detail

/* === Plain struct object conversion === */

/** Metadata binding one C++ struct member to a JS object property. */
template <const char* Name, typename Struct, typename Member>
struct struct_field {
  static constexpr const char* name = Name;
  using struct_type = Struct;
  using member_type = Member;
  using value_type = typename detail::unwrap_optional<detail::intrinsic_t<Member>>::type;
  static constexpr bool is_optional = detail::is_std_optional<detail::intrinsic_t<Member>>::value;

  Member Struct::* ptr;
};

/** Helper for declaring a `struct_field` inside a manual `struct_traits<T>` specialization. */
template <const char* Name, typename Struct, typename Member>
constexpr struct_field<Name, Struct, Member> make_field(Member Struct::* ptr) noexcept {
  return {ptr};
}

/**
 * Specialize this trait (or use `ANYBIND_STRUCT` / `ANYBIND_STRUCT_DEFAULTS`)
 * to convert a plain C++ struct to and from a plain JavaScript object.
 */
template <typename T, typename SFINAE = void>
struct struct_traits {
  static constexpr bool is_specialized = false;
};

namespace detail {

template <typename FieldsTuple, bool HasDefaults>
struct struct_ts;

template <typename... Fields, bool HasDefaults>
struct struct_ts<std::tuple<Fields...>, HasDefaults> {
  using in_obj = anybind::ts::object<
      anybind::ts::field<Fields::name, ts_of<typename Fields::value_type>,
                         Fields::is_optional || HasDefaults>...>;
  using out_obj = anybind::ts::object<
      anybind::ts::field<Fields::name, ts_of<typename Fields::value_type>,
                         Fields::is_optional>...>;
  using type = std::conditional_t<HasDefaults, anybind::ts::io<in_obj, out_obj>, out_obj>;
};

template <typename T>
struct type_caster<T, std::enable_if_t<anybind::struct_traits<T>::is_specialized>> {
  using traits_t = anybind::struct_traits<T>;
  using fields_t = decltype(traits_t::fields());
  using ts = typename struct_ts<fields_t, traits_t::has_defaults>::type;

  template <typename Field>
  static void read_field(napi_env env, napi_value obj, T& out, const Field& f) {
    using ValT = typename Field::value_type;
    ANYBIND_DETAIL_NO_NESTED_VIEW(ValT);
    const char* raw_name = Field::name;
    napi_value js_key = nullptr;
    check(env, napi_create_string_utf8(env, raw_name, NAPI_AUTO_LENGTH, &js_key));
    bool own = false;
    check(env, napi_has_own_property(env, obj, js_key, &own));
    napi_value js_val = nullptr;
    check(env, napi_get_property(env, obj, js_key, &js_val));
    napi_valuetype val_type = napi_undefined;
    check(env, napi_typeof(env, js_val, &val_type));
    // Inherited properties count (e.g. getters on a class prototype), except
    // the built-ins every object inherits, such as `constructor` or `toString`.
    const bool missing =
        val_type == napi_undefined || (!own && inherited_from_object_prototype(env, obj, js_key));
    if (missing) {
      if constexpr (traits_t::has_defaults) {
        // Keep the C++ default member initializer from `T out{}`.
        return;
      } else if constexpr (Field::is_optional) {
        out.*(f.ptr) = std::nullopt;
        return;
      } else {
        throw type_error(std::string("property '") + raw_name + "': missing required property");
      }
    }
    try {
      out.*(f.ptr) = type_caster<ValT>::from_js(env, js_val);
    } catch (const type_error& e) {
      throw type_error(std::string("property '") + raw_name + "': " + e.what());
    }
  }

  template <typename Field>
  static void write_field(napi_env env, napi_value obj, const T& value, const Field& f) {
    using ValT = typename Field::value_type;
    const auto& member = value.*(f.ptr);
    napi_value js_val = nullptr;
    if constexpr (Field::is_optional) {
      if (!member.has_value()) return;
      js_val = type_caster<ValT>::to_js(env, *member);
    } else {
      js_val = type_caster<ValT>::to_js(env, member);
    }
    define_own_property(env, obj, Field::name, js_val);
  }

  template <size_t... I>
  static T from_js_impl(napi_env env, napi_value value, [[maybe_unused]] const fields_t& fields,
                        std::index_sequence<I...>) {
    T out{};
    (read_field(env, value, out, std::get<I>(fields)), ...);
    return out;
  }

  template <size_t... I>
  static napi_value to_js_impl(napi_env env, const T& value,
                               [[maybe_unused]] const fields_t& fields, std::index_sequence<I...>) {
    napi_value obj = nullptr;
    check(env, napi_create_object(env, &obj));
    (write_field(env, obj, value, std::get<I>(fields)), ...);
    return obj;
  }

  static T from_js(napi_env env, napi_value value) {
    expect_plain_object(env, value);
    constexpr auto fields = traits_t::fields();
    return from_js_impl(env, value, fields,
                        std::make_index_sequence<std::tuple_size_v<fields_t>>{});
  }

  static napi_value to_js(napi_env env, const T& value) {
    constexpr auto fields = traits_t::fields();
    return to_js_impl(env, value, fields, std::make_index_sequence<std::tuple_size_v<fields_t>>{});
  }
};

template <>
struct type_caster<anybind::value> {
  using ts = anybind::ts::unknown;
  static anybind::value from_js(napi_env env, napi_value v) { return anybind::value(env, v); }
  static napi_value to_js(napi_env env, const anybind::value& v) {
    if (!v || v.env() != env) {
      throw std::runtime_error("anybind: cannot return an empty or foreign-environment JS handle");
    }
    return v.raw();
  }
};

template <>
struct type_caster<anybind::object> {
  using ts = anybind::ts::record<anybind::ts::string, anybind::ts::unknown>;
  static anybind::object from_js(napi_env env, napi_value v) {
    expect_type(env, v, napi_object, "object");
    return anybind::object(env, v);
  }
  static napi_value to_js(napi_env env, const anybind::object& v) {
    if (!v || v.env() != env) {
      throw std::runtime_error("anybind: cannot return an empty or foreign-environment JS handle");
    }
    return v.raw();
  }
};

template <>
struct type_caster<anybind::function> {
  using ts = anybind::ts::io<
      anybind::ts::function<anybind::ts::unknown(anybind::ts::rest<anybind::ts::never>)>,
      anybind::ts::function<anybind::ts::unknown(anybind::ts::rest<anybind::ts::unknown>)>>;
  static anybind::function from_js(napi_env env, napi_value v) {
    expect_type(env, v, napi_function, "function");
    return anybind::function(env, v);
  }
  static napi_value to_js(napi_env env, const anybind::function& v) {
    if (!v || v.env() != env) {
      throw std::runtime_error("anybind: cannot return an empty or foreign-environment JS handle");
    }
    return v.raw();
  }
};

}  // namespace detail

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
    std::string sig = declare_function(name, [&](std::string_view head) {
      return detail::ts_signature<typename traits::return_type>(
          head, typename traits::args{}, slots, min_args, indices{}, ts_ctx);
    });

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
  /*
   * Emits the declaration for export `name`. A valid identifier becomes
   * `export function name(...)`. A reserved word (`delete`, `default`) or a
   * name that is not an identifier (`my-fn`) is declared under a private local
   * name and re-exported with `export { local as name }`; the string form
   * `export { local as "my-fn" }` needs TypeScript 5.6 or later.
   */
  template <typename Sig>
  std::string declare_function(const char* name, Sig&& signature) {
    const std::string_view export_name = name ? std::string_view(name) : std::string_view{};
    const bool ident = detail::ts_is_ident(export_name);
    if (ident && !detail::ts_is_reserved_word(export_name)) {
      return signature("export function " + std::string(export_name));
    }
    std::string base = "__anybind_";
    for (char c : export_name) {
      const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '$';
      base += ok ? c : '_';
    }
    std::string local = base;
    for (size_t n = 2; std::find(local_names_.begin(), local_names_.end(), local) !=
                       local_names_.end();
         ++n) {
      local = base + "_" + std::to_string(n);
    }
    local_names_.push_back(local);
    std::string out = signature("declare function " + local);
    out += "export { ";
    out += local;
    out += " as ";
    if (ident) {
      out += export_name;
    } else {
      detail::ts_append_quoted(out, export_name);
    }
    out += " };\n";
    return out;
  }

  napi_env env_;
  napi_value exports_;
  detail::type_registry* registry_;
  std::string dts_;
  std::vector<std::string> local_names_;
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

/* === Plain struct registration macros (1 to 32 fields, MSVC-safe) === */

#define ANYBIND_PP_EXPAND(x) x
#define ANYBIND_PP_CAT_IMPL(a, b) a##b
#define ANYBIND_PP_CAT(a, b) ANYBIND_PP_CAT_IMPL(a, b)

#define ANYBIND_PP_NARGS_IMPL(                                                                    \
    p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20,    \
    p21, p22, p23, p24, p25, p26, p27, p28, p29, p30, p31, p32, n, ...)                           \
  n
#define ANYBIND_PP_NARGS(...)                                                                     \
  ANYBIND_PP_EXPAND(ANYBIND_PP_NARGS_IMPL(                                                        \
      __VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14,    \
      13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define ANYBIND_PP_MAP_1(fn, c, x) fn(c, x)
#define ANYBIND_PP_MAP_2(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_1(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_3(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_2(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_4(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_3(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_5(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_4(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_6(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_5(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_7(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_6(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_8(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_7(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_9(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_8(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_10(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_9(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_11(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_10(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_12(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_11(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_13(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_12(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_14(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_13(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_15(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_14(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_16(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_15(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_17(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_16(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_18(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_17(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_19(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_18(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_20(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_19(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_21(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_20(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_22(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_21(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_23(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_22(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_24(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_23(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_25(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_24(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_26(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_25(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_27(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_26(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_28(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_27(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_29(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_28(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_30(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_29(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_31(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_30(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_32(fn, c, x, ...) fn(c, x) ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_31(fn, c, __VA_ARGS__))

#define ANYBIND_PP_MAP_COMMA_1(fn, c, x) fn(c, x)
#define ANYBIND_PP_MAP_COMMA_2(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_1(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_3(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_2(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_4(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_3(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_5(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_4(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_6(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_5(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_7(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_6(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_8(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_7(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_9(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_8(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_10(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_9(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_11(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_10(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_12(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_11(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_13(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_12(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_14(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_13(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_15(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_14(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_16(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_15(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_17(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_16(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_18(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_17(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_19(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_18(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_20(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_19(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_21(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_20(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_22(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_21(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_23(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_22(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_24(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_23(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_25(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_24(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_26(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_25(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_27(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_26(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_28(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_27(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_29(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_28(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_30(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_29(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_31(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_30(fn, c, __VA_ARGS__))
#define ANYBIND_PP_MAP_COMMA_32(fn, c, x, ...) fn(c, x), ANYBIND_PP_EXPAND(ANYBIND_PP_MAP_COMMA_31(fn, c, __VA_ARGS__))

#define ANYBIND_PP_MAP(fn, c, ...) \
  ANYBIND_PP_EXPAND(ANYBIND_PP_CAT(ANYBIND_PP_MAP_, ANYBIND_PP_NARGS(__VA_ARGS__))(fn, c, __VA_ARGS__))

#define ANYBIND_PP_MAP_COMMA(fn, c, ...) \
  ANYBIND_PP_EXPAND(                     \
      ANYBIND_PP_CAT(ANYBIND_PP_MAP_COMMA_, ANYBIND_PP_NARGS(__VA_ARGS__))(fn, c, __VA_ARGS__))

#define ANYBIND_DETAIL_STRUCT_NAME(Type, field) static constexpr char anybind_name_##field[] = #field;
#define ANYBIND_DETAIL_STRUCT_ENTRY(Type, field) ::anybind::make_field<anybind_name_##field>(&Type::field)

#define ANYBIND_DETAIL_DEFINE_STRUCT(Type, HasDefaults, ...)                                      \
  namespace anybind {                                                                             \
  template <>                                                                                     \
  struct struct_traits<Type> {                                                                    \
    static constexpr bool is_specialized = true;                                                  \
    static constexpr bool has_defaults = (HasDefaults);                                           \
    ANYBIND_PP_MAP(ANYBIND_DETAIL_STRUCT_NAME, Type, __VA_ARGS__)                                 \
    static constexpr auto fields() noexcept {                                                     \
      return std::make_tuple(ANYBIND_PP_MAP_COMMA(ANYBIND_DETAIL_STRUCT_ENTRY, Type, __VA_ARGS__)); \
    }                                                                                             \
  };                                                                                              \
  } /* namespace anybind */                                                                       \
  static_assert(true, "")

/**
 * Registers a plain C++ struct for value-based conversion to/from JS objects.
 * Invoke at global scope. Non-`std::optional` fields are required on JS input;
 * `std::optional<T>` fields become optional TS properties (`name?: T`).
 *
 *   struct Point { double x; double y; };
 *   ANYBIND_STRUCT(Point, x, y);
 */
#define ANYBIND_STRUCT(Type, ...) ANYBIND_DETAIL_DEFINE_STRUCT(Type, false, __VA_ARGS__)

/**
 * Registers a plain C++ struct whose fields have default member initializers.
 * Missing or `undefined` JS properties keep their C++ default values, and
 * TypeScript declarations mark all fields optional in parameter position
 * while keeping non-`std::optional` fields required in return position.
 *
 *   struct Options { bool gfm = true; bool xhtml = false; };
 *   ANYBIND_STRUCT_DEFAULTS(Options, gfm, xhtml);
 */
#define ANYBIND_STRUCT_DEFAULTS(Type, ...) ANYBIND_DETAIL_DEFINE_STRUCT(Type, true, __VA_ARGS__)
