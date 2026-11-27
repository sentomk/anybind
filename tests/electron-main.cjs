// Runs the test suite inside Electron's main process, where the addon is
// loaded by Electron itself rather than in ELECTRON_RUN_AS_NODE mode.
//
//   npx electron tests/electron-main.cjs   (Linux CI: under xvfb-run, with --no-sandbox)

const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { app } = require("electron");

app.disableHardwareAcceleration();
app.whenReady().then(async () => {
  try {
    // run.mjs calls process.exit(1) itself when a test fails.
    await import(pathToFileURL(path.join(__dirname, "run.mjs")).href);
    app.exit(0);
  } catch (err) {
    console.error(err);
    app.exit(1);
  }
});
