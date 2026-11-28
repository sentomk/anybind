/*
 * Tests for function overloads: dispatch order (declaration order, exact
 * arity before ignoring extra arguments), errors when nothing matches,
 * TypeErrors from a function body, and overload signatures in the `.d.ts`.
 */
#include <anybind/anybind.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Size {
  double w = 0.0;
  double h = 0.0;
};

}  // namespace

ANYBIND_STRUCT(Size, w, h);

ANYBIND_MODULE(test_overloads, m) {
  using namespace anybind::literals;

  // By argument type, in declaration order: int is tried before double.
  m.def("describe", [](int) { return std::string("int"); }, "value"_a);
  m.def("describe", [](double) { return std::string("double"); }, "value"_a);
  m.def("describe", [](const std::string&) { return std::string("string"); }, "value"_a);
  m.def("describe", [](const std::vector<int>& v) { return "list of " + std::to_string(v.size()); },
        "values"_a);
  m.def("describe", [](const Size&) { return std::string("size"); }, "size"_a);
  m.def("describe", [](const std::function<int(int)>& f) { return "fn:" + std::to_string(f(1)); },
        "fn"_a);

  // By argument count.
  m.def("area", [](double side) { return side * side; }, "side"_a);
  m.def("area", [](double w, double h) { return w * h; }, "w"_a, "h"_a);

  // Defaults and optional parameters take part in arity matching.
  m.def(
      "pad", [](const std::string& s, int width) { return s + "/" + std::to_string(width); },
      "s"_a, "width"_a = 8);
  m.def(
      "pad", [](int n, std::optional<int> width) {
        return std::to_string(n) + "#" + std::to_string(width.value_or(4));
      },
      "n"_a, "width"_a);

  // A TypeError thrown by the body must not fall through to the next overload.
  m.def("strict", [](int) -> std::string { throw anybind::type_error("rejected by body"); },
        "x"_a);
  m.def("strict", [](double) { return std::string("double"); }, "x"_a);

  // Overloads under an export name that needs an alias in the .d.ts.
  m.def("delete", [](int x) { return x; }, "x"_a);
  m.def("delete", [](const std::string& s) { return static_cast<int>(s.size()); }, "s"_a);
}
