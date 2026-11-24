// Interaction checks for the new panel, against the mock session inside the fake VS Code webview.
// Usage: node cmajor/tests/panel/test-panel-mock.mjs [Fixture]     (default Tremolo; SineSynth is also good)
// With PANEL_TEST_SCREENSHOTS=<folder>, it saves screenshots there (mock-*.png) as it goes.

import { startServer, fileOverrides, cmajorRoot } from "./harness/serve.mjs";
import { openChrome, sleep } from "./harness/cdp.mjs";
import { join, resolve } from "node:path";
import { readFile } from "node:fs/promises";
import { mkdirSync } from "node:fs";

const shots = process.env.PANEL_TEST_SCREENSHOTS ? resolve (process.env.PANEL_TEST_SCREENSHOTS) : "";

if (shots)
    mkdirSync (shots, { recursive: true });

const fixture = process.argv[2] ?? "Tremolo";

let failures = 0;
const check = (ok, message) => { console.log (`${ok ? "  ok  " : "  FAIL"} ${message}`); if (! ok) ++failures; };

const server = await startServer();
const page = await openChrome ({ width: 1400, height: 860, port: 9339 });
const inPanel = (js) => page.evaluate (`(async () => { const panel = document.querySelector ("cmaj-patch-panel"); const mock = window.cmajMock; ${js} })()`, { frame: true });
const waitInPanel = (js, timeoutMs = 5000) => page.waitFor (`(() => { const panel = document.querySelector ("cmaj-patch-panel"); const mock = window.cmajMock; return ${js}; })()`, { frame: true, timeoutMs });
const shoot = (file, clip) => shots ? page.screenshot (file, clip) : Promise.resolve();

try
{
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    check (await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000), "the patch loads and the panel makes a connection");
    await sleep (600);

    // VS Code integration
    const messages = await page.evaluate ("window.vscodeMessages");
    check (messages.some (m => m.newServerStatus?.loaded), "the status is sent to VS Code");
    check (await inPanel ("return panel.dataset.theme === 'dark' && panel.hasAttribute ('data-host-theme') && getComputedStyle (panel).getPropertyValue ('--cp-accent').trim() === '#0078d4'"),
           "VS Code's theme is applied");

    // tabs and keyboard (key events go to the focused frame)
    await page.evaluate ("document.querySelector ('iframe').focus(); true");
    await page.key ("2");
    check (await waitInPanel ("panel.workspace.mainTab === 'graph'"), "key 2 shows the graph");
    check (await waitInPanel ("!! panel.graphView.graph.shadowRoot.querySelector ('svg .node')", 5000), "the graph is fetched when its tab is shown");
    await page.key ("3");
    // a patch whose endpoints are all host-timeline ones (TimelineTester) has no I/O widgets, and no audio
    const fixtureHasAudio = await inPanel ("return [...mock.getInputs(), ...mock.getOutputs()].some (e => (e.purpose ?? '').startsWith ('audio'))");
    check (await waitInPanel ("panel.workspace.mainTab === 'io' && (panel.ioView.widgets.length > 0 || ! " + fixtureHasAudio + ")"), "key 3 shows the I/O widgets");
    await sleep (800);
    await shoot (join (shots, `mock-${fixture}-io.png`));

    const audioWidgetGetsData = fixtureHasAudio ? `panel.ioView.widgets.some (w => w.display && w.display.lastDataTime > performance.now() - 300)` : "true";
    if (! fixtureHasAudio) console.log ("        (this fixture has no audio endpoints: the audio meter checks are skipped)");
    check (await waitInPanel (audioWidgetGetsData), "the audio meters get data");

    // only one server listener per endpoint (the GUI's own listeners aside)
    const listenerCounts = await inPanel (`const counts = {}; for (const l of mock.listeners.values()) counts[l.endpointID + "/" + l.granularity] = (counts[l.endpointID + "/" + l.granularity] ?? 0) + 1; return counts;`);
    console.log ("        server listeners:", JSON.stringify (listenerCounts));

    // status diffing: a new status with the same endpoints mustn't re-create the widgets
    await inPanel ("window.firstWidget = panel.ioView.widgets[0]; mock.sendSessionStatus(); mock.setPlaying (true);");
    await sleep (200);
    check (await inPanel ("return panel.ioView.widgets[0] === window.firstWidget"), "status updates don't re-create the widgets");

    // a rebuild keeps the widgets, and the hub re-adds the server listeners the server dropped
    await inPanel ("mock.rebuild()");
    check (await waitInPanel ("mock.isLoaded"), "rebuilt");
    await sleep (400);
    check (await inPanel ("return panel.ioView.widgets[0] === window.firstWidget"), "a rebuild keeps the widgets");
    check (await waitInPanel (audioWidgetGetsData, 3000), "the meters get data again after a rebuild (listeners re-added)");

    // switching away pauses the I/O view's subscriptions
    const ioViewHasWidgets = await inPanel ("return panel.ioView.widgets.length > 0");
    const before = await inPanel ("return mock.listeners.size");
    await page.key ("1");
    await sleep (200);
    const after = await inPanel ("return mock.listeners.size");
    check (after < before || before === 0 || ! ioViewHasWidgets, `hiding the I/O view releases its listeners (${before} → ${after})`);

    // the GUI tab
    check (await waitInPanel ("!! panel.guiView.view"), "the GUI tab shows the patch's view");
    await inPanel ("window.marker = 123; window.firstView = panel.guiView.view;");

    // hot reload: asset changes re-create the GUI without reloading the page
    await inPanel ("mock.assetFilesChanged()");
    check (await waitInPanel ("panel.guiView.view && panel.guiView.view !== window.firstView", 4000), "an asset change re-creates the GUI");
    check (await inPanel ("return window.marker === 123"), "...without reloading the page");
    check (! (await page.evaluate ("window.vscodeMessages")).some (m => m.reloading), "...and without the reloading handshake");

    if (fixture === "Tremolo")
        await testRelativeImportReload();

    // a plain rebuild keeps the GUI
    await inPanel ("window.secondView = panel.guiView.view; mock.rebuild();");
    await waitInPanel ("mock.isLoaded");
    await sleep (300);
    check (await inPanel ("return panel.guiView.view === window.secondView"), "a DSP rebuild keeps the GUI instance");

    // the floating window: a second instance
    await page.key ("g");
    check (await waitInPanel ("panel.floatWindow.isOpen && !! panel.floatWindow.guiView?.view", 4000), "G opens the floating GUI with its own view");
    check (await inPanel ("return panel.floatWindow.guiView.view !== panel.guiView.view && panel.workspace.mainTab === 'graph'"),
           "it's a separate instance, and the main area switches to the graph");

    const size = await inPanel ("return panel.floatWindow.sizeText.textContent");
    const manifestView = await inPanel ("return panel.store.get ('manifest')?.view ?? null");
    console.log ("        floating window:", size, "manifest view:", JSON.stringify (manifestView));

    await inPanel ("panel.floatWindow.applyPreset (panel.floatWindow.getPresets()[2].value)");
    await sleep (100);
    check (await inPanel ("return panel.floatWindow.presetSelect.value === panel.floatWindow.getPresets()[2].value"), "a size preset resizes the window");

    // drag the window by its title bar, then resize it
    const rect = await inPanel ("const r = panel.floatWindow.titleBar.getBoundingClientRect(); return { x: r.left + 40, y: r.top + 14 };");
    await page.drag (rect.x, rect.y, rect.x - 300, rect.y - 200);
    const moved = await inPanel ("const r = panel.floatWindow.titleBar.getBoundingClientRect(); return { x: r.left + 40, y: r.top + 14 };");
    check (Math.abs (moved.x - (rect.x - 300)) < 3 && Math.abs (moved.y - (rect.y - 200)) < 3, "the title bar drags the window");

    await inPanel ("panel.floatWindow.setFixed (false)");
    const corner = await inPanel ("const r = panel.floatWindow.element.querySelector ('.cp-float-rb').getBoundingClientRect(); return { x: r.left + 7, y: r.top + 7 };");
    const sizeBefore = await inPanel ("return panel.floatWindow.sizeText.textContent");
    await page.drag (corner.x, corner.y, corner.x - 120, corner.y - 60);
    const sizeAfter = await inPanel ("return panel.floatWindow.sizeText.textContent");
    check (sizeAfter !== sizeBefore && await inPanel ("return panel.floatWindow.presetSelect.value === 'custom'"), `the corner resizes it (${sizeBefore} → ${sizeAfter}), shown as Custom`);

    await inPanel ("panel.floatWindow.setFixed (true)");
    check (await inPanel ("return panel.floatWindow.presetSelect.value === 'manifest' && panel.floatWindow.element.classList.contains ('cp-fixed')"), "Fixed snaps to the manifest size");

    // drag it off the page: it's kept (partly) inside
    const title = await inPanel ("const r = panel.floatWindow.titleBar.getBoundingClientRect(); return { x: r.left + 40, y: r.top + 14 };");
    await page.drag (title.x, title.y, 1390, 850);
    check (await inPanel ("const r = panel.floatWindow.element.getBoundingClientRect(); return r.left < innerWidth - 40 && r.top < innerHeight - 20;"), "it can't be dragged out of view");
    await inPanel ("panel.floatWindow.setPosition (700, 330); panel.floatWindow.setFixed (false); panel.floatWindow.applyPreset ('manifest');");

    // both instances live at once
    await page.key ("1");
    await sleep (600);
    await shoot (join (shots, `mock-${fixture}-both.png`));
    await page.key ("2");
    await sleep (500);
    await shoot (join (shots, `mock-${fixture}-graph-float.png`));

    await page.key ("g");
    check (await waitInPanel ("! panel.floatWindow.isOpen && ! panel.floatWindow.guiView"), "G closes it again (disposing its view)");

    // build errors
    await inPanel ("mock.setBuildError()");
    check (await waitInPanel ("panel.workspace.drawerTab === 'problems' && panel.store.get ('diagnostics').length === 1"), "a build error opens Problems");
    check (await inPanel ("return panel.topBar.buildChip.dataset.state === 'error' && ! panel.workspace.drawer.tabs[1].badge.hidden"), "the build chip and the badge show it");
    await page.key ("1");
    check (await inPanel ("return ! panel.guiView.banner.hidden && !! panel.guiView.view"), "the GUI keeps the last good build, with a banner");
    await sleep (300);
    await shoot (join (shots, `mock-${fixture}-error.png`));

    await page.evaluate ("window.vscodeMessages.length = 0");
    await inPanel ("document.querySelector ('.cp-problem-location').click()");
    const shown = (await page.evaluate ("window.vscodeMessages")).find (m => m.showSourceFile)?.showSourceFile;
    check (/:12:5$/.test (shown ?? ""), `clicking the problem opens its source (${shown})`);

    await inPanel ("mock.clearBuildError()");
    check (await waitInPanel ("panel.store.get ('loaded') && panel.store.get ('diagnostics').length === 0 && panel.guiView.banner.hidden"), "fixing the error clears it all");

    // Phase 3: the graph, fetched once per graphVersion, kept up to date, and remembered
    const graphRequests = "mock.messageLog.filter (m => m.type === 'req_graph').length";
    const codegenGraphRequests = "mock.messageLog.filter (m => m.type === 'req_codegen' && m.codeType === 'graph').length";
    const nodeCount = "panel.graphView.graph.shadowRoot.querySelectorAll ('svg .node').length";
    const hasExtraNode = "!! panel.graphView.graph.shadowRoot.querySelector ('svg [data-node-id=\"n_extra\"]')";
    const graphState = "({ version: panel.graphView.graph.shownVersion, collapsed: panel.graphView.graph.view.getCollapsedPaths(), k: panel.graphView.graph.view.transform.k })";

    await page.key ("2");
    check (await waitInPanel (`panel.graphView.graph.shownVersion === mock.getGraphVersion() && ${nodeCount} > 0`), "the graph tab shows the current graphVersion");
    check (await inPanel (`return ${codegenGraphRequests} === 0 && ${graphRequests} >= 1`), "it's fetched with req_graph, not req_codegen");

    // the default view: the text at its designed size (not the whole graph squeezed in), anchored at the top-left
    const viewState = `(() => { const g = panel.graphView.graph, v = g.view;
                                 return { mode: g.viewMode, k: v.transform.k, x: v.transform.x, y: v.transform.y, fitK: v.getFitScale(),
                                          drawnWidth: v.positioned.width * v.transform.k, containerWidth: v.container.clientWidth,
                                          zoomText: panel.graphView.zoomText.textContent }; })()`;
    const isDefaultView = (s) => s.mode === "default" && s.k >= 1 - 1e-9 && s.k <= 1.25 + 1e-9 && (s.fitK >= 1 || Math.abs (s.k - 1) < 1e-9)
                                  && s.y > 0 && s.y <= 40 && (s.drawnWidth <= s.containerWidth - 40 || Math.abs (s.x - 20) < 1);
    await sleep (200);
    const initialView = await inPanel (`return ${viewState}`);
    check (isDefaultView (initialView), `the graph opens at a readable zoom, anchored at its top-left (${JSON.stringify (initialView)})`);
    check (initialView.zoomText === Math.round (initialView.k * 100) + "%", `the tab bar shows the zoom (${initialView.zoomText})`);

    await page.setSize (520, 860);
    await sleep (500);
    const narrowView = await inPanel (`return ${viewState}`);
    check (isDefaultView (narrowView), `...and stays readable in a narrow panel (k ${narrowView.k.toFixed (3)}, where fitting would give ${narrowView.fitK.toFixed (3)})`);
    await shoot (join (shots, `mock-${fixture}-graph-narrow.png`));

    await page.key ("f");
    await sleep (300);
    const fittedNarrow = await inPanel (`return ${viewState}`);
    check (fittedNarrow.mode === "fitted" && Math.abs (fittedNarrow.k - Math.min (fittedNarrow.fitK, 1.5)) < 1e-6, `F fits the whole graph (k ${fittedNarrow.k.toFixed (3)})`);
    await page.setSize (1400, 860);
    await sleep (500);
    const fittedWide = await inPanel (`return ${viewState}`);
    check (fittedWide.mode === "fitted" && Math.abs (fittedWide.k - Math.min (fittedWide.fitK, 1.5)) < 1e-6, "...and stays fitted when the panel is resized");
    check (await inPanel ("return panel.settings.current.get ('graphView')?.fitted === true"), "...which is saved");

    await page.key ("0");
    await sleep (300);
    check (isDefaultView (await inPanel (`return ${viewState}`)), "0 goes back to the default view");
    await inPanel ("panel.graphView.graph.zoomBy (1.25)");
    await inPanel ("panel.graphView.zoomText.click()");
    await sleep (300);
    check (isDefaultView (await inPanel (`return ${viewState}`)), "...as does clicking the zoom readout");

    // a view the user has moved is left alone when the tab is hidden and shown again, or the panel resized
    await inPanel ("panel.graphView.graph.zoomBy (0.8)");
    const movedK = await inPanel ("return panel.graphView.graph.view.transform.k");
    await page.key ("3");
    await sleep (200);
    await page.key ("2");
    await page.setSize (1300, 860);
    await sleep (500);
    await page.setSize (1400, 860);
    await sleep (300);
    check (await inPanel (`return panel.graphView.graph.viewMode === "user" && Math.abs (panel.graphView.graph.view.transform.k - ${movedK}) < 1e-9`),
           "a zoom the user chose survives switching tabs and resizing");
    await page.key ("0");
    await sleep (300);

    let requestsBefore = await inPanel (`return ${graphRequests}`);
    await inPanel ("mock.rebuild()");
    await waitInPanel ("mock.isLoaded");
    await sleep (400);
    check (await inPanel (`return ${graphRequests}`) === requestsBefore, "a rebuild that doesn't change the graph doesn't fetch it again");

    // a single-processor patch (TimelineTester) has no child nodes to copy, collapse or show live widgets for
    const isGraphPatch = await inPanel ("return (mock.graph?.root?.nodes?.length ?? 0) > 0");
    let nodesBefore = await inPanel (`return ${nodeCount}`);

    if (isGraphPatch)
    {
    // collapse a nested graph (if there is one) and zoom, then change the graph's structure
    const nestedGraphPath = await inPanel (`const view = panel.graphView.graph.view;
                                             const find = (n, d) => (n.isGraph && d >= 1 && n.nodes?.length) ? n : (n.nodes ?? []).map (c => find (c, d + 1)).find (Boolean);
                                             const nested = find (view.model.root, 0);
                                             if (nested && ! view.collapsed.has (nested.id)) await view.toggleCollapsed (nested.id);
                                             return nested?.path ?? null;`);
    await inPanel ("panel.graphView.graph.zoomBy (1.25)");
    await sleep (500);
    const stateBefore = await inPanel (`return ${graphState}`);
    nodesBefore = await inPanel (`return ${nodeCount}`);
    console.log ("        nested graph:", nestedGraphPath, "state:", JSON.stringify (stateBefore));

    await inPanel (`const g = structuredClone (mock.graph);
                    const copy = structuredClone (g.root.nodes.find (n => ! n.isGraph) ?? g.root.nodes[0]);
                    copy.id = "n_extra"; copy.path = "extraNode"; copy.name = "extraNode";
                    delete copy.nodes; delete copy.connections;
                    for (const e of copy.endpoints ?? []) e.path = "extraNode." + e.name;
                    g.root.nodes.push (copy);
                    mock.setGraph (g);`);
    check (await waitInPanel (`panel.graphView.graph.shownVersion === mock.getGraphVersion() && ${hasExtraNode} && ${nodeCount} === ${nodesBefore + 1}`, 4000),
           `a rebuild that changes the graph's structure shows the new graph (${nodesBefore} → ${await inPanel (`return ${nodeCount}`)} nodes)`);
    const stateAfter = await inPanel (`return ${graphState}`);
    check (JSON.stringify (stateAfter.collapsed) === JSON.stringify (stateBefore.collapsed) && Math.abs (stateAfter.k - stateBefore.k) < 1e-9,
           "...keeping the collapsed graphs and the zoom");
    await shoot (join (shots, `mock-${fixture}-graph-rebuilt.png`));

    // a build error keeps the last good graph
    await inPanel ("mock.setBuildError()");
    await sleep (400);
    check (await inPanel (`return ${hasExtraNode} && panel.graphView.graph.messageElement.style.display !== 'block'`), "a build error keeps showing the last good graph");
    await inPanel ("mock.buildError = ''; mock.rebuild();");
    await waitInPanel ("panel.store.get ('loaded')");
    }
    else
    {
        console.log ("        (a single-processor patch: the graph-structure and live-graph checks are skipped)");
    }

    // an older server: no graphVersion, so it falls back to req_codegen "graph" after each build
    const codegenBefore = await inPanel (`return ${codegenGraphRequests}`);
    await inPanel ("mock.setLegacyServer (true); mock.setGraph (structuredClone (mock.fixture.graph));");
    check (await waitInPanel (`${codegenGraphRequests} > ${codegenBefore} && ! ${hasExtraNode} && ${nodeCount} === ${nodesBefore}`, 4000), "with an older server, it falls back to req_codegen \"graph\"");
    await inPanel ("mock.setLegacyServer (false); mock.rebuild();");
    await waitInPanel (`mock.isLoaded && panel.graphView.graph.shownVersion === mock.getGraphVersion()`, 4000);
    await sleep (500);
    const savedState = await inPanel (`return ${graphState}`);

    // state copy and paste, through VS Code's clipboard
    await inPanel ("panel.ctx.actions.copyState()");
    await sleep (200);
    const clip = await page.evaluate ("window.fakeClipboard");
    check (clip.includes ("parameters"), "Copy puts the state on (VS Code's) clipboard");
    await page.evaluate (`window.fakeClipboard = ${JSON.stringify (JSON.stringify ({ parameters: [{ name: "rate", value: 3 }] }))}`);
    await inPanel ("await panel.ctx.actions.pasteState()");
    await sleep (200);
    check (fixture !== "Tremolo" || await inPanel ("return mock.parameterValues.get ('rate') === 3"), "Paste applies a state from the clipboard");

    // console
    await page.key ("/");
    check (await inPanel ("return document.activeElement === panel.consoleView.filterInput && panel.workspace.drawerTab === 'console'"), "/ focuses the console filter");
    await inPanel ("document.activeElement.blur()");

    // audio on/off
    const playing = await inPanel ("return panel.store.get ('playing')");
    await page.key (" ");
    check (await waitInPanel (`panel.store.get ('playing') === ${! playing}`), "Space toggles audio");
    await page.key (" ");

    // light theme from VS Code
    await page.evaluate ("window.setTheme ('light')");
    check (await waitInPanel ("panel.dataset.theme === 'light'"), "a light VS Code theme switches the palette");
    await page.key ("3");
    await sleep (500);
    await shoot (join (shots, `mock-${fixture}-io-light.png`));

    // narrow, like VS Code's side panel
    await page.setSize (520, 860);
    await sleep (400);
    check (await inPanel ("return panel.workspace.element.hasAttribute ('data-narrow') && ! panel.workspace.isInspectorOpen"), "a narrow panel hides the inspector");
    await shoot (join (shots, `mock-${fixture}-narrow.png`));
    await page.setSize (1400, 860);

    // the layout is remembered for the patch
    check (await inPanel ("return panel.settings.patchOrGlobal.get ('mainTab') === 'io' && panel.settings.current !== null"), "the layout is saved per patch");

    // the graph's collapsed nodes and zoom are remembered across a page reload
    const problemsBeforeReload = [...page.problems];
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    check (await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000), "after a page reload, the patch loads again");
    await page.evaluate ("document.querySelector ('iframe').focus(); true");
    await page.key ("2");
    check (await waitInPanel (`panel.graphView.graph.shownVersion === mock.getGraphVersion()`, 5000), "...and shows its graph");
    await sleep (300);
    const restoredState = await inPanel (`return ${graphState}`);
    check (JSON.stringify (restoredState.collapsed) === JSON.stringify (savedState.collapsed) && Math.abs (restoredState.k - savedState.k) < 1e-9,
           `...with the same collapsed graphs and zoom (${JSON.stringify (restoredState)})`);
    await shoot (join (shots, `mock-${fixture}-graph-restored.png`));

    // Fit resets the saved zoom to "fitted"
    await page.key ("f");
    await sleep (500);
    check (await inPanel ("const s = panel.settings.current.get ('graphView'); return s?.transform === null && s?.fitted === true"), "F (fit) saves the graph as fitted again");
    page.problems.unshift (...problemsBeforeReload);

    if (isGraphPatch)
        await testLiveMode();

    await testCommandPalette();
    await testCPUHistory();

    if (await inPanel ("return mock.getInputs().some (e => e.purpose === 'midi in')"))
        await testMIDIControls();
    else
        check (await inPanel ("return ! panel.ctx.commands.getAll().some (c => c.id.startsWith ('midi.')) && ! panel.ctx.commands.run ('midi.panic')"),
               "without a MIDI input, the MIDI commands are disabled");

    if (await inPanel ("return mock.getInputs().some (e => e.purpose === 'audio in')"))
        await testSignalGenerator();
    else
        check (await inPanel ("return ! panel.ctx.commands.getAll().some (c => c.id === 'audio.useTestSignal' || c.id === 'audio.useLiveInput') && ! panel.ctx.commands.run ('audio.useTestSignal')"),
               "without an audio input, the audio input commands are disabled");

    await testTransport();

    if (await inPanel ("return mock.getParameters().length > 0"))
        await testParameterFuzzing();
    else
        check (await inPanel ("return ! panel.ctx.commands.getAll().some (c => c.id.startsWith ('parameters.')) && ! panel.ctx.commands.run ('parameters.randomise')"),
               "without parameters, the fuzzing commands are disabled");

    if (fixtureHasAudio)
        await testSpectrum (isGraphPatch);
    else
        check (await inPanel ("return panel.ctx.commands.search ('spectrum').every (r => ! r.command.id.startsWith ('spectrum:'))"),
               "without audio endpoints, there are no spectrum commands");

    if (await inPanel ("return mock.getOutputs().some (e => e.purpose === 'audio out')"))
        await testRecording();
    else
        check (await inPanel ("return ! panel.ctx.commands.getAll().some (c => c.id === 'recording.toggle') && ! panel.ctx.commands.run ('recording.toggle') && panel.captureButton.element.disabled && panel.captureButton.element.title.startsWith (\"Can't record\")"),
               "without audio outputs, recording is disabled, and the Capture button says why");

    check (page.problems.length === 0, `no console errors or exceptions${page.problems.length ? ":\n        " + page.problems.join ("\n        ") : ""}`);
}
finally
{
    await page.close();
    server.close();
}

