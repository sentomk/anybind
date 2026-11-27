/*
 * Tests for plain C++ struct <-> JS object conversion via `ANYBIND_STRUCT`,
 * `ANYBIND_STRUCT_DEFAULTS`, and manual `anybind::struct_traits<T>`.
 */
#include <anybind/anybind.h>
#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct User {
  std::string name;
  std::optional<int> age;
  std::optional<std::string> bio;
};

struct RenderOptions {
  bool gfm = true;
  bool xhtml = false;
  int tabWidth = 4;
  std::optional<std::string> baseUrl;
};

struct Polygon {
  int64_t id = 0;
  Point origin;
  std::vector<Point> vertices;
};

namespace {

struct LocalFlag {
  bool enabled = false;
};

}  // namespace

/* Field names that also exist on Object.prototype must not read the built-ins. */
struct ProtoRequired {
  int constructor = 0;
};

struct ProtoOptional {
  std::optional<std::string> toString;
};

struct ProtoDefaults {
  int constructor = 7;
  int valueOf = 8;
};

struct CustomKeys {
  std::string kebabValue;
  int count = 0;
};

/* Counts copies so tests can check that converted arguments are moved, not copied. */
static int g_tracked_copies = 0;

struct Tracked {
  int n = 0;
  Tracked() = default;
  Tracked(const Tracked& other) : n(other.n) { ++g_tracked_copies; }
  Tracked(Tracked&& other) noexcept : n(other.n) {}
  Tracked& operator=(const Tracked& other) {
    n = other.n;
    ++g_tracked_copies;
    return *this;
  }
  Tracked& operator=(Tracked&& other) noexcept {
    n = other.n;
    return *this;
  }
};

ANYBIND_STRUCT(Point, x, y);
ANYBIND_STRUCT(User, name, age, bio);
ANYBIND_STRUCT_DEFAULTS(RenderOptions, gfm, xhtml, tabWidth, baseUrl);
ANYBIND_STRUCT(Polygon, id, origin, vertices);
ANYBIND_STRUCT(LocalFlag, enabled);
ANYBIND_STRUCT(Tracked, n);
ANYBIND_STRUCT(ProtoRequired, constructor);
ANYBIND_STRUCT(ProtoOptional, toString);
ANYBIND_STRUCT_DEFAULTS(ProtoDefaults, constructor, valueOf);

namespace anybind {
template <>
struct struct_traits<CustomKeys> {
  static constexpr bool is_specialized = true;
  static constexpr bool has_defaults = false;
  static constexpr char kKebab[] = "kebab-key";
  static constexpr char kCount[] = "count";
  static constexpr auto fields() noexcept {
    return std::make_tuple(make_field<kKebab>(&CustomKeys::kebabValue),
                           make_field<kCount>(&CustomKeys::count));
  }
};
}  // namespace anybind

ANYBIND_MODULE(test_struct, m) {
  using namespace anybind::literals;

  m.def(
      "translate", [](Point p, double dx, double dy) { return Point{p.x + dx, p.y + dy}; }, "p"_a,
      "dx"_a = 0.0, "dy"_a = 0.0);
  m.def(
      "echoUser", [](const User& u) { return u; }, "user"_a);
  m.def(
      "resolveOptions", [](const RenderOptions& opts) { return opts; },
      "options"_a = RenderOptions{});
  m.def(
      "echoPolygon", [](const Polygon& poly) { return poly; }, "poly"_a);
  m.def(
      "pointOrLabel", [](const std::variant<Point, std::string>& v) { return v; }, "v"_a);
  m.def(
      "flipLocal", [](LocalFlag f) { return LocalFlag{!f.enabled}; }, "flag"_a);
  m.def(
      "echoCustom", [](const CustomKeys& c) { return c; }, "c"_a);

  m.def(
      "echoProtoRequired", [](const ProtoRequired& v) { return v; }, "v"_a);
  m.def(
      "echoProtoOptional", [](const ProtoOptional& v) { return v; }, "v"_a);
  m.def(
      "echoProtoDefaults", [](const ProtoDefaults& v) { return v; }, "v"_a);

  // Every way of receiving an argument must avoid copying the converted value.
  m.def("trackedCopies", [] { return g_tracked_copies; });
  m.def("trackedByValue", [](Tracked t) { return t.n; });
  m.def("trackedByConstRef", [](const Tracked& t) { return t.n; });
  m.def("trackedByRef", [](Tracked& t) { return ++t.n; });
  m.def("trackedByRvalue", [](Tracked&& t) {
    Tracked owned(std::move(t));
    return owned.n;
  });
  m.def("trackedVector", [](std::vector<Tracked> v) { return static_cast<int>(v.size()); });
  m.def("trackedMap", [](std::map<std::string, Tracked> v) { return static_cast<int>(v.size()); });
}
