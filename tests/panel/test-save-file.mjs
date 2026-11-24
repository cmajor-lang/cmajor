// Tests HostBridge.saveFile end to end (feature 4, recording):
//   - in the fake VS Code webview (mock/vscode.html): a WAV round-trips through { saveFile } /
//     { saveFileResult } intact, cancelling works, concurrent saves resolve to the right callers,
//     and a file bigger than one chunk arrives whole;
//   - on the chooser (browser) page: the <a download> path really downloads the file (CDP's
//     Browser.setDownloadBehavior sends it to a temporary folder, and its bytes are checked);
//   - the VS Code extension's handler (tools/vscode/cmajor/src/cmaj_save_file.js), with a fake vscode API.
//     The extension lives next to the cmajor folder (in the cmajor-dev repo), or wherever
//     CMAJ_VSCODE_EXTENSION points (its folder, holding src/): without it, those checks are skipped.
// Usage: node cmajor/tests/panel/test-save-file.mjs [Fixture]     (default Tremolo; VERBOSE=1 lists passes too)

import { startServer, cmajorRoot } from "./harness/serve.mjs";
import { openChrome, sleep } from "./harness/cdp.mjs";
import { createRequire } from "node:module";
import { mkdtempSync, readdirSync, readFileSync, rmSync, existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";

const fixture = process.argv[2] ?? "Tremolo";
const extensionFolder = resolve (process.env.CMAJ_VSCODE_EXTENSION ?? join (cmajorRoot, "../tools/vscode/cmajor"));
const saveFileHandlerModule = join (extensionFolder, "src/cmaj_save_file.js");

let passed = 0, failed = 0;

function check (ok, what)
{
    if (ok) { ++passed; if (process.env.VERBOSE) console.log ("  ok " + what); }
    else { ++failed; console.log ("  FAIL " + what); }
}

// Runs in the panel's page: encodes a test WAV with the real encoder, so both sides can compare bytes
const makeWAV = `(async () => {
    const { encodeWAV } = await import ("/panel_api/core/wav-encoder.js");
    const channels = [0, 1].map (c => Float32Array.from ({ length: 4800 }, (_, i) => Math.sin (i * 0.03 * (c + 1)) * 0.7));
    window.testWAV = new Uint8Array (encodeWAV (channels, 48000, { format: "pcm24" }));
    window.panelBridge = document.getElementById ("cmaj-patch-panel").bridge;
    return window.testWAV.length;
})()`;

//==============================================================================
// the extension's handler, with a fake vscode API
async function testExtensionHandler()
{
    const require = createRequire (import.meta.url);
    const { SaveFileHandler, getSaveFilters, sanitiseFileName } = require (saveFileHandlerModule);
    const toBase64 = (bytes) => Buffer.from (bytes).toString ("base64");

    const makeFakeVSCode = () =>
    {
        const fake = {
            dialogs: [], written: [], answer: (options) => ({ fsPath: "/chosen/" + options.defaultUri.path.split ("/").pop() }), failWrite: null,
            Uri: { joinPath: (base, ...parts) => ({ path: [base.path, ...parts].join ("/") }) },
            window: { showSaveDialog: async (options) => { fake.dialogs.push (options); return fake.answer (options); } },
            workspace: { fs: { writeFile: async (uri, data) => { if (fake.failWrite) throw new Error (fake.failWrite); fake.written.push ({ uri, data }); } } }
        };

        return fake;
    };

    const fake = makeFakeVSCode();
    const replies = [];
    const handler = new SaveFileHandler (fake, () => ({ path: "/patches/Tremolo" }), (m) => replies.push (m));
    const wav = Uint8Array.from ({ length: 1000 }, (_, i) => i & 255);

    await handler.handleMessage ({ requestID: "r1", suggestedName: "take.wav", mimeType: "audio/wav", size: 1000, chunkIndex: 0, numChunks: 1, dataBase64: toBase64 (wav) });
    check (fake.dialogs[0]?.defaultUri.path === "/patches/Tremolo/take.wav", "extension: the dialog starts next to the patch");
    check (JSON.stringify (fake.dialogs[0]?.filters) === JSON.stringify ({ "WAV audio": ["wav"], "All files": ["*"] }), "extension: a WAV filter");
    check (fake.written[0]?.uri.fsPath === "/chosen/take.wav" && Buffer.from (fake.written[0].data).equals (Buffer.from (wav)), "extension: writes the bytes to the chosen file");
    check (JSON.stringify (replies[0]) === JSON.stringify ({ saveFileResult: { requestID: "r1", saved: true, path: "/chosen/take.wav" } }), "extension: replies saved with the path");

    fake.answer = () => undefined;
    await handler.handleMessage ({ requestID: "r2", suggestedName: "x.wav", dataBase64: toBase64 ([1]) });
    check (replies[1]?.saveFileResult.cancelled === true && replies[1].saveFileResult.saved === false && fake.written.length === 1, "extension: a cancelled dialog replies cancelled");

    fake.answer = (o) => ({ fsPath: "/ro/" + o.defaultUri.path.split ("/").pop() });
    fake.failWrite = "EACCES: permission denied";
    await handler.handleMessage ({ requestID: "r3", suggestedName: "y.wav", dataBase64: toBase64 ([1]) });
    check (replies[2]?.saveFileResult.error?.includes ("EACCES") && ! replies[2].saveFileResult.saved, "extension: a write failure replies with the error");
    fake.failWrite = null;

    // chunks, interleaved between two requests
    const p1 = [Uint8Array.of (1, 2), Uint8Array.of (3)], p2 = [Uint8Array.of (9), Uint8Array.of (8, 7)];
    const send = (id, parts, i) => handler.handleMessage ({ requestID: id, suggestedName: id + ".bin", size: 3, chunkIndex: i, numChunks: 2, dataBase64: toBase64 (parts[i]) });
    await send ("a", p1, 0); await send ("b", p2, 0);
    check (replies.length === 3 && fake.dialogs.length === 3, "extension: no dialog until every chunk has arrived");
    await send ("b", p2, 1); await send ("a", p1, 1);
    const written = Object.fromEntries (fake.written.slice (1).map (w => [w.uri.fsPath, Array.from (w.data).join()]));
    check (written["/ro/a.bin"] === "1,2,3" && written["/ro/b.bin"] === "9,8,7", "extension: interleaved chunked uploads join correctly");
    check (replies.slice (3).map (r => r.saveFileResult.requestID).join() === "b,a", "extension: each reply carries its own request ID");

    await handler.handleMessage ({ requestID: "c", suggestedName: "c.bin", size: 5, chunkIndex: 0, numChunks: 1, dataBase64: toBase64 ([1, 2]) });
    check (/incomplete/.test (replies.at (-1).saveFileResult.error), "extension: a size mismatch is an error");
    await handler.handleMessage ({ requestID: "d" });
    check (replies.at (-1).saveFileResult.requestID === "d" && /no data/.test (replies.at (-1).saveFileResult.error), "extension: a request with no data is an error");
    handler.dispose();

    check (JSON.stringify (getSaveFilters ("a.FLAC")) === JSON.stringify ({ "FLAC files": ["flac"], "All files": ["*"] })
             && JSON.stringify (getSaveFilters ("noext")) === JSON.stringify ({ "All files": ["*"] }), "extension: filters for other names");
    check (sanitiseFileName ("../a/b:c.wav") === ".._a_b_c.wav" && sanitiseFileName ("") === "untitled" && sanitiseFileName ("..") === "untitled",
           "extension: suggested names can't name folders");
}

if (existsSync (saveFileHandlerModule))
    await testExtensionHandler();
else
    console.log (`  (skipping the extension handler checks: there's no ${saveFileHandlerModule})`);

const server = await startServer();
const page = await openChrome ({ port: 9338 });
const downloads = mkdtempSync (join (tmpdir(), "cmaj-save-test-"));

try
{
    //==============================================================================
    // the fake VS Code webview
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}`);
    check (await page.waitFor ("window.cmajMock?.isLoaded === true", { frame: true, timeoutMs: 15000 }), "vscode: the mock patch loads");
    await page.waitFor ("document.getElementById ('cmaj-patch-panel')?.bridge?.isVSCode === true", { frame: true });

    const size = await page.evaluate (makeWAV, { frame: true });
    const result = await page.evaluate (`panelBridge.saveFile ({ suggestedName: "${fixture}.wav", data: testWAV, mimeType: "audio/wav" })`, { frame: true });
    check (result?.saved === true && result.path === `/fake/vscode/${fixture}.wav`, "vscode: saveFile resolves saved, with the host's path: " + JSON.stringify (result));

    const saved = await page.evaluate (`(() => { const f = savedFiles.at (-1); return f && { name: f.name, mimeType: f.mimeType, size: f.size, numChunks: f.numChunks, bytes: Array.from (f.bytes) }; })()`);
    const sent = await page.evaluate ("Array.from (testWAV)", { frame: true });
    check (saved?.name === `${fixture}.wav` && saved.mimeType === "audio/wav" && saved.size === size && saved.numChunks === 1, "vscode: the host got the name, type and size");
    check (saved && saved.bytes.length === sent.length && saved.bytes.every ((b, i) => b === sent[i]), "vscode: the WAV's bytes arrive intact");
    check (saved && String.fromCharCode (...saved.bytes.slice (0, 4)) === "RIFF", "vscode: ...and it's a RIFF file");

    await page.evaluate ("window.nextSaveCancels = true");
    const cancelled = await page.evaluate (`panelBridge.saveFile ({ suggestedName: "c.wav", data: testWAV })`, { frame: true });
    check (cancelled?.saved === false && cancelled.cancelled === true && ! cancelled.path, "vscode: cancelling resolves cancelled");
    check (await page.evaluate ("savedFiles.length") === 1, "vscode: ...and nothing was saved");

    // concurrent saves, answered in reverse order
    await page.evaluate ("window.holdSaveReplies = true");
    await page.evaluate (`window.concurrent = Promise.all (["one", "two", "three"].map ((n, i) =>
                              panelBridge.saveFile ({ suggestedName: n + ".bin", data: new Uint8Array ([i, i, i]) }))); 0`, { frame: true });
    check (await page.waitFor ("heldSaveReplies.length === 3"), "vscode: three saves in flight at once");
    await page.evaluate ("releaseSaveReplies ([2, 1, 0])");
    const three = await page.evaluate ("concurrent", { frame: true });
    check (three?.map (r => r.path).join() === "/fake/vscode/one.bin,/fake/vscode/two.bin,/fake/vscode/three.bin", "vscode: replies in reverse order reach the right callers");
    check (await page.evaluate ("savedFiles.slice (1).map (f => f.name + ':' + f.bytes.join ('')).join()") === "one.bin:000,two.bin:111,three.bin:222",
           "vscode: each file has its own bytes");
    await page.evaluate ("window.holdSaveReplies = false");

    // a file of several chunks
    const big = await page.evaluate (`(async () => {
        const { saveFileChunkSize } = await import ("/panel_api/core/host-bridge.js");
        const data = new Uint8Array (saveFileChunkSize * 2 + 1234).map ((_, i) => (i * 31) >> 3);
        const r = await panelBridge.saveFile ({ suggestedName: "big.wav", data, mimeType: "audio/wav" });
        let sum = 0; for (const b of data) sum = (sum * 31 + b) % 1000000007;
        return { r, size: data.length, sum };
    })()`, { frame: true });
    const bigSaved = await page.evaluate (`(() => { const f = savedFiles.at (-1); let sum = 0; for (const b of f.bytes) sum = (sum * 31 + b) % 1000000007;
                                                    return { name: f.name, numChunks: f.numChunks, size: f.bytes.length, sum }; })()`);
    check (big?.r?.saved && bigSaved.name === "big.wav" && bigSaved.numChunks === 3 && bigSaved.size === big.size && bigSaved.sum === big.sum,
           `vscode: a ${(big?.size / 1e6).toFixed (1)} MB file goes in 3 chunks and arrives whole`);

    //==============================================================================
    // the browser (chooser) page: a real download
    await page.send ("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: downloads, eventsEnabled: true });
    let downloadState = null;
    page.onEvent (e => { if (e.method === "Browser.downloadProgress") downloadState = e.params.state; });

    await page.navigate (`${server.url}/session_${fixture}/cmaj-patch-chooser.html`);
    await page.waitFor ("!! document.getElementById ('cmaj-patch-panel')?.bridge");
    const browserSize = await page.evaluate (makeWAV);
    check (await page.evaluate ("panelBridge.isVSCode") === false, "browser: not in VS Code mode");

    const browserResult = await page.evaluate (`panelBridge.saveFile ({ suggestedName: "recording.wav", data: new Blob ([testWAV], { type: "audio/wav" }), mimeType: "audio/wav" })`);
    check (browserResult?.saved === true && browserResult.path === undefined, "browser: saveFile resolves saved, with no path");

    for (let i = 0; i < 50 && ! readdirSync (downloads).includes ("recording.wav"); ++i)
        await sleep (100);

    const files = readdirSync (downloads);
    check (files.includes ("recording.wav"), "browser: the download appears, with the suggested name: " + files.join (", "));

    if (files.includes ("recording.wav"))
    {
        const bytes = readFileSync (join (downloads, "recording.wav"));
        const expected = await page.evaluate ("Array.from (testWAV)");
        check (bytes.length === browserSize && expected.every ((b, i) => b === bytes[i]), "browser: the downloaded file has the right bytes");
    }

    check (downloadState === "completed", "browser: Chrome reports the download completed");
    check (await page.evaluate ("document.querySelectorAll ('a[download]').length") === 0, "browser: the link was removed");

    const problems = page.problems.filter (p => ! /favicon/.test (p));
    check (problems.length === 0, "no console errors: " + problems.join ("; "));
}
finally
{
    console.log (`${passed}/${passed + failed} save-file checks passed`);
    process.exitCode = failed === 0 ? 0 : 1;
    rmSync (downloads, { recursive: true, force: true });
    await page.close();
    server.close();
}