console.log (failures === 0 ? "\nAll mock panel checks passed" : `\n${failures} mock panel checks FAILED`);
process.exitCode = failures === 0 ? 0 : 1;

//==============================================================================
// The command palette: Cmd-K (headless Chrome on a Mac reports a Mac platform, so Mod is Cmd),
// typing to search, Enter to run, Escape to close, and jumping to a parameter
async function testCommandPalette()
{
    const metaModifier = 4;
    const type = async (text) => { for (const c of text) await page.key (c); };
    const isMac = await inPanel ("return panel.ctx.commands.isMac");
    const openPalette = () => page.key ("k", "KeyK", { modifiers: isMac ? metaModifier : 2, text: undefined });

    await page.evaluate ("document.querySelector ('iframe').focus(); true");
    await inPanel ("panel.workspace.showMainTab ('gui'); document.activeElement?.blur()");
    await openPalette();
    check (await waitInPanel ("panel.palette.isOpen && document.activeElement === panel.palette.input"),
           "Cmd/Ctrl-K opens the command palette, with the focus in its field");
    check (await inPanel ("return panel.palette.list.children.length > 10 && panel.palette.input.getAttribute ('aria-activedescendant') === 'cp-palette-option-0'"),
           "...listing the commands, the first one active");
    await shoot (join (shots, `mock-${fixture}-palette-dark.png`));

    await inPanel ("panel.ctx.commands.recent = []");   // earlier checks used Fit the graph, whose recency would rank it first
    await type ("graph");
    check (await waitInPanel ("panel.palette.results[0]?.command.id === 'view.graph'"), "typing \"graph\" puts Show the graph first");
    check (await inPanel ("return panel.palette.list.querySelector ('.cp-active mark.cp-match')?.textContent === 'graph'"), "...with the matched letters marked");
    await page.key ("Enter");
    check (await waitInPanel ("! panel.palette.isOpen && panel.workspace.mainTab === 'graph'"), "Enter runs it and closes the palette");

    // Escape gives the focus back
    await inPanel ("panel.topBar.resetButton.focus()");
    await openPalette();
    await waitInPanel ("panel.palette.isOpen");
    await page.key ("ArrowDown");
    check (await inPanel ("return panel.palette.input.getAttribute ('aria-activedescendant') === 'cp-palette-option-1'"), "the arrow keys move the active option");
    await page.key ("Escape");
    check (await waitInPanel ("! panel.palette.isOpen && document.activeElement === panel.topBar.resetButton"), "Escape closes it and gives the focus back");

    // jumping to a parameter focuses its control in the I/O view
    const parameter = await inPanel ("return (panel.store.get ('inputs') ?? []).find (e => e.purpose === 'parameter' && ! e.annotation?.hidden)?.endpointID ?? null");

    if (parameter)
    {
        await openPalette();
        await waitInPanel ("panel.palette.isOpen");
        await type ("@" + parameter);
        check (await waitInPanel (`panel.palette.results[0]?.command.id === 'endpoint:${parameter}'`), `"@${parameter}" finds the parameter`);
        await page.key ("Enter");
        check (await waitInPanel (`panel.workspace.mainTab === 'io' && panel.ioView.findWidget ('${parameter}')?.element.contains (document.activeElement)
                                    && panel.store.get ('selection')?.endpointID === '${parameter}'`),
               "...and Enter shows the I/O view with its control focused");
    }

    // the top bar's button opens it too, and a click outside closes it
    await inPanel ("panel.topBar.paletteButton.click()");
    check (await waitInPanel ("panel.palette.isOpen"), "the top bar's Commands button opens it");
    await page.evaluate ("window.setTheme ('light')");
    await waitInPanel ("panel.dataset.theme === 'light'");
    await sleep (200);
    await shoot (join (shots, `mock-${fixture}-palette-light.png`));
    await inPanel ("panel.workspace.element.dispatchEvent (new PointerEvent ('pointerdown', { bubbles: true, composed: true }))");
    check (await waitInPanel ("! panel.palette.isOpen"), "a click outside closes it");
    await page.evaluate ("window.setTheme ('dark')");
}

//==============================================================================
// Hot reload of a module that the view imports relatively (Tremolo: ui/index.js imports
// ./stompbox/view.js, which imports ./logo.js). After a file change the panel asks for the
// view under /~v<N>/, so every module it imports is a new URL, and is fetched fresh: an edit
// to logo.js runs, rather than the browser's cached copy.
async function testRelativeImportReload()
{
    const logoPath = "/session_Tremolo/ui/stompbox/logo.js";
    const original = await readFile (join (cmajorRoot, "examples/patches/Tremolo/ui/stompbox/logo.js"), "utf8");
    const edit = (marker) => fileOverrides.set (logoPath, original + `\nglobalThis.cmajHotReloadMarker = ${JSON.stringify (marker)};\n`);

    try
    {
        for (const marker of ["edit 1", "edit 2"])
        {
            edit (marker);
            await inPanel ("window.viewBeforeEdit = panel.guiView.view; mock.assetFilesChanged()");
            check (await waitInPanel (`panel.guiView.view && panel.guiView.view !== window.viewBeforeEdit && globalThis.cmajHotReloadMarker === ${JSON.stringify (marker)}`, 4000),
                   `a change to a relatively imported module (${marker}) runs its new code in the re-created GUI`);
        }

        check (await inPanel ("return window.marker === 123"), "...still without reloading the page");
        check (await inPanel ("return performance.getEntriesByType ('resource').some (e => /\\/~v\\d+\\/ui\\/stompbox\\/logo\\.js$/.test (e.name))"),
               "...because it was fetched from a version folder (/~vN/)");
    }
    finally
    {
        fileOverrides.delete (logoPath);
    }

    await inPanel ("window.viewBeforeEdit = panel.guiView.view; mock.assetFilesChanged()");
    await waitInPanel ("panel.guiView.view && panel.guiView.view !== window.viewBeforeEdit", 4000);
}

