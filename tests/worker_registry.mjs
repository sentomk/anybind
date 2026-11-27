import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { parentPort, workerData } from "node:worker_threads";

const require = createRequire(import.meta.url);
const reg = require(workerData.registryPath);

assert.equal(reg.failures(), "");
assert.equal(reg.hasGamma(), false);
assert.match(
  reg.__anybind_dts__,
  /export function echoAlpha\(a: Alpha\): Alpha;/,
);
reg.registerGamma();
assert.equal(reg.hasGamma(), true);

parentPort.postMessage({
  addr: reg.registryAddr(),
  salt: reg.alphaTagLower(),
});

// Stay alive until main verifies all workers concurrently, avoiding heap reuse.
parentPort.once("message", () => {
  if (typeof parentPort.close === "function") {
    parentPort.close();
  } else if (typeof globalThis.close === "function") {
    globalThis.close();
  }
});
