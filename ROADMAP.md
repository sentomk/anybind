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

The first minor release must answer one question for users: *can I bind my C++ library
with this?* It ships only once every release criterion below is met.

### Scope, in dependency order

1. **Foundations**
   - [ ] Composable TypeScript type names (to support `Array<T>`, `T | undefined`, nested
     types); today each caster has a fixed `ts_name`.
   - [ ] Per-environment type registry via instance data, so the addon works when loaded
     several times (worker threads, multiple contexts).
   - [ ] Handle types for holding JS values in C++: `object`, `function`, persistent
     references.
   - [ ] `.d.ts` generation at build time, without loading the `.node` (needed for
     cross-compilation, e.g. HarmonyOS or Android arm64 built on an x86 host).
2. **Type system**
   - [ ] `std::vector` / `std::array` ↔ `Array`, `std::optional` ↔ `T | undefined`,
     `std::variant` ↔ union, `std::map` / plain structs ↔ objects, `std::string_view`
   - [ ] 64-bit integers ↔ `bigint`
   - [ ] Synchronous callbacks: `std::function` ↔ JS functions
   - [ ] Function overloads, emitted as TypeScript overloads
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
  get expensive once users adopt it
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