//==============================================================================
// Phase 4: the graph's live mode (views/graph-live.js)
async function testLiveMode()
{
    const live = "panel.graphView.live";
    const graph = "panel.graphView.graph";

    // it uses only the graph element's overlay API (no reaching into its view or shadow DOM)
    const liveSource = await inPanel ("return await (await fetch ('/panel_api/views/graph-live.js')).text()");
    const overlayAPI = new Set (["append", "setBoundaryWidgets", "getBoundaryWidgetPlacements", "setBoundaryActivity", "highlightBoundaryEndpoint", "clearHighlight"]);
    const graphUses = [...new Set ([...liveSource.matchAll (/this\.graph\.(\w+)/g)].map (m => m[1]))];
    check (graphUses.length > 0 && graphUses.every (u => overlayAPI.has (u)), `graph-live.js only uses the graph's overlay API (${graphUses.join (", ")})`);

    // its widgets are made once per load: status updates and rebuilds of the same patch keep them
    await page.key ("2");
    await waitInPanel (`${live}.items.some (i => i.entry)`, 5000);
    const attachesBefore = await inPanel (`window.firstLiveItem = ${live}.items[0]; return ${live}.attachCount`);
    await inPanel ("mock.sendSessionStatus(); mock.rebuild();");
    await waitInPanel ("mock.isLoaded");
    await sleep (400);
    check (attachesBefore === 1, `the live widgets were made once for this load (attachCount ${attachesBefore})`);
    check (await inPanel (`return ${live}.attachCount === ${attachesBefore} && ${live}.items[0] === window.firstLiveItem`),
           "a status update and a rebuild don't re-create them");

    // how far each placed widget is from where it should be: centred on its port, 10 layout units outside it
    const alignment = `(() => { const ports = new Map ([...${graph}.shadowRoot.querySelectorAll ('.port[data-port-id]')].map (e => [e.dataset.portId, e.getBoundingClientRect()]));
                                 const k = ${graph}.getZoom();
                                 return ${live}.items.filter (i => i.entry).map (i => { const r = i.element.getBoundingClientRect(), p = ports.get (i.entry.portId);
                                     return { id: i.endpoint.endpointID, dy: (r.top + r.height / 2) - (p.top + p.height / 2),
                                              gapError: (i.isInput ? p.left - r.right : r.left - p.right) - 10 * k, left: r.left, top: r.top }; }); })()`;
    const isAligned = (list) => list.length > 0 && list.every (a => Math.abs (a.dy) < 1.5 && Math.abs (a.gapError) < 1.5);

    await page.key ("2");
    await inPanel ("panel.graphView.showDefaultView()");
    await sleep (600);

    // the mode: Live by default, with the legend
    check (await inPanel (`return panel.graphView.mode === "live" && panel.graphView.modeButtons.find (b => b.dataset.mode === "live").getAttribute ("aria-pressed") === "true"
                                  && ! panel.graphView.legend.hidden && panel.graphView.legend.textContent.includes ("Boundary signals only")`),
           "live mode is on by default, and the legend says \"boundary signals only\"");

    const counts = await inPanel (`return { items: ${live}.items.length, placed: ${live}.items.filter (i => i.entry).length,
                                            gutter: ${graph}.view.positioned.boundaryWidgets.length, ids: ${live}.items.map (i => i.endpoint.endpointID) }`);
    check (counts.items > 0 && counts.placed === counts.gutter, `each endpoint with something to show has a compact widget in the gutter (${counts.placed}/${counts.items}: ${counts.ids.join (", ")})`);
    check (await inPanel (`return ${live}.items.every (i => i.widget.size === "compact" && i.element.querySelector (".cp-ew-compact"))`), "...from the widget registry, at the compact size");

    // the widgets line up with their ports, and follow pan and zoom
    const aligned = await inPanel (`return ${alignment}`);
    check (isAligned (aligned), `the widgets line up with their ports (worst: ${Math.max (...aligned.map (a => Math.abs (a.dy))).toFixed (2)}px off-centre, ${Math.max (...aligned.map (a => Math.abs (a.gapError))).toFixed (2)}px off the gap)`);

    await inPanel (`${graph}.zoomBy (1.3); ${graph}.view.panBy (-37, 23);`);
    await sleep (200);
    const moved = await inPanel (`return ${alignment}`);
    check (isAligned (moved) && moved[0].left !== aligned[0].left && moved[0].top !== aligned[0].top, "...and stay lined up when the graph is zoomed and panned");

    // semantic zoom: below the threshold they become dots on their ports
    const dotState = `(() => { const i = ${live}.items.find (i => i.entry), port = ${graph}.shadowRoot.querySelector ('.port[data-port-id="' + i.entry.portId + '"]').getBoundingClientRect();
                               const d = i.dot.getBoundingClientRect();
                               return { widget: getComputedStyle (i.element).display, dot: getComputedStyle (i.dot).display, k: ${graph}.getZoom(),
                                        dotSize: d.width, nearPort: Math.abs ((d.top + d.height / 2) - (port.top + port.height / 2)) < 1.5
                                                                    && (i.isInput ? port.left - d.right : d.left - port.right) > 0
                                                                    && (i.isInput ? port.left - d.right : d.left - port.right) < 8 }; })()`;
    await inPanel (`${graph}.zoomBy (0.6 / ${graph}.getZoom())`);
    await sleep (200);
    const small = await inPanel (`return ${dotState}`);
    check (small.widget === "none" && small.dot === "block" && small.nearPort && Math.abs (small.dotSize - 9) < 0.5,
           `below ${await inPanel ("return (await import ('/panel_api/views/graph-live.js')).liveWidgetMinZoom")}× zoom, each widget is a dot beside its port, the same size on screen (${JSON.stringify (small)})`);
    await inPanel (`${graph}.zoomBy (0.75 / ${graph}.getZoom())`);
    await sleep (200);
    const big = await inPanel (`return ${dotState}`);
    check (big.widget !== "none" && big.dot === "none", `...and above it, the widgets are back (k ${big.k.toFixed (2)})`);

    await inPanel ("panel.graphView.showDefaultView()");
    await sleep (300);

    // the popover: clicking a widget's name opens its full-size widget; clicking a control doesn't
    const nameAt = await inPanel (`const i = ${live}.items.find (i => i.entry && i.isInput) ?? ${live}.items.find (i => i.entry);
                                   const r = i.widget.nameElement.getBoundingClientRect(); return { x: r.left + 8, y: r.top + r.height / 2, id: i.endpoint.endpointID };`);
    await page.click (nameAt.x, nameAt.y);
    await sleep (300);
    const popover = await inPanel (`const p = ${live}.popover;
                                    return { open: p.isOpen, full: ${live}.popoverWidget?.size, id: ${live}.popoverWidget?.endpoint.endpointID,
                                             hasWidget: !! p.element.querySelector (".cp-ew-full"), title: p.element.querySelector (".cp-section-title").textContent,
                                             inPanel: p.element.parentElement === panel, onScreen: (() => { const r = p.element.getBoundingClientRect(); return r.top >= 0 && r.bottom <= innerHeight; })() };`);
    check (popover.open && popover.full === "full" && popover.hasWidget && popover.id === nameAt.id && popover.onScreen,
           `clicking a widget opens a popover with the full widget (${popover.title})`);
    await shoot (join (shots, `mock-${fixture}-live-popover.png`));
    await page.key ("Escape");
    await sleep (100);
    check (await inPanel (`return ! ${live}.popover.isOpen && ${live}.popoverWidget === null`), "...which Escape closes, disposing the full widget");

    const sliderAt = await inPanel (`const s = ${live}.items.find (i => i.entry && i.element.querySelector (".cp-slider"))?.element.querySelector (".cp-slider");
                                     if (! s) return null; const r = s.getBoundingClientRect(); return { x: r.left + r.width * 0.3, y: r.top + r.height / 2 };`);

    if (sliderAt)
    {
        await page.click (sliderAt.x, sliderAt.y);
        await sleep (200);
        check (await inPanel (`return ! ${live}.popover.isOpen`), "clicking a widget's slider moves it, without opening the popover");
    }

    // wire activity: only on the root's own ports and the wires attached to them, scaled by level
    const activity = `(() => { const root = ${graph}.view.model.root;
                               const allowed = new Set ([...root.inputs, ...root.outputs].map (e => e.path));
                               for (const c of root.connections) if (c.from.node === null || c.to.node === null) allowed.add (c.path);
                               const active = [...${graph}.shadowRoot.querySelectorAll ('.active')];
                               return { paths: active.map (e => e.dataset.path), outside: active.map (e => e.dataset.path).filter (p => ! allowed.has (p)),
                                        edges: active.filter (e => e.classList.contains ('edge-group')).map (e => e.dataset.path),
                                        levels: active.map (e => Number (e.style.getPropertyValue ('--activity'))) }; })()`;

    if (fixture === "SineSynth")
        await inPanel (`const id = panel.store.get ('inputs').find (e => e.purpose === 'midi in').endpointID;
                        panel.store.get ('connection').patchConnection.sendMIDIInputEvent (id, 0x903c64);
                        setTimeout (() => panel.store.get ('connection').patchConnection.sendMIDIInputEvent (id, 0x803c00), 400);`);

    await sleep (250);
    const lit = await inPanel (`return ${activity}`);
    check (lit.edges.length > 0 && lit.outside.length === 0 && lit.levels.every (l => l > 0 && l <= 1),
           `the wires attached to live boundary ports show activity, and nothing further in does (${lit.edges.join (", ")})`);
    check (new Set (lit.levels.map (l => l.toFixed (2))).size > 1 || lit.levels.some (l => l < 1), `...scaled by level (${lit.levels.map (l => l.toFixed (2)).join (", ")})`);

    if (fixture === "Tremolo")
    {
        await inPanel ("panel.store.get ('connection').patchConnection.sendEventOrValue ('rate', 5)");
        check (await waitInPanel (`${activity}.edges.includes ("rate -> lfo.rate")`, 2000), "a parameter change pulses the wire it drives");
        check (! (await inPanel (`return ${activity}`)).edges.includes ("bypass -> out"), "...but a value wire into an audio output doesn't take that output's level");
    }

    // hovering a parameter's widget highlights the signal path it drives
    const param = await inPanel (`const i = ${live}.items.find (i => i.entry && i.endpoint.purpose === "parameter") ?? ${live}.items.find (i => i.entry && i.isInput);
                                  const r = i.element.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + 4, portId: i.entry.portId, id: i.endpoint.endpointID };`);
    await page.mouse ("mouseMoved", param.x, param.y);
    await sleep (150);
    const highlight = await inPanel (`const svg = ${graph}.shadowRoot.querySelector ('svg');
                                      const edges = [...svg.querySelectorAll ('.edge-group.highlighted, .edge.highlighted')].map (e => e.dataset.fromPort);
                                      const others = [...svg.querySelectorAll ('.edge:not(.highlighted)')];
                                      return { has: svg.classList.contains ('has-highlight'), fromPort: edges.includes (${JSON.stringify (param.portId)}), count: edges.length,
                                               others: others.length, dimmed: others.every (e => getComputedStyle (e).opacity < 0.5) };`);
    check (highlight.has && highlight.fromPort && highlight.dimmed,
           `hovering the ${param.id} widget highlights the signal path it drives (${highlight.count} wires), dimming the other ${highlight.others}`);
    await shoot (join (shots, `mock-${fixture}-live-hover.png`));
    await page.mouse ("mouseMoved", 600, 300);
    await sleep (100);
    await page.mouse ("mouseMoved", 5, 5);
    await sleep (150);
    check (await inPanel (`return ! ${graph}.shadowRoot.querySelector ('svg').classList.contains ('has-highlight')`), "...and moving away clears it");

    // its subscriptions pause while the tab is hidden, or the page is
    const hubAudioFeeds = "[...panel.store.get ('connection').hub.feeds.values()].filter (f => f.granularity === 400).length";
    await inPanel ("document.activeElement?.blur()");     // (the slider clicked above has the focus, and keys typed into it aren't shortcuts)
    await page.key ("1");
    await sleep (200);
    const paused = await inPanel (`return { active: ${live}.group.isActive, audioFeeds: ${hubAudioFeeds}, io: panel.ioView.isActive, tab: panel.workspace.mainTab,
                                            feeds: [...panel.store.get ('connection').hub.feeds.values()].map (f => f.endpointID + '/' + f.granularity + ' ×' + f.listeners.size) }`);
    check (paused.active === false && paused.audioFeeds === 0, `switching to another tab pauses the live widgets' subscriptions (no audio feeds left: ${JSON.stringify (paused)})`);
    const baseline = await inPanel ("const c = {}; for (const l of mock.listeners.values()) c[l.endpointID + '/' + l.granularity] = (c[l.endpointID + '/' + l.granularity] ?? 0) + 1; return c;");
    await page.key ("2");
    await sleep (200);
    check (await inPanel (`return ${live}.group.isActive === true && ${hubAudioFeeds} > 0`), "...and showing the graph again resumes them");

    await inPanel (`Object.defineProperty (document, "visibilityState", { configurable: true, get: () => "hidden" }); document.dispatchEvent (new Event ("visibilitychange"));`);
    check (await inPanel (`return ${live}.group.isActive === false`), "a hidden page (e.g. a background VS Code tab) pauses them too");
    await inPanel (`delete document.visibilityState; document.dispatchEvent (new Event ("visibilitychange"));`);
    check (await inPanel (`return ${live}.group.isActive === true`), "...until it's visible again");

    // the graph and the I/O view share the hub's server listeners
    await inPanel ("panel.ioView.setActive (true)");
    await sleep (400);
    const both = await inPanel ("const c = {}; for (const l of mock.listeners.values()) c[l.endpointID + '/' + l.granularity] = (c[l.endpointID + '/' + l.granularity] ?? 0) + 1; return c;");
    const extra = Object.entries (both).filter (([key, n]) => n - (baseline[key] ?? 0) > 1);
    check (extra.length === 0 && Object.keys (both).length > 0,
           `with the I/O view live too, there's still one server listener per endpoint feed (${JSON.stringify (both)}, without them: ${JSON.stringify (baseline)})`);
    await inPanel ("panel.ioView.setActive (false)");

    // Structure | Live, remembered per patch
    await inPanel (`panel.graphView.modeButtons.find (b => b.dataset.mode === "structure").click()`);
    await sleep (500);
    const structure = await inPanel (`return { mode: panel.graphView.mode, pref: panel.settings.current.get ("graphMode"), items: ${live}.items.length, group: ${live}.group,
                                               gutter: ${graph}.view.positioned.boundaryWidgets.length, layerHidden: ${live}.element.hidden, legend: panel.graphView.legend.hidden,
                                               active: ${graph}.shadowRoot.querySelectorAll ('.active').length,
                                               pressed: panel.graphView.modeButtons.find (b => b.dataset.mode === "structure").getAttribute ("aria-pressed") }`);
    check (structure.mode === "structure" && structure.pref === "structure" && structure.items === 0 && structure.group === null && structure.gutter === 0
             && structure.layerHidden && structure.legend && structure.active === 0 && structure.pressed === "true",
           `Structure removes the widgets, the gutter, the activity and the legend, and is saved (${JSON.stringify (structure)})`);
    await shoot (join (shots, `mock-${fixture}-structure.png`));

    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    await page.evaluate ("document.querySelector ('iframe').focus(); true");
    await page.key ("2");
    await waitInPanel (`${graph}.shownVersion === mock.getGraphVersion()`, 5000);
    await sleep (300);
    check (await inPanel (`return panel.graphView.mode === "structure" && ${live}.items.length === 0 && ${graph}.view.positioned.boundaryWidgets.length === 0`),
           "...and still Structure after a page reload");

    await inPanel (`panel.graphView.modeButtons.find (b => b.dataset.mode === "live").click()`);
    check (await waitInPanel (`${live}.items.some (i => i.entry) && ${graph}.view.positioned.boundaryWidgets.length > 0 && panel.settings.current.get ("graphMode") === "live"`, 3000),
           "Live brings the widgets back");
    await sleep (600);
    await shoot (join (shots, `mock-${fixture}-live.png`));
}

