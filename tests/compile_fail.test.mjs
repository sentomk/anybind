import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const buildDir = fileURLToPath(new URL("../build", import.meta.url));

const dangle = /the view would dangle/;
const cases = [
  [1, "std::vector<std::string_view> parameter", dangle],
  [2, "std::variant with a std::string_view alternative", dangle],
  [3, "std::map with std::string_view values", dangle],
  [4, "std::array of std::optional<std::string_view>", dangle],
  [5, "struct field holding std::string_view", dangle],
  [
    6,
    "value::as<std::string_view>()",
    /must not be or contain std::string_view/,
  ],
];

function build(id) {
  return spawnSync(
    "cmake",
    ["--build", buildDir, "--target", `compile_fail_${id}`],
    { encoding: "utf8" },
  );
}

test("control: the same bindings with owning types compile", () => {
  const r = build(0);
  assert.equal(r.status, 0, `${r.stdout}\n${r.stderr}`);
});

for (const [id, label, message] of cases) {
  test(`rejects at compile time: ${label}`, () => {
    const r = build(id);
    assert.notEqual(r.status, 0, `compile_fail_${id} unexpectedly compiled`);
    assert.match(`${r.stdout}\n${r.stderr}`, message);
  });
}
