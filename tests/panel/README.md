# Patch panel tests

Tests for the patch panel: the browser UI in `modules/embedded_assets/files/panel_api` that `cmaj server` serves, and that the VS Code extension shows. They drive headless Chrome over the DevTools protocol from Node, with no other dependencies (no npm install).

## Running them

```
$ node cmajor/tests/panel/run_panel_tests.mjs [--cmaj=<path to cmaj>] [--only=core,mock,save,graph]
```

This runs each suite in turn, prints a summary, and exits with 1 if any of them failed. It needs:

- **Node 22 or later** (the suites use Node's built-in `fetch` and `WebSocket`)
- **Google Chrome** (or Chromium), found in the usual place for the platform, or wherever `CHROME_PATH` points
- **a cmaj binary**, for the `graph` suite only: `--cmaj=<path>`, or `CMAJ_BINARY`, or else the first of `build/cmajor/tools/command/cmaj` and `build/cmajor/tools/command/Release/cmaj` in the repo that contains this `cmajor` folder

The suites:

| Suite | Script | What it checks | Needs |
|---|---|---|---|
| `core` | `test-core.mjs` | The modules in `panel_api/core` (runs `mock/core-tests.js` in the browser) | Chrome |
| `mock` | `test-panel-mock.mjs` | The whole panel, in a fake VS Code webview, against a mock session that replays recorded server data (`fixtures/`) | Chrome |
| `save` | `test-save-file.mjs` | Saving files: through the VS Code extension's handler, and as a browser download | Chrome (the extension handler checks are skipped if `tools/vscode/cmajor` isn't next to this `cmajor` folder, or set with `CMAJ_VSCODE_EXTENSION`) |
| `graph` | `test-graph-protocol.mjs` | The graph protocol of a real `cmaj server`: versions, caching, edits and build errors, for some example patches and `patches/GraphLinks` | Chrome isn't used; cmaj |

Each script can also be run on its own (`node cmajor/tests/panel/test-core.mjs`, etc.). `test-panel-mock.mjs` takes a fixture name (default `Tremolo`), and saves screenshots as it goes if `PANEL_TEST_SCREENSHOTS` names a folder.

The runner sets `CMAJ_SERVER_ARGS` to `--inputs=0` unless it's already set, so `cmaj server` never opens an audio input device: on macOS that can ask for microphone access, and some devices hang when opened for full-duplex use. The server still opens an audio output.

Nothing uses a fixed port: the mock servers pick free ones, and so does the harness for `cmaj server` (which can't pick its own). Temporary files go in the OS's temp folder.

## In CI

The macOS arm64 test job in `tools/ci/develop/Jenkinsfile` and `tools/ci/main/Jenkinsfile` (in the cmajor-dev repo) runs

```
node cmajor/tests/panel/run_panel_tests.mjs --cmaj=./cmaj
```

from its `test` folder, where the stashes lay out `cmajor/` like this repo (including `modules/embedded_assets/files` and `javascript/cmaj_api`, which the mock server serves), with the extension's sources in `tools/vscode/cmajor/src`. The agent needs Node 22+ and Chrome.

## Layout

- `run_panel_tests.mjs` - the runner
- `test-*.mjs` - the suites
- `harness/cdp.mjs` - a minimal headless Chrome driver
- `harness/serve.mjs` - a server that lays files out the way `cmaj server` does, with the mock session in place of the websocket (run it on its own to try the panel in a browser: `node cmajor/tests/panel/harness/serve.mjs`, then open `http://127.0.0.1:51090/vscode.html?fixture=Tremolo`)
- `harness/cmaj-binary.mjs` - finding cmaj, and a free port for its server
- `mock/` - the mock session, the fake VS Code webview, and the core tests' page
- `fixtures/` - what a real server sent for some example patches, which the mock session replays
- `patches/` - patches made for these tests
- `capture-fixtures.mjs` - re-records the fixtures

## Re-recording the fixtures

When the server's messages change, re-record them with a real server:

```
$ node cmajor/tests/panel/capture-fixtures.mjs [patch.cmajorpatch ...]
```

With no arguments it records Pro54, Tremolo, SineSynth, MidiEcho and HelloWorld from `examples/patches` (TimelineTester is recorded by naming it). Paths inside this `cmajor` folder are written as `${CMAJOR_ROOT}/...`, which `harness/serve.mjs` fills in when it serves them, so the fixtures work wherever the repo is.