//==============================================================================
// The CPU history: the sparkline gets the mock's cpu_info (with a peak), the CPU button opens
// the popover (Enter, then Escape), the command opens it, and an old server's level-only report
async function testCPUHistory()
{
    const topBar = "panel.topBar";
    const enter = () => page.key ("Enter", "Enter", { text: "\r", windowsVirtualKeyCode: 13 });

    await inPanel ("panel.workspace.showMainTab ('gui')");
    check (await waitInPanel (`! ${topBar}.cpuButton.hidden && ${topBar}.cpuHistory.length >= 3`, 8000), "the CPU history fills from the cpu_info reports");
    check (await inPanel (`const h = ${topBar}.cpuHistory; let withPeak = 0; h.forEach ((t, l, p) => { if (p !== l) ++withPeak; }); return withPeak > 0`),
           "...which carry a peak");

    // the sparkline has drawn something besides its background
    await sleep (300);
    const sparkline = await inPanel (`const c = ${topBar}.cpuSparkline.canvas; const d = c.getContext ('2d').getImageData (0, 0, c.width, c.height).data;
                                      const colours = new Set(); for (let i = 0; i < d.length; i += 4) colours.add (d[i] + ',' + d[i + 1] + ',' + d[i + 2]);
                                      return { w: c.width, h: c.height, colours: colours.size, visible: c.getBoundingClientRect().width > 0 }`);
    check (sparkline.visible && sparkline.w > 40 && sparkline.colours > 2, `the sparkline draws the history (${JSON.stringify (sparkline)})`);

    // an older server's report (no peak): the level stands in for it
    await inPanel ("mock.sendCPUInfo (0.37, null)");
    check (await waitInPanel (`Math.abs (${topBar}.cpuHistory.latest?.peak - 0.37) < 1e-6 && ${topBar}.cpuHistory.latest.level === ${topBar}.cpuHistory.latest.peak`),
           "a cpu_info without a peak uses the level for it");
    check (await inPanel (`return ${topBar}.cpuButton.getAttribute ('aria-label').includes ('37%') && ${topBar}.cpuButton.getAttribute ('aria-haspopup') === 'dialog'`),
           "the CPU button's label gives the current load");

    // keyboard: Enter opens the popover, Escape closes it and gives the focus back
    await inPanel ("mock.sendCPUInfo (0.2, 1.3)");
    await inPanel (`${topBar}.cpuButton.focus()`);
    await enter();
    check (await waitInPanel (`${topBar}.cpuPopover.isOpen && ${topBar}.cpuButton.getAttribute ('aria-expanded') === 'true'`), "Enter on the CPU button opens the CPU history");
    await sleep (200);
    const stats = await inPanel (`const v = ${topBar}.cpuHistoryView; return { current: v.stats.current.value.textContent, average: v.stats.average.value.textContent,
                                          peak: v.stats.peak.value.textContent, over: v.stats.overBudget.value.textContent,
                                          overMarked: v.stats.overBudget.element.classList.contains ('cp-over'), budget: v.budgetText.textContent,
                                          chart: v.chart.canvas.width, empty: v.emptyLabel.hidden }`);
    // (the mock keeps reporting, so the current load may have moved on, and a later spike may be higher)
    check (stats.current.endsWith ("%") && stats.average.endsWith ("%") && parseFloat (stats.peak) >= 130 && Number (stats.over) >= 1 && stats.overMarked && stats.empty,
           `...with the current, average and peak load and the over-budget count (${JSON.stringify (stats)})`);
    check (/ms \(\d+ frames at [\d.]+ kHz\)/.test (stats.budget), `...and the time budget per block (${stats.budget})`);
    check (stats.chart > 200, "...and the 60 s chart");
    await shoot (join (shots, `mock-${fixture}-cpu-dark.png`));

    await page.key ("Escape");
    check (await waitInPanel (`! ${topBar}.cpuPopover.isOpen && document.activeElement === ${topBar}.cpuButton`), "Escape closes it and gives the focus back");

    // the command opens it
    check (await inPanel ("return panel.ctx.commands.run ('cpu.history')"), "the Show CPU history command runs");
    check (await waitInPanel (`${topBar}.cpuPopover.isOpen`), "...and opens the popover");
    check (await inPanel ("return panel.ctx.commands.search ('cpu hist')[0]?.command.id === 'cpu.history'"), "the palette finds it");
    await page.evaluate ("window.setTheme ('light')");
    await waitInPanel ("panel.dataset.theme === 'light'");
    await sleep (300);
    await shoot (join (shots, `mock-${fixture}-cpu-light.png`));
    await inPanel ("panel.workspace.element.dispatchEvent (new PointerEvent ('pointerdown', { bubbles: true, composed: true }))");
    check (await waitInPanel (`! ${topBar}.cpuPopover.isOpen`), "a click outside closes it");
    await page.evaluate ("window.setTheme ('dark')");

    // stopping the audio hides the button and breaks the history
    await inPanel ("panel.ctx.actions.toggleAudio()");
    check (await waitInPanel (`${topBar}.cpuButton.hidden && Number.isNaN (${topBar}.cpuHistory.get (${topBar}.cpuHistory.length - 1).level)`),
           "stopping the audio hides the CPU button and marks a gap");
    await inPanel ("panel.ctx.actions.toggleAudio()");
    await waitInPanel (`! ${topBar}.cpuButton.hidden`);
}

//==============================================================================
// The MIDI input's controls: the channel and velocity of the keyboard's notes (with real clicks
// on the keys), the octave, bend (spring-back), mod, sustain, panic, the commands, the keyboard
// access, and the settings surviving a page reload
async function testMIDIControls()
{
    const widget = "panel.ioView.widgets.find (w => w.controls)";
    const controls = `${widget}.controls`;
    const midiID = await inPanel (`panel.workspace.showMainTab ('io'); return ${widget}?.endpoint.endpointID ?? null`);
    check (!! midiID, "the MIDI input's widget has the controls");

    if (! midiID)
        return;

    const sent = `mock.getSentMIDI (${JSON.stringify (midiID)})`;
    const lastSent = (status) => inPanel (`return ${sent}.filter (m => (m >> 20) === ${status}).at (-1) ?? null`);
    const clearSent = () => inPanel ("mock.messageLog.length = 0");
    await inPanel (`${widget}.element.scrollIntoView ({ block: "start" })`);
    await sleep (200);

    // where to click on a natural key: near its left edge (clear of the black key), at a fraction of its height
    const keyPoint = (note, fraction) => inPanel (`const r = ${widget}.keyboard.shadowRoot.getElementById ("note${note}").getBoundingClientRect();
                                                   return { x: r.left + 4, y: r.top + 3 + (r.height - 6) * ${fraction} };`);
    const clickKey = async (note, fraction = 0.8) => { const p = await keyPoint (note, fraction); await page.click (p.x, p.y); await sleep (100); };
    const rootNote = () => inPanel (`return Number (${widget}.keyboard.getAttribute ("root-note"))`);

    // channel: a real click on a key sends its note on the chosen channel
    await clearSent();
    await clickKey (48);
    const onChannel1 = await lastSent (9);
    check (onChannel1 !== null && (onChannel1 >> 16) === 0x90 && ((onChannel1 >> 8) & 0x7f) === 48 && (onChannel1 & 0x7f) === 100,
           `clicking a key sends a note-on on channel 1 at velocity 100 (${onChannel1?.toString (16)})`);
    check ((await lastSent (8) ?? 0) >> 16 === 0x80, "...and a note-off when it's released");

    await inPanel (`const s = ${controls}.channelSelect; s.value = "3"; s.dispatchEvent (new Event ("change"));`);
    await clickKey (48);
    const onChannel3 = await lastSent (9);
    check (onChannel3 !== null && (onChannel3 >> 16) === 0x92, `after choosing channel 3, the status byte is 0x92 (${onChannel3?.toString (16)})`);
    check (((await lastSent (8)) >> 16) === 0x82, "...for the note-off too");

    // velocity by position: soft at the top of a key, loud at the bottom
    await inPanel (`${controls}.velocityButtons.find (b => b.dataset.mode === "position").click()`);
    check (await inPanel (`return ${controls}.velocityInput.disabled`), "By position disables the fixed velocity field");
    await clickKey (48, 0.02);
    const topVelocity = (await lastSent (9)) & 0x7f;
    await clickKey (48, 0.98);
    const bottomVelocity = (await lastSent (9)) & 0x7f;
    check (topVelocity < 20 && bottomVelocity > 110, `velocity by position: the top of a key is soft, the bottom loud (${topVelocity}, ${bottomVelocity})`);

    // octave
    const root = await rootNote();
    await inPanel (`${controls}.octaveUp.click()`);
    check (await rootNote() === root + 12 && await inPanel (`return ${controls}.octaveText.textContent === "+1"`), `octave + shifts root-note by 12 (${root} → ${await rootNote()})`);
    await clickKey (root + 12 + 12);
    check (((await lastSent (9)) >> 8 & 0x7f) === root + 24, "...and the keys play the shifted notes");

    // bend: a drag sends 14-bit bends and springs back to the centre on release
    await clearSent();
    const strip = await inPanel (`const r = ${controls}.bendStrip.element.getBoundingClientRect(); return { x: r.left, y: r.top + r.height / 2, w: r.width };`);
    await page.drag (strip.x + strip.w / 2, strip.y, strip.x + strip.w - 1, strip.y, 6);
    check (await waitInPanel (`${sent}.filter (m => (m >> 20) === 0xe).at (-1) === 0xe20040 && ${controls}.bendAmount === 0`, 3000), "the bend springs back to the centre (8192) on release");
    const bends = await inPanel (`return ${sent}.filter (m => (m >> 20) === 0xe).map (m => ((m >> 8) & 0x7f) | ((m & 0x7f) << 7))`);
    check (Math.max (...bends) > 15000 && bends.length > 4 && bends.every (b => b >= 8192), `...after bending up (${bends.length} messages, max ${Math.max (...bends)})`);

    // keyboard access: arrows bend it, and releasing them returns it
    await clearSent();
    await inPanel (`${controls}.bendStrip.element.focus()`);
    await page.key ("ArrowLeft");
    check (await waitInPanel (`${sent}.filter (m => (m >> 20) === 0xe).length >= 2 && ${sent}.filter (m => (m >> 20) === 0xe).at (-1) === 0xe20040`, 3000)
           && await inPanel (`return ${sent}.some (m => (m >> 20) === 0xe && (m & 0x7f) < 64)`), "Left arrow on the bend strip bends down, and it returns on release");
    check (await inPanel (`const e = ${controls}.bendStrip.element; return e.getAttribute ("role") === "slider" && e.getAttribute ("aria-valuenow") === "0"`), "the strip is a slider at 0");

    await inPanel (`${controls}.modStrip.element.focus()`);
    await page.key ("End");
    await page.key ("ArrowLeft");
    check (await lastSent (0xb) === 0xb2017e && await inPanel (`return ${controls}.modStrip.element.getAttribute ("aria-valuenow") === "126"`), "End then Left on the mod strip: CC1 126, and it stays");
    check (await inPanel ("return panel.workspace.mainTab === 'io'"), "...without the keys reaching the panel's shortcuts");

    await inPanel (`${controls}.sustainSwitch.focus()`);
    await page.key ("Enter", "Enter", { text: "\r", windowsVirtualKeyCode: 13 });
    check (await waitInPanel (`${controls}.sustainSwitch.getAttribute ("aria-checked") === "true"`) && await lastSent (0xb) === 0xb2407f, "Enter on the sustain switch sends CC64 127");

    // panic, with a key held down
    const p = await keyPoint (root + 12 + 14, 0.8);
    await page.mouse ("mouseMoved", p.x, p.y);
    await page.mouse ("mousePressed", p.x, p.y, { buttons: 1 });
    await sleep (100);
    await clearSent();
    await inPanel (`${controls}.panicButton.click()`);
    await page.mouse ("mouseReleased", p.x, p.y);
    await sleep (100);
    const panic = await inPanel (`return ${sent}`);
    const noteOffs = panic.filter (m => (m >> 20) === 8);
    const ccs = panic.filter (m => (m >> 20) === 0xb);
    check (noteOffs.length === 1 && noteOffs[0] === (0x820000 | ((root + 26) << 8)), `panic sends a note-off for the held note (${noteOffs.map (m => m.toString (16))})`);
    check (ccs.length === 48 && new Set (ccs.map (m => (m >> 16) & 15)).size === 16 && [64, 123, 120].every (cc => ccs.filter (m => ((m >> 8) & 0x7f) === cc).length === 16),
           `...then CC64, CC123 and CC120 on all 16 channels (${ccs.length} CCs)`);
    check (await inPanel (`return ${controls}.sustainSwitch.getAttribute ("aria-checked") === "false"`), "...and releases the sustain switch");

    // the commands
    check (await inPanel ("return panel.ctx.commands.search ('panic')[0]?.command.id === 'midi.panic'"), "the palette finds MIDI panic");
    await clearSent();
    check (await inPanel ("return panel.ctx.commands.run ('midi.panic')") && await inPanel (`return ${sent}.length`) === 48, "the MIDI panic command sends the CCs");
    await inPanel ("panel.ctx.commands.run ('midi.octaveUp')");
    check (await rootNote() === root + 24, "the octave-up command shifts the keyboard");
    await inPanel ("panel.ctx.commands.run ('midi.octaveDown')");

    await page.evaluate ("window.setTheme ('light')");
    await sleep (300);
    await shoot (join (shots, `mock-${fixture}-midi-light.png`));
    await page.evaluate ("window.setTheme ('dark')");

    // the settings are remembered per patch
    const problemsBeforeReload = [...page.problems];
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    await inPanel ("panel.workspace.showMainTab ('io')");
    check (await waitInPanel (`!! ${widget}`), "after a reload, the MIDI widget is back");
    const restored = await inPanel (`const c = ${controls}; return { channel: c.channelSelect.value, mode: c.velocityButtons.find (b => b.classList.contains ("cp-on"))?.dataset.mode,
                                                                    octave: c.octaveText.textContent, root: c.keyboard.getAttribute ("root-note"), mod: c.modValue }`);
    check (restored.channel === "3" && restored.mode === "position" && restored.octave === "+1" && restored.root === String (root + 12) && restored.mod === 0,
           `...with its channel, velocity mode and octave, but not the mod (${JSON.stringify (restored)})`);
    page.problems.unshift (...problemsBeforeReload);
    await sleep (500);
    await shoot (join (shots, `mock-${fixture}-midi-dark.png`));

    // put them back, for the next run
    await inPanel (`${controls}.setValues ({ velocityMode: "fixed", channel: 1, octave: 0 })`);
}

