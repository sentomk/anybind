#include <anybind/anybind.h>

#include <cstdint>
#include <stdexcept>
#include <string>

static int add(int a, int b) { return a + b; }

ANYBIND_MODULE(test_basic, m) {
  m.def("add", &add);
  m.def("scale", [](double x, float k) { return x * k; });
  m.def("greet", [](const std::string& who) { return "hello, " + who; });
  m.def("negate", [](bool b) { return !b; });
  m.def("twice64", [](std::int64_t v) { return v * 2; });
  m.def("noop", [] {});
  m.def("fail", [] { throw std::runtime_error("boom"); });
  m.def("rejectArg", [](int) -> int { throw anybind::type_error("custom type error"); });
  m.def("next", [counter = 0]() mutable { return ++counter; });
}
