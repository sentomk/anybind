/*
 * A module whose initialization calls a Node-API function that the stub
 * generator does not implement: its build-time stub generation must fail with
 * a clear error, while the addon itself loads normally.
 */
#include <anybind/anybind.h>

ANYBIND_MODULE(test_stubgen_unsupported, m) {
  napi_value external = nullptr;
  anybind::detail::check(m.env(),
                         napi_create_external(m.env(), nullptr, nullptr, nullptr, &external));
  m.def("ping", [] { return 1; });
}
