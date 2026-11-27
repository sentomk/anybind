/*
 * Tests for structured TypeScript type descriptors (`anybind::ts`) and the
 * runtime serializer, including nested types and asymmetric in/out types.
 */
#include <anybind/anybind.h>

#include <string>

namespace {

inline constexpr char kOk[] = "ok";
inline constexpr char kEsc[] = "a\"b\\c\nd";
inline constexpr char kX[] = "x";
inline constexpr char kLabel[] = "label";
inline constexpr char kKebab[] = "kebab-key";

struct BigIntLike {};
struct BigIntList {};
struct CallbackLike {};
struct Widget {};

}  // namespace

namespace anybind {
namespace detail {

template <>
struct type_caster<BigIntLike> {
  using ts = anybind::ts::io<anybind::ts::union_<anybind::ts::number, anybind::ts::bigint>,
                             anybind::ts::bigint>;
  static BigIntLike from_js(napi_env, napi_value) { return {}; }
  static napi_value to_js(napi_env env, BigIntLike) {
    napi_value u;
    check(env, napi_get_undefined(env, &u));
    return u;
  }
};

template <>
struct type_caster<BigIntList> {
  using ts = anybind::ts::array<ts_of<BigIntLike>>;
  static BigIntList from_js(napi_env, napi_value) { return {}; }
  static napi_value to_js(napi_env env, BigIntList) {
    napi_value u;
    check(env, napi_get_undefined(env, &u));
    return u;
  }
};

template <>
struct type_caster<CallbackLike> {
  using ts = anybind::ts::function<ts_of<BigIntLike>(ts_of<BigIntLike>)>;
  static CallbackLike from_js(napi_env, napi_value) { return {}; }
  static napi_value to_js(napi_env env, CallbackLike) {
    napi_value u;
    check(env, napi_get_undefined(env, &u));
    return u;
  }
};

}  // namespace detail
}  // namespace anybind

static std::string run_checks() {
  namespace ts = anybind::ts;
  using anybind::ts_of;
  using anybind::detail::ts_context;
  using anybind::detail::ts_dir;
  using anybind::detail::ts_format;

  std::string failures;
  auto expect = [&](const char* label, const std::string& actual, const std::string& expected) {
    if (actual != expected) {
      failures += std::string(label) + ": got `" + actual + "`, expected `" + expected + "`\n";
    }
  };

  // Primitives and literals.
  expect("void", ts_format<ts::void_>(), "void");
  expect("boolean", ts_format<ts::boolean>(), "boolean");
  expect("number", ts_format<ts::number>(), "number");
  expect("bigint", ts_format<ts::bigint>(), "bigint");
  expect("string", ts_format<ts::string>(), "string");
  expect("null", ts_format<ts::null>(), "null");
  expect("undefined", ts_format<ts::undefined>(), "undefined");
  expect("unknown", ts_format<ts::unknown>(), "unknown");
  expect("never", ts_format<ts::never>(), "never");
  expect("lit-bool", ts_format<ts::literal<true>>(), "true");
  expect("lit-int", ts_format<ts::literal<-42>>(), "-42");
  expect("lit-str", ts_format<ts::literal<kOk>>(), "\"ok\"");
  expect("lit-esc", ts_format<ts::literal<kEsc>>(), "\"a\\\"b\\\\c\\nd\"");

  // Arrays, tuples, unions and operator precedence.
  expect("arr", ts_format<ts::array<ts::string>>(), "string[]");
  expect("arr-arr", ts_format<ts::array<ts::array<ts::number>>>(), "number[][]");
  expect("arr-union", ts_format<ts::array<ts::union_<ts::number, ts::bigint>>>(),
         "(number | bigint)[]");
  expect("arr-fn", ts_format<ts::array<ts::function<ts::void_()>>>(), "(() => void)[]");
  expect("tuple-empty", ts_format<ts::tuple<>>(), "[]");
  expect("tuple", ts_format<ts::tuple<ts::string, ts::number, ts::boolean>>(),
         "[string, number, boolean]");
  expect("union-empty", ts_format<ts::union_<>>(), "never");
  expect("union-single", ts_format<ts::union_<ts::string>>(), "string");
  expect("union-fn", ts_format<ts::union_<ts::function<ts::void_(ts::number)>, ts::undefined>>(),
         "((arg0: number) => void) | undefined");
  expect("union-lits",
         ts_format<ts::union_<ts::literal<kOk>, ts::literal<1>, ts::literal<false>>>(),
         "\"ok\" | 1 | false");

  // Records and objects.
  expect("record", ts_format<ts::record<ts::string, ts::array<ts::number>>>(),
         "Record<string, number[]>");
  expect("obj-empty", ts_format<ts::object<>>(), "{}");
  expect("obj",
         ts_format<ts::object<ts::field<kX, ts::number>, ts::field<kLabel, ts::string, true>,
                              ts::field<kKebab, ts::boolean>>>(),
         "{ x: number; label?: string; \"kebab-key\": boolean }");

  // Asymmetric in/out types nested inside containers and callbacks.
  using BigTs = ts_of<BigIntLike>;
  expect("io-in", ts_format<BigTs>(ts_dir::in), "number | bigint");
  expect("io-out", ts_format<BigTs>(ts_dir::out), "bigint");
  expect("arr-io-in", ts_format<ts::array<BigTs>>(ts_dir::in), "(number | bigint)[]");
  expect("arr-io-out", ts_format<ts::array<BigTs>>(ts_dir::out), "bigint[]");
  expect("fn-io-in", ts_format<ts::function<BigTs(BigTs)>>(ts_dir::in),
         "(arg0: bigint) => number | bigint");
  expect("fn-io-out", ts_format<ts::function<BigTs(BigTs)>>(ts_dir::out),
         "(arg0: number | bigint) => bigint");

  // Bound class placeholder with and without a resolver.
  expect("bound-default", ts_format<ts::bound<Widget>>(), "unknown");
  auto resolve = [](void*, const void* id) -> std::string {
    if (id == anybind::detail::type_id<Widget>()) return "Widget";
    return "Other";
  };
  ts_context ctx{+resolve, nullptr};
  expect("bound-resolved", ts_format<ts::array<ts::bound<Widget>>>(ts_dir::out, ctx), "Widget[]");

  return failures;
}

ANYBIND_MODULE(test_ts, m) {
  using namespace anybind::literals;
  m.def("failures", [] { return run_checks(); });
  m.def("echoBig", [](BigIntLike x) { return x; }, "value"_a);
  m.def("echoBigList", [](BigIntList xs) { return xs; }, "items"_a);
  m.def(
      "wrapCallback", [](CallbackLike, int) { return CallbackLike{}; }, "cb"_a = CallbackLike{},
      "n"_a);
}
