import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { workerData } from "node:worker_threads";

const require = createRequire(import.meta.url);
const h = require(workerData.handlesPath);

const localObj = { tag: "worker" };
h.storeObject(localObj);
h.storeCallback((n) => n + 10);
assert.equal(h.loadObject(), localObj);
assert.equal(h.invokeStored(5), 15);
// Intentionally leave both strong refs populated through worker teardown.
