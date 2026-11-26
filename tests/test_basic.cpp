#include <anybind/anybind.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

static int add(int a, int b) { return a + b; }

ANYBIND_MODULE(test_basic, m) {
  using namespace anybind::literals;
  m.def(
      "power", [](double base, int exp) { return std::pow(base, exp); }, anybind::arg("base"),
      anybind::arg("exp") = 2);
  m.def(
      "welcome",
      [](const std::string& who, const std::string& prefix) { return prefix + ", " + who; },
      "who"_a = "world", "prefix"_a = "hello");
  m.def(
      "formatTag",
      [](const std::string& prefix, int id, const std::string& suffix) {
        return prefix + std::to_string(id) + suffix;
      },
      "prefix"_a = "item-", "id"_a, "suffix"_a = "");
  m.def("add", &add);
  m.def("scale", [](double x, float k) { return x * k; });
  m.def("greet", [](const std::string& who) { return "hello, " + who; });
  m.def("negate", [](bool b) { return !b; });
  m.def("twice64", [](std::int64_t v) { return v * 2; });
  m.def("noop", [] {});
  m.def("fail", [] { throw std::runtime_error("boom"); });
  m.def("rejectArg", [](int) -> int { throw anybind::type_error("custom type error"); });
  m.def("next", [counter = 0]() mutable { return ++counter; });
  m.def(
      "keywords", [](int a, int b, int c, int d) { return a + b + c + d; }, "default"_a, "this"_a,
      "my-arg"_a, "2d"_a);
  m.def(
      "fits", [](float a, std::uint8_t b, std::int64_t c) { return a + b + c; }, "a"_a = 2,
      "b"_a = 255, "c"_a = -1);
}
