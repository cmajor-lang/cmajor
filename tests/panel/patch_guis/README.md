# Patch GUI tests

`test-patch-guis.mjs` (the `guis` suite of `run_panel_tests.mjs`) tests the custom GUI of every patch in `examples/patches` whose manifest has a `view` with a `src`. A new example patch with a GUI is tested as soon as it's added: there's nothing to register.

```
$ node cmajor/tests/panel/test-patch-guis.mjs [--only=TX81Z,Pro54]
```

It needs Node 22+, Chrome and a cmaj binary (see [../README.md](../README.md)), and takes about a minute and a half for all of them. Set `PANEL_TEST_SCREENSHOTS=<folder>` to get a screenshot of each GUI (`gui-<patch>.png`), plus any that a patch's own tests take. `CMAJ_PATCH_GUI_FOLDERS` adds folders to look for patches in (separated like `PATH`).

## How a GUI is run

Each patch is loaded in a real `cmaj server`, so its manifest and endpoints are what the server reports for the code as it is. Its GUI is then shown on its own in headless Chrome, by `../mock/patch-view.html`, with `createPatchViewHolder()` as hosts do, and connected to the mock session (`../mock/mock-server-session.js`) instead of a server. The mock session acts like a patch: parameters keep and echo their values, MIDI sent to an input goes back to that input's listeners, stored state is kept, outputs send synthesised audio, and gestures are tracked. If the manifest has a worker, it runs too, with its own connection.

## The checks that every GUI gets

| ID | Check |
|---|---|
| `opens`, `custom`, `size` | The view opens, it's the patch's own view, and it has a size |
| `errors-opening` | No errors when it opens and runs for a second |
| `errors-using` | No errors from clicking and dragging all over it (a 7 × 5 grid, with Escape after each) |
| `gestures` | Every parameter gesture it started has ended, and none ended that hadn't started |
| `notes` | Every MIDI note it sent has a note-off |
| `errors-resizing` | No errors from resizing it to half and one-and-a-half times its size |
| `listeners`, `page-listeners`, `errors-closing` | Taking it out of the page removes every listener it added to its connection, and to the document and window, without errors |

A check that fails for a patch that's known not to pass it yet is listed in `knownIssues`, at the top of `test-patch-guis.mjs`, with the reason. Those are reported, but don't fail the tests. When one gets fixed, the tests say so: take it off the list then, so that it can't come back. A new GUI should pass them all.

## A patch's own tests

For checks of what a particular GUI does, add `patch_guis/<patch file name>.mjs` (e.g. `TX81Z.mjs` for `TX81Z.cmajorpatch`). It's run after the basic checks, and should export a function like this:

```js
import { sleep } from "../harness/patch-gui.mjs";

export default async function testMyPatch (guis, name, t)
{
    const page = await guis.open (name, { width: 1200, height: 700 });   // also deviceScaleFactor, query

    const r = await page.rectOf (".my-knob");           // in the view's shadow root
    await page.clearSent();
    await page.drag (r.x, r.y, r.x, r.y - 50);
    await sleep (50);

    const sent = await page.getSent();                  // e.g. { type: "send_value", id, value }
    t.check (sent.some (m => m.type === "send_value" && m.id === "gainIn"), "dragging the knob changes the gain");
    t.check (await page.inView ("this.querySelector ('.label').textContent") === "Gain", "it has a label");
    t.check ((await page.getErrors()).length === 0, "no errors");
}
```

Each `guis.open()` starts the page afresh, so separate tests can't affect each other. Use plenty of them.

What the page has:

- `page.inView (expression)` evaluates an expression in the page with `this` as the view. `page.evaluate()` evaluates one with no `this`.
- `page.rectOf (selector)` returns `{ x, y, left, top, right, bottom, width, height }` of an element, in the view's shadow root if it has one, or `null`. `x` and `y` are its centre.
- `page.click()`, `page.drag()`, `page.mouse()`, `page.key()` and `page.setSize()` come from `../harness/cdp.mjs`, as do `page.screenshot()` and `page.send()` for raw DevTools protocol calls.
- `page.getSent()` returns the messages the view's connection has sent since `page.clearSent()`.
- `page.getErrors()` returns uncaught errors, rejections and `console.error`s.
- In the page:
  - `window.hostSetsValue (id, value)` changes a parameter as a host or automation would.
  - `window.receiveEvent (id, value)` delivers an event to an input from elsewhere, e.g. `receiveEvent ("midiIn", { message: 0x903c64 })`.
  - `window.getStoredState (key)` reads the stored state.
  - `window.session` is the mock session, `window.pc` the view's connection, and `window.removeView()` takes the view out of the page.
- `query` options for `open()`:
  - `"worker=0"` doesn't run the worker.
  - `"workerLatency=<ms>"` delays everything the worker is sent, like a host whose worker runs elsewhere, to check that the GUI copes with its replies coming late.
  - `"nocapture"` makes `setPointerCapture()` do nothing, as in some hosts' webviews, to check that drags still end properly without it.

`t.check (condition, what)` records a result. Put the values that matter in `what`, so that a failure says what went wrong.

Tips:

- Wait a little (`sleep (50)`) before checking what a GUI did in response to the session. The session answers asynchronously, and echoes MIDI and parameter changes back.
- The GUI starts with the parameters' `init` values. Tests that depend on them, or on a default preset, will need updating when those change.
