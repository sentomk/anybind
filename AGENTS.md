# anybind Project Rules

## C++ Style & Conventions

- **C++ standard:** Keep C++17 as the baseline. Gate any future C++20 features behind feature-test macros.
- **Integer and size types:** Include `<stdint.h>` and `<stddef.h>`, and write unqualified `size_t`, `int*_t`, `uint*_t`, `uintptr_t`, `intmax_t`, and `uintmax_t` (do not use `<cstdint>`, `<cstddef>`, or `std::`-qualified integer/size types).
- **Comments:**
  - Public API documentation comments: `/** ... */`
  - Outer / section / block comments: `/* ... */`
  - Inside-block comments and namespace closing comments: `// ...`
- **Naming:** Keep public APIs and JS-facing error messages clear and professional. Internal helpers under `namespace detail` or `private:` may use shorter, informal names (avoiding reserved C++ identifiers such as `__foo` or `_Foo`).
- **Compiler warnings:** All code must compile cleanly with zero warnings under `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) and `/W4 /WX` (MSVC).

## Verification

- Before committing changes, verify that `npm test` and `npx biome ci .` pass.
