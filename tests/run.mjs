import { registerTests } from "./suite.mjs";

const cases = [];
registerTests((name, fn) => {
  cases.push({ name, fn });
});

let failed = 0;
for (const { name, fn } of cases) {
  try {
    await fn();
    console.log(`✔ ${name}`);
  } catch (err) {
    failed += 1;
    console.error(`✖ ${name}`);
    console.error(err);
  }
}

console.log(`\n${cases.length - failed}/${cases.length} passed`);
if (failed > 0) {
  process.exit(1);
}
