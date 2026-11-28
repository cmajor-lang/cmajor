// Tests patches' custom GUIs, each on its own in headless Chrome against the mock session, with
// the endpoints a real `cmaj server` reports for the patch (see harness/patch-gui.mjs).
//
// Every patch in cmajor/examples/patches whose manifest has a custom view gets the same basic
// checks (see testAnyPatchGui()): that it opens without errors, survives being clicked and dragged
// all over and resized, ends every parameter gesture and note it starts, and removes its listeners
// when it's closed. A patch can also have its own tests, in patch_guis/<patch file name>.mjs (see
// patch_guis/README.md).
//
// Usage: node cmajor/tests/panel/test-patch-guis.mjs [--only=TX81Z,Pro54]
//
// CMAJ_PATCH_GUI_FOLDERS adds more folders to search for patches (separated like PATH).
// PANEL_TEST_SCREENSHOTS names a folder to save a screenshot of each GUI in.

import { readFileSync, readdirSync, existsSync, mkdirSync } from "node:fs";
import { join, dirname, basename, delimiter, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { cmajorRoot } from "./harness/serve.mjs";
import { startPatchGuiServer, TestResults, formatErrors, sleep } from "./harness/patch-gui.mjs";

//==============================================================================
// Checks that are known to fail, for now: patch file name -> { check ID: why }. These are
// reported, but don't fail the tests. When one gets fixed, the tests say so: take it off here,
// so that it can't come back. A new GUI should never need an entry.
const knownIssues = {
};

//==============================================================================
const here = dirname (fileURLToPath (import.meta.url));
const only = process.argv.find (a => a.startsWith ("--only="))?.substring (7).split (",").map (s => s.trim()).filter (Boolean);
const screenshots = process.env.PANEL_TEST_SCREENSHOTS;

if (screenshots)
    mkdirSync (screenshots, { recursive: true });

const folders = [join (cmajorRoot, "examples/patches"),
                 ...(process.env.CMAJ_PATCH_GUI_FOLDERS?.split (delimiter).filter (Boolean).map (f => resolve (f)) ?? [])];

const patches = folders.flatMap (f => findPatchesWithCustomViews (f))
                       .filter (p => ! only || only.includes (basename (p, ".cmajorpatch")));

if (patches.length === 0)
{
    console.error ("No patches with custom GUIs found" + (only ? ` matching --only=${only.join (",")}` : ""));
    process.exit (1);
}

const guis = await startPatchGuiServer();
const results = [];

try
{
    for (const patchFile of patches)
    {
        const patchName = basename (patchFile, ".cmajorpatch");
        const t = new TestResults (patchName);
        results.push (t);
        console.log (`\n--- ${patchName} (${patchFile})`);

        let name;

        try
        {
            name = await guis.addPatch (patchFile);
        }
        catch (e)
        {
            t.fail ("the patch loads in cmaj server: " + e.message);
            continue;
        }

        await runTest (t, () => testAnyPatchGui (guis, name, t, patchName));

        const ownTests = join (here, "patch_guis", patchName + ".mjs");

        if (existsSync (ownTests))
        {
            const module = await import (pathToFileURL (ownTests).href);
            await runTest (t, () => module.default (guis, name, t));
        }
    }
}
finally
{
    guis.close();
}

console.log ("\nPatch GUI results:");

for (const t of results)
{
    console.log ("  " + t.summary());

    for (const f of t.failures)
        console.log ("      FAILED: " + f);

    for (const f of t.fixed)
        console.log ("      FIXED (take it off knownIssues): " + f);
}

process.exitCode = results.every (t => t.ok) ? 0 : 1;

//==============================================================================
async function runTest (t, fn)
{
    try
    {
        await fn();
    }
    catch (e)
    {
        t.fail ("the test threw: " + (e?.stack ?? e));
    }
}

/// The checks that every custom patch GUI gets. Each has an ID, for knownIssues.
async function testAnyPatchGui (guis, name, t, patchName)
{
    const check = (id, condition, what) => knownIssues[patchName]?.[id] ? t.checkKnownIssue (condition, what, knownIssues[patchName][id])
                                                                          : t.check (condition, what);
    // (each of these checks only the errors since the last one)
    let numErrorsChecked = 0;

    const checkNoErrors = async (id, what) =>
    {
        const errors = (await page.getErrors()).slice (numErrorsChecked);
        numErrorsChecked += errors.length;
        check (id, errors.length === 0, `no errors ${what}: ${formatErrors (errors) || "none"}`);
    };

    const manifestView = guis.getStatus (name).manifest.view ?? {};
    const width = Math.max (400, manifestView.width ?? 800), height = Math.max (300, manifestView.height ?? 600);
    const page = await guis.open (name, { width, height });

    if (! check ("opens", ! page.failedToOpen, "the view opens: " + formatErrors (await page.getErrors())))
        return;

    check ("custom", await page.evaluate ("window.isCustomView"), "it's the patch's own view, not the generic one");

    const rect = await page.evaluate ("(() => { const r = window.view.getBoundingClientRect(); return { left: r.left, top: r.top, width: r.width, height: r.height }; })()");
    check ("size", rect.width > 10 && rect.height > 10, `the view has a size: ${Math.round (rect.width)} x ${Math.round (rect.height)}`);

    // (let any meters, animations or timers run for a moment)
    await sleep (1000);

    if (screenshots)
        await page.screenshot (join (screenshots, `gui-${patchName}.png`));

    await checkNoErrors ("errors-opening", "when it opens");

    // Click and drag all over it, the way someone trying it out might
    await page.clearSent();
    const columns = 7, rows = 5;

    for (let row = 0; row < rows; ++row)
    {
        for (let column = 0; column < columns; ++column)
        {
            const x = rect.left + rect.width * (column + 0.5) / columns;
            const y = rect.top + rect.height * (row + 0.5) / rows;

            if ((row + column) % 2)
                await page.click (x, y);
            else
                await page.drag (x, y, x + 15, y - 40, 5);

            // (anything that opened a menu or editor, or took the keyboard focus, gets put away)
            await page.key ("Escape");
        }
    }

    await sleep (500);
    await checkNoErrors ("errors-using", "from clicking and dragging all over it");

    const gestures = await page.evaluate ("({ open: window.session.getOpenGestures(), unmatched: window.session.unmatchedGestureEnds })");
    check ("gestures", Object.keys (gestures.open).length === 0 && gestures.unmatched.length === 0,
           "every parameter gesture it started has ended, and none ended that hadn't started: " + JSON.stringify (gestures));

    const stuckNotes = getNotesLeftOn (await page.getSent());
    check ("notes", stuckNotes.length === 0, "every MIDI note it sent has a note-off: " + (stuckNotes.join (", ") || "none left on"));

    // Resize it
    for (const scale of [0.5, 1.5, 1])
    {
        await page.setSize (Math.round (width * scale), Math.round (height * scale));
        await sleep (200);
    }

    await checkNoErrors ("errors-resizing", "from resizing it");

    // Close it
    await page.evaluate ("window.removeView(); 1");
    await sleep (200);
    const listenersLeft = await page.evaluate (`Object.entries (window.pc.listenersPerType).filter (([, l]) => l.length > 0).map (([type, l]) => type + " x" + l.length)`);
    check ("listeners", listenersLeft.length === 0, "closing it removes all its listeners from the patch connection: " + (listenersLeft.join (", ") || "none left"));
    const pageListenersLeft = await page.evaluate ("window.getPageListenersLeft()");
    check ("page-listeners", pageListenersLeft.length === 0, "closing it removes all the listeners it added to the document and window: " + (pageListenersLeft.join (", ") || "none left"));
    await checkNoErrors ("errors-closing", "from closing it");
}

/// The notes that MIDI messages sent by the view turned on and didn't turn off again
function getNotesLeftOn (sent)
{
    const held = new Map();

    for (const msg of sent)
    {
        const message = msg.type === "send_value" ? msg.value?.message : undefined;

        if (typeof message !== "number")
            continue;

        const status = (message >> 16) & 0xf0, key = `${msg.id} ch${((message >> 16) & 0x0f) + 1} note ${(message >> 8) & 0x7f}`;
        const count = held.get (key) ?? 0;

        if (status === 0x90 && (message & 0x7f) > 0)
            held.set (key, count + 1);
        else if ((status === 0x80 || status === 0x90) && count > 0)
            held.set (key, count - 1);
    }

    return [...held].filter (([, n]) => n > 0).map (([key]) => key);
}

function findPatchesWithCustomViews (folder, depth = 0)
{
    if (! existsSync (folder) || depth > 3)
        return [];

    const found = [];

    for (const entry of readdirSync (folder, { withFileTypes: true }).sort ((a, b) => a.name.localeCompare (b.name)))
    {
        const path = join (folder, entry.name);

        if (entry.isDirectory())
        {
            found.push (...findPatchesWithCustomViews (path, depth + 1));
        }
        else if (entry.name.endsWith (".cmajorpatch"))
        {
            try
            {
                if (JSON.parse (readFileSync (path, "utf8")).view?.src)
                    found.push (path);
            }
            catch {}
        }
    }

    return found;
}
