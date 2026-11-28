// Runs a real `cmaj server`, to load patches in it and get what it reports about them.
//
//   const server = await startCmajServer();
//   const status = await server.loadPatch ("path/to/Patch.cmajorpatch");   // its session_status
//   server.close();
//
// The status is the server's last session_status once the patch has loaded: manifest, details
// (the endpoints), manifestFile, etc, which is what the mock session needs to stand in for it (see
// makeFixtureFromStatus()). loadPatch() throws if the patch doesn't build.
//
// The binary is found by findCmaj() (see cmaj-binary.mjs), and CMAJ_SERVER_ARGS adds options.

import { spawn } from "node:child_process";
import { resolve } from "node:path";
import { requireCmaj, getFreePort, getServerArgs } from "./cmaj-binary.mjs";
import { sleep } from "./cdp.mjs";

export async function startCmajServer()
{
    const cmaj = requireCmaj();
    const server = spawn (cmaj, ["server", `--port=${await getFreePort()}`, "--timeoutMs=30000", ...getServerArgs()],
                          { stdio: ["ignore", "pipe", "pipe"] });

    let output = "";
    server.stdout.on ("data", d => output += d);
    server.stderr.on ("data", d => output += d);
    process.on ("exit", () => server.kill());

    let serverURL;

    for (let i = 0; i < 100 && ! serverURL; ++i)
    {
        await sleep (100);
        serverURL = /Cmajor server active:\s*(\S+)/.exec (output)?.[1];
    }

    if (! serverURL)
    {
        server.kill();
        throw new Error ("cmaj server didn't start:\n" + output);
    }

    const sessionModule = await (await fetch (`${serverURL}/cmaj-patch-server.js`)).text();
    const socketURL = JSON.parse (/new WebSocket \(("[^"]+")/.exec (sessionModule)[1]);

    // (one session for all of them, so that each patch replaces the last one, rather than
    // leaving them all running)
    const ws = new WebSocket (`${socketURL}/PATCHGUITESTS`);
    await new Promise ((r, j) => { ws.onopen = r; ws.onerror = () => j (new Error ("couldn't connect to cmaj server")); });

    let statuses = [];

    ws.onmessage = (m) =>
    {
        const msg = JSON.parse (m.data);

        if (msg.type === "ping")
            ws.send (JSON.stringify ({ type: "ping" }));
        else if (msg.type === "session_status")
            statuses.push (msg.message);
    };

    const isSameFile = (a, b) => a && resolve (a) === resolve (b);

    return {
        /// Loads a patch, and returns the session_status once it has loaded
        async loadPatch (patchFile, { timeoutSeconds = 120 } = {})
        {
            statuses = [];
            ws.send (JSON.stringify ({ type: "load_patch", file: resolve (patchFile) }));

            for (const start = Date.now();; await sleep (100))
            {
                const status = statuses.findLast (s => isSameFile (s.manifestFile, patchFile) && (s.loaded || s.error));

                if (status?.loaded)
                    return status;

                if (status?.error)
                    throw new Error (`${patchFile} didn't build:\n${status.error}`);

                if (Date.now() - start > timeoutSeconds * 1000)
                    throw new Error (`${patchFile} didn't load in ${timeoutSeconds} s: ${statuses.at (-1)?.status ?? "no status"}`);
            }
        },

        close()
        {
            ws.close();
            server.kill();
        },

        get output()    { return output; },
    };
}

/// A fixture for the mock session (see ../mock/mock-server-session.js) made from a status that
/// loadPatch() returned: it has the patch's endpoints, but no recorded data, so outputs get
/// synthesised audio and events aren't replayed
export function makeFixtureFromStatus (patchFile, status)
{
    return { patchFile: resolve (patchFile).replaceAll ("\\", "/"), capturedAt: new Date().toISOString(),
             listeners: {}, status, graph: null, patchList: [], messages: [] };
}
