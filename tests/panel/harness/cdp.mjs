// Minimal headless-Chrome driver over the DevTools protocol (no dependencies).
//
//   const page = await openChrome ({ width: 1400, height: 860 });
//   await page.navigate (url);
//   await page.waitFor ("!! document.querySelector ('x')");
//   await page.evaluate ("1 + 1");                 // in the top frame
//   await page.evaluate ("...", { frame: true });  // in the first iframe (e.g. the panel inside fake VS Code)
//   await page.screenshot ("file.png");
//   page.problems  // console errors and exceptions seen so far
//   await page.close();
//
// Chrome is the one in CHROME_PATH, or else the first of the usual places that exists (see
// findChrome()).

import { spawn } from "node:child_process";
import { writeFileSync, rmSync, readFileSync, existsSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const sleep = (ms) => new Promise (r => setTimeout (r, ms));
export { sleep };

const chromeLocations = {
    darwin: ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
             "/Applications/Chromium.app/Contents/MacOS/Chromium"],
    linux:  ["/usr/bin/google-chrome", "/usr/bin/google-chrome-stable", "/usr/bin/chromium", "/usr/bin/chromium-browser",
             "/snap/bin/chromium"],
    win32:  [join (process.env["PROGRAMFILES"] ?? "C:\\Program Files", "Google\\Chrome\\Application\\chrome.exe"),
             join (process.env["PROGRAMFILES(X86)"] ?? "C:\\Program Files (x86)", "Google\\Chrome\\Application\\chrome.exe"),
             join (process.env["LOCALAPPDATA"] ?? "", "Google\\Chrome\\Application\\chrome.exe")],
};

/// The Chrome executable to use: CHROME_PATH, or the first of the usual locations that exists,
/// or null if there's none
export function findChrome()
{
    if (process.env.CHROME_PATH)
        return existsSync (process.env.CHROME_PATH) ? process.env.CHROME_PATH : null;

    return (chromeLocations[process.platform] ?? []).find (p => existsSync (p)) ?? null;
}