//==============================================================================
// The audio input's Signal mode: choosing it, the type's fields, throttled single-field sends from
// a real drag and from the keys, another client's change, Live, the commands, the settings
// surviving a reload, and an older server that knows nothing of generators
async function testSignalGenerator()
{
    const widget = "panel.ioView.widgets.find (w => w.generatorForm)";
    const form = `${widget}.generatorForm`;
    const id = await inPanel (`panel.workspace.showMainTab ('io'); return ${widget}?.endpoint.endpointID ?? null`);
    check (!! id, "the audio input's widget has a Signal mode");

    if (! id)
        return;

    const q = JSON.stringify (id);
    const sets = `mock.messageLog.filter (m => m.type === 'set_custom_audio_input' && m.generator)`;
    const clearLog = () => inPanel ("mock.messageLog.length = 0");
    const shownFields = `[...${form}.rows].filter (([, r]) => ! r.row.hidden).map (([f]) => f).join()`;
    const mockSettings = `mock.getGeneratorSettings (${q})`;
    const centreOf = (expr) => inPanel (`const r = (${expr}).getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, left: r.left, w: r.width };`);
    const chooseType = (type) => inPanel (`const s = ${form}.typeSelect; s.value = "${type}"; s.dispatchEvent (new Event ("change"));`);

    check (await inPanel (`return mock.messageLog.some (m => m.type === 'req_audio_input_generator' && m.endpoint === ${q})`), "the widget asks the server for the generator settings");

    // (start from nothing saved, as on a first run)
    await inPanel (`panel.settings.patch.set ('audioInputGenerators', {}); ${widget}.reloadSavedSettings(); ${widget}.element.scrollIntoView ({ block: 'start' })`);
    await sleep (200);

    // choosing Signal, with a real click
    await clearLog();
    const signalButton = await centreOf (`${widget}.generatorButton`);
    await page.click (signalButton.x, signalButton.y);
    check (await waitInPanel (`mock.audioInputModes.get (${q}) === 'generator' && ${widget}.mode === 'generator' && ! ${form}.element.hidden
                                && ${widget}.generatorButton.getAttribute ('aria-pressed') === 'true'`), "clicking Signal makes the input play a generator, and shows the form");
    check (await inPanel (`const s = ${sets}; return s.length === 1 && Object.keys (s[0].generator).length === 7 && s[0].generator.type === 'sine'`),
           "...by sending the generator's settings");
    check (await waitInPanel (`Math.abs (parseFloat (${widget}.valueElement.textContent) + 12) < 0.3`, 5000),
           `the input's meter shows the sine's -12 dBFS (${await inPanel (`return ${widget}.valueElement.textContent`)})`);
    check (await inPanel (`return ${shownFields}`) === "level,frequency", "a sine shows the level and the frequency");

    // the type's fields
    const expectedFields = { sweep: "level,startFrequency,endFrequency,sweepSeconds", impulse: "level,impulseRate", white: "level", pink: "level", silence: "" };

    for (const [type, fields] of Object.entries (expectedFields))
    {
        await clearLog();
        await chooseType (type);
        const shown = await inPanel (`return ${shownFields}`);
        check (shown === fields && await waitInPanel (`${mockSettings}.type === '${type}'`)
               && await inPanel (`return JSON.stringify (${sets}.map (m => m.generator)) === '[{"type":"${type}"}]'`),
               `${type}: shows ${fields || "no fields"}, and sends only the type`);
    }

    // (the readout is the held peak: 1.5 s of hold, then a fall of 80 dB at peakReleaseDBPerSecond, about 6.5 s in all)
    check (await waitInPanel (`${widget}.valueElement.textContent.includes ('∞')`, 10000), "silence: the meter goes to -∞");
    await chooseType ("sine");
    await sleep (300);

    // a real drag on the frequency slider: only the frequency, and fewer messages than input events
    await clearLog();
    await inPanel (`window.sliderInputs = 0; ${form}.rows.get ('frequency').slider.addEventListener ('input', () => ++window.sliderInputs)`);
    const slider = await centreOf (`${form}.rows.get ('frequency').slider`);
    await page.drag (slider.left + 4, slider.y, slider.left + slider.w - 4, slider.y, 30);
    await sleep (300);
    const drag = await inPanel (`const s = ${sets}; return { inputs: window.sliderInputs, sent: s.length, onlyFrequency: s.every (m => Object.keys (m.generator).join() === 'frequency'),
                                                             last: s.at (-1)?.generator.frequency, form: ${form}.settings.frequency, server: ${mockSettings}.frequency,
                                                             readout: ${form}.rows.get ('frequency').input.value }`);
    check (drag.sent >= 2 && drag.onlyFrequency, `dragging the frequency slider sends only the frequency (${JSON.stringify (drag)})`);
    check (drag.sent < drag.inputs, `...in fewer messages than input events (${drag.sent} for ${drag.inputs})`);
    check (drag.last === drag.form && drag.server === drag.form && drag.form > 10000, "...and the last value arrives");

    // the keys on the level slider
    await clearLog();
    await inPanel (`${form}.rows.get ('level').slider.focus()`);

    for (let i = 0; i < 6; ++i)
        await page.key ("ArrowLeft");

    await sleep (300);
    const keys = await inPanel (`const s = ${sets}; return { sent: s.length, onlyLevel: s.every (m => Object.keys (m.generator).join() === 'level'),
                                                             form: ${form}.settings.level, server: ${mockSettings}.level, tab: panel.workspace.mainTab }`);
    check (keys.sent >= 1 && keys.sent <= 6 && keys.onlyLevel && keys.form === -15 && keys.server === -15, `the arrow keys step the level, sending only the level (${JSON.stringify (keys)})`);
    check (keys.tab === "io", "...without the keys reaching the panel's shortcuts");
    check (await inPanel (`const s = ${form}.rows.get ('level').slider; return s.getAttribute ('aria-valuetext') === '-15.0 dBFS' && !! s.getAttribute ('aria-labelledby')`),
           "the slider is labelled, with its value as text");

    // typing into a readout
    await clearLog();
    await inPanel (`const i = ${form}.rows.get ('frequency').input; i.focus(); i.value = '1k'; i.dispatchEvent (new KeyboardEvent ('keydown', { key: 'Enter', bubbles: true }));`);
    check (await waitInPanel (`${mockSettings}.frequency === 1000`) && await inPanel (`return JSON.stringify (${sets}.map (m => m.generator)) === '[{"frequency":1000}]'`),
           "typing 1k and Enter in the readout sends 1000 Hz");
    await inPanel ("document.activeElement.blur()");

    // another client's change: the form follows, and sends nothing
    await clearLog();
    await inPanel (`mock.setGeneratorFromAnotherClient (${q}, { type: 'impulse', impulseRate: 10, level: -20 })`);
    check (await waitInPanel (`${form}.settings.type === 'impulse' && ${form}.typeSelect.value === 'impulse' && ${form}.rows.get ('impulseRate').input.value === '10 Hz'
                                && ${form}.rows.get ('level').input.value === '-20.0 dBFS'`), "another client's change shows in the form");
    await sleep (300);
    check (await inPanel (`return ${sets}.length === 0`), "...without being sent back");

    await shoot (join (shots, `mock-${fixture}-signal.png`));

    // Live goes back
    const liveButton = await centreOf (`${widget}.buttons.find (b => b.dataset.mode === 'live')`);
    await page.click (liveButton.x, liveButton.y);
    check (await waitInPanel (`mock.audioInputModes.get (${q}) === 'live' && ${widget}.mode === 'live' && ${form}.element.hidden`), "Live goes back to the live input, and hides the form");

    // the commands
    check (await inPanel ("return panel.ctx.commands.search ('test signal')[0]?.command.id === 'audio.useTestSignal'"), "the palette finds the test signal command");
    check (await inPanel ("return panel.ctx.commands.search ('live audio input')[0]?.command.id === 'audio.useLiveInput'"), "...and the live input one");
    await clearLog();
    check (await inPanel ("return panel.ctx.commands.run ('audio.useTestSignal')")
           && await waitInPanel (`mock.audioInputModes.get (${q}) === 'generator' && ${mockSettings}.type === 'impulse'`), "the test signal command plays the last signal");
    check (await inPanel ("return panel.ctx.commands.run ('audio.useLiveInput')") && await waitInPanel (`mock.audioInputModes.get (${q}) === 'live'`), "the live input command goes back");

    // the settings are remembered per patch (the mock server starts afresh, with the defaults)
    const problemsBeforeReload = [...page.problems];
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    await inPanel ("panel.workspace.showMainTab ('io')");
    check (await waitInPanel (`!! ${widget}`), "after a reload, the audio input widget is back");
    await sleep (300);
    check (await inPanel (`return ${form}.settings.type === 'impulse' && ${form}.settings.impulseRate === 10 && ${form}.settings.level === -20`),
           `...with the saved settings in its form (${await inPanel (`return JSON.stringify (${form}.settings)`)})`);
    await inPanel (`${widget}.generatorButton.click()`);
    check (await waitInPanel (`mock.audioInputModes.get (${q}) === 'generator' && ${mockSettings}.type === 'impulse' && ${mockSettings}.impulseRate === 10`),
           "...which choosing Signal sends to the (restarted) server");

    // an older server: no answer to req_audio_input_generator, and a generator request goes live
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark&generators=off`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    await inPanel ("panel.workspace.showMainTab ('io')");
    check (await waitInPanel (`!! ${widget} && ${widget}.generatorButton.disabled`, 6000), "with an older server, Signal is disabled after a while");
    check (await inPanel (`return ${widget}.generatorNote.hidden && ${widget}.generatorButton.title.includes ('too old')`), "...with a tooltip saying why, and no note");
    check (await inPanel ("return ! panel.ctx.commands.getAll().some (c => c.id === 'audio.useTestSignal') && panel.ctx.commands.getAll().some (c => c.id === 'audio.useLiveInput')"),
           "...and the test signal command is disabled");
    await inPanel (`${widget}.buttons.find (b => b.dataset.mode === 'mute').click()`);
    check (await waitInPanel (`${widget}.mode === 'mute'`), "the other sources still work");
    await shoot (join (shots, `mock-${fixture}-signal-old-server.png`));

    // put things back, for the next run
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    await inPanel ("panel.settings.patch.set ('audioInputGenerators', {})");
    page.problems.unshift (...problemsBeforeReload);
}

//==============================================================================
// The transport: shown for a patch with timeline inputs (or by the setting), the buttons, keys and
// commands sending partial set_transports and showing the broadcast, the tempo field (keys, typing,
// a drag), the position animating only while playing, the info-rate request following the
// subscription, and an older server that never answers
async function testTransport()
{
    const t = "panel.topBar.transport";
    const timelinePurposes = JSON.stringify (["time signature", "tempo", "transport state", "timeline position"]);
    const hasTimeline = await inPanel (`return mock.getInputs().some (e => ${timelinePurposes}.includes (e.purpose))`);
    const sets = "mock.messageLog.filter (m => m.type === 'set_transport').map (({ type, ...m }) => JSON.stringify (m)).join (' ')";
    const clearLog = () => inPanel ("mock.messageLog.length = 0");
    const centreOf = (expr) => inPanel (`const r = (${expr}).getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 };`);
    const clickOn = async (expr) => { const c = await centreOf (expr); await page.click (c.x, c.y); };
    const enter = () => page.key ("Enter", "Enter", { text: "\r", windowsVirtualKeyCode: 13 });
    const focusPanel = async () => { await page.evaluate ("document.querySelector ('iframe').focus(); true"); await inPanel ("document.activeElement?.blur()"); };

    await inPanel ("panel.workspace.showMainTab ('io')");
    await sleep (200);
    check (await inPanel (`return ${t}.element.hidden === ${! hasTimeline}`),
           hasTimeline ? "the transport shows for a patch with timeline inputs" : "the transport is hidden for a patch without timeline inputs");

    if (! hasTimeline)
    {
        check (await inPanel ("return ! panel.ctx.commands.run ('transport.playStop') && ! panel.ctx.commands.getAll().some (c => c.category === 'Transport')"),
               "...and its commands are disabled");
        check (await inPanel ("return ! mock.transportInfoRates.has ('self')"), "...and no position updates are asked for");

        // the setting, through the settings popover's switch
        await inPanel ("panel.topBar.showSettings()");
        await sleep (100);
        await clickOn ("panel.settingsView.transportSwitch");
        check (await waitInPanel (`! ${t}.element.hidden && panel.settingsView.transportSwitch.getAttribute ('aria-checked') === 'true'
                                    && panel.settings.global.get ('alwaysShowTransport') === true`), "the \"Always show the transport\" switch shows it");
        await page.key ("Escape");
    }

    check (await waitInPanel ("mock.transportInfoRates.get ('self') === 50"), "while it's shown, the session asks for position updates every 50 ms");
    check (await waitInPanel (`${t}.isSupported === true && ${t}.state?.sampleRate > 0`), "...and the server's state arrives");
    check (await inPanel (`return ${t}.tempoInput.value === '120' && ${t}.numeratorSelect.value === '4' && ${t}.denominatorSelect.value === '4'
                                && ${t}.playButton.getAttribute ('aria-pressed') === 'true'`), "it shows the defaults: 120 BPM, 4/4, playing");

    // playing: the position moves, through the scheduler
    await inPanel ("mock.transportFrame = 44100 * 4.1");
    await sleep (300);
    const p1 = await inPanel (`return { text: ${t}.positionText.textContent, frames: ${t}.numAnimationFrames }`);
    await sleep (300);
    const p2 = await inPanel (`return { text: ${t}.positionText.textContent, frames: ${t}.numAnimationFrames, animating: ${t}.isAnimating }`);
    check (p1.text !== p2.text && p2.frames > p1.frames + 5 && p2.animating && /^\d+\.\d\.\d{3}$/.test (p2.text),
           `while playing, the position advances smoothly (${p1.text} → ${p2.text}, ${p2.frames - p1.frames} frames)`);
    check (/^3\.[1-4]\./.test (p2.text), `...in bars.beats.ticks (about 8.5 quarter notes in: ${p2.text})`);
    await shoot (join (shots, `mock-${fixture}-transport.png`));

    // stop, with a real click
    await clearLog();
    await clickOn (`${t}.playButton`);
    check (await waitInPanel (`mock.transport.playing === false && ${t}.playButton.getAttribute ('aria-pressed') === 'false'`) && await inPanel (`return ${sets}`) === '{"playing":false}',
           "clicking play/stop sends { playing: false }, and shows the broadcast");
    await sleep (200);
    const s1 = await inPanel (`return { text: ${t}.positionText.textContent, frames: ${t}.numAnimationFrames, sent: mock.transportStatesSent, frame: mock.transportFrame }`);
    await sleep (400);
    const s2 = await inPanel (`return { text: ${t}.positionText.textContent, frames: ${t}.numAnimationFrames, sent: mock.transportStatesSent, frame: mock.transportFrame,
                                       animating: ${t}.isAnimating, pending: panel.ctx.scheduler.pending.has (${t}.drawFrame) }`);
    check (s1.text === s2.text && s1.frame === s2.frame, `while stopped, the position stays put (${s2.text})`);
    check (s2.frames === s1.frames && ! s2.animating && ! s2.pending, "...and the scheduler isn't asked for frames");
    check (s2.sent === s1.sent, "...and the server stops sending states (after the one for the stop)");

    // P plays again
    await focusPanel();
    await clearLog();
    await page.key ("p");
    check (await waitInPanel (`mock.transport.playing === true && ${t}.isAnimating`) && await inPanel (`return ${sets}`) === '{"playing":true}', "P plays the transport again");

    // record doesn't stop play; loop
    await clearLog();
    await clickOn (`${t}.recordButton`);
    check (await waitInPanel (`mock.transport.recording && mock.transport.playing && ${t}.recordButton.getAttribute ('aria-pressed') === 'true' && ${t}.playButton.classList.contains ('cp-on')`)
           && await inPanel (`return ${sets}`) === '{"recording":true}', "record sends { recording: true }, and the transport keeps playing");
    check (await inPanel (`return getComputedStyle (${t}.recordButton).color !== getComputedStyle (${t}.loopButton).color`), "record's on state has its own colour");
    await clearLog();
    await clickOn (`${t}.loopButton`);
    check (await waitInPanel (`mock.transport.looping && ${t}.loopButton.getAttribute ('aria-pressed') === 'true'`) && await inPanel (`return ${sets}`) === '{"looping":true}',
           "loop sends { looping: true }");
    await shoot (join (shots, `mock-${fixture}-transport-record.png`));
    await clickOn (`${t}.recordButton`);
    await clickOn (`${t}.loopButton`);
    check (await waitInPanel ("! mock.transport.recording && ! mock.transport.looping && mock.transport.playing"), "...and both turn off again");

    // Home rewinds
    await focusPanel();
    await clearLog();
    await page.key ("Home");
    check (await waitInPanel ("mock.transportFrame < 44100 * 0.5") && await inPanel (`return ${sets}`) === '{"rewind":true}', "Home rewinds the transport");
    check (await waitInPanel (`${t}.positionText.textContent.startsWith ('1.')`), "...and the position shows bar 1");

    // the tempo: typing, the arrow keys, a drag
    await clearLog();
    await inPanel (`${t}.tempoInput.focus(); ${t}.tempoInput.select()`);
    for (const c of "300") await page.key (c);
    await enter();
    check (await waitInPanel ("mock.transport.tempo === 300") && await inPanel (`return ${sets}`) === '{"tempo":300}', "typing 300 and Enter sets the tempo");
    await clearLog();
    await page.key ("ArrowUp", "ArrowUp", { modifiers: 8 });
    await page.key ("ArrowDown", "ArrowDown");
    check (await waitInPanel (`mock.transport.tempo === 309 && ${t}.tempoInput.value === '309'`) && await inPanel (`return ${sets}`) === '{"tempo":310} {"tempo":309}',
           "Shift-↑ adds 10, ↓ takes 1");
    await inPanel (`${t}.tempoInput.select()`);
    for (const c of "5000") await page.key (c);
    await enter();
    check (await waitInPanel ("mock.transport.tempo === 999") && (await inPanel (`return ${sets}`)).endsWith ('{"tempo":999}'), "a typed 5000 is sent clamped, as 999");
    check (await inPanel ("return panel.workspace.mainTab === 'io'"), "...and typing digits doesn't trigger the panel's shortcuts");
    await inPanel (`${t}.tempoInput.select()`);
    for (const c of "120") await page.key (c);
    await enter();
    await waitInPanel ("mock.transport.tempo === 120");
    await inPanel ("document.activeElement.blur()");

    await clearLog();
    const tempo = await centreOf (`${t}.tempoInput`);
    await page.drag (tempo.x, tempo.y, tempo.x, tempo.y - 30, 10);
    await sleep (200);
    const dragged = await inPanel (`const s = mock.messageLog.filter (m => m.type === 'set_transport'); return { n: s.length, onlyTempo: s.every (m => Object.keys (m).join() === 'type,tempo'),
                                         tempo: mock.transport.tempo, focused: document.activeElement === ${t}.tempoInput }`);
    check (dragged.n >= 2 && dragged.onlyTempo && dragged.tempo === 130 && ! dragged.focused, `dragging the tempo up 30 px adds 10 BPM, sending only the tempo (${JSON.stringify (dragged)})`);
    await inPanel (`${t}.setTempo (120)`);

    // the time signature
    await clearLog();
    await inPanel (`const s = ${t}.numeratorSelect; s.value = '3'; s.dispatchEvent (new Event ('change'))`);
    check (await waitInPanel ("mock.transport.numerator === 3") && await inPanel (`return ${sets}`) === '{"numerator":3}', "the numerator select sends { numerator: 3 }");
    await inPanel (`const s = ${t}.denominatorSelect; s.value = '8'; s.dispatchEvent (new Event ('change'))`);
    check (await waitInPanel (`mock.transport.denominator === 8 && ${t}.denominatorSelect.value === '8'`), "the denominator select sends 8");
    await inPanel (`${t}.setNumerator (4); ${t}.setDenominator (4)`);
    await waitInPanel ("mock.transport.numerator === 4 && mock.transport.denominator === 4");

    // another client's change shows, and isn't sent back
    await clearLog();
    await inPanel ("mock.setTransportFromAnotherClient ({ tempo: 140.5 })");
    check (await waitInPanel (`${t}.tempoInput.value === '140.5'`) && await inPanel (`return ${sets}`) === '', "another client's tempo change shows, and isn't sent back");
    await inPanel (`${t}.setTempo (120)`);
    check (await waitInPanel (`mock.transport.tempo === 120 && ${t}.tempoInput.getAttribute ('aria-valuenow') === '120' && ${t}.tempoInput.value === '120'`),
           "setting the tempo back to 120 shows");

    // the time readout, remembered
    await clickOn (`${t}.positionButton`);
    check (await waitInPanel (`/^\\d\\d:\\d\\d\\.\\d{3}$/.test (${t}.positionText.textContent) && panel.settings.global.get ('transportTimeFormat') === 'time'`),
           `clicking the position shows the time (${await inPanel (`return ${t}.positionText.textContent`)}), and remembers the choice`);
    await clickOn (`${t}.positionButton`);
    check (await waitInPanel (`${t}.timeFormat === 'bars'`), "...and clicking again goes back to bars");

    // keyboard access: every control is focusable and named; Enter and Space work on the buttons
    const access = await inPanel (`const c = ${t}; return [...c.controls, c.positionButton].map (e => ({ tag: e.tagName, ok: e.tabIndex >= 0 && ! e.disabled
                                         && !! (e.getAttribute ('aria-label') || e.textContent.trim()) })).filter (e => ! e.ok).length`);
    const roles = await inPanel (`return { group: ${t}.element.getAttribute ('role'), tempo: ${t}.tempoInput.getAttribute ('role'), now: ${t}.tempoInput.getAttribute ('aria-valuenow') }`);
    check (access === 0 && roles.group === 'group' && roles.tempo === 'spinbutton' && roles.now === '120',
           `every control is focusable and labelled (the tempo is a spinbutton: ${access} unlabelled, ${JSON.stringify (roles)})`);
    await clearLog();
    const audioWasOn = await inPanel ("return panel.store.get ('playing')");
    await inPanel (`${t}.loopButton.focus()`);
    await page.key (" ", "Space");
    check (await waitInPanel ("mock.transport.looping === true") && await inPanel (`return panel.store.get ('playing') === ${audioWasOn}`),
           "Space on the focused loop button toggles it (not the audio)");
    await enter();
    check (await waitInPanel ("mock.transport.looping === false"), "...and so does Enter");

    // the commands, and the palette
    check (await inPanel ("return panel.ctx.commands.search ('play transport')[0]?.command.id === 'transport.playStop'"), "the palette finds Play/stop the transport");
    check (await inPanel ("return panel.ctx.commands.search ('rewind')[0]?.command.id === 'transport.rewind' && panel.ctx.commands.formatShortcut ('Home') === 'Home'"),
           "...and Rewind the transport (shown with Home)");
    check (await inPanel ("return panel.ctx.commands.search ('toggle rec')[0]?.command.id === 'transport.record'"), "...and Toggle recording");
    await inPanel ("panel.ctx.actions.showCommandPalette ('transport')");
    await sleep (200);
    const paletteRows = await inPanel ("return [...panel.querySelectorAll ('.cp-palette-option')].map (o => o.textContent).filter (t => t.includes ('Transport')).length");
    check (paletteRows >= 4, `...which lists the four transport commands (${paletteRows})`);
    await shoot (join (shots, `mock-${fixture}-transport-palette.png`));
    await page.key ("Escape");
    check (await inPanel ("return panel.ctx.commands.run ('transport.loop')") && await waitInPanel ("mock.transport.looping === true"), "the Toggle loop command runs");
    check (await inPanel ("return panel.ctx.commands.run ('transport.record')") && await waitInPanel ("mock.transport.recording === true && mock.transport.playing"), "...and Toggle recording");
    await inPanel ("panel.ctx.commands.run ('transport.loop'); panel.ctx.commands.run ('transport.record')");
    check (await waitInPanel ("! mock.transport.looping && ! mock.transport.recording"), "...and both turn off again");

    // unloading the patch withdraws the request; loading it again asks again
    await clearLog();
    await inPanel ("panel.unloadPatch()");
    check (await waitInPanel (`${t}.element.hidden && ! mock.transportInfoRates.has ('self')`)
           && await inPanel ("return mock.messageLog.filter (m => m.type === 'set_transport_info_rate').map (m => m.intervalMs).join() === '0'"),
           "unloading the patch hides the transport and sends intervalMs: 0");
    await inPanel ("panel.loadPatch (mock.patchFile)");
    check (await waitInPanel (`! ${t}.element.hidden && mock.transportInfoRates.get ('self') === 50`, 5000), "loading it again shows it, and asks again");

    // an older server: no answer, so after 2 s it's disabled
    const problemsBeforeReload = [...page.problems];
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark&transport=off`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    check (await waitInPanel (`${t}.isSupported === false && ${t}.playButton.disabled && ${t}.tempoInput.disabled`, 5000), "with an older server, the transport is disabled after a while");
    check (await inPanel (`return ${t}.element.title.includes ('too old') && ${t}.playButton.title.includes ('too old') && ! ${t}.element.hidden`), "...with a tooltip saying why");
    check (await inPanel ("return ! panel.ctx.commands.getAll().some (c => c.category === 'Transport') && ! panel.ctx.commands.run ('transport.playStop')"), "...and the commands are disabled");
    check (await inPanel (`return ! ${t}.isAnimating`), "...and nothing animates");
    await shoot (join (shots, `mock-${fixture}-transport-old-server.png`));

    // disposing the panel withdraws the request
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    check (await waitInPanel ("mock.transportInfoRates.get ('self') === 50"), "after a reload, it asks for updates again");
    await inPanel ("mock.messageLog.length = 0; panel.dispose()");
    await sleep (100);
    check (await inPanel ("return ! mock.transportInfoRates.has ('self') && mock.messageLog.some (m => m.type === 'set_transport_info_rate' && m.intervalMs === 0)"),
           "disposing the panel sends intervalMs: 0");

    // put things back, for the next run
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000);
    await inPanel ("panel.ctx.actions.setAlwaysShowTransport (false); panel.settings.global.set ('transportTimeFormat', undefined)");
    page.problems.unshift (...problemsBeforeReload);
}

