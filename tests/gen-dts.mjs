// Writes the generated .d.ts of every test addon to build/dts, so that
// `tsc -p tests/tsconfig.dts.json` can check that the declarations are valid.
//
//   npm run build:test && node tests/gen-dts.mjs

import { spawnSync } from "node:child_process";
import { mkdirSync, readdirSync, rmSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const addonDir = path.join(root, "build", "tests");
const outDir = path.join(root, "build", "dts");
const stubgen = path.join(root, "bin", "anybind-stubgen.mjs");

// test_bad_default must fail to load. test_registry refers to class names whose
// declarations are not generated yet (classes are 0.1 step 3).
const skip = new Set(["test_bad_default", "test_registry"]);

const addons = readdirSync(addonDir)
  .filter((f) => f.endsWith(".node"))
  .map((f) => path.basename(f, ".node"))
  .filter((name) => !skip.has(name));
if (addons.length === 0) {
  console.error(`no addons in ${addonDir}; run \`npm run build:test\` first`);
  process.exit(1);
}

rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
for (const name of addons) {
  const r = spawnSync(
    process.execPath,
    [
      stubgen,
      path.join(addonDir, `${name}.node`),
      path.join(outDir, `${name}.d.ts`),
    ],
    { encoding: "utf8" },
  );
  if (r.status !== 0) {
    console.error(`anybind-stubgen failed for ${name}:\n${r.stderr}`);
    process.exit(1);
  }
}
console.log(`wrote ${addons.length} declaration files to build/dts`);
