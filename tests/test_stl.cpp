/*
 * Tests for STL type casters (`std::string_view`, `std::optional`,
 * `std::vector`, `std::array`).
 */
#include <anybind/anybind.h>
#include <stddef.h>
#include <stdint.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

ANYBIND_MODULE(test_stl, m) {
  using namespace anybind::literals;

  // `std::string_view` parameter, default value and return (including empty view).
  m.def(
      "svLen", [](std::string_view s) { return static_cast<uint32_t>(s.size()); },
      "s"_a = "world");
  m.def(
      "svTrimPrefix",
      [](std::string_view s, std::string_view prefix) -> std::string_view {
        if (s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix) {
          return s.substr(prefix.size());
        }
        return s;
      },
      "s"_a, "prefix"_a = std::string_view("pre_"));
  m.def("svEmpty", []() -> std::string_view { return {}; });

  // `std::optional<T>` including `std::optional<std::string_view>` and defaults.
  m.def(
      "optDouble",
      [](std::optional<int> v) -> std::optional<int> {
        if (!v) return std::nullopt;
        return *v * 2;
      },
      "v"_a = std::nullopt);
  m.def(
      "optGreeting",
      [](std::optional<std::string_view> name, int repeat) {
        std::string who = name ? std::string(*name) : "guest";
        std::string out;
        for (int i = 0; i < repeat; ++i) {
          if (i > 0) out += ',';
          out += who;
        }
        return out;
      },
      "name"_a = std::nullopt, "repeat"_a);

  // `std::vector<T>`: primitives, `vector<bool>`, nested vectors, vector of optionals.
  m.def(
      "sumInts",
      [](const std::vector<int>& nums) {
        int total = 0;
        for (int n : nums) total += n;
        return total;
      },
      "nums"_a);
  m.def(
      "invertFlags",
      [](std::vector<bool> flags) {
        for (size_t i = 0; i < flags.size(); ++i) flags[i] = !flags[i];
        return flags;
      },
      "flags"_a);
  m.def(
      "flatten",
      [](const std::vector<std::vector<int>>& grid) {
        std::vector<int> out;
        for (const auto& row : grid) {
          out.insert(out.end(), row.begin(), row.end());
        }
        return out;
      },
      "grid"_a);
  m.def(
      "compact",
      [](const std::vector<std::optional<int>>& items) {
        std::vector<int> out;
        for (const auto& item : items) {
          if (item) out.push_back(*item);
        }
        return out;
      },
      "items"_a);

  // `std::array<T, N>`: tuple up to 16 elements, empty array, and >16 fallback.
  m.def(
      "scaleVec3",
      [](std::array<double, 3> v, double k) {
        return std::array<double, 3>{v[0] * k, v[1] * k, v[2] * k};
      },
      "v"_a, "k"_a = 1.0);
  m.def("emptyArr", [](std::array<int, 0> a) { return a; }, "a"_a);
  m.def("bigArr", [](std::array<int, 20> a) { return a; }, "a"_a);

  // `std::map` and `std::unordered_map` <-> `Record<string, V>`.
  m.def(
      "sumMap",
      [](const std::map<std::string, int>& scores) {
        int total = 0;
        for (const auto& [k, v] : scores) {
          (void)k;
          total += v;
        }
        return total;
      },
      "scores"_a);
  m.def(
      "echoGroups",
      [](const std::unordered_map<std::string, std::vector<int>>& groups) { return groups; },
      "groups"_a);

  // `std::variant` and `std::monostate` <-> union types.
  using SqlVal = std::variant<std::monostate, int, std::string>;
  m.def(
      "describeVariant",
      [](const SqlVal& v) -> std::string {
        if (std::holds_alternative<std::monostate>(v)) return "null";
        if (std::holds_alternative<int>(v)) return "int:" + std::to_string(std::get<int>(v));
        return "str:" + std::get<std::string>(v);
      },
      "v"_a);
  m.def(
      "echoSql", [](const SqlVal& v) { return v; }, "v"_a);
  m.def(
      "intOrDouble",
      [](const std::variant<int, double>& v) -> std::string {
        return std::holds_alternative<int>(v) ? "int" : "double";
      },
      "v"_a);
  m.def(
      "bigOrDouble", [](const std::variant<int64_t, double>& v) { return v; }, "v"_a);
  m.def(
      "listVariant",
      [](const std::variant<std::vector<int>, std::vector<std::string>>& v) { return v; }, "v"_a);
}
