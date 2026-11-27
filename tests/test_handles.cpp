/*
 * Tests for scoped JS handle types (`value`, `object`, `function`).
 */
#include <anybind/anybind.h>

#include <stdexcept>
#include <string>
#include <string_view>

ANYBIND_MODULE(test_handles, m) {
  using namespace anybind::literals;

  napi_env env = m.env();

  // `value` passthrough, predicates and conversions.
  m.def("echoValue", [](anybind::value v) { return v; }, "v"_a);
  m.def(
      "describeValue",
      [](anybind::value v) -> std::string {
        if (v.is_undefined()) return "undefined";
        if (v.is_null()) return "null";
        if (v.is_boolean()) return v.as<bool>() ? "bool:true" : "bool:false";
        if (v.is_number()) return "number:" + std::to_string(v.as<int>());
        if (v.is_string()) return "string:" + v.as<std::string>();
        if (v.is_bigint()) return "bigint";
        if (v.is_symbol()) return "symbol";
        if (v.is_function()) return "function";
        if (v.is_array()) return "array";
        if (v.is_object()) return "object";
        return "other";
      },
      "v"_a);
  m.def(
      "sameValue", [](anybind::value a, anybind::value b) { return a.strict_equals(b); }, "a"_a,
      "b"_a);
  m.def("makeNull", [env] { return anybind::value::null(env); });
  m.def("makeGreetingValue", [env] { return anybind::value::from(env, "hi"); });
  m.def("returnEmptyValue", [] { return anybind::value{}; });

  // `object` creation and property manipulation.
  m.def(
      "makePoint",
      [env](double x, double y) {
        auto obj = anybind::object::create(env);
        obj.set("x", x);
        obj.set("y", y);
        obj.set("kind", "point");
        obj.set("tag", std::string_view("2d"));
        return obj;
      },
      "x"_a, "y"_a);
  m.def(
      "inspectObject",
      [env](anybind::object obj) {
        auto out = anybind::object::create(env);
        out.set("hasProto", obj.has("inherited"));
        out.set("hasOwnProto", obj.has_own("inherited"));
        out.set("hasOwnA", obj.has_own("a"));
        out.set("a", obj.get<int>("a"));
        out.set("deletedB", obj.del("b"));
        std::string joined;
        for (const auto& k : obj.keys()) {
          if (!joined.empty()) joined += ',';
          joined += k;
        }
        out.set("keys", joined);
        return out;
      },
      "obj"_a);

  // `function` invocation, `this` binding and exception propagation.
  m.def("callZero", [](anybind::function fn) { return fn(); }, "fn"_a);
  m.def(
      "callAdd", [](anybind::function fn, int a, int b) { return fn.call(a, b).as<int>(); }, "fn"_a,
      "a"_a, "b"_a);
  m.def(
      "callMethod",
      [](anybind::object recv, anybind::function fn, const std::string& suffix) {
        return fn.call_with_this(recv, suffix).as<std::string>();
      },
      "recv"_a, "fn"_a, "suffix"_a);
  m.def("echoFunction", [](anybind::function fn) { return fn; }, "fn"_a);
}
