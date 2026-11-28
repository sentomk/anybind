/*
 * Tests for `std::function` <-> JS function conversion: calling JS callbacks
 * from C++, returning C++ callables to JS, stored callbacks, error
 * propagation and the generated `.d.ts`.
 */
#include <anybind/anybind.h>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
};

/* A callback kept across calls into the addon, per environment. */
struct StoredCallback {
  std::function<int(int)> fn;
};

}  // namespace

ANYBIND_STRUCT(Point, x, y);

ANYBIND_MODULE(test_callbacks, m) {
  using namespace anybind::literals;
  napi_env env = m.env();

  // JS -> C++: calling a JS callback with converted arguments and result.
  m.def(
      "apply", [](const std::function<int(int)>& f, int x) { return f(x); }, "f"_a, "x"_a);
  m.def(
      "forEachIndexed",
      [](const std::vector<std::string>& items,
         const std::function<void(const std::string&, int)>& cb) {
        for (size_t i = 0; i < items.size(); ++i) cb(items[i], static_cast<int>(i));
      },
      "items"_a, "cb"_a);
  m.def(
      "transform",
      [](std::string s, std::function<std::string(std::string_view)> f) { return f(s); }, "s"_a,
      "f"_a);
  m.def(
      "sumWith",
      [](const std::function<std::vector<int>(const Point&)>& f) {
        int total = 0;
        for (int v : f(Point{1.5, 2.5})) total += v;
        return total;
      },
      "f"_a);
  m.def(
      "callMany",
      [](const std::function<int(int)>& f, int n) {
        long long total = 0;
        for (int i = 0; i < n; ++i) total += f(i);
        return static_cast<double>(total);
      },
      "f"_a, "n"_a);
  m.def(
      "maybeApply",
      [](std::optional<std::function<int(int)>> f, int x) { return f ? (*f)(x) : -1; }, "f"_a,
      "x"_a = 0);
  m.def(
      "catchInCpp",
      [](const std::function<void()>& f) {
        try {
          f();
        } catch (const anybind::error_already_set&) {
          return std::string("rethrown");
        }
        return std::string("no error");
      },
      "f"_a);

  // Callbacks stored across calls (per environment).
  m.def(
      "storeCallback",
      [env](std::function<int(int)> f) { anybind::env_data<StoredCallback>(env).fn = std::move(f); },
      "f"_a);
  m.def(
      "callStored",
      [env](int x) {
        auto& stored = anybind::env_data<StoredCallback>(env);
        if (!stored.fn) throw std::runtime_error("no stored callback");
        return stored.fn(x);
      },
      "x"_a);
  m.def("clearStored", [env] { anybind::env_data<StoredCallback>(env).fn = nullptr; });

  // C++ -> JS: returning C++ callables.
  m.def(
      "makeAdder",
      [](int n) { return std::function<int(int)>([n](int x) { return x + n; }); }, "n"_a);
  m.def(
      "makeGreeter",
      [](std::string greeting) {
        return std::function<std::string(const std::string&, std::optional<std::string>)>(
            [greeting](const std::string& who, std::optional<std::string> punct) {
              return greeting + ", " + who + punct.value_or("!");
            });
      },
      "greeting"_a);
  m.def(
      "makeThrower",
      [] { return std::function<void()>([] { throw std::runtime_error("from C++"); }); });
  m.def("makeEmpty", [] { return std::function<void()>{}; });
  m.def(
      "compose",
      [](std::function<int(int)> f, std::function<int(int)> g) {
        return std::function<int(int)>([f, g](int x) { return g(f(x)); });
      },
      "f"_a, "g"_a);
}
