# Roadmap

Releases are gated on quality criteria, not dates. Until 0.1.0, anybind stays on 0.0.x
development snapshots and is not announced.

## Positioning

Using only Node-API is not unique on its own: `node-addon-api` is header-only and
ABI-stable too, and `napi-rs` covers the same ground for Rust. anybind's value lies in what
the C++ options lack:

- **nanobind/pybind11-style ergonomics:** automatic type conversion and argument checking
  instead of hand-written `CallbackInfo` handling.
- **Accurate TypeScript declarations** generated from C++ signatures, with no `any`.
- **A build and distribution toolchain** (prebuilds, per-platform packages), the way
  `napi-rs` does it for Rust.
- **Runtime portability proven in CI**, not just promised by the ABI.

Target users are people who **already have C++ code**: native modules in Electron apps,
media/CAD/game/inference engines, and HarmonyOS native development (ArkTS native modules
use Node-API).

## Done (0.0.x)

- [x] Free functions and lambdas: `bool`, arithmetic types, `std::string`
- [x] C++ exceptions → JS errors, argument type checking
- [x] `.d.ts` generation (`anybind-stubgen`)
- [x] Named arguments (`anybind::arg("x")`) and default values
- [x] Windows support (MSVC; delay-loaded, so addons also load in Electron and other hosts)

## 0.1.0: binding a real C++ library

The first minor release must answer one question for users: _can I bind my C++ library
with this?_ It ships only once every release criterion below is met.

### Scope, in dependency order

1. **Foundations**
   - [x] Structured TypeScript type descriptors, replacing the fixed `ts_name` string.
     - Each `type_caster` declares `using ts = ...`: a tree of template types such as
       `ts::string`, `ts::array<ts_of<T>>`, `ts::union_<...>`.
     - First node set, limited to what 0.1 needs: primitives, literals, `array`, `tuple`,
       `union_`, `undefined`, object/record, `function`, `bound<T>` (placeholder for a
       bound class) and `io<In, Out>` (different parameter and return types, e.g.
       `anybind::bigint<T>` is `number | bigint` as a parameter and `bigint` as a return
       value).
     - Optional parameters stay a function-signature concern; the existing `arg` default
       value logic is kept.
     - The runtime serializer produces the TS text for `__anybind_dts__`; other outputs
       (ArkTS, JSON Schema) can be added later. Build-time extraction does not use a
       compile-time encoder (see the build-time `.d.ts` item below), which also settles
       how class names are known at build time.
     - Risks: deeply nested types may slow compilation or hit template depth limits, so
       prototype and measure first. The `ts::` nodes become public API (users need them
       for custom casters in 0.3), so they are part of the 0.1 API review.
     - Done when: all existing types are migrated, the generated `.d.ts` is identical to
       today's, there are tests for nested types and for differing parameter/return
       types, and `npm test` and `biome ci` pass.
   - [x] Per-environment type registry via instance data, so the addon works when loaded
         several times (worker threads, multiple contexts).
   - [x] Handle types for holding JS values in C++: `object`, `function`, persistent
         references.
   - [ ] `.d.ts` generation at build time, without loading the `.node` (needed for
         cross-compilation, e.g. HarmonyOS or Android arm64 built on an x86 host).
     - Encoding signatures into a binary section at compile time does not work with the
       current API: export names (`m.def("add", ...)`), `arg` names and whether a
       parameter has a default are run-time values, which C++17 cannot turn into
       constants without a macro-based API.
     - Instead, the binding sources are also compiled for the host into a small
       executable, linked with a recording Node-API implementation that needs no JS
       engine. It runs the module's initialization and writes the `.d.ts` the module
       builds, so the output is the same text as `__anybind_dts__`.
     - Node-API functions the recorder does not implement are generated from the
       `node-api-headers` symbol list as stubs that report "not supported during stub
       generation", so bindings that use them elsewhere still link.
     - CMake: `anybind_generate_dts(<target>)` builds the executable and generates the
       `.d.ts` as part of the build. When cross-compiling, it runs through
       `CMAKE_CROSSCOMPILING_EMULATOR` if set; otherwise the `.d.ts` comes from a host
       build of the same project.
     - Class names need no compile-time mechanism: `class_<T>("Foo")` runs during
       initialization like any other registration.
     - Limits: the binding code must compile for the host, and initialization code that
       calls unsupported Node-API functions fails stub generation with a clear error.
2. **Type system**
   - [x] `std::vector` / `std::array` ↔ `Array`, `std::optional` ↔ `T | undefined`,
         `std::variant` ↔ union, `std::map` / plain structs ↔ objects, `std::string_view`
   - [x] 64-bit integers ↔ `number` (safe range, so `size_t` results stay plain numbers and
         the `.d.ts` is the same on every platform); `anybind::bigint<T>` ↔ `bigint` for full
         precision
   - [x] Synchronous callbacks: `std::function` ↔ JS functions (callbacks may be stored and
         called later, but only on the JS thread; cross-thread calls are step 4)
   - [x] Function overloads, emitted as TypeScript overloads. Extra arguments are ignored
         (as in JS), so dispatch tries overloads that take every argument first, in
         declaration order, then those that would ignore extra arguments, fewest ignored
         first.
3. **Classes**
   - [ ] `class_<T>`: constructors, methods, properties, static members
   - [ ] Enums and inheritance
   - [ ] Ownership policies (take ownership, reference, reference-internal,
         `std::shared_ptr`)
   - [ ] Object identity: the same C++ object always maps to the same JS object
   - [ ] Class declarations in the generated `.d.ts`, with C++ doc comments carried over
4. **Binary data and async**
   - [ ] Zero-copy `TypedArray` / `ArrayBuffer` / `Buffer` views
   - [ ] Run work on a thread pool and return a `Promise`
   - [ ] Thread-safe callbacks
5. **Polish**
   - [ ] Clear, consistent error messages
   - [ ] Project template (`npm create anybind`)

### Release criteria

- [ ] Three real libraries fully bound with anybind, each covering a different area:
  - **md4c**: free functions and strings (the first integration)
  - **RE2**: classes and lifetimes
  - **zstd or xxHash**: buffers, `bigint`, async
- [ ] CI green on Node.js 18–24, Electron, Bun and Deno, across Linux, macOS and Windows
- [ ] Published comparison with `node-addon-api`: lines of binding code, compile time,
      binary size, call overhead
- [ ] Documentation: getting started, conversion rules for every type, ownership guide,
      migration guide from `node-addon-api`
- [ ] Public API review: the API shape should be stable at release, since breaking changes
      get expensive once users adopt it (including the `ts::` descriptor nodes)
- [ ] Scope locked: anything not listed above goes to 0.2

**Launch demo:** one `.node` file loaded in Node, Bun, Deno and Electron, the generated
`.d.ts` with classes and overloads, and the code-size comparison with `node-addon-api`.

## 0.2: distribution and ecosystem

- [ ] `anybind` CLI: cross-platform prebuilds, per-platform npm packages, GitHub Actions
      templates
- [ ] React Native (Node-API for Hermes)
- [ ] C++20 coroutines ↔ `Promise`

## 0.3: advanced

- [ ] Sharing bound types between separate anybind addons
- [ ] Documented public API for custom type casters
- [ ] Finer-grained ownership and lifetime control

## Open questions

- **HarmonyOS ArkTS: 0.1 or 0.2?** Worth pulling into 0.1 (with a HarmonyOS demo) if the
  launch targets the Chinese and HarmonyOS developer community, where there is likely no
  comparable tool yet. The cost is more 0.1 work and a more complex CI.
