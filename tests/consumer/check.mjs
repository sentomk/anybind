// Checks the README example after it was built from the installed package.
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
// Multi-config generators (Visual Studio) put the module in a per-config subdirectory.
const addonPath = ["./build/example.node", "./build/Release/example.node"].find(
  (p) => existsSync(new URL(p, import.meta.url)),
);
assert.ok(addonPath, "example.node was not built");
const m = require(addonPath);

assert.equal(m.add(1, 2), 3);
assert.equal(m.greet("anybind"), "hello, anybind");
assert.throws(() => m.add("1", 2), {
  name: "TypeError",
  message: /argument 1: expected number/,
});

const dts = readFileSync(new URL("./example.d.ts", import.meta.url), "utf8");
assert.match(dts, /export function add\(arg0: number, arg1: number\): number;/);
assert.match(dts, /export function greet\(arg0: string\): string;/);
console.log("consumer example OK");
