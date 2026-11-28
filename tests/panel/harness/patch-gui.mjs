// Helpers for testing patch GUIs: a patch's view, opened in headless Chrome on its own, against
// the mock session (see ../mock/patch-view.html for what the page provides).
//
//   const guis = await startPatchGuiServer();             // a cmaj server and a file server
//   const name = await guis.addPatch ("path/to/Patch.cmajorpatch");
//   const page = await guis.open (name, { width: 1200, height: 700 });
//   await page.inView ("this.shadowRoot.querySelector ('.knob') !== null");
//   guis.close();
//
// open() reuses one Chrome page for every view, starting it afresh each time.
//
// addPatch() loads the patch in a real `cmaj server` (so the endpoints, manifest etc. are what the
// server reports for today's code) and makes a fixture of its status for the mock session.
//
// The checks: const t = new TestResults ("TX81Z"); t.check (condition, "what");  ...  t.summary()

import { basename } from "node:path";
import { openChrome, sleep } from "./cdp.mjs";
import { startServer, fixtureOverrides } from "./serve.mjs";
import { startCmajServer, makeFixtureFromStatus } from "./cmaj-server.mjs";

export { sleep };

export async function startPatchGuiServer()
{
    const cmajServer = await startCmajServer();
    const fileServer = await startServer();
    let page = null;

    return {
        /// Loads the patch in cmaj server, and returns the fixture name to open() it with
        async addPatch (patchFile)
        {
            const status = await cmajServer.loadPatch (patchFile);
            const name = basename (patchFile, ".cmajorpatch").replace (/[^A-Za-z0-9]/g, "") + fixtureOverrides.size;
            fixtureOverrides.set (name, makeFixtureFromStatus (patchFile, status));
            return name;
        },

        getStatus (name)    { return fixtureOverrides.get (name)?.status; },

        /// Opens a fixture's patch view (in the one Chrome page that the tests share, which starts
        /// afresh each time), and waits for it to be ready, or to fail: see page.failedToOpen.
        /// options: width, height, deviceScaleFactor, and query (extra URL parameters for
        /// patch-view.html, e.g. "nocapture" or "worker=0")
        async open (name, { width = 1400, height = 860, deviceScaleFactor = 1, query = "" } = {})
        {
            if (! page)
            {
                page = addViewHelpers (await openChrome ({ width, height, port: 9341 }));

                // (so that a GUI that opens a file chooser doesn't wait for one)
                await page.send ("Page.setInterceptFileChooserDialog", { enabled: true });
            }

            await page.navigate ("about:blank");
            await page.send ("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor, mobile: false });
            page.problems.length = 0;
            await page.navigate (`${fileServer.url}/session_${name}/patch-view.html?fixture=${name}${query ? "&" + query : ""}`);

            if (! await page.waitFor ("window.ready === true || window.failedToOpen === true", { timeoutMs: 20000 }))
                throw new Error (`The view for ${name} didn't open (no page errors yet: ${page.problems.join ("; ")})`);

            page.failedToOpen = await page.evaluate ("window.failedToOpen === true");
            await sleep (300);
            return page;
        },

        close()
        {
            cmajServer.close();
            fileServer.close();
            page?.close();
        }
    };
}

function addViewHelpers (page)
{
    /// Evaluates an expression with `this` as the view (or, for a function body, runs it)
    page.inView = (expression) => page.evaluate (`(function() { return (${expression}); }).call (window.view)`);

    /// The rectangle of the first element matching a selector, in the view's shadow root if it has
    /// one (else in the view): { x, y (its centre), left, top, right, bottom, width, height }, or null
    page.rectOf = (selector) => page.evaluate (`(() => {
        const root = window.view.shadowRoot ?? window.view;
        const e = root.querySelector (${JSON.stringify (selector)});
        if (! e) return null;
        const r = e.getBoundingClientRect();
        return { x: r.left + r.width / 2, y: r.top + r.height / 2, left: r.left, top: r.top, right: r.right, bottom: r.bottom, width: r.width, height: r.height };
    })()`);

    /// The messages the view has sent since the last clearSent(), e.g. { type: "send_value", id, value }
    page.getSent = () => page.evaluate ("window.sent");
    page.clearSent = () => page.evaluate ("window.sent = []; 1");

    /// Page errors so far: from the page (uncaught errors) and from Chrome (exceptions, console.error)
    page.getErrors = async () => [...await page.evaluate ("window.errors"), ...page.problems];

    return page;
}

//==============================================================================
export class TestResults
{
    constructor (name)
    {
        this.name = name;
        this.passed = 0;
        this.failures = [];
        this.knownIssues = [];
        this.fixed = [];
    }

    check (condition, what)
    {
        if (condition)
            ++this.passed;
        else
            this.failures.push (what);

        console.log (`${condition ? "  PASS" : "  FAIL"}  ${this.name}: ${what}`);
        return !! condition;
    }

    fail (what)     { return this.check (false, what); }

    /// A check that's known to fail for now (issue says why): its failure is reported, but
    /// doesn't fail the tests, and if it passes, that's reported too, so it can come off the list
    checkKnownIssue (condition, what, issue)
    {
        if (condition)
        {
            ++this.passed;
            this.fixed.push (what);
            console.log (`  FIXED ${this.name}: ${what} (a known issue that's now fixed: take it off the list)`);
        }
        else
        {
            this.knownIssues.push (`${what} (known issue: ${issue})`);
            console.log (`  KNOWN ${this.name}: ${what} (known issue: ${issue})`);
        }

        return !! condition;
    }

    get ok()        { return this.failures.length === 0; }

    summary()
    {
        return `${this.name}: ${this.passed} passed`
                 + (this.failures.length ? `, ${this.failures.length} FAILED` : "")
                 + (this.knownIssues.length ? `, ${this.knownIssues.length} known issue(s)` : "")
                 + (this.fixed.length ? `, ${this.fixed.length} known issue(s) now FIXED` : "");
    }
}

/// Page errors as one line: the first line of each different one
export function formatErrors (errors)
{
    const text = [...new Set (errors.map (e => String (e).split ("\n")[0]))].join ("; ");
    return text.length > 500 ? text.substring (0, 500) + "..." : text;
}
