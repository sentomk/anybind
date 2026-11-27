/*
 * Tests for 64-bit integer <-> JS bigint conversion (`int64_t`, `uint64_t`),
 * including full 64-bit bounds, safe number inputs, STL composition and `.d.ts`.
 */
#include <anybind/anybind.h>
#include <stdint.h>

#include <limits>
#include <optional>
#include <vector>

ANYBIND_MODULE(test_bigint, m) {
  using namespace anybind::literals;

  m.def(
      "echoI64", [](int64_t v) { return v; }, "v"_a = int64_t{-1});
  m.def(
      "echoU64", [](uint64_t v) { return v; }, "v"_a = std::numeric_limits<uint64_t>::max());
  m.def(
      "addI64", [](int64_t a, int64_t b) { return a + b; }, "a"_a, "b"_a);
  m.def(
      "addU64", [](uint64_t a, uint64_t b) { return a + b; }, "a"_a, "b"_a);
  m.def(
      "incAllI64",
      [](const std::vector<int64_t>& nums) {
        std::vector<int64_t> out;
        out.reserve(nums.size());
        for (int64_t n : nums) out.push_back(n + 1);
        return out;
      },
      "nums"_a);
  m.def(
      "optU64", [](std::optional<uint64_t> v) { return v; }, "v"_a = std::nullopt);
}
