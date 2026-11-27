/*
 * Tests for 64-bit integers: plain `int64_t` / `uint64_t` / `size_t` map to
 * JS numbers (safe range only), and `anybind::bigint<T>` maps to JS bigint
 * with full 64-bit precision, including STL composition and `.d.ts`.
 */
#include <anybind/anybind.h>
#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <optional>
#include <vector>

ANYBIND_MODULE(test_bigint, m) {
  using namespace anybind::literals;
  using anybind::bigint;

  // Plain 64-bit integers: `number` in the `.d.ts`, bigint also accepted as input.
  m.def(
      "echoI64", [](int64_t v) { return v; }, "v"_a = int64_t{-1});
  m.def(
      "echoU64", [](uint64_t v) { return v; }, "v"_a);
  m.def(
      "echoSize", [](size_t v) { return v; }, "v"_a);
  m.def("count", [] { return std::vector<int>{1, 2, 3}.size(); });
  m.def(
      "addI64", [](int64_t a, int64_t b) { return a + b; }, "a"_a, "b"_a);
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

  // `anybind::bigint<T>`: `number | bigint` in, `bigint` out.
  m.def(
      "bigI64", [](bigint<int64_t> v) { return v; }, "v"_a = bigint<int64_t>(-1));
  m.def(
      "bigU64", [](bigint<uint64_t> v) { return v; },
      "v"_a = bigint<uint64_t>(std::numeric_limits<uint64_t>::max()));
  m.def(
      "bigAddI64", [](bigint<int64_t> a, bigint<int64_t> b) { return bigint<int64_t>(a + b); },
      "a"_a, "b"_a);
  m.def(
      "bigIncAll",
      [](const std::vector<bigint<int64_t>>& nums) {
        std::vector<bigint<int64_t>> out;
        out.reserve(nums.size());
        for (int64_t n : nums) out.push_back(n + 1);
        return out;
      },
      "nums"_a);
  m.def(
      "bigOptU64", [](std::optional<bigint<uint64_t>> v) { return v; }, "v"_a);
}
