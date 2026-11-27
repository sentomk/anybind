/*
 * Tests for the per-environment type registry (`napi_set_instance_data`),
 * including worker-thread isolation and live `ts::bound<T>` resolution.
 */
#include <anybind/anybind.h>
#include <stdint.h>

#include <stdexcept>
#include <string>

namespace {

struct Alpha {};
struct Beta {};
struct Gamma {};

}  // namespace

namespace anybind {
namespace detail {

template <>
struct type_caster<Alpha> {
  using ts = anybind::ts::bound<Alpha>;
  static Alpha from_js(napi_env, napi_value) { return {}; }
  static napi_value to_js(napi_env env, Alpha) {
    napi_value u;
    check(env, napi_get_undefined(env, &u));
    return u;
  }
};

}  // namespace detail
}  // namespace anybind

static std::string run_checks(napi_env env) {
  using anybind::detail::type_registry;

  std::string failures;
  auto expect = [&](const char* label, bool ok) {
    if (!ok) failures += std::string(label) + "\n";
  };

  auto& reg = type_registry::get(env);
  expect("try_get matches get", type_registry::try_get(env) == &reg);

  const auto* alpha = reg.find<Alpha>();
  const auto* beta = reg.find<Beta>();
  expect("alpha registered", alpha != nullptr && alpha->name == "Alpha");
  expect("beta registered", beta != nullptr && beta->name == "Beta");
  expect("alpha has ctor_ref", alpha != nullptr && alpha->ctor_ref != nullptr);
  expect("gamma not registered initially", reg.find<Gamma>() == nullptr);

  if (alpha != nullptr && beta != nullptr) {
    expect("same env salt", alpha->tag.lower == beta->tag.lower);
    expect("distinct upper tags",
           alpha->tag.upper != beta->tag.upper && alpha->tag.upper != 0 && beta->tag.upper != 0);
  }

  // Duplicate registration must fail without leaking a newly created ref.
  napi_value dummy = nullptr;
  napi_ref dup_ref = nullptr;
  anybind::detail::check(env, napi_create_object(env, &dummy));
  anybind::detail::check(env, napi_create_reference(env, dummy, 1, &dup_ref));
  bool threw = false;
  try {
    reg.register_type<Alpha>(env, "Alpha", dup_ref);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  expect("duplicate registration throws", threw);

  return failures;
}

ANYBIND_MODULE(test_registry, m) {
  using namespace anybind::literals;
  using anybind::detail::check;
  using anybind::detail::type_registry;

  napi_env env = m.env();
  auto& reg = type_registry::get(env);

  // Attach a real persistent napi_ref so env teardown exercises ref cleanup.
  napi_value proto = nullptr;
  napi_ref alpha_ref = nullptr;
  check(env, napi_create_object(env, &proto));
  check(env, napi_create_reference(env, proto, 1, &alpha_ref));
  reg.register_type<Alpha>(env, "Alpha", alpha_ref);
  reg.register_type<Beta>(env, "Beta");

  m.def("failures", [env] { return run_checks(env); });
  m.def("echoAlpha", [](Alpha a) { return a; }, "a"_a);
  m.def("registryAddr", [env] {
    auto* ptr = &type_registry::get(env);
    return std::to_string(reinterpret_cast<uintptr_t>(ptr));
  });
  m.def("alphaTagLower", [env] {
    const auto* info = type_registry::get(env).find<Alpha>();
    return std::to_string(info ? info->tag.lower : 0);
  });
  m.def("hasGamma", [env] { return type_registry::get(env).find<Gamma>() != nullptr; });
  m.def("registerGamma", [env] { type_registry::get(env).register_type<Gamma>(env, "Gamma"); });
}
