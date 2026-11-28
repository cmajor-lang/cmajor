// Runs the test suites for the patch panel and the patches' GUIs one after another, and exits
// with 1 if any of them fails (or can't run). This is what CI runs: see README.md.
//
// Usage: node cmajor/tests/panel/run_panel_tests.mjs [--cmaj=<path>] [--only=core,mock,save,graph,guis]
//
//   --cmaj=<path>   the cmaj binary for the suites that run a real `cmaj server` (otherwise
//                   CMAJ_BINARY, or the usual build folders: see harness/cmaj-binary.mjs)
//   --only=...      just these suites (by name, from the list below)
//
// CMAJ_SERVER_ARGS defaults to "--inputs=0", so the server never opens an audio input (which
// can ask for microphone access, or hang on some devices). CHROME_PATH chooses the browser.

import { spawn } from "node:child_process";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { findChrome } from "./harness/cdp.mjs";
import { findCmaj } from "./harness/cmaj-binary.mjs";

const here = dirname (fileURLToPath (import.meta.url));

const suites = [
    { name: "core",  script: "test-core.mjs",           needsChrome: true,  needsCmaj: false, timeoutMinutes: 3,  about: "panel_api/core, in the browser" },
    { name: "mock",  script: "test-panel-mock.mjs",     needsChrome: true,  needsCmaj: false, timeoutMinutes: 10, about: "the panel against the mock session" },
    { name: "save",  script: "test-save-file.mjs",      needsChrome: true,  needsCmaj: false, timeoutMinutes: 3,  about: "saving files, in VS Code and a browser" },
    { name: "graph", script: "test-graph-protocol.mjs", needsChrome: false, needsCmaj: true,  timeoutMinutes: 10, about: "the graph protocol of a real cmaj server" },
    { name: "guis",  script: "test-patch-guis.mjs",     needsChrome: true,  needsCmaj: true,  timeoutMinutes: 10, about: "the example patches' custom GUIs" },
];

const args = process.argv.slice (2);
const getArg = (name) => args.find (a => a.startsWith (`--${name}=`))?.substring (name.length + 3);
const only = getArg ("only")?.split (",").map (s => s.trim()).filter (Boolean);

if (only?.some (n => ! suites.some (s => s.name === n)))
    fail (`Unknown suite in --only: choose from ${suites.map (s => s.name).join (", ")}`);

const toRun = only ? suites.filter (s => only.includes (s.name)) : suites;

//==============================================================================
// what the suites need
const nodeMajor = Number (process.versions.node.split (".")[0]);

if (nodeMajor < 22)
    fail (`The panel tests need Node 22 or later (for its built-in WebSocket): this is ${process.version}`);

if (toRun.some (s => s.needsChrome) && ! findChrome())
    fail ("Couldn't find Chrome: install Google Chrome, or set CHROME_PATH to its executable");

// (findCmaj() looks at CMAJ_BINARY first, and the suites get this environment)
if (getArg ("cmaj"))
    process.env.CMAJ_BINARY = resolve (getArg ("cmaj"));

const env = { ...process.env, CMAJ_SERVER_ARGS: process.env.CMAJ_SERVER_ARGS ?? "--inputs=0" };

if (toRun.some (s => s.needsCmaj))
{
    env.CMAJ_BINARY = findCmaj();

    if (! env.CMAJ_BINARY)
        fail ("Couldn't find the cmaj binary: build it, or pass --cmaj=<path> (or set CMAJ_BINARY)");
}

console.log (`Node ${process.version}, Chrome: ${findChrome() ?? "none"}${env.CMAJ_BINARY ? `, cmaj: ${env.CMAJ_BINARY}` : ""}`);

//==============================================================================
const results = [];

for (const suite of toRun)
{
    console.log (`\n=== ${suite.name}: ${suite.about} (${suite.script})`);
    const start = performance.now();
    const outcome = await runSuite (suite);
    const seconds = ((performance.now() - start) / 1000).toFixed (1);
    results.push ({ ...suite, ...outcome, seconds });
    console.log (`=== ${suite.name}: ${outcome.passed ? "passed" : "FAILED" + (outcome.reason ? ` (${outcome.reason})` : "")} in ${seconds} s`);
}

console.log ("\nPanel test summary:");

for (const r of results)
    console.log (`  ${r.passed ? "passed" : "FAILED"}  ${r.name.padEnd (6)} ${r.seconds.padStart (6)} s  ${r.about}`);

const failures = results.filter (r => ! r.passed).length;
console.log (failures === 0 ? "\nAll panel test suites passed" : `\n${failures} panel test suite(s) FAILED`);
process.exit (failures === 0 ? 0 : 1);

//==============================================================================
function runSuite (suite)
{
    return new Promise (resolveOutcome =>
    {
        const child = spawn (process.execPath, [join (here, suite.script)], { env, stdio: "inherit" });
        let timedOut = false;

        const timer = setTimeout (() =>
        {
            timedOut = true;
            child.kill ("SIGKILL");
        }, suite.timeoutMinutes * 60 * 1000);

        child.on ("error", (e) =>
        {
            clearTimeout (timer);
            resolveOutcome ({ passed: false, reason: e.message });
        });

        child.on ("exit", (code, signal) =>
        {
            clearTimeout (timer);

            if (timedOut)
                resolveOutcome ({ passed: false, reason: `timed out after ${suite.timeoutMinutes} minutes` });
            else
                resolveOutcome ({ passed: code === 0, reason: code === 0 ? "" : (signal ? `killed by ${signal}` : `exit code ${code}`) });
        });
    });
}

function fail (message)
{
    console.error (message);
    process.exit (1);
}
