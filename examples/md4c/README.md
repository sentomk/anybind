# md4c example

A minimal [anybind](../../README.md) integration wrapping [md4c](https://github.com/mity/md4c)
(`release-0.5.2`) as a Node-API addon.

## Build and run

From the repository root (after `npm ci`):

```sh
cmake -S examples/md4c -B examples/md4c/build
cmake --build examples/md4c/build
node examples/md4c/demo.mjs
```

Generate TypeScript declarations:

```sh
node bin/anybind-stubgen.mjs examples/md4c/build/md4c.node examples/md4c/md4c.d.ts
```
