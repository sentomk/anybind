import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const addonPath = fileURLToPath(new URL("./build/md4c.node", import.meta.url));
const stubgen = fileURLToPath(
  new URL("../../bin/anybind-stubgen.mjs", import.meta.url),
);
const md4c = require(addonPath);

const sample = [
  "# Hello, anybind + md4c",
  "",
  "Built with **C++17** and ~~zero~~ minimal glue.",
  "",
  "| Runtime | Supported |",
  "| ------- | --------- |",
  "| Node.js | yes       |",
  "| Bun     | yes       |",
].join("\n");

const html = md4c.toHtml(sample);
assert.match(html, /<h1>Hello, anybind \+ md4c<\/h1>/);
assert.match(html, /<del>zero<\/del>/);
assert.match(html, /<table>/);

// CommonMark mode disables GFM extensions like strikethrough.
const cm = md4c.toHtml("~~plain~~", false);
assert.equal(cm, "<p>~~plain~~</p>\n");

// XHTML mode closes void elements like <br />.
const xhtml = md4c.toHtml("a  \nb", true, true);
assert.match(xhtml, /<br \/>/);

console.log("=== Rendered HTML ===");
console.log(html.trim());

const dts = spawnSync(process.execPath, [stubgen, addonPath], {
  encoding: "utf8",
});
assert.equal(dts.status, 0, dts.stderr);
console.log("\n=== Generated .d.ts ===");
console.log(dts.stdout.trim());
