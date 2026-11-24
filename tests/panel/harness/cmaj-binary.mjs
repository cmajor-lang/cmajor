// Finding the cmaj binary for the tests that run a real `cmaj server`, and a free port for it
// (`cmaj server` can't pick one itself).
//
// The binary is CMAJ_BINARY if that's set, or else the first that exists of the usual build
// outputs in the cmajor-dev repo that this cmajor folder is in (Ninja, then Xcode/Visual Studio
// Release folders).

import { existsSync } from "node:fs";
import { createServer } from "node:net";
import { join, resolve } from "node:path";
import { cmajorRoot } from "./serve.mjs";

const exe = process.platform === "win32" ? "cmaj.exe" : "cmaj";

/// The cmaj binary to test, or null if there isn't one
export function findCmaj()
{
    if (process.env.CMAJ_BINARY)
        return existsSync (process.env.CMAJ_BINARY) ? resolve (process.env.CMAJ_BINARY) : null;

    const repo = resolve (cmajorRoot, "..");

    return [join (repo, "build/cmajor/tools/command", exe),
            join (repo, "build/cmajor/tools/command/Release", exe)].find (p => existsSync (p)) ?? null;
}

/// findCmaj(), or throws a helpful error
export function requireCmaj()
{
    const cmaj = findCmaj();

    if (! cmaj)
        throw new Error ("Couldn't find the cmaj binary: build it, or set CMAJ_BINARY (or pass --cmaj=<path> to run_panel_tests.mjs)");

    return cmaj;
}

/// A TCP port that's free on 127.0.0.1 right now
export function getFreePort()
{
    return new Promise ((resolvePort, reject) =>
    {
        const server = createServer();
        server.on ("error", reject);
        server.listen (0, "127.0.0.1", () =>
        {
            const { port } = server.address();
            server.close (() => resolvePort (port));
        });
    });
}

/// The extra options for `cmaj server` from CMAJ_SERVER_ARGS (e.g. "--inputs=0")
export function getServerArgs()
{
    return process.env.CMAJ_SERVER_ARGS?.split (/\s+/).filter (Boolean) ?? [];
}
