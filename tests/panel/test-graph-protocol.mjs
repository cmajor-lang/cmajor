// Phase 3 protocol checks against a real `cmaj server`, over a plain websocket (no browser):
//   - once a client has asked for a graph, a loaded status has a graphVersion, and req_graph
//     returns that version and its graph
//   - the graph matches what `cmaj generate --target=graph` makes from the same files
//   - req_graph and req_codegen "graph" are answered from the cache: fast, identical, and they
//     don't cause any builds (no statuses arrive while they're answered)
//   - a source edit that only adds a comment at the end keeps the version; one that moves
//     lines (so the source locations change) or changes the structure gives a new version,
//     whose graph has the change
//   - a build error drops graphVersion, and fixing it brings it back
// Usage: node cmajor/tests/panel/test-graph-protocol.mjs [patch.cmajorpatch ...]
// CMAJ_BINARY is the cmaj to test (see harness/cmaj-binary.mjs); CMAJ_SERVER_ARGS adds options to
// `cmaj server`, e.g. "--inputs=0". The patches are copied into a temporary folder and edited there.

import { spawn, execFileSync } from "node:child_process";
import { cpSync, rmSync, readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { join, dirname, resolve, basename } from "node:path";
import { tmpdir } from "node:os";
import { cmajorRoot, panelTestsRoot } from "./harness/serve.mjs";
import { requireCmaj, getFreePort, getServerArgs } from "./harness/cmaj-binary.mjs";

const cmaj = requireCmaj();
const examples = join (cmajorRoot, "examples/patches");
const tempRoot = mkdtempSync (join (tmpdir(), "cmaj-graph-protocol-test-"));

const defaultPatches = [
    join (examples, "Tremolo/Tremolo.cmajorpatch"),
    join (examples, "SineSynth/SineSynth.cmajorpatch"),
    join (examples, "Pro54/Pro54.cmajorpatch"),
    join (examples, "808/808.cmajorpatch"),            // externals (samples)
    join (examples, "Piano/Piano.cmajorpatch"),        // externals (samples)
    join (panelTestsRoot, "patches/GraphLinks/GraphLinks.cmajorpatch"),
];

const patches = process.argv.length > 2 ? process.argv.slice (2).map (p => resolve (p)) : defaultPatches;
const sleep = (ms) => new Promise (r => setTimeout (r, ms));

let failures = 0;
const check = (ok, message) => { console.log (`${ok ? "  ok  " : "  FAIL"} ${message}`); if (! ok) ++failures; };

const server = spawn (cmaj, ["server", `--port=${await getFreePort()}`, "--timeoutMs=20000", ...getServerArgs()],
                      { stdio: ["ignore", "pipe", "pipe"] });
let serverOutput = "";
server.stdout.on ("data", d => serverOutput += d);
server.stderr.on ("data", d => serverOutput += d);

try
{
    let serverURL;

    for (let i = 0; i < 100 && ! serverURL; ++i)
    {
        await sleep (100);
        serverURL = /Cmajor server active:\s*(\S+)/.exec (serverOutput)?.[1];
    }

    if (! serverURL)
        throw new Error ("cmaj server didn't start:\n" + serverOutput);

    const sessionModule = await (await fetch (`${serverURL}/cmaj-patch-server.js`)).text();
    const socketURL = JSON.parse (/new WebSocket \(("[^"]+")/.exec (sessionModule)[1]);

    for (const patchFile of patches)
        await testPatch (socketURL, patchFile);
}
finally
{
    server.kill();
    rmSync (tempRoot, { recursive: true, force: true });
}

console.log (failures === 0 ? "\nAll protocol checks passed" : `\n${failures} FAILED`);
process.exit (failures === 0 ? 0 : 1);

//==============================================================================
async function testPatch (socketURL, sourcePatch)
{
    const name = basename (sourcePatch, ".cmajorpatch");
    console.log (`\n${name}`);

    const tempFolder = join (tempRoot, name);
    rmSync (tempFolder, { recursive: true, force: true });
    cpSync (dirname (sourcePatch), tempFolder, { recursive: true });
    const patchFile = join (tempFolder, basename (sourcePatch));
    const manifest = JSON.parse (readFileSync (patchFile, "utf8"));
    const sourceFile = join (tempFolder, [manifest.source].flat()[0]);
    const originalSource = readFileSync (sourceFile, "utf8");

    // (the server's file checker compares modification times, which may only have one-second
    // resolution, so writes are kept more than a second apart)
    const writeSource = async (text) =>
    {
        await sleep (1100);
        writeFileSync (sourceFile, text);
    };

    const session = await openSession (socketURL, "GRAPHPROTO" + name.toUpperCase().replace (/[^A-Z0-9]/g, ""));

    try
    {
        // (the server only makes the graph once a client has asked for one, the way the panel's
        // graph view does: until then there's nothing to give, and the statuses have no graphVersion)
        const early = await session.request ({ type: "req_graph" });
        check (early?.graph === "" && early?.version === "", "before a patch is loaded, req_graph gives an empty graph and version");

        session.send ({ type: "load_patch", file: patchFile });
        let status = await session.waitForStatus (s => s.loaded && s.graphVersion, 60000);
        check (!! status, `loads, and the status has a graphVersion (${status?.graphVersion})`);

        if (! status)
        {
            console.log ("    last status: " + JSON.stringify (session.lastStatus)?.substring (0, 400));
            return;
        }

        // req_graph
        const reply = await session.request ({ type: "req_graph" });
        check (reply?.version === status.graphVersion, "req_graph's version matches the status's graphVersion");
        const graph = JSON.parse (reply?.graph || "null");
        check (graph?.format === "cmajor-patch-graph" || typeof graph?.root === "object", "req_graph returns the graph JSON");

        // the same as the command line makes
        const generatedFile = join (tempFolder, "generated-graph.json");
        execFileSync (cmaj, ["generate", "--target=graph", `--output=${generatedFile}`, patchFile], { stdio: "ignore" });
        const generated = JSON.parse (readFileSync (generatedFile, "utf8"));
        const describe = (g) => JSON.stringify ([...collectPaths (g)].sort());
        check (describe (graph) === describe (generated), `it has the same nodes, endpoints and connections as cmaj generate --target=graph (${collectPaths (graph).size} paths)`);
        check (JSON.stringify (graph) === JSON.stringify (generated), "and is identical to it");

        // cached: fast, and no builds. (First let the statuses from loading settle: a fresh server can
        // send one more, e.g. once its audio device has started, which isn't a build.)
        for (let quiet = 0, last = session.statusCount; quiet < 5; ++quiet)
        {
            await sleep (100);

            if (session.statusCount !== last)
            {
                last = session.statusCount;
                quiet = -1;
            }
        }

        const statusesBefore = session.statusCount;
        const times = [];

        for (let i = 0; i < 5; ++i)
        {
            const start = performance.now();
            const r = await session.request ({ type: "req_graph" });
            times.push (performance.now() - start);

            if (r.version !== status.graphVersion)
                times.push (Infinity);
        }

        const codegenStart = performance.now();
        const codegen = await session.request ({ type: "req_codegen", codeType: "graph", options: {} });
        const codegenTime = performance.now() - codegenStart;
        await sleep (300);

        check (Math.max (...times) < 100, `req_graph is answered from the cache (${times.map (t => t.toFixed (1)).join (", ")} ms)`);
        check (codegen?.code === reply.graph, `req_codegen "graph" still works, and gives the same cached JSON (${codegenTime.toFixed (1)} ms)`);
        check (session.statusCount === statusesBefore, "no builds happened while they were answered (no statuses)");

        // an edit that doesn't change the graph: same version, and no need to fetch it again
        await writeSource (originalSource + "\n// (a comment at the end)\n");
        status = await session.waitForStatus (s => s.loaded && ! (s.status ?? "").startsWith ("Loading"), 30000, true);
        check (status?.graphVersion === reply.version, "a rebuild that doesn't change the graph keeps the same graphVersion");

        // an edit that moves every line down: the source locations change, so the version does
        await writeSource ("// (a comment at the start)\n" + originalSource);
        status = await session.waitForStatus (s => s.loaded && s.graphVersion && s.graphVersion !== reply.version, 30000);
        check (!! status, "a rebuild that moves the source lines gives a new graphVersion");

        if (status)
        {
            const moved = JSON.parse ((await session.request ({ type: "req_graph" })).graph);
            const firstLine = (g) => findFirstLocation (g)?.line;
            check (firstLine (moved) === firstLine (graph) + 1, `and its graph has the new source locations (line ${firstLine (graph)} → ${firstLine (moved)})`);
        }

        // a structural edit (GraphLinks: a third voice, and a new node)
        if (name === "GraphLinks")
        {
            const before = status.graphVersion;
            await writeSource (originalSource.replace ("node voices = Voice[2];", "node voices = Voice[3];\n        node extra = Tone;"));
            status = await session.waitForStatus (s => s.loaded && s.graphVersion && s.graphVersion !== before, 30000);
            check (!! status, "a structural edit gives a new graphVersion");
            const changed = JSON.parse ((await session.request ({ type: "req_graph" })).graph);
            const voices = changed?.root?.nodes?.find (n => n.path === "voices");
            check (changed?.root?.nodes?.some (n => n.path === "extra"), "and the new graph has the new node");
            check (JSON.stringify (voices ?? {}).includes ("3"), `and the node array's new size (${JSON.stringify (voices?.arraySize ?? voices?.size ?? "?")})`);
        }

        // a build error: no graphVersion (so a client keeps its last good graph), then back
        await writeSource (originalSource + "\n this is not valid code \n");
        status = await session.waitForStatus (s => s.error, 30000);
        check (!! status && ! status.graphVersion, "a failed build's status has no graphVersion");
        const failedReply = await session.request ({ type: "req_graph" });
        check (failedReply?.graph === "" && failedReply?.version === "", "and req_graph gives an empty graph and version");

        await writeSource (originalSource);
        status = await session.waitForStatus (s => s.loaded && s.graphVersion, 30000);
        check (status?.graphVersion === reply.version, "fixing it brings back the original graphVersion");
    }
    finally
    {
        session.close();
    }
}

//==============================================================================
function collectPaths (graph)
{
    const paths = new Set();

    const visit = (value, key) =>
    {
        if (Array.isArray (value))
            value.forEach (v => visit (v));
        else if (value && typeof value === "object")
            for (const [k, v] of Object.entries (value))
                visit (v, k);
        else if (key === "path" && typeof value === "string")
            paths.add (value);
    };

    visit (graph);
    return paths;
}

function findFirstLocation (graph)
{
    let found;

    const visit = (value) =>
    {
        if (found || ! value || typeof value !== "object")
            return;

        if (typeof value.file === "string" && typeof value.line === "number" && ! value.file.includes ("std_library"))
        {
            found = value;
            return;
        }

        for (const v of Object.values (value))
            visit (v);
    };

    visit (graph);
    return found;
}

async function openSession (socketURL, sessionID)
{
    const ws = new WebSocket (socketURL + "/" + sessionID);
    await new Promise ((r, j) => { ws.onopen = r; ws.onerror = j; });

    const waiters = new Set();
    const replies = new Map();
    let nextReply = 0;

    const session = {
        statusCount: 0,
        lastStatus: null,
        send: (msg) => ws.send (JSON.stringify (msg)),
        close: () => ws.close(),

        request (msg)
        {
            const replyType = `reply_${msg.type}_${++nextReply}`;
            return new Promise ((resolve) => { replies.set (replyType, resolve); session.send ({ ...msg, replyType }); });
        },

        /// Resolves with the first status (from now on, or already received unless onlyNew) for
        /// which predicate is true, or null after the timeout
        waitForStatus (predicate, timeoutMs, onlyNew = false)
        {
            if (! onlyNew && session.lastStatus && predicate (session.lastStatus))
                return Promise.resolve (session.lastStatus);

            return new Promise ((resolve) =>
            {
                const waiter = { predicate, resolve };
                waiters.add (waiter);
                setTimeout (() => { if (waiters.delete (waiter)) resolve (null); }, timeoutMs);
            });
        }
    };

    ws.onmessage = (m) =>
    {
        const msg = JSON.parse (m.data);

        if (msg.type === "ping")
            return session.send ({ type: "ping" });

        if (msg.type === "session_status")
        {
            ++session.statusCount;
            if (process.env.LOG_STATUS) console.log (`      [status ${(performance.now() / 1000).toFixed (1)}s] loaded=${msg.message.loaded} status=${JSON.stringify (msg.message.status)} error=${!! msg.message.error} graphVersion=${msg.message.graphVersion}`);
            session.lastStatus = msg.message;

            for (const w of [...waiters])
            {
                if (w.predicate (msg.message))
                {
                    waiters.delete (w);
                    w.resolve (msg.message);
                }
            }
        }

        if (replies.has (msg.type))
        {
            replies.get (msg.type) (msg.message);
            replies.delete (msg.type);
        }
    };

    session.send ({ type: "req_session_status" });
    return session;
}
