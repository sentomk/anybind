// The example from README.md.
#include <anybind/anybind.h>

#include <string>

int add(int a, int b) { return a + b; }

ANYBIND_MODULE(example, m) {
  m.def("add", &add);
  m.def("greet", [](const std::string& who) { return "hello, " + who; });
}