//==============================================================================
// Parameter fuzzing (feature 3): Randomise (a real click) with balanced gestures, an exclusion kept
// across a reload, a sine LFO started from the automation popover by keyboard and sent at ~30 Hz with
// ramp frames, a real slider drag stopping it (the fuzzer's gesture ends first), Restore, the commands
// and the palette, and no gesture left open after a rebuild that removes a parameter or an unload
async function testParameterFuzzing()
{
    const fuzzer = "panel.store.get ('connection').fuzzer";
    const enter = () => page.key ("Enter", "Enter", { text: "\r", windowsVirtualKeyCode: 13 });
    const tab = () => page.key ("Tab", "Tab", { windowsVirtualKeyCode: 9 });
    const rectOf = (js) => inPanel (`const e = ${js}; e.scrollIntoView ({ block: "nearest" }); const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, left: r.left, w: r.width };`);
    const clickOn = async (js) => { const r = await rectOf (js); await page.click (r.x, r.y); await sleep (150); };
    const mockValues = () => inPanel ("return Object.fromEntries (mock.getParameters().map (p => [p.endpointID, mock.getParameterValue (p)]))");
    const gestureCheck = () => inPanel ("return { open: mock.getOpenGestures(), unmatched: mock.unmatchedGestureEnds.length }");
    const noOpenGestures = async () => { const g = await gestureCheck(); return Object.keys (g.open).length === 0 && g.unmatched === 0; };

    // (a floating GUI window left open would cover the inspector)
    await inPanel (`panel.ctx.actions.setFloatingGUIOpen (false); panel.workspace.showMainTab ('io'); panel.workspace.setInspectorOpen (true); panel.settings.patch.set ('fuzzExclusions', []);`);
    await sleep (300);
    const ids = await inPanel ("return panel.ioView.widgets.filter (w => w.model?.kind === 'slider').map (w => w.endpoint.endpointID).slice (0, 2)");
    check (ids.length === 2, `two slider parameters to fuzz (${ids})`);

    if (ids.length < 2)
        return;

    const [id, other] = ids.map (i => JSON.stringify (i));
    const numParameters = await inPanel ("return mock.getParameters().length");
    const original = await mockValues();
    check (await waitInPanel (`${fuzzer}.values.size === ${numParameters}`), "the fuzzer follows every parameter's value");

    // Randomise: a real click on the I/O view's button
    await inPanel ("mock.messageLog.length = 0");
    await clickOn ("panel.ioView.fuzzToolbar.randomiseButton");
    check (await waitInPanel (`mock.messageLog.filter (m => m.type === "send_value").length === ${numParameters}`), `Randomise sends a value to each of the ${numParameters} parameters`);
    await sleep (200);
    const log = await inPanel ("return mock.messageLog.filter (m => m.type.startsWith ('send_')).map (m => [m.type, m.id])");
    const bracketed = log.length === 3 * numParameters && log.every ((m, i) => m[0] === ["send_gesture_start", "send_value", "send_gesture_end"][i % 3] && m[1] === log[i - i % 3][1]);
    check (bracketed && await noOpenGestures(), "...each as gesture start, value, end, leaving none open");
    const randomised = await mockValues();
    const changed = Object.keys (original).filter (k => randomised[k] !== original[k]).length;
    check (changed >= Math.ceil (numParameters / 2), `...and the values change (${changed} of ${numParameters})`);
    check (await inPanel ("return ! panel.ioView.fuzzToolbar.snapshotChip.hidden && ! panel.ioView.fuzzToolbar.restoreButton.disabled"), "the snapshot shows, and Restore is enabled");

    // the automation popover: open it from the inspector row, start a sine LFO by keyboard
    await inPanel (`panel.inspectorView.revealParameter (${id})`);
    await clickOn (`panel.inspectorView.parameterRows.get (${id}).automateButton`);
    check (await inPanel ("return panel.inspectorView.automationPopover.isOpen && document.activeElement?.dataset.mode === 'off'"), "the row's button opens the popover, focused on Off");
    await tab();
    await tab();
    check (await inPanel ("return document.activeElement?.dataset.mode === 'sine'"), "Tab moves through the modes");
    await enter();
    check (await waitInPanel (`${fuzzer}.getAutomation (${id})?.mode === "sine"`), "Enter on Sine starts a sine LFO");
    check (await inPanel (`return panel.inspectorView.parameterRows.get (${id}).row.classList.contains ("cp-automated") && panel.ioView.findWidget (${id}).element.classList.contains ("cp-automated")`),
           "the inspector row and the I/O widget show that it's automated");
    check (await inPanel ("return panel.ioView.fuzzToolbar.countText.textContent === '1 automated'"), "the toolbar counts it");
    await inPanel ("mock.messageLog.length = 0");
    await sleep (2000);
    const stream = await inPanel (`return mock.messageLog.filter (m => m.type === "send_value" && m.id === ${id})`);
    check (stream.length >= 40 && stream.length <= 62, `the mock receives a stream of values at ≤ ~30 Hz (${stream.length} in 2 s)`);
    check (stream.every (m => m.rampFrames === 1470), `...each with rampFrames 1470 (44.1 kHz / 30) (${[...new Set (stream.map (m => m.rampFrames))]})`);
    check (JSON.stringify ((await gestureCheck()).open) === JSON.stringify ({ [JSON.parse (id)]: 1 }), "...inside one open gesture");

    // keyboard: an exclusion, from the popover's switch (Escape closes it, giving the focus back)
    await page.key ("Escape");
    check (await inPanel (`return ! panel.inspectorView.automationPopover.isOpen && document.activeElement === panel.inspectorView.parameterRows.get (${id}).automateButton`),
           "Escape closes the popover and focuses its button again");
    await inPanel (`panel.inspectorView.revealParameter (${other})`);
    await clickOn (`panel.inspectorView.parameterRows.get (${other}).automateButton`);
    await inPanel ("panel.inspectorView.automationEditor.excludeSwitch.focus()");
    await enter();
    check (await waitInPanel (`${fuzzer}.isExcluded (${other}) && panel.settings.patch.get ('fuzzExclusions', []).includes (${other})`), "Enter on the exclude switch excludes it, in the patch settings");
    await page.key ("Escape");

    // grabbing the slider: a real drag stops the automation, its gesture ending before the user's
    await inPanel ("mock.messageLog.length = 0");
    const slider = await rectOf (`panel.ioView.findWidget (${id}).control`);
    await page.drag (slider.left + slider.w * 0.3, slider.y, slider.left + slider.w * 0.7, slider.y, 6);
    await sleep (300);
    const grab = await inPanel (`return mock.messageLog.filter (m => m.id === ${id} && m.type.startsWith ("send_")).map (m => m.type === "send_value" ? (m.rampFrames ? "fuzz" : "user") : m.type.slice (13))`);
    const firstStart = grab.indexOf ("start");
    check (firstStart > 0 && grab.lastIndexOf ("end", firstStart) === firstStart - 1 && grab.slice (firstStart).every (x => x !== "fuzz") && grab.at (-1) === "end",
           `a real drag: the fuzzer's gesture ends, then the user's starts, with no fuzzer values after (${[...new Set (grab)].join()}, ${grab.length})`);
    check (await inPanel (`return ! ${fuzzer}.isAutomating (${id}) && ! panel.ioView.findWidget (${id}).element.classList.contains ("cp-automated")`) && await noOpenGestures(),
           "...the automation stops, and no gesture is left open");

    // keyboard on the toolbar: Randomise skips the exclusion; Restore puts every original value back
    await inPanel ("mock.messageLog.length = 0; panel.ioView.fuzzToolbar.randomiseButton.focus()");
    await enter();
    check (await waitInPanel (`mock.messageLog.filter (m => m.type === "send_value").length === ${numParameters - 1}`) &&
           await inPanel (`return ! mock.messageLog.some (m => m.type === "send_value" && m.id === ${other})`), "Enter on Randomise randomises all but the excluded one");
    await inPanel ("panel.ioView.fuzzToolbar.restoreButton.focus()");
    await enter();
    await sleep (300);
    const restored = await mockValues();
    const wrong = Object.keys (original).filter (k => restored[k] !== original[k]);
    check (wrong.length === 0, `Enter on Restore returns every original value (${wrong.length ? "wrong: " + wrong : "all " + numParameters})`);
    check (await inPanel ("return panel.ioView.fuzzToolbar.restoreButton.disabled && panel.ioView.fuzzToolbar.snapshotChip.hidden") && await noOpenGestures(), "...and the snapshot is gone");

    // the commands and the palette
    check (await inPanel ("return ! panel.ctx.commands.run ('parameters.restore') && ! panel.ctx.commands.run ('parameters.stopAutomation')"), "Restore and Stop all are disabled with nothing to do");
    await inPanel ("mock.messageLog.length = 0; panel.ctx.commands.recent = []; panel.ctx.actions.showCommandPalette ('randomise all')");
    await sleep (200);
    check (await inPanel ("return panel.ctx.commands.search ('randomise all')[0]?.command.id === 'parameters.randomise'"), "the palette finds Randomise all parameters");
    await enter();
    check (await waitInPanel (`mock.messageLog.filter (m => m.type === "send_value").length === ${numParameters - 1}`), "...and Enter runs it");
    await inPanel (`${fuzzer}.automate (${id}, { mode: "triangle" })`);
    const stopped = await inPanel ("return panel.ctx.commands.run ('parameters.stopAutomation')") && await inPanel (`return ${fuzzer}.numAutomations === 0`);
    await sleep (100);
    check (stopped && await noOpenGestures(), "the Stop all command stops it");
    check (await inPanel ("return panel.ctx.commands.run ('parameters.restore')") && await inPanel (`return ! ${fuzzer}.hasSnapshot`), "the Restore command restores");

    // screenshots: the I/O view with an automation running, and the popover open on a row
    await inPanel (`${fuzzer}.automate (${id}, { mode: "sine", rate: 0.5, depth: 0.6 }); panel.ioView.element.scrollTop = 0; panel.ioView.fuzzToolbar.element.scrollIntoView ({ block: "start" })`);
    await sleep (800);
    await shoot (join (shots, `mock-${fixture}-fuzz-io-dark.png`));
    await inPanel (`panel.inspectorView.revealParameter (${id}); panel.inspectorView.showAutomation (${id})`);
    await sleep (300);
    await shoot (join (shots, `mock-${fixture}-fuzz-popover-dark.png`));
    await page.evaluate ("window.setTheme ('light')");
    await sleep (400);
    await shoot (join (shots, `mock-${fixture}-fuzz-popover-light.png`));
    await inPanel ("panel.inspectorView.automationPopover.close()");
    await sleep (200);
    await shoot (join (shots, `mock-${fixture}-fuzz-io-light.png`));
    await page.evaluate ("window.setTheme ('dark')");

    // a rebuild that removes an automated parameter ends its gesture; one that keeps it keeps it running
    await inPanel (`${fuzzer}.automate (${other}, { mode: "ramp" })`);
    await inPanel ("mock.rebuild()");
    await sleep (500);
    check (await inPanel (`return ${fuzzer}.isAutomating (${id}) && ${fuzzer}.isAutomating (${other}) && Object.keys (mock.getOpenGestures()).length === 2`), "a rebuild keeps both automations, and their two gestures");
    // (it stays an input, as a plain value rather than a parameter: Tremolo's own GUI looks every one of them up)
    await inPanel (`window.savedInputs = mock.fixture.status.details.inputs; mock.fixture.status.details.inputs = window.savedInputs.map (e => e.endpointID === ${other} ? { ...e, purpose: '' } : e); mock.rebuild()`);
    check (await waitInPanel (`! ${fuzzer}.isAutomating (${other}) && ${fuzzer}.isAutomating (${id})`), "a rebuild without one parameter stops its automation");
    await sleep (200);
    check (JSON.stringify (await inPanel ("return mock.getOpenGestures()")) === JSON.stringify ({ [JSON.parse (id)]: 1 }), "...ending its gesture, and only its");
    await inPanel ("mock.fixture.status.details.inputs = window.savedInputs; mock.rebuild()");
    await sleep (400);

    // unloading the patch ends the rest
    await inPanel ("panel.unloadPatch()");
    await sleep (300);
    check (await noOpenGestures(), "unloading the patch leaves no gesture open");

    // the exclusion is kept across a reload
    const problemsBeforeReload = [...page.problems];
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    check (await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000), "after a reload, the patch loads again");
    await sleep (300);
    check (await inPanel (`return ${fuzzer}.isExcluded (${other})`), "...and the exclusion is still there");
    await inPanel (`mock.messageLog.length = 0; ${fuzzer}.randomiseAll()`);
    await sleep (200);
    check (await inPanel (`return mock.messageLog.some (m => m.type === "send_value") && ! mock.messageLog.some (m => m.type === "send_value" && m.id === ${other})`), "...so Randomise still skips it");
    await inPanel (`${fuzzer}.restore(); panel.settings.patch.set ('fuzzExclusions', [])`);
    page.problems.unshift (...problemsBeforeReload);
}

