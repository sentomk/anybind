/*
 * Each ANYBIND_CF_CASE 1..6 must fail to compile: converting JS into a
 * std::string_view that is not a top-level parameter would dangle.
 * Case 0 is the control: the same bindings with owning std::string compile.
 * Built on demand by tests/compile_fail.test.mjs (targets are EXCLUDE_FROM_ALL).
 */
#include <anybind/anybind.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if ANYBIND_CF_CASE == 0
using text_t = std::string;
#else
using text_t = std::string_view;
#endif

#if ANYBIND_CF_CASE == 0 || ANYBIND_CF_CASE == 5
struct Holder {
  std::vector<text_t> parts;
};
ANYBIND_STRUCT(Holder, parts);
#endif

ANYBIND_MODULE(compile_fail, m) {
#if ANYBIND_CF_CASE == 0
  m.def("f1", [](std::vector<text_t> v) { return v.size() > 0; });
  m.def("f2", [](std::variant<text_t, int> v) { return v.index(); });
  m.def("f3", [](const std::map<std::string, text_t>& v) { return v.empty(); });
  m.def("f4", [](std::array<std::optional<text_t>, 2> v) { return v[0].has_value(); });
  m.def("f5", [](const Holder& h) { return h.parts.empty(); });
  m.def("f6", [](anybind::value v) { return v.as<text_t>().size() > 0; });
#elif ANYBIND_CF_CASE == 1
  m.def("f", [](std::vector<std::string_view> v) { return v.size() > 0; });
#elif ANYBIND_CF_CASE == 2
  m.def("f", [](std::variant<std::string_view, int> v) { return v.index(); });
#elif ANYBIND_CF_CASE == 3
  m.def("f", [](const std::map<std::string, std::string_view>& v) { return v.empty(); });
#elif ANYBIND_CF_CASE == 4
  m.def("f", [](std::array<std::optional<std::string_view>, 2> v) { return v[0].has_value(); });
#elif ANYBIND_CF_CASE == 5
  m.def("f", [](const Holder& h) { return h.parts.empty(); });
#elif ANYBIND_CF_CASE == 6
  m.def("f", [](anybind::value v) { return v.as<std::string_view>().size() > 0; });
#else
#error "ANYBIND_CF_CASE must be 0..6"
#endif
}
