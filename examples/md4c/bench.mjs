import { createRequire } from "node:module";
import { performance } from "node:perf_hooks";
import { fileURLToPath } from "node:url";

/**
 * Benchmark comparing `anybind + md4c` against pure-JS Markdown renderers
 * (`marked`, `markdown-it`) on tiny / medium / large GFM inputs.
 *
 * Build md4c in Release mode first:
 *   cmake -S examples/md4c -B examples/md4c/build -DCMAKE_BUILD_TYPE=Release
 *   cmake --build examples/md4c/build
 *
 * Optional JS baselines (installed locally under examples/md4c/node_modules):
 *   npm install --no-save --prefix examples/md4c marked markdown-it
 */
async function createParsers() {
  const require = createRequire(import.meta.url);
  const addonPath = fileURLToPath(
    new URL("./build/md4c.node", import.meta.url),
  );

  /** @type {typeof import("./md4c.d.ts")} */
  const md4c = require(addonPath);

  const parsers = [{ name: "anybind+md4c", render: (md) => md4c.toHtml(md) }];

  try {
    const { Marked } = await import("marked");
    const marked = new Marked({ gfm: true });
    parsers.push({
      name: "marked",
      render: (md) => marked.parse(md, { async: false }),
    });
  } catch (err) {
    if (err?.code !== "ERR_MODULE_NOT_FOUND") throw err;
    console.warn("Skipping marked (not installed in examples/md4c).");
  }

  try {
    const { default: MarkdownIt } = await import("markdown-it");
    const mdIt = new MarkdownIt({ linkify: true });
    parsers.push({
      name: "markdown-it",
      render: (md) => mdIt.render(md),
    });
  } catch (err) {
    if (err?.code !== "ERR_MODULE_NOT_FOUND") throw err;
    console.warn("Skipping markdown-it (not installed in examples/md4c).");
  }

  return parsers;
}

/* === Fixtures === */

const tinyDoc =
  "Hello **anybind**! Visit [Node-API](https://nodejs.org/api/n-api.html) " +
  "for ~~old~~ _modern_ C++ bindings.\n";

const section = [
  "## Performance & Compatibility",
  "",
  "This section mixes **bold**, _italic_, ~~strikethrough~~, `inline code`,",
  "and an autolink https://example.com/anybind to exercise GFM rules.",
  "",
  "> Native addons built with Node-API stay ABI-stable across Node.js,",
  "> Electron, Bun, and Deno without recompiling per engine release.",
  "",
  "- [x] Free functions and lambdas",
  "- [x] Named arguments with default values",
  "- [ ] Classes, ownership policies, and zero-copy TypedArrays",
  "",
  "| Library | Language | Auto `.d.ts` | Notes |",
  "| ------- | -------- | ------------ | ----- |",
  "| anybind | C++17    | yes          | Header-only Node-API |",
  "| md4c    | C        | via anybind  | CommonMark + GFM |",
  "",
  "```cpp",
  "ANYBIND_MODULE(md4c, m) {",
  '  m.def("toHtml", &to_html, "markdown"_a, "gfm"_a = true);',
  "}",
  "```",
  "",
].join("\n");

const fixtures = [
  { name: "tiny (~110 B)", input: tinyDoc, minMs: 250 },
  { name: "medium (~5 KB)", input: section.repeat(8), minMs: 350 },
  { name: "large (~200 KB)", input: section.repeat(320), minMs: 500 },
];

/* === Runner === */

function benchOne(render, input, minMs) {
  // Warm up V8 JIT and native caches before measuring.
  const warmupEnd = performance.now() + 80;
  let sink = 0;
  while (performance.now() < warmupEnd) {
    sink ^= render(input).length;
  }

  let iters = 0;
  const start = performance.now();
  let elapsed = 0;
  do {
    // Batch small inputs to keep timer overhead negligible.
    const batch = input.length < 1024 ? 50 : 1;
    for (let i = 0; i < batch; i++) {
      sink ^= render(input).length;
    }
    iters += batch;
    elapsed = performance.now() - start;
  } while (elapsed < minMs);

  const bytes = Buffer.byteLength(input, "utf8");
  const avgMs = elapsed / iters;
  const opsPerSec = (iters / elapsed) * 1000;
  const mbPerSec = (bytes * iters) / (elapsed * 1000);
  return { avgMs, opsPerSec, mbPerSec, iters, sink };
}

function formatTable(rows) {
  const baseline = rows[0].opsPerSec;
  return rows.map((r) => ({
    parser: r.name,
    "ops/s": Math.round(r.opsPerSec).toLocaleString("en-US"),
    "avg (ms)": r.avgMs.toFixed(r.avgMs < 0.1 ? 4 : 3),
    "MB/s": r.mbPerSec.toFixed(1),
    relative: `${(r.opsPerSec / baseline).toFixed(2)}x`,
  }));
}

async function main() {
  const parsers = await createParsers();
  if (parsers.length === 0) {
    console.log(
      "No parsers registered yet. Edit createParsers() in examples/md4c/bench.mjs.",
    );
    return;
  }

  // Sanity-check that every parser returns non-empty HTML.
  for (const p of parsers) {
    const out = p.render(tinyDoc);
    if (typeof out !== "string" || out.length === 0) {
      throw new Error(`Parser "${p.name}" did not return a non-empty string`);
    }
  }

  for (const fx of fixtures) {
    const kb = (Buffer.byteLength(fx.input, "utf8") / 1024).toFixed(2);
    console.log(`\n=== ${fx.name} (${kb} KB) ===`);
    const rows = parsers.map((p) => ({
      name: p.name,
      ...benchOne(p.render, fx.input, fx.minMs),
    }));
    console.table(formatTable(rows));
  }
}

main();
