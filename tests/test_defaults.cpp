// Table-driven checks for detail::arithmetic_value_fits, which guards
// numeric default values against silent truncation and overflow.
#include <anybind/anybind.h>

#include <cstdint>
#include <limits>
#include <string>

using anybind::detail::arithmetic_value_fits;

#define EXPECT_FITS(T, v, expected)                        \
  if (arithmetic_value_fits<T>(v) != (expected)) {         \
    failures += #T " <- " #v " should be " #expected "\n"; \
  }

static std::string run_checks() {
  std::string failures;
  constexpr double inf = std::numeric_limits<double>::infinity();
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();

  // Floating point into integers.
  EXPECT_FITS(int, 1.0, true);
  EXPECT_FITS(int, 1.9, false);
  EXPECT_FITS(int, -2147483648.0, true);
  EXPECT_FITS(int, 2147483648.0, false);
  EXPECT_FITS(std::uint8_t, -1.0, false);
  EXPECT_FITS(std::int64_t, 9223372036854775808.0, false);
  EXPECT_FITS(std::uint64_t, 18446744073709551616.0, false);
  EXPECT_FITS(int, nan, false);
  EXPECT_FITS(int, inf, false);
  EXPECT_FITS(bool, 1.0, true);
  EXPECT_FITS(bool, 2.0, false);

  // Integers into integers.
  EXPECT_FITS(std::uint8_t, 255, true);
  EXPECT_FITS(std::uint8_t, 256, false);
  EXPECT_FITS(std::uint8_t, -1, false);
  EXPECT_FITS(std::int8_t, -128, true);
  EXPECT_FITS(std::int8_t, 128, false);
  EXPECT_FITS(std::uint32_t, std::int64_t{-1}, false);
  EXPECT_FITS(std::int32_t, std::uint32_t{2147483648u}, false);
  EXPECT_FITS(std::int64_t, std::uint64_t{9223372036854775807u}, true);
  EXPECT_FITS(std::int64_t, std::uint64_t{9223372036854775808u}, false);
  EXPECT_FITS(std::int64_t, -1, true);
  EXPECT_FITS(unsigned, true, true);
  EXPECT_FITS(bool, 1, true);
  EXPECT_FITS(bool, 2, false);

  // Integers into floating point.
  EXPECT_FITS(float, 16777216, true);
  EXPECT_FITS(float, 16777217, false);
  EXPECT_FITS(float, -16777216, true);
  EXPECT_FITS(double, std::int64_t{9007199254740992}, true);
  EXPECT_FITS(double, std::int64_t{9007199254740993}, false);
  EXPECT_FITS(double, std::uint64_t{18446744073709551615u}, false);
  EXPECT_FITS(double, 2147483647, true);

  // Floating point into floating point: rounding is accepted, overflow is not.
  EXPECT_FITS(float, 0.1, true);
  EXPECT_FITS(float, 1e300, false);
  EXPECT_FITS(float, -1e300, false);
  EXPECT_FITS(float, inf, true);
  EXPECT_FITS(float, nan, true);
  EXPECT_FITS(double, 1.5f, true);

  return failures;
}

ANYBIND_MODULE(test_defaults, m) {
  m.def("failures", [] { return run_checks(); });
}
