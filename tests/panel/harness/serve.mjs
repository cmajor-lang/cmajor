// A static server that lays files out the way `cmaj server` does, but with a mock session
// instead of a websocket, so the panel can be tested without a running server.
//
//   /session_<Fixture>/cmaj-patch-runner.html    the real runner template (VS Code page)
//   /session_<Fixture>/cmaj-patch-chooser.html   the real chooser template (browser page)
//   /session_<Fixture>/patch-view.html           mock/patch-view.html: just the patch's GUI
//   /session_<Fixture>/<file>                    files from that fixture's patch folder (patch GUIs)
//   /cmaj-patch-server.js                        mock/cmaj-patch-server.js (MockServerSession)
//   /panel_api/...                               cmajor/modules/embedded_assets/files/panel_api
//   /cmaj_api/...                                cmajor/javascript/cmaj_api (+ a cmaj-version.js stub)
//   /mock/..., /fixtures/...                     the folders next to this one
//   /vscode.html?fixture=Name                    a fake VS Code webview around the runner page
//
// The fixtures hold paths as "${CMAJOR_ROOT}/examples/...", which is replaced with the cmajor
// folder this is running in when they're served (see loadFixture()). fixtureOverrides adds
// fixtures that aren't files.
//
// startServer (port, { mounts }) serves extra folders too: mounts is { "/prefix/": folder }.
// With port 0 (the default) it picks a free port; the server's url says which.
//
// Usage: node cmajor/tests/panel/harness/serve.mjs [port]     (default 51090)

import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { join, dirname, resolve, extname, normalize, sep } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname (fileURLToPath (import.meta.url));

/// The folder with the panel tests (cmajor/tests/panel)
export const panelTestsRoot = resolve (here, "..");

/// The cmajor folder (the submodule, or a copy of it with the same layout)
export const cmajorRoot = resolve (here, "../../..");

const assets = join (cmajorRoot, "modules/embedded_assets/files");
const api = join (cmajorRoot, "javascript/cmaj_api");
const fixturePlaceholder = "${CMAJOR_ROOT}";
const cmajVersionStub = `export function getCmajorVersion() { return "0.0.0-mock"; }`;

const types = { ".js": "text/javascript", ".mjs": "text/javascript", ".html": "text/html", ".css": "text/css",
                ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png", ".jpg": "image/jpeg",
                ".wav": "audio/wav", ".woff2": "font/woff2" };

const within = (root, sub) =>
{
    const p = normalize (join (root, sub));
    return (p === root || p.startsWith (root + sep)) ? p : null;
};

/// Fixtures to serve that aren't in the fixtures folder: name -> fixture object (e.g. one made
/// from a status that a real server has just sent, as the patch GUI tests do)
export const fixtureOverrides = new Map();

/// A fixture's JSON text, with the placeholder replaced by this cmajor folder (with forward
/// slashes, as the server reports paths), or null if there's no such fixture
export async function loadFixtureText (name)
{
    if (fixtureOverrides.has (name))
        return JSON.stringify (fixtureOverrides.get (name));

    const file = within (join (panelTestsRoot, "fixtures"), name + ".json");

    if (! file || ! existsSync (file))
        return null;

    const root = JSON.stringify (cmajorRoot.replaceAll ("\\", "/")).slice (1, -1);
    return (await readFile (file, "utf8")).replaceAll (fixturePlaceholder, root);
}

export async function loadFixture (name)
{
    const text = await loadFixtureText (name);
    return text === null ? null : JSON.parse (text);
}

/// Files to serve in a session folder instead of (or as well as) the patch's own, for tests that
/// "edit" a patch's files: "/session_<Fixture>/<path>" -> content
export const fileOverrides = new Map();

export function startServer (port = 0, { mounts = {} } = {})
{
    const server = createServer (async (req, res) =>
    {
        const url = new URL (req.url, "http://localhost");
        let path = decodeURIComponent (url.pathname);
        let file = null, content = null;

        const session = /^\/session_([A-Za-z0-9]+)(\/.*)$/.exec (path);
        const mount = Object.keys (mounts).find (prefix => path.startsWith (prefix));

        if (session)
        {
            // (like the server's removeVersionFolder(): the panel asks for a patch's files under
            // /~v<N>/ after they change, so the browser loads fresh copies of them all)
            const [, fixture] = session;
            const rest = session[2].replace (/^\/~v\d+\//, "/");
            const override = fileOverrides.get (`/session_${fixture}${rest}`);

            if (override !== undefined)
            {
                content = override;
                path = rest;
            }
            else if (rest === "/patch-view.html")
            {
                // (served from the session's folder, like the server's runner page, so that a GUI's
                // page-relative URLs find its files, as they do in hosts)
                file = join (panelTestsRoot, "mock/patch-view.html");
            }
            else if (rest === "/cmaj-patch-runner.html" || rest === "/cmaj-patch-chooser.html")
            {
                const template = rest.includes ("runner") ? "embedded_patch_runner_template.html" : "embedded_patch_chooser_template.html";
                content = (await readFile (join (assets, template), "utf8")).replaceAll ("SESSION_ID", fixture);
                path = rest;
            }
            else
            {
                const patchFile = (await loadFixture (fixture))?.patchFile;

                if (patchFile)
                    file = within (dirname (resolve (patchFile)), rest);

                // (like the server, which serves its own files under a session's folder too, for
                // anything that isn't in the patch: GUIs can import "../cmaj_api/..." as they would
                // in a plugin, where the patch's folder is the root)
                if (! (file && existsSync (file)) && rest.startsWith ("/cmaj_api/"))
                {
                    file = null;
                    path = rest;

                    if (rest === "/cmaj_api/cmaj-version.js")
                        content = cmajVersionStub;
                    else
                        file = within (api, rest.substring (10));
                }
            }
        }
        else if (mount)                                         file = within (resolve (mounts[mount]), path.substring (mount.length));
        else if (path === "/cmaj-patch-server.js")             file = join (panelTestsRoot, "mock/cmaj-patch-server.js");
        else if (path === "/cmaj_api/cmaj-version.js")         content = cmajVersionStub;
        else if (path.startsWith ("/panel_api/"))              file = within (assets, path.substring (1));
        else if (path.startsWith ("/cmaj_api/"))               file = within (api, path.substring (10));
        else if (path.startsWith ("/mock/"))                   file = within (join (panelTestsRoot, "mock"), path.substring (6));
        else if (path.startsWith ("/fixtures/"))               content = await loadFixtureText (path.substring (10).replace (/\.json$/, ""));
        else if (path === "/vscode.html" || path === "/")      file = join (panelTestsRoot, "mock/vscode.html");

        try
        {
            if (content === null && file)
                content = await readFile (file);

            if (content === null)
                throw new Error ("not found");

            res.writeHead (200, { "Content-Type": types[extname (file ?? path)] ?? "application/octet-stream",
                                  "Cache-Control": "no-store" });
            res.end (content);
        }
        catch (e)
        {
            res.writeHead (404);
            res.end ("not found: " + path);
        }
    });

    return new Promise (r => server.listen (port, "127.0.0.1", () =>
    {
        server.port = server.address().port;
        server.url = `http://127.0.0.1:${server.port}`;
        r (server);
    }));
}

if (process.argv[1] === fileURLToPath (import.meta.url))
{
    const server = await startServer (Number (process.argv[2] ?? 51090));
    console.log (`mock panel server: ${server.url}/vscode.html?fixture=Tremolo`);
}