//==============================================================================
// The spectrum analyser (widgets/spectrum-analyser.js): the I/O widget's toggle, the trace against the
// mock's known signal, the readout (pointer and keyboard), idling, releasing and sharing the server
// listener (with the live graph's popover), the command, and the settings surviving a reload
async function testSpectrum (isGraphPatch)
{
    const io = "panel.ioView";
    await inPanel ("panel.workspace.showMainTab ('io')");
    check (await waitInPanel (`${io}.widgets.some (w => w.spectrumToggle)`), "the I/O view's audio widgets have a Spectrum toggle");
    check (await inPanel (`return ${io}.widgets.filter (w => (w.endpoint.purpose ?? '').startsWith ('audio')).every (w => w.spectrumToggle?.button.getAttribute ('aria-pressed') === 'false'
                                   && w.headerElement.contains (w.spectrumToggle.button) && ! w.spectrumToggle.isShown)`),
           "...in every audio widget's header, off (aria-pressed false)");

    const info = await inPanel (`const w = ${io}.widgets.find (w => w.endpoint.purpose === 'audio out') ?? ${io}.widgets.find (w => w.spectrumToggle);
                                 return { id: w.endpoint.endpointID, isOutput: w.endpoint.purpose === 'audio out' }`);
    const id = JSON.stringify (info.id);
    const view = `${io}.findSpectrum (${id})`;
    const fullListeners = `mock.getListenerCount (${id}, { fullAudioData: true })`;
    // (the mock's signals: see mock-server-session.js; an input is live here, so it's the "live" sine)
    const expected = info.isOutput ? { frequency: 1000, db: -12, note: "B5", text: "1.00 kHz" } : { frequency: 440, db: -20, note: "A4", text: "440 Hz" };
    await inPanel (`panel.settings.patch.set ('spectrumEndpoints', []); panel.settings.patch.set ('liveSpectrumEndpoints', [])`);

    // a real click on the toggle
    const toggleAt = await inPanel (`const b = ${io}.findWidget (${id}).spectrumToggle.button; b.scrollIntoView ({ block: 'center' }); const r = b.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }`);
    await page.click (toggleAt.x, toggleAt.y);
    check (await waitInPanel (`!! ${view} && ${io}.findWidget (${id}).spectrumToggle.button.getAttribute ('aria-pressed') === 'true'`),
           `clicking the toggle shows ${info.id}'s spectrum`);
    check (await inPanel (`const w = ${io}.findWidget (${id}); return w.display.element.nextSibling === ${view}.element`), "...under its meter");
    check (await waitInPanel (`${fullListeners} === 1`), "...with one full-audio server listener");

    // the trace peaks at the mock's frequency and level (within a bin and about 1 dB)
    check (await waitInPanel (`${view}.analyser.isFull && ${view}.numChunks >= 10`, 5000), "the analyser fills from the full-audio stream");
    await sleep (600);
    const peak = await inPanel (`const v = ${view}, a = v.analyser; let k = 1; for (let i = 1; i < a.binCount; ++i) if (a.levelsDB[i] > a.levelsDB[k]) k = i;
                                 return { bin: k, frequency: a.getBinFrequency (k), db: a.levelsDB[k], binWidth: a.sampleRate / a.fftSize, sampleRate: a.sampleRate,
                                          peak: v.peak, drawn: Math.max (...v.levelColumns), aria: v.canvas.getAttribute ('aria-label') }`);
    check (Math.abs (peak.frequency - expected.frequency) <= peak.binWidth && Math.abs (peak.db - expected.db) < 1.5
             && Math.abs (peak.peak.frequency - expected.frequency) < peak.binWidth && Math.abs (peak.peak.db - expected.db) < 1,
           `the spectrum peaks at ${expected.frequency} Hz, ${expected.db} dB (bin ${peak.bin}: ${peak.frequency.toFixed (1)} Hz, ${peak.db.toFixed (2)} dB; interpolated ${peak.peak.frequency.toFixed (1)} Hz, ${peak.peak.db.toFixed (2)} dB, at ${peak.sampleRate} Hz)`);
    check (Math.abs (peak.drawn - expected.db) < 2, `...and so does the drawn trace (${peak.drawn.toFixed (2)} dB)`);
    const summary = /^Spectrum of .+: peak −(\d+) dB at (.+)$/.exec (peak.aria ?? "");
    check (summary && Math.abs (Number (summary[1]) - Math.abs (expected.db)) <= 1 && summary[2] === expected.text, `the canvas's aria-label sums it up ("${peak.aria}")`);
    check (await inPanel (`return ${view}.canvas.getAttribute ('role') === 'img' && ${view}.canvas.tabIndex === 0 && ${view}.toolbar.getAttribute ('role') === 'toolbar'`),
           "an img role, focusable, with a toolbar of options");

    // the hover readout
    const canvasPoint = `(f) => { const v = ${view}, r = v.canvas.getBoundingClientRect(); return { x: r.left + v.xOf (f) * r.width / v.canvas.width, y: r.top + r.height / 2 }; }`;
    const at = await inPanel (`${view}.canvas.scrollIntoView ({ block: 'center' }); return (${canvasPoint}) (${expected.frequency})`);
    await page.mouse ("mouseMoved", at.x, at.y);
    await sleep (150);
    const hover = await inPanel (`return { f: ${view}.pointerFrequency, text: ${view}.readout.textContent, dim: ${view}.readout.classList.contains ('cp-dim') }`);
    check (hover.f && Math.abs (hover.f / expected.frequency - 1) < 0.02 && hover.text.endsWith (` · ${expected.note}`) && / · −\d+\.\d dB · /.test (hover.text) && ! hover.dim,
           `hovering shows the frequency, level and note ("${hover.text}")`);
    await page.mouse ("mouseMoved", 4, 4);
    await sleep (400);
    const away = await inPanel (`return { f: ${view}.pointerFrequency, text: ${view}.readout.textContent, dim: ${view}.readout.classList.contains ('cp-dim') }`);
    check (away.f === null && away.dim && away.text.startsWith ("Peak "), `...and leaving shows the peak ("${away.text}")`);

    // the keyboard cursor
    await inPanel (`${view}.canvas.focus()`);
    await page.key ("ArrowRight");
    await sleep (100);
    const k1 = await inPanel (`return { f: ${view}.cursorFrequency, peak: ${view}.peak.frequency, live: ${view}.readout.getAttribute ('aria-live'), text: ${view}.readout.textContent }`);
    check (k1.f && Math.abs (k1.f - k1.peak) < 11 && k1.live === "polite" && k1.text.endsWith (expected.note), `→ puts the cursor on the peak, announced ("${k1.text}")`);
    await page.key ("ArrowRight");
    await page.key ("ArrowRight");
    await sleep (100);
    const k2 = await inPanel (`return { f: ${view}.cursorFrequency, tab: panel.workspace.mainTab }`);
    check (Math.abs (k2.f / k1.f - Math.pow (2, 2 / 12)) < 1e-6 && k2.tab === "io", "→ moves it a semitone at a time (and no panel shortcut sees the keys)");
    await page.key ("End");
    check (await inPanel (`return ${view}.cursorFrequency === ${view}.maxFrequency`), "End goes to Nyquist");
    await page.key ("Escape");
    check (await inPanel (`return ${view}.cursorFrequency === null && document.activeElement === ${view}.canvas`), "Escape hides the cursor");

    // the controls
    await inPanel (`${view}.averagingButtons[2].click(); ${view}.peakButton.click()`);
    check (await inPanel (`const v = ${view}; return v.analyser.averagingTime === 1 && v.analyser.peakDecayDBPerSecond === Infinity && v.averagingButtons[2].getAttribute ('aria-pressed') === 'true'
                                  && v.peakButton.getAttribute ('aria-pressed') === 'false'`), "the averaging and peak-hold buttons work");
    await inPanel (`${view}.setOption ('averaging', 'fast'); ${view}.setOption ('peakHold', true)`);

    // it idles when no data comes (the audio stopped: the mock renders nothing, so sends nothing)
    await inPanel ("mock.setPlaying (false)");
    await sleep (3800);
    const idle1 = await inPanel (`return { animating: ${view}.isAnimating, draws: ${view}.drawCount, chunks: ${view}.numChunks }`);
    await sleep (600);
    const idle2 = await inPanel (`return { animating: ${view}.isAnimating, draws: ${view}.drawCount, chunks: ${view}.numChunks }`);
    check (! idle1.animating && idle2.draws === idle1.draws && idle2.chunks === idle1.chunks, `with no new data it stops drawing (${idle1.draws} → ${idle2.draws} draws)`);
    await inPanel ("mock.setPlaying (true)");
    check (await waitInPanel (`${view}.numChunks > ${idle2.chunks} && ${view}.isAnimating`), "...and starts again when data comes back");

    // hiding the I/O view releases the listener; showing it gets it back
    await inPanel ("panel.workspace.showMainTab ('gui')");
    check (await waitInPanel (`${fullListeners} === 0`), "hiding the I/O view releases the full-audio listener");
    await inPanel ("panel.workspace.showMainTab ('io')");
    check (await waitInPanel (`${fullListeners} === 1`), "...and showing it again takes it back");

    // the live graph's popover: its own toggle, sharing the one listener with the I/O view
    if (isGraphPatch && await inPanel (`return panel.graphView.live.items.some (i => i.endpoint.endpointID === ${id})`))
    {
        const live = "panel.graphView.live";
        await inPanel ("panel.workspace.showMainTab ('graph')");
        await waitInPanel (`${live}.items.some (i => i.entry && i.endpoint.endpointID === ${id})`, 5000);
        await inPanel ("panel.graphView.showDefaultView()");
        await sleep (400);
        await inPanel (`${live}.togglePopover (${live}.items.find (i => i.endpoint.endpointID === ${id}))`);
        check (await waitInPanel (`${live}.popover.isOpen && !! ${live}.popoverWidget?.spectrumToggle`), "the live graph's popover has a Spectrum toggle");
        await inPanel (`${live}.popoverWidget.spectrumToggle.button.click()`);
        await sleep (300);
        check (await inPanel (`return !! ${live}.popoverWidget.spectrumToggle.view && ${live}.popover.element.classList.contains ('cp-live-popover-spectrum')
                                       && ${live}.popover.element.getBoundingClientRect().width > 400`), "...which shows the analyser, in a wider popover");
        // (the two can't both be on screen, so make the I/O view live too, as if it were)
        await inPanel (`${io}.setActive (true)`);
        await sleep (300);
        check (await inPanel (`return ${fullListeners} === 1 && !! ${view} && !! ${live}.popoverWidget.spectrumToggle.view`),
               "the I/O view's and the popover's analysers share one server listener");
        check (await waitInPanel (`${live}.popoverWidget.spectrumToggle.view.numChunks > 4 && ${view}.numChunks > 4`), "...and both get the data");
        await inPanel (`${io}.setActive (false)`);
        await sleep (900);
        await shoot (join (shots, `mock-${fixture}-spectrum-popover.png`));
        await page.key ("Escape");
        check (await waitInPanel (`! ${live}.popover.isOpen && ${fullListeners} === 0`), "closing the popover releases it");
        check (await inPanel (`return JSON.stringify (panel.settings.patch.get ('liveSpectrumEndpoints')) === JSON.stringify ([${id}])`), "...and the popover remembers it was open");
        await inPanel ("panel.workspace.showMainTab ('io')");
    }

    // toggling it off releases the listener (the meter's min/max one stays)
    await inPanel (`${io}.setSpectrumShown (${id}, false)`);
    check (await waitInPanel (`! ${view} && ${fullListeners} === 0 && mock.getListenerCount (${id}, { fullAudioData: false }) >= 1`), "turning it off releases the full-audio listener");

    // the command
    check (await inPanel (`return panel.ctx.commands.search ('spectrum').some (r => r.command.id === 'spectrum:' + ${id} && r.command.category === 'Audio')`), "the palette offers \"Show spectrum for …\"");
    await inPanel ("panel.workspace.showMainTab ('gui')");
    check (await inPanel (`return panel.ctx.commands.run ('spectrum:' + ${id})`), "...which runs");
    check (await waitInPanel (`panel.workspace.mainTab === 'io' && !! ${view} && document.activeElement === ${view}.canvas`), "...showing the I/O tab and the analyser, focused");
    await sleep (800);
    await shoot (join (shots, `mock-${fixture}-spectrum-io.png`));

    // the settings survive a reload
    await inPanel (`${view}.setOption ('fftSize', 8192)`);
    const problemsBeforeReload = [...page.problems];
    await page.navigate (`${server.url}/vscode.html?fixture=${fixture}&theme=dark`);
    check (await waitInPanel ("mock?.isLoaded && !! panel.store.get ('connection')", 10000), "after a reload, the patch loads again");
    await inPanel ("panel.workspace.showMainTab ('io')");
    check (await waitInPanel (`!! ${view} && ${io}.findWidget (${id}).spectrumToggle.button.getAttribute ('aria-pressed') === 'true' && ${view}.analyser.fftSize === 8192`),
           "...and the spectrum is open again, with its FFT size");
    page.problems.unshift (...problemsBeforeReload);

    await inPanel (`${view}.setOption ('fftSize', 4096); ${io}.setSpectrumShown (${id}, false); panel.settings.patch.set ('liveSpectrumEndpoints', [])`);
}

