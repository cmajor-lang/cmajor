// Records what a real `cmaj server` sends for some example patches, so that the mock session
// (../mock/mock-server-session.js) can replay realistic data without a server.
// Usage: node cmajor/tests/panel/capture-fixtures.mjs [patch.cmajorpatch ...]
// Writes fixtures/<PatchName>.json, with this cmajor folder's path replaced by "${CMAJOR_ROOT}" (see
// harness/serve.mjs), so they don't depend on where the repo is. CMAJ_BINARY is the cmaj to use
// (see harness/cmaj-binary.mjs).

import { spawn } from "node:child_process";
import { writeFileSync, mkdirSync } from "node:fs";
import { join, resolve, basename } from "node:path";
import { cmajorRoot, panelTestsRoot } from "./harness/serve.mjs";
import { requireCmaj, getFreePort, getServerArgs } from "./harness/cmaj-binary.mjs";

const cmaj = requireCmaj();
const examples = join (cmajorRoot, "examples/patches");
const outDir = join (panelTestsRoot, "fixtures");
mkdirSync (outDir, { recursive: true });

const defaultPatches = [
    "Pro54/Pro54.cmajorpatch",                      // custom GUI, lots of parameters, MIDI in
    "Tremolo/Tremolo.cmajorpatch",                  // audio in, parameters
    "SineSynth/SineSynth.cmajorpatch",              // MIDI instrument
    "MidiEcho/MidiEcho.cmajorpatch",                // MIDI out events
    "HelloWorld/HelloWorld.cmajorpatch",            // simplest
].map (p => join (examples, p));

const patches = process.argv.length > 2 ? process.argv.slice (2).map (p => resolve (p)) : defaultPatches;
const sleep = (ms) => new Promise (r => setTimeout (r, ms));

// CMAJ_SERVER_ARGS adds options to `cmaj server`, e.g. "--inputs=0" when the audio input device hangs CoreAudio
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
        await capture (socketURL, patchFile);
}
finally
{
    server.kill();
}

//==============================================================================
async function capture (socketURL, patchFile)
{
    const name = basename (patchFile, ".cmajorpatch");
    const sessionID = "CAPTURE" + name.toUpperCase().replace (/[^A-Z0-9]/g, "");
    const ws = new WebSocket (socketURL + "/" + sessionID);
    await new Promise ((r, j) => { ws.onopen = r; ws.onerror = j; });

    const received = [];
    const replies = new Map();
    const startTime = Date.now();

    ws.onmessage = (m) =>
    {
        const msg = JSON.parse (m.data);

        if (msg.type === "ping")
        {
            ws.send (JSON.stringify ({ type: "ping" }));
            return;
        }

        if (msg.type?.startsWith ("reply_"))
            replies.get (msg.type)?.(msg.message);

        // a reload of the patch drops the server's endpoint listeners, so add them again
        if (msg.type === "status")
            subscribe?.();

        received.push ({ t: Date.now() - startTime, ...msg });
    };

    let subscribe = null;

    const send = (m) => ws.send (JSON.stringify (m));
    const request = (m) => new Promise (resolve =>
    {
        const replyType = "reply_" + m.type + "_" + Math.floor (Math.random() * 1e8);
        replies.set (replyType, resolve);
        send ({ ...m, replyType });
    });

    send ({ type: "req_session_status" });
    send ({ type: "req_audio_device_props" });
    send ({ type: "load_patch", file: patchFile });

    let status;

    for (let i = 0; i < 1200 && ! status?.loaded && ! status?.error; ++i)
    {
        await sleep (100);
        status = received.filter (m => m.type === "session_status").at (-1)?.message;
    }

    if (! status?.loaded)
    {
        console.log (`${name}: didn't load: ${status?.error ?? status?.status}`);
        ws.close();
        return;
    }

    send ({ type: "set_audio_playback_active", active: true });
    send ({ type: "set_cpu_info_rate", framesPerCallback: 15000 });

    // listen to every output and every event input, the way the panel will
    const listeners = {};

    subscribe = () =>
    {
        for (const e of [...status.details.outputs, ...status.details.inputs])
        {
            const isAudio = e.purpose === "audio out" || e.purpose === "audio in";

            if (isAudio || e.endpointType === "event")
            {
                const replyType = `event_${e.endpointID}_capture`;
                listeners[e.endpointID] = replyType;
                send ({ type: "remove_endpoint_listener", endpoint: e.endpointID, replyType });
                send ({ type: "add_endpoint_listener", endpoint: e.endpointID, replyType, granularity: isAudio ? 400 : undefined });
            }
        }
    };

    subscribe();

    // poke the patch: a few MIDI notes into each MIDI input, and every parameter's value
    for (const e of status.details.inputs)
    {
        if (e.purpose === "midi in")
        {
            for (const note of [60, 64, 67])
                send ({ type: "send_value", id: e.endpointID, value: { message: 0x900000 | (note << 8) | 100 } });

            setTimeout (() => { for (const note of [60, 64, 67])
                                    send ({ type: "send_value", id: e.endpointID, value: { message: 0x800000 | (note << 8) } }); }, 600);
        }

        if (e.purpose === "parameter")
            send ({ type: "req_param_value", id: e.endpointID });
    }

    await sleep (1500);

    const graph = await request ({ type: "req_codegen", codeType: "graph", options: {} });
    const patchList = await request ({ type: "req_patchlist" });

    ws.close();

    // keep it a sensible size: all the non-data messages, plus up to 300 messages per data stream
    const isData = (m) => m.type?.startsWith ("event_") || m.type === "cpu_info";
    const messages = received.filter (m => ! m.type?.startsWith ("reply_"));
    const perType = {};
    const fixture = {
        patchFile,
        capturedAt: new Date().toISOString(),
        listeners,
        status: received.filter (m => m.type === "session_status").at (-1)?.message ?? status,
        graph: graph?.code ? JSON.parse (graph.code) : null,
        patchList,
        messages: messages.filter (m => ! isData (m) || (perType[m.type] = (perType[m.type] ?? 0) + 1) <= 300),
    };

    // (the path as the server reports it, with forward slashes, as it appears in JSON strings)
    const rootInJSON = JSON.stringify (cmajorRoot.replaceAll ("\\", "/")).slice (1, -1);
    writeFileSync (join (outDir, name + ".json"), JSON.stringify (fixture, null, 1).replaceAll (rootInJSON + "/", "${CMAJOR_ROOT}/"));
    console.log (`${name}: ${messages.length} messages, ${messages.filter (isData).length} data, graph ${graph?.code ? "ok" : "missing"}`);
}
