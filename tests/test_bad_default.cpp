// A default value that would be truncated must make the module fail to load.
#include <anybind/anybind.h>

ANYBIND_MODULE(test_bad_default, m) {
  using namespace anybind::literals;
  m.def("narrow", [](int x) { return x; }, "x"_a = 1.9);
}
