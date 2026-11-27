/*
 * Tests for scoped JS handle types (`value`, `object`, `function`) and
 * persistent references (`ref<T>`).
 */
#include <anybind/anybind.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

  // Persistent `ref<T>` storage across calls (per-environment via shared_ptr).
  auto saved_obj = std::make_shared<anybind::ref<anybind::object>>();
  auto saved_fn = std::make_shared<anybind::ref<anybind::function>>();

  m.def(
      "storeObject",
      [saved_obj](anybind::object obj) { *saved_obj = anybind::ref<anybind::object>::strong(obj); },
      "obj"_a);
  m.def("loadObject", [env, saved_obj]() -> anybind::value {
    auto current = saved_obj->get();
    return current ? anybind::value(*current) : anybind::value::undefined(env);
  });
  m.def("clearObject", [saved_obj] { saved_obj->reset(); });

  m.def(
      "storeCallback",
      [saved_fn](anybind::function fn) { *saved_fn = anybind::ref<anybind::function>::strong(fn); },
      "fn"_a);
  m.def(
      "invokeStored",
      [saved_fn](int x) {
        auto fn = saved_fn->get();
        if (!fn) throw std::runtime_error("no callback stored");
        return fn->call(x).as<int>();
      },
      "x"_a);
  m.def("clearCallback", [saved_fn] { saved_fn->reset(); });

  // Direct `ref<T>` lifecycle checks (move, clone, refcount, weak, error paths).
  m.def(
      "checkRefLifecycle",
      [](anybind::object obj) -> std::string {
        std::string failures;
        auto expect = [&](const char* label, bool ok) {
          if (!ok) failures += std::string(label) + "\n";
        };

        anybind::ref<anybind::object> empty;
        expect("empty is falsy", !empty);
        expect("empty get is nullopt", !empty.get().has_value());

        auto r1 = anybind::ref<anybind::object>::strong(obj);
        expect("strong is truthy", static_cast<bool>(r1));
        auto got1 = r1.get();
        expect("strong get matches", got1.has_value() && got1->strict_equals(obj));

        // Move construction and move assignment transfer ownership cleanly.
        anybind::ref<anybind::object> r2(std::move(r1));
        expect("moved-from is empty", !r1);
        expect("moved-to is valid", static_cast<bool>(r2));

        auto cloned = r2.clone();
        auto got_clone = cloned.get();
        expect("clone matches original", got_clone.has_value() && got_clone->strict_equals(obj));
        expect("clone has distinct napi_ref", cloned.raw() != r2.raw());

        // Refcount transitions: 1 -> 2 -> 1 -> 0 -> 1.
        expect("inc 1->2", r2.ref_count_inc() == 2);
        expect("dec 2->1", r2.ref_count_dec() == 1);
        expect("dec 1->0", r2.ref_count_dec() == 0);
        expect("inc 0->1", r2.ref_count_inc() == 1);

        // Weak ref stays valid while caller holds `obj`.
        auto weak = anybind::ref<anybind::object>::weak(obj);
        auto got_weak = weak.get();
        expect("weak get matches live obj", got_weak.has_value() && got_weak->strict_equals(obj));

        r2.reset();
        expect("reset clears ref", !r2 && !r2.get().has_value());

        bool threw_empty_ctor = false;
        try {
          anybind::ref<anybind::object> bad(anybind::object{});
        } catch (const std::invalid_argument&) {
          threw_empty_ctor = true;
        }
        expect("empty handle throws in ctor", threw_empty_ctor);

        bool threw_empty_clone = false;
        try {
          auto bad_clone = empty.clone();
          (void)bad_clone;
        } catch (const std::runtime_error&) {
          threw_empty_clone = true;
        }
        expect("clone on empty throws", threw_empty_clone);

        return failures;
      },
      "obj"_a);
  m.def(
      "refFromValue",
      [](anybind::value v) {
        auto r = anybind::ref<anybind::value>::strong(v);
        return r.get().has_value();
      },
      "v"_a);
}