/// (port only names the profile directory: Chrome picks a free debugging port itself, because
/// a Chrome that was killed mid-run can be left holding its port, and connecting to that hangs)
export async function openChrome ({ width = 1400, height = 860, port = 9335, deviceScaleFactor = 1 } = {})
{
    const chromePath = findChrome();

    if (! chromePath)
        throw new Error ("Couldn't find Chrome: install Google Chrome, or set CHROME_PATH to its executable");

    const profile = mkdtempSync (join (tmpdir(), `cmaj-panel-chrome-${port}-`));

    const chrome = spawn (chromePath, ["--headless=new", "--disable-gpu", "--remote-debugging-port=0", `--user-data-dir=${profile}`,
                                       "--autoplay-policy=no-user-gesture-required", `--window-size=${width},${height}`,
                                       "--hide-scrollbars", "--no-first-run", "--no-default-browser-check", "about:blank"], { stdio: "ignore" });

    process.on ("exit", () => { try { chrome.kill(); rmSync (profile, { recursive: true, force: true }); } catch {} });

    const fetchWithTimeout = (url) => fetch (url, { signal: AbortSignal.timeout (1000) });
    let target;

    for (let i = 0; i < 50 && ! target; ++i)
    {
        await sleep (200);

        try
        {
            const debugPort = readFileSync (`${profile}/DevToolsActivePort`, "utf8").split ("\n")[0].trim();
            target = (await (await fetchWithTimeout (`http://127.0.0.1:${debugPort}/json/list`)).json()).find (t => t.type === "page");
        }
        catch {}
    }

    if (! target)
        throw new Error ("Chrome didn't start");

    const ws = new WebSocket (target.webSocketDebuggerUrl);
    await new Promise (r => ws.onopen = r);

    let nextID = 1;
    const pending = new Map();
    const problems = [];
    const contexts = new Map();     // frameId -> executionContextId (main world)
    let frameTree = null;

    const eventListeners = new Set();

    ws.onmessage = (msg) =>
    {
        const data = JSON.parse (msg.data);

        if (data.method)
            for (const l of eventListeners)
                l (data);

        if (data.id && pending.has (data.id))
        {
            pending.get (data.id) (data);
            pending.delete (data.id);
        }
        else if (data.method === "Runtime.executionContextCreated")
        {
            const c = data.params.context;

            if (c.auxData?.isDefault)
                contexts.set (c.auxData.frameId, c.id);
        }
        else if (data.method === "Runtime.exceptionThrown")
            problems.push ("exception: " + (data.params.exceptionDetails.exception?.description ?? data.params.exceptionDetails.text));
        else if (data.method === "Runtime.consoleAPICalled" && data.params.type === "error")
            problems.push ("console.error: " + data.params.args.map (a => a.value ?? a.description).join (" "));
        else if (data.method === "Log.entryAdded" && data.params.entry.level === "error" && ! /favicon\.ico/.test (data.params.entry.url ?? ""))
            problems.push ("log: " + data.params.entry.text + " " + (data.params.entry.url ?? ""));
    };

    const send = (method, params = {}) => new Promise (resolve =>
    {
        const id = nextID++;
        pending.set (id, resolve);
        ws.send (JSON.stringify ({ id, method, params }));
    });

    await send ("Runtime.enable");
    await send ("Log.enable");
    await send ("Page.enable");
    await send ("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor, mobile: false });

    const getChildFrameContext = async () =>
    {
        frameTree = (await send ("Page.getFrameTree")).result.frameTree;
        const child = frameTree.childFrames?.[0]?.frame.id;
        return child ? contexts.get (child) : undefined;
    };

    const page = {
        problems,
        send,

        /// Calls listener ({ method, params }) for every protocol event; returns a function that removes it
        onEvent (listener)          { eventListeners.add (listener); return () => eventListeners.delete (listener); },

        /// Pauses the page's JavaScript and returns its stack, e.g. to see what a hung page is doing
        async getStuckStack()
        {
            const paused = new Promise (resolve => { const remove = page.onEvent (e => { if (e.method === "Debugger.paused") { remove(); resolve (e.params); } }); });
            await send ("Debugger.enable");
            send ("Debugger.pause");
            const params = await Promise.race ([paused, sleep (5000).then (() => null)]);
            return params ? params.callFrames.map (f => `${f.functionName || "(anonymous)"} ${f.url.split ("/").pop()}:${f.location.lineNumber + 1}`) : ["(didn't pause)"];
        },

        async navigate (url)
        {
            contexts.clear();
            await send ("Page.navigate", { url });
            await page.waitFor ("document.readyState === 'complete'");
        },

        async evaluate (expression, { frame = false } = {})
        {
            const params = { expression, awaitPromise: true, returnByValue: true };

            if (frame)
            {
                const contextId = await getChildFrameContext();

                if (! contextId)
                    throw new Error ("no iframe context");

                params.contextId = contextId;
            }

            const r = await send ("Runtime.evaluate", params);

            if (r.result?.exceptionDetails)
                throw new Error (r.result.exceptionDetails.exception?.description ?? r.result.exceptionDetails.text);

            return r.result?.result?.value;
        },

        async waitFor (expression, { timeoutMs = 15000, frame = false } = {})
        {
            for (const start = Date.now(); Date.now() - start < timeoutMs; await sleep (100))
            {
                try
                {
                    if (await page.evaluate (expression, { frame }))
                        return true;
                }
                catch {}
            }

            return false;
        },

        async mouse (type, x, y, extra = {})
        {
            await send ("Input.dispatchMouseEvent", { type, x, y, button: "left", clickCount: 1, ...extra });
        },

        async click (x, y)
        {
            await page.mouse ("mouseMoved", x, y);
            await page.mouse ("mousePressed", x, y, { buttons: 1 });
            await page.mouse ("mouseReleased", x, y);
        },

        async drag (x0, y0, x1, y1, steps = 10)
        {
            await page.mouse ("mouseMoved", x0, y0);
            await page.mouse ("mousePressed", x0, y0, { buttons: 1 });

            for (let i = 1; i <= steps; ++i)
                await page.mouse ("mouseMoved", x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, { buttons: 1 });

            await page.mouse ("mouseReleased", x1, y1);
        },

        async key (key, code = key, extra = {})
        {
            await send ("Input.dispatchKeyEvent", { type: "keyDown", key, code, text: key.length === 1 ? key : undefined, ...extra });
            await send ("Input.dispatchKeyEvent", { type: "keyUp", key, code, ...extra });
        },

        async setSize (w, h)
        {
            await send ("Emulation.setDeviceMetricsOverride", { width: w, height: h, deviceScaleFactor, mobile: false });
        },

        /// "light" | "dark" | "" (the system's), for prefers-color-scheme
        async setColourScheme (scheme)
        {
            await send ("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: scheme }] });
        },

        /// clip (optional): { x, y, width, height } in CSS pixels, of the page
        async screenshot (file, clip)
        {
            const r = await send ("Page.captureScreenshot", clip ? { format: "png", clip: { ...clip, scale: 1 }, captureBeyondViewport: true } : { format: "png" });
            writeFileSync (file, Buffer.from (r.result.data, "base64"));
        },

        async close()
        {
            ws.close();
            chrome.kill ("SIGKILL");

            // (don't wait for it to finish exiting: on some machines a killed Chrome can take
            // minutes to, which would keep node running, and its websocket can stay open, so if
            // that's all that's left after a moment, exit anyway, with process.exitCode)
            chrome.unref();
            setTimeout (() => process.exit(), 2000).unref();
        }
    };

    return page;
}
