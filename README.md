# anybind

> Bind to any runtime. Never return `any`.

**anybind** is a modern, header-only C++ binding library for **every runtime that implements
[Node-API](https://nodejs.org/api/n-api.html)**: Node.js, Electron, Bun, Deno, React Native
(via Node-API for Hermes), HarmonyOS ArkTS, and embedded JS engines. It is inspired by
[nanobind](https://github.com/wjakob/nanobind) and pybind11, and it generates accurate
TypeScript declarations straight from your C++ signatures.

> **Status: pre-alpha.** The API will change. Not ready for production use.

## Example

```cpp
#include <anybind/anybind.h>

int add(int a, int b) { return a + b; }

ANYBIND_MODULE(example, m) {
  m.def("add", &add);
  m.def("greet", [](const std::string& who) { return "hello, " + who; });
}
```

```cmake
cmake_minimum_required(VERSION 3.21)
project(example CXX)
execute_process(COMMAND node -p "require('anybind').cmakeModule"
                OUTPUT_VARIABLE ANYBIND_CMAKE OUTPUT_STRIP_TRAILING_WHITESPACE)
include(${ANYBIND_CMAKE})
anybind_add_module(example example.cpp)
```

```js
const m = require("./build/example.node");
m.add(1, 2); // 3
m.add("1", 2); // TypeError: argument 1: expected number
```

```sh
npx anybind-stubgen build/example.node example.d.ts
# export function add(arg0: number, arg1: number): number;
# export function greet(arg0: string): string;
```

## Design goals

- **Any runtime.** Uses only the ABI-stable Node-API C interface. No V8 internals, no
  `node-addon-api` dependency.
- **Never `any`.** TypeScript declarations are generated from C++ types.
- **Small and fast to compile.** Header-only, C++17, minimal template bloat.
- **Modern C++.** C++20 coroutines ↔ Promises, zero-copy TypedArrays (planned).

## Roadmap

- [x] Free functions and lambdas: `bool`, arithmetic types, `std::string`
- [x] C++ exceptions → JS errors, argument type checking
- [x] `.d.ts` generation (`anybind-stubgen`)
- [x] Named arguments (`anybind::arg("x")`) and default values
- [ ] Classes, methods, properties, enums
- [ ] Ownership and lifetime policies
- [ ] `std::vector`, `std::optional`, `std::variant`, `std::function`, `bigint`
- [ ] TypedArray / ArrayBuffer zero-copy views
- [ ] Async: thread-safe functions, C++20 coroutines ↔ Promise
- [x] Windows support (MSVC; delay-loaded, so addons also load in Electron and other hosts)
- [ ] CI across Node.js, Bun, Deno, Electron, React Native, HarmonyOS

## Development

```sh
npm ci
npm test   # configures CMake, builds the test addons, runs node --test
```

Requires CMake ≥ 3.21, a C++17 compiler (MSVC or clang-cl on Windows), and Node.js ≥ 18.
Node-API headers come from the [`node-api-headers`](https://github.com/nodejs/node-api-headers)
package, so no Node.js source headers need to be installed.

## License

[MIT](LICENSE)