//==============================================================================
// Recording: capture from the top bar / C, a dropout, a marker, the samples, the viewer's zoom, pan
// and selection, exporting a selection as 24-bit WAV through the fake VS Code saveFile, cancel,
// the memory limit, hidden views, a rebuild and an unload during a capture, discarding
async function testRecording()
{
    const rec = "panel.recordingController";
    const view = "panel.recordingsView";
    const wave = `${view}.waveform`;
    const id = await inPanel ("return mock.getOutputs().find (e => e.purpose === 'audio out').endpointID");
    const idJS = JSON.stringify (id);
    const latest = "panel.recordings.recordings[panel.recordings.recordings.length - 1]";
    const focusPanel = async () => { await page.evaluate ("document.querySelector ('iframe').focus(); true"); await inPanel ("document.activeElement?.blur()"); };

    await inPanel (`panel.recordings.discardAll(); mock.setPlaying (true); panel.workspace.showMainTab ('io'); panel.workspace.showDrawerTab ('console')`);
    check (await inPanel (`const c = panel.ctx.commands; return c.search ('record').some (r => r.command.id === 'recording.toggle' && r.command.category === 'Recording' && r.command.shortcut === 'C')
                                  && c.search ('recordings').some (r => r.command.id === 'recording.show') && ! c.getAll().some (x => x.id === 'recording.marker')`),
           "the palette offers Start or stop recording (C) and Show the recordings; no marker command until recording");
    check (await inPanel ("const b = panel.captureButton.element; return ! b.disabled && ! b.hidden && b.getAttribute ('aria-pressed') === 'false' && panel.topBar.element.contains (b)"),
           "the top bar has an enabled Capture button");

    // C starts a capture; then every view that shows audio is hidden
    await focusPanel();
    await page.key ("c");
    check (await waitInPanel (`${rec}.isRecording && panel.captureButton.element.classList.contains ('cp-capturing')`), "C starts a capture, shown on the button");
    const startTime = Date.now();
    check (await inPanel (`return JSON.stringify (${latest}.endpointIDs) === JSON.stringify (mock.getOutputs().filter (e => e.purpose === 'audio out').map (e => e.endpointID))`),
           "it records the patch's audio outputs");
    await inPanel ("panel.workspace.showMainTab ('gui'); panel.workspace.showDrawerTab ('console')");
    await sleep (400);
    check (await inPanel (`return mock.getListenerCount (${idJS}, { fullAudioData: true }) === 1`), "with the I/O view and the Recordings tab hidden, the capture keeps its server listener");

    await sleep (1100);
    const droppedBefore = await inPanel ("return mock.fullAudioChunksDropped");
    await inPanel (`mock.dropFullAudioChunks (2, ${idJS})`);
    check (await waitInPanel (`mock.fullAudioChunksDropped === ${droppedBefore + 2}`), "the mock drops two chunks");
    await focusPanel();
    await page.key ("m");
    await sleep (500);
    check (await inPanel (`return /^0:0\\d\\.\\d$/.test (panel.captureButton.time.textContent) && / \\/ 256 MB$/.test (panel.captureButton.memory.textContent)`),
           "the button shows the elapsed time and the memory");
    await page.key ("c");
    const elapsed = (Date.now() - startTime) / 1000;
    check (await waitInPanel (`! ${rec}.isRecording`), "C stops it");

    const info = await inPanel (`const r = ${latest}; return { seconds: r.durationSeconds, reason: r.stopReason?.code, dropouts: r.dropouts, markers: r.markers.filter (m => m.kind === 'user'),
                                                                 rate: r.sampleRate, aligned: ! r.isUnaligned }`);
    check (info.seconds > elapsed * 0.7 && info.seconds < elapsed * 1.15 && info.reason === "user", `about the right length (${info.seconds.toFixed (2)} s, ${elapsed.toFixed (2)} s elapsed), stopped by the user`);
    check (info.dropouts.length === 1 && info.dropouts[0].numFrames === 2048 && info.dropouts[0].endpointID === id && info.aligned,
           `one dropout of 2 × 1024 frames, and no others from hiding the views (${JSON.stringify (info.dropouts)})`);
    check (info.markers.length === 1 && info.markers[0].isApproximate && info.markers[0].frame > info.dropouts[0].startFrame, "M added one (approximate) marker, after the dropout");

    // the samples: a 1 kHz sine at −12 dBFS (plus the mock's small harmonics and noise)
    const signal = await inPanel (`const r = ${latest}; const n = Math.min (r.dropouts[0].startFrame, 22050); const s = r.getSamples (${idJS}, 0, 0, n);
                                   let peak = 0, crossings = 0; for (let i = 0; i < n; ++i) { peak = Math.max (peak, Math.abs (s[i])); if (i > 0 && (s[i - 1] < 0) !== (s[i] < 0)) ++crossings; }
                                   return { peak, perSecond: crossings / (n / r.sampleRate) }`);
    check (signal.peak > 0.24 && signal.peak < 0.32, `the peak is about −12 dBFS (${signal.peak.toFixed (3)})`);
    check (Math.abs (signal.perSecond / 2000 - 1) < 0.05, `the zero crossings are a 1 kHz sine's (${signal.perSecond.toFixed (0)}/s)`);

    // the Recordings tab and its viewer
    await inPanel ("panel.ctx.commands.run ('recording.show'); panel.workspace.setSize ('drawerHeight', 420)");
    check (await waitInPanel (`panel.workspace.drawerTab === 'recordings' && ${wave}.recording === ${latest} && ${wave}.drawCount > 0
                               && panel.querySelector ('.cp-rec-row.cp-selected .cp-rec-dropouts')?.textContent === '1 dropout'`),
           "Show the recordings opens the tab, with the new recording selected, drawn, and its dropout listed");
    await sleep (300);
    const zoom = await inPanel (`const w = ${wave}, r = w.canvas.getBoundingClientRect(); const clientX = Math.round (r.left + r.width * 0.3), x = clientX - r.left; const before = { ...w.view };
                                 w.canvas.dispatchEvent (new WheelEvent ('wheel', { deltaY: -200, ctrlKey: true, clientX, clientY: r.top + 60, bubbles: true, cancelable: true }));
                                 return { before, after: { ...w.view }, x }`);
    const frameAt = (v, x) => v.start + x * v.framesPerPixel;
    check (zoom.after.framesPerPixel < zoom.before.framesPerPixel / 2 && Math.abs (frameAt (zoom.after, zoom.x) - frameAt (zoom.before, zoom.x)) < 1,
           `Ctrl-wheel zooms in around the pointer (${zoom.before.framesPerPixel.toFixed (1)} → ${zoom.after.framesPerPixel.toFixed (1)} frames/px)`);
    await inPanel (`${wave}.canvas.focus()`);
    await page.key ("ArrowRight");
    check (await inPanel (`return ${wave}.view.start > ${zoom.after.start} && panel.workspace.mainTab === 'gui'`), "→ pans (and isn't a panel shortcut)");
    await page.key ("f");
    check (await inPanel (`return ${wave}.isFitted && panel.workspace.mainTab === 'gui'`), "F fits");

    // drag a selection, and export it as 24-bit
    const drag = await inPanel (`const w = ${wave}, r = w.canvas.getBoundingClientRect(), rate = w.sampleRate;
                                 return { x0: r.left + (0.2 * rate - w.view.start) / w.view.framesPerPixel, x1: r.left + (0.5 * rate - w.view.start) / w.view.framesPerPixel, y: r.top + 80 }`);
    await page.drag (drag.x0, drag.y, drag.x1, drag.y);
    const selection = await inPanel (`return ${wave}.selection`);
    check (selection && Math.abs (selection.startFrame / info.rate - 0.2) < 0.01 && Math.abs (selection.endFrame / info.rate - 0.5) < 0.01
             && await inPanel (`return ! ${wave}.selectionText.hidden && ! ${wave}.exportSelectionButton.disabled && ${wave}.description.textContent.startsWith ('Selection from')`),
           `dragging selects 0.2–0.5 s (${JSON.stringify (selection)}), shown and described`);

    const savedBefore = await page.evaluate ("window.savedFiles.length");
    await inPanel (`${wave}.exportSelectionButton.click()`);
    check (await waitInPanel (`${view}.exporter.isOpen && ${view}.exporter.range === 'selection'`), "Export selection opens the export popover, for the selection");
    await inPanel (`const e = ${view}.exporter; e.formatSelect.value = 'pcm24'; e.formatSelect.dispatchEvent (new Event ('change')); e.exportButton.click()`);
    check (await waitInPanel (`window.parent.savedFiles.length === ${savedBefore + 1} && ${view}.exporter.status.dataset.state === 'ok'`, 8000), "...which saves a file through VS Code");
    await shoot (join (shots, `mock-${fixture}-recording-export.png`));
    const wav = await inPanel (`const f = window.parent.savedFiles[window.parent.savedFiles.length - 1], b = f.bytes, v = new DataView (b.buffer, b.byteOffset, b.byteLength);
        const text = (o) => String.fromCharCode (b[o], b[o + 1], b[o + 2], b[o + 3]); let fmt = -1, data = -1, dataSize = 0;
        for (let o = 12; o + 8 <= b.length; o += 8 + v.getUint32 (o + 4, true) + (v.getUint32 (o + 4, true) & 1)) { if (text (o) === 'fmt ') fmt = o + 8; if (text (o) === 'data') { data = o + 8; dataSize = v.getUint32 (o + 4, true); } }
        const channels = v.getUint16 (fmt + 2, true), bits = v.getUint16 (fmt + 14, true), frames = dataSize / (channels * 3);
        const sel = ${wave}.selection, r = ${latest}; let worst = 0;
        for (let c = 0; c < channels; ++c) { const s = r.getSamples (r.endpointIDs[0], c, sel.startFrame, frames);
            for (let i = 0; i < frames; ++i) { const o = data + (i * channels + c) * 3; let x = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16); if (x & 0x800000) x -= 0x1000000;
                worst = Math.max (worst, Math.abs (x / 8388607 - Math.max (-1, Math.min (1, s[i])))); } }
        return { name: f.name, bits, frames, expected: sel.endFrame - sel.startFrame, worst, status: ${view}.exporter.status.textContent }`);
    check (wav.bits === 24 && wav.frames === wav.expected && wav.worst <= 1 / 8388607,
           `the file is 24-bit, ${wav.frames} frames (expected ${wav.expected}), and its samples match the recording (worst error ${wav.worst.toExponential (2)})`);
    check (wav.name.endsWith (`-${id}.wav`) && wav.name.includes ("-Recording ") && wav.status === `Saved to /fake/vscode/${wav.name}`,
           `named patch-recording-endpoint ("${wav.name}"), and the saved path is shown`);

    await page.evaluate ("window.nextSaveCancels = true; true");
    await inPanel (`${view}.exporter.exportButton.click()`);
    check (await waitInPanel (`${view}.exporter.status.textContent === 'Cancelled' && ${view}.exporter.status.dataset.state === 'cancelled'`, 8000), "a cancelled save says Cancelled");
    await inPanel (`${view}.exporter.close()`);

    // the memory limit is remembered
    await inPanel (`const s = ${view}.capSelect; s.value = '128'; s.dispatchEvent (new Event ('change'))`);
    check (await inPanel (`return panel.settings.global.get ('recordingCapMB') === 128 && panel.recordings.capBytes === 128 * 1048576 && ${view}.gaugeText.textContent.endsWith ('of 128 MB')`),
           "the memory limit is set, shown and saved globally");
    await inPanel (`const s = ${view}.capSelect; s.value = '256'; s.dispatchEvent (new Event ('change'))`);

    // a rebuild during a capture: a dropout, and it carries on
    await inPanel (`${rec}.start()`);
    await sleep (700);
    await inPanel ("mock.rebuild()");
    await waitInPanel ("mock.isLoaded");
    const framesAfterRebuild = await inPanel (`return ${latest}.numFrames`);
    await sleep (700);
    const rebuilt = await inPanel (`const r = ${latest}; return { recording: r.isRecording, dropouts: r.dropouts, frames: r.numFrames }`);
    check (rebuilt.recording && rebuilt.frames > framesAfterRebuild && rebuilt.dropouts.length === 1 && rebuilt.dropouts[0].numFrames >= 2048,
           `a rebuild during a capture leaves one dropout (${JSON.stringify (rebuilt.dropouts)}), and the capture carries on`);
    await inPanel (`${rec}.stop()`);

    // unloading the patch stops a capture: connection lost
    await inPanel (`${rec}.start()`);
    await sleep (400);
    await inPanel ("mock.unload()");
    check (await waitInPanel (`${latest}.stopReason?.code === 'connection-lost' && ! panel.store.get ('connection')`), "unloading the patch stops the capture (connection lost)");
    check (await waitInPanel (`[...panel.querySelectorAll ('.cp-rec-stop-reason')].some (e => e.textContent.includes ('unloaded'))`), "...and its row says why");
    check (await inPanel ("return panel.captureButton.element.hidden && ! panel.ctx.commands.run ('recording.toggle')"), "with no patch, nothing can be recorded");
    await inPanel ("panel.handleMessageFromVSCode ({ patchToLoad: mock.patchFile })");
    check (await waitInPanel ("mock.isLoaded && !! panel.store.get ('connection')", 5000), "the patch loads again");
    check (await inPanel ("return panel.recordings.recordings.length === 3"), "...and the recordings are kept");

    // discarding frees the memory
    const before = await inPanel ("return { used: panel.recordings.usedBytes, own: panel.recordings.recordings[0].usedBytes }");
    await inPanel ("panel.querySelector (`.cp-rec-row[data-recording='${panel.recordings.recordings[0].id}'] .cp-rec-discard`).click()");
    check (await inPanel (`return panel.recordings.usedBytes === ${before.used - before.own} && panel.recordings.recordings.length === 2`), `discarding one frees its memory (${before.own} bytes)`);
    await inPanel (`${view}.discardAllButton.click()`);
    check (await inPanel ("return panel.recordings.recordings.length === 2"), "Discard all asks first");
    await inPanel (`${view}.discardAllButton.click()`);
    check (await inPanel (`return panel.recordings.usedBytes === 0 && panel.recordings.recordings.length === 0 && ! ${view}.emptyMessage.hidden && ${wave}.recording === null`),
           "...then discards everything, back to the empty state");
    await inPanel ("panel.workspace.setSize ('drawerHeight', 220); panel.workspace.showDrawerTab ('console')");
}
