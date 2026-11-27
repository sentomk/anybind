// Replaces the addon's instance data in a worker, so the main thread's
// registry stays intact, and checks that anybind refuses the foreign pointer.
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { workerData } from "node:worker_threads";

const require = createRequire(import.meta.url);
const reg = require(workerData.registryPath);

assert.equal(reg.registryTrusted(), true);
assert.equal(reg.bumpCounter(), 1);
reg.clobberInstanceData();
assert.equal(reg.registryTrusted(), false);
assert.throws(() => reg.bumpCounter(), {
  name: "Error",
  message: /instance data was replaced by napi_set_instance_data\(\)/,
});
