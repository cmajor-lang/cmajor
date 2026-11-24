// Tests for panel_api/core/*. Run by test-core.mjs in headless Chrome (or open
// /mock/core-tests.html in a browser). Results go in window.testResults.

import { el, Disposer, formatNumber, setPressed } from "/panel_api/core/dom.js";
import { Store, applySessionStatus, isEqualJSON } from "/panel_api/core/store.js";
import { PanelSettings, PanelSettingsScopes } from "/panel_api/core/persistence.js";
import { FrameScheduler } from "/panel_api/core/frame-scheduler.js";
import { EndpointDataHub } from "/panel_api/core/endpoint-data-hub.js";
import { HostBridge } from "/panel_api/core/host-bridge.js";
import { PatchConnection } from "/cmaj_api/cmaj-patch-connection.js";
import { ServerSession } from "/cmaj_api/cmaj-server-session.js";
import { LevelDisplay, getScopeLanes, silenceDB } from "/panel_api/widgets/audio-meter.js";
import { ValueControlWidget } from "/panel_api/widgets/value-control.js";
import { isShortOptionList, maxOptionButtonsTextLength } from "/panel_api/widgets/parameter-model.js";
import { PatchGraphView } from "/panel_api/cmaj-patch-graph/patch-graph-view.js";
import { AudioInputWidget } from "/panel_api/widgets/audio-input-source.js";
import { CodeView } from "/panel_api/views/code-view.js";
import { BoundaryActivity, activityHalfLifeMs } from "/panel_api/views/graph-live.js";
import { fuzzyMatch, splitByMatches } from "/panel_api/core/fuzzy.js";
import { CommandRegistry, formatShortcut, shortcutMatches } from "/panel_api/core/commands.js";
import { CPUHistory, cpuHistorySeconds } from "/panel_api/core/cpu-history.js";
import { CPUHistoryChart, describeBlockBudget, formatLoad } from "/panel_api/widgets/cpu-history-chart.js";
import { encodeMIDIMessage, createNoteOn, createNoteOff, createControlChange, createPitchBend, getPitchBendValue, getVelocityFromPosition,
         getPanicMessages, getRootNoteForOctave, normaliseMIDIControlSettings, formatOctaveShift, MIDIControls, createMIDICommands,
         loadMIDIControlSettings, pitchBendCentre } from "/panel_api/widgets/midi-controls.js";
import { MIDIInputWidget } from "/panel_api/widgets/midi-input.js";
import { createAudioInputCommands, getGeneratorSupport } from "/panel_api/widgets/audio-input-source.js";
import { mergeGeneratorSettings, normaliseGeneratorSettings, defaultGeneratorSettings, frequencyToSliderPosition, sliderPositionToFrequency,
         valueToSliderPosition, sliderPositionToValue, generatorSliderRanges, roundToSignificantFigures, formatFrequency, formatGeneratorLevel,
         formatSeconds, parseGeneratorValue, ThrottledSender, getChangedGeneratorFields, loadSavedGeneratorSettings,
         generatorFieldsForType } from "/panel_api/widgets/signal-generator.js";

const results = [];
let currentTest = "";

function check (ok, what)
{
    results.push ({ test: currentTest, ok: !! ok, what });
}

async function test (name, fn)
{
    currentTest = name;

    try { await fn(); }
    catch (e) { results.push ({ test: name, ok: false, what: "threw: " + (e.stack ?? e) }); }
}

const tick = (ms = 0) => new Promise (r => setTimeout (r, ms));

//==============================================================================
await test ("dom: el()", () =>
{
    let clicked = 0;
    const e = el ("button", { class: ["a", false, "b"], title: "<t>", dataset: { x: "1" }, style: { color: "red", "--k": "2" },
                              onclick: () => ++clicked, "aria-label": "lbl", disabled: false },
                  "text <b>", 3, null, [el ("span", null, "s")]);

    e.click();
    check (e.className === "a b", "class array");
    check (e.title === "<t>" && e.dataset.x === "1", "props and dataset");
    check (e.style.color === "red" && e.style.getPropertyValue ("--k") === "2", "style object with custom properties");
    check (e.getAttribute ("aria-label") === "lbl", "attributes");
    check (clicked === 1, "listener");
    check (e.textContent === "text <b>3s" && e.childNodes.length === 3, "children (strings become text)");
    check (! e.disabled, "disabled: false");

    const svg = el ("svg:circle", { r: 3, class: "dot" });
    check (svg instanceof SVGElement && svg.getAttribute ("r") === "3" && svg.getAttribute ("class") === "dot", "svg elements");

    check (formatNumber (1234.5678) === "1235" && formatNumber (0.123456) === "0.123" && formatNumber (12.345) === "12.35", "formatNumber");
});

await test ("dom: Disposer", () =>
{
    const order = [];
    const d = new Disposer();
    d.add (() => order.push (1));
    d.add (() => order.push (2), () => order.push (3));
    let n = 0;
    const target = new EventTarget();
    d.listen (target, "x", () => ++n);
    target.dispatchEvent (new Event ("x"));
    d.dispose();
    target.dispatchEvent (new Event ("x"));
    d.dispose();
    check (order.join () === "3,2,1", "runs in reverse order, once");
    check (n === 1, "removes listeners");
});

//==============================================================================
await test ("store", () =>
{
    const store = new Store ({ a: 1, b: 2 });
    const calls = [];
    store.subscribe (["a"], (s, changed) => calls.push (["a", s.a, [...changed].join()]));
    const unsubscribe = store.subscribe (null, (s, changed) => calls.push (["all", [...changed].join()]));
    store.set ({ a: 1 });
    check (calls.length === 0, "no notification when nothing changed");
    store.set ({ b: 3 });
    check (calls.length === 1 && calls[0][0] === "all", "keyed subscriber not told about other keys");
    store.set ({ a: 5, b: 4 });
    check (calls.length === 3 && calls[1][1] === 5 && calls[1][2] === "a,b", "changed keys passed on");
    unsubscribe();
    store.set ({ b: 9 });
    check (calls.length === 3, "unsubscribe");
    let now = null;
    store.subscribe (["a"], s => now = s.a, true);
    check (now === 5, "callNow");

    check (isEqualJSON ({ x: [1, { y: 2 }] }, { x: [1, { y: 2 }] }) && ! isEqualJSON ({ x: [1] }, { x: [1, 2] }) && ! isEqualJSON ([], {}), "isEqualJSON");
});

await test ("store: applySessionStatus diffs the endpoints", async () =>
{
    const fixture = await (await fetch ("/fixtures/Tremolo.json")).json();
    const store = new Store();
    let endpointChanges = 0;
    store.subscribe (["inputs", "outputs"], () => ++endpointChanges);

    applySessionStatus (store, { connected: true, loaded: false, status: "Loading" });
    check (store.get ("statusText") === "Loading" && store.get ("inputs").length === 0, "loading");
    const firstInputs = store.get ("inputs");
    applySessionStatus (store, { connected: true, loaded: false });
    check (store.get ("inputs") === firstInputs, "an empty list keeps its identity");

    applySessionStatus (store, structuredClone ({ ...fixture.status, connected: true }));
    const inputs = store.get ("inputs");
    const changesAfterLoad = endpointChanges;
    check (inputs.length === fixture.status.details.inputs.length, "endpoints from the status");
    check (store.get ("lastBuildTime") > 0, "build time noted");

    // the same status again (as a fresh object, as the server sends) mustn't count as a change
    applySessionStatus (store, structuredClone ({ ...fixture.status, connected: true, playing: false }));
    check (store.get ("inputs") === inputs && endpointChanges === changesAfterLoad, "an identical endpoint list is unchanged");
    check (store.get ("playing") === false, "other keys still update");

    const changed = structuredClone ({ ...fixture.status, connected: true });
    changed.details.inputs[0].annotation.max = 999;
    applySessionStatus (store, changed);
    check (store.get ("inputs") !== inputs && endpointChanges === changesAfterLoad + 1, "a changed endpoint is a change");

    const lastGood = store.get ("inputs");
    applySessionStatus (store, { connected: true, loaded: false, status: "Loading: Tremolo.cmajorpatch" });
    check (store.get ("inputs") === lastGood, "the endpoints are kept while the patch rebuilds");
    check (store.get ("manifest")?.name === fixture.status.manifest.name, "...and the manifest");
    applySessionStatus (store, { connected: true, loaded: false, error: "x.cmajor:1:2: error: bad" });
    check (store.get ("error").includes ("bad") && store.get ("inputs") === lastGood, "...and while it has a build error");
    check (store.get ("manifestFile") === fixture.status.manifestFile, "the manifest file is remembered while there's an error");
    applySessionStatus (store, structuredClone ({ ...fixture.status, connected: true }));
    const reloaded = store.get ("inputs");
    applySessionStatus (store, { connected: false, loaded: false, status: "Cannot connect to the Cmajor server" });
    check (store.get ("inputs") === reloaded && store.get ("manifest"), "...and while the server is out of touch");
    applySessionStatus (store, { connected: true, loaded: false });
    check (store.get ("inputs").length === 0 && store.get ("outputs").length === 0, "unloading clears the endpoints");
});

//==============================================================================
await test ("persistence", () =>
{
    const data = new Map();
    const fakeStorage = { getItem: k => data.get (k) ?? null, setItem: (k, v) => data.set (k, v), removeItem: k => data.delete (k) };

    const a = new PanelSettings ("/x/A.cmajorpatch", fakeStorage);
    a.set ("tab", "graph");
    a.set ("sizes", { w: 1, h: 2 });
    const a2 = new PanelSettings ("/x/A.cmajorpatch", fakeStorage);
    const b = new PanelSettings ("/x/B.cmajorpatch", fakeStorage);
    check (a2.get ("tab") === "graph" && a2.get ("sizes").h === 2, "values survive a reload");
    check (b.get ("tab", "gui") === "gui", "scoped per patch");
    a2.remove ("tab");
    check (new PanelSettings ("/x/A.cmajorpatch", fakeStorage).get ("tab") === undefined, "remove");

    data.set ("cmaj-patch-panel:/x/C", "{not json");
    check (new PanelSettings ("/x/C", fakeStorage).get ("x", 7) === 7, "corrupt data is ignored");

    const broken = { getItem: () => { throw new Error(); }, setItem: () => { throw new Error(); }, removeItem() {} };
    const c = new PanelSettings ("/y", broken);
    c.set ("x", 1);
    check (c.get ("x") === 1, "works (in memory) when storage throws");

    const real = new PanelSettings ("core-test-scope");
    real.set ("n", 42);
    check (new PanelSettings ("core-test-scope").get ("n") === 42, "real localStorage");
    real.clear();
});

await test ("settings scopes", () =>
{
    const data = new Map();
    const fakeStorage = { getItem: k => data.get (k) ?? null, setItem: (k, v) => data.set (k, v), removeItem: k => data.delete (k) };
    const s = new PanelSettingsScopes (fakeStorage);

    s.patch.set ("floatRect", { w: 1 });
    check (s.patch.get ("floatRect", null) === null, "patch scope: nothing is kept without a patch");

    s.setPatch ("/x/A.cmajorpatch");
    s.patch.set ("floatRect", { w: 300 });
    s.patchOrGlobal.set ("mainTab", "graph");
    s.global.set ("theme", "light");

    s.setPatch ("/x/B.cmajorpatch");
    check (s.patch.get ("floatRect", "none") === "none", "patch scope: a new patch doesn't inherit another's geometry");
    check (s.patchOrGlobal.get ("mainTab", "gui") === "graph", "patchOrGlobal: a new patch gets the last choice");
    s.patchOrGlobal.set ("mainTab", "io");
    check (s.global.get ("theme") === "light", "global scope");

    s.setPatch ("/x/A.cmajorpatch");
    check (s.patch.get ("floatRect")?.w === 300 && s.patchOrGlobal.get ("mainTab") === "graph", "each patch keeps its own");
    check (s.forPatch ("/x/A.cmajorpatch") === s.current && s.forPatch ("/x/B.cmajorpatch") !== s.current
             && s.forPatch ("/x/B.cmajorpatch").get ("mainTab") === "io" && s.forPatch ("") === null, "forPatch");

    s.setPatch (null);
    check (s.patch.get ("floatRect", 0) === 0 && s.patchOrGlobal.get ("mainTab") === "io", "no patch: defaults, and the global values");
});

await test ("store: batch", () =>
{
    const store = new Store ({ a: 1, b: 1 });
    const calls = [];
    store.subscribe (["a", "b"], (s, changed) => calls.push ([...changed].sort().join()));
    const result = store.batch (() =>
    {
        store.set ({ a: 2 });
        store.batch (() => store.set ({ b: 2 }));
        check (calls.length === 0, "no notifications inside a batch");
        return 7;
    });
    check (result === 7 && calls.length === 1 && calls[0] === "a,b", "one notification with every changed key");
    store.batch (() => store.set ({ a: 2 }));
    check (calls.length === 1, "an empty batch tells nobody");
});

//==============================================================================
await test ("frame scheduler", () =>
{
    let frames = [];
    const s = new FrameScheduler (fn => { frames.push (fn); return frames.length; }, () => { frames = []; });
    const runFrame = (t) => { const f = frames; frames = []; f.forEach (fn => fn (t)); };

    let a = 0, b = 0;
    const drawA = () => { ++a; };
    let bFramesLeft = 3;
    const drawB = () => { ++b; return --bFramesLeft > 0; };

    s.schedule (drawA);
    s.schedule (drawA);
    s.schedule (drawB);
    check (frames.length === 1, "one rAF request for many schedules");
    runFrame (16);
    check (a === 1 && b === 1, "each function runs once per frame");
    runFrame (32);
    runFrame (48);
    check (a === 1 && b === 3, "returning true keeps a function running");
    check (frames.length === 0 && ! s.isRunning, "idles when nothing is scheduled");

    const cancel = s.schedule (drawA);
    cancel();
    check (frames.length === 0 && ! s.isRunning, "cancel stops the loop");

    s.schedule (() => { throw new Error ("boom"); });
    s.schedule (drawA);
    const errors = console.error;
    console.error = () => {};
    runFrame (64);
    console.error = errors;
    check (a === 2, "an exception doesn't stop the others");
});

//==============================================================================
class FakeConnection  extends PatchConnection
{
    constructor()
    {
        super();
        this.sent = [];
        this.serverListeners = new Map();   // replyType -> endpoint
    }

    sendMessageToServer (m)
    {
        this.sent.push (m);

        if (m.type === "add_endpoint_listener")     this.serverListeners.set (m.replyType, m.endpoint);
        if (m.type === "remove_endpoint_listener")  this.serverListeners.delete (m.replyType);
    }

    // what the server would send to every listener on an endpoint
    emit (endpoint, value)
    {
        for (const [replyType, e] of [...this.serverListeners])
            if (e === endpoint)
                this.deliverMessageFromServer ({ type: replyType, message: value });
    }

    // what the server sends after a rebuild, or in answer to a req_status (e.g. from a patch
    // GUI that has just been created): it keeps its listeners through a rebuild
    sendStatus()
    {
        this.deliverMessageFromServer ({ type: "status", message: {} });
    }

    get numListenerMessages()   { return this.sent.filter (m => m.type.endsWith ("_endpoint_listener")).length; }
}

await test ("endpoint data hub", () =>
{
    const connection = new FakeConnection();
    const hub = new EndpointDataHub (connection);
    const got = { a: [], b: [], c: [] };

    const unsubA = hub.subscribe ("out", v => got.a.push (v), { granularity: 400 });
    const unsubB = hub.subscribe ("out", v => got.b.push (v), { granularity: 400 });
    const unsubC = hub.subscribe ("out", v => got.c.push (v), { granularity: 1024 });

    check (connection.serverListeners.size === 2 && hub.numServerListeners === 2, "one server listener per endpoint+granularity");
    connection.emit ("out", { min: [0], max: [1] });
    check (got.a.length === 1 && got.b.length === 1 && got.c.length === 1, "fanned out to everyone, once each");

    const messagesBefore = connection.numListenerMessages;
    connection.sendStatus();
    connection.sendStatus();
    check (connection.numListenerMessages === messagesBefore && connection.serverListeners.size === 2,
           "a status (a rebuild, or the answer to a req_status) doesn't remove and re-add the listeners");
    connection.emit ("out", { min: [0], max: [0.5] });
    check (got.a.length === 2 && got.c.length === 2, "data keeps flowing");

    unsubA();
    check (connection.serverListeners.size === 2, "still listening while someone is");
    unsubB();
    unsubB();
    check (connection.serverListeners.size === 1, "removed with the last subscriber (and unsubscribing twice is harmless)");
    unsubC();
    check (connection.serverListeners.size === 0 && hub.numServerListeners === 0, "all gone");

    // parameters
    const values = [];
    const unsubP = hub.subscribeParameter ("rate", v => values.push (v));
    check (connection.sent.some (m => m.type === "req_param_value" && m.id === "rate"), "asks for the parameter's value");
    connection.deliverMessageFromServer ({ type: "param_value", message: { endpointID: "rate", value: 3 } });
    const late = [];
    hub.subscribeParameter ("rate", v => late.push (v));
    check (values[0] === 3 && late[0] === 3, "parameter values cached and replayed");
    const requests = () => connection.sent.filter (m => m.type === "req_param_value").length;
    const requestsBefore = requests();
    hub.handleNewBuild();
    check (requests() === requestsBefore + 1, "a new build asks for the parameters' values again");
    unsubP();

    // groups
    const group = hub.createGroup();
    const g = [];
    group.subscribe ("midiOut", v => g.push (v));
    check (connection.serverListeners.size === 1, "group subscribes");
    group.setActive (false);
    check (connection.serverListeners.size === 0, "inactive group holds no server listeners");
    connection.emit ("midiOut", 1);
    group.setActive (true);
    connection.emit ("midiOut", 2);
    check (g.join() === "2" && connection.serverListeners.size === 1, "reactivated");
    group.dispose();
    check (connection.serverListeners.size === 0, "group dispose");

    hub.subscribe ("out", () => {});
    hub.dispose();
    check (connection.serverListeners.size === 0, "dispose removes the server listeners");
});

//==============================================================================
await test ("host bridge", async () =>
{
    const posted = [], opened = [], alerts = [];
    const fakeWindow = { open: (u) => opened.push (u), alert: (m) => alerts.push (m),
                         navigator: { clipboard: { writeText: async (t) => fakeWindow.clip = t, readText: async () => fakeWindow.clip } } };
    const bridge = new HostBridge (fakeWindow);

    check (! bridge.isVSCode, "browser mode by default");
    bridge.openSourceFile ("/a/b.cmajor:12:5");
    check (opened[0] === "file:///a/b.cmajor", "browser: opens a file URL without the position");
    bridge.showErrorAlert ("bad");
    check (alerts[0] === "bad", "browser: alert");
    await bridge.writeClipboard ("hello");
    check (await bridge.readClipboard() === "hello", "browser: clipboard");
    check (! bridge.openTextDocument ("x", "cpp") && ! bridge.canOpenTextDocuments, "browser: can't open documents");

    fakeWindow.sendMessageToVSCode = (m) => posted.push (m);
    check (bridge.isVSCode, "VS Code mode once sendMessageToVSCode exists");
    bridge.openSourceFile ("/a/b.cmajor:12:5");
    bridge.notifyStatus ({ loaded: true });
    check (posted[0].showSourceFile === "/a/b.cmajor:12:5" && posted[1].newServerStatus.loaded, "VS Code: messages");

    const reading = bridge.readClipboard();
    check (posted.at (-1).readClipboard === true, "VS Code: asks for the clipboard");
    check (bridge.handleMessage ({ clipboardText: "pasted" }) === true, "VS Code: the reply is consumed");
    check (await reading === "pasted", "VS Code: clipboard read resolves");
    check (bridge.handleMessage ({ clipboardText: "unasked" }) === false, "an unrequested clipboardText is left for the panel");

    const themes = [];
    bridge.addThemeListener (t => themes.push (t));
    check (bridge.handleMessage ({ theme: { kind: "light" } }) && themes[0].kind === "light" && bridge.getTheme().kind === "light", "theme messages");
    check (bridge.handleMessage ({ patchToLoad: "x" }) === false, "other messages are left for the panel");
});

//==============================================================================
/// A scheduler driven by hand: frame (time) runs one frame at that time
function makeManualScheduler()
{
    let frames = [];
    const scheduler = new FrameScheduler (fn => { frames.push (fn); return frames.length; }, () => { frames = []; });
    scheduler.frame = (t) => { const f = frames; frames = []; f.forEach (fn => fn (t)); };
    scheduler.hasFrameRequested = () => frames.length > 0;
    return scheduler;
}

await test ("frame scheduler: dt and going idle", () =>
{
    const s = makeManualScheduler();
    const dts = [];
    let framesLeft = 4;
    const fn = (_time, dt) => { dts.push (dt); return --framesLeft > 0; };

    s.schedule (fn);
    s.frame (1000);
    s.frame (1040);
    s.frame (2040);     // (a stall)
    s.frame (2041);
    check (Math.abs (dts[0] - 1000 / 60) < 0.001, `the first frame counts as one 60 Hz frame (${dts[0]})`);
    check (dts[1] === 40, "then dt is the time since the previous frame");
    check (dts[2] === FrameScheduler.maxFrameInterval, `a stall is clamped to ${FrameScheduler.maxFrameInterval} ms`);
    check (! s.isRunning && ! s.hasFrameRequested(), "idle once nothing returns true");

    s.schedule (fn);
    s.frame (9000);
    check (Math.abs (dts[4] - 1000 / 60) < 0.001, "after being idle, the next frame counts as one 60 Hz frame again");

    // a function scheduled from inside a frame runs on the next one (into the swapped set)
    let inner = 0;
    s.schedule (() => { s.schedule (() => { ++inner; }); });
    s.frame (9016);
    check (inner === 0 && s.isRunning, "scheduling during a frame waits for the next one");
    s.frame (9032);
    check (inner === 1 && ! s.isRunning, "...which runs it, then goes idle");
});

await test ("graph activity decays by time, then goes idle", () =>
{
    const s = makeManualScheduler();
    let lastLevels = null, numFrames = 0;
    const activity = new BoundaryActivity (s, (levels) => { lastLevels = levels; ++numFrames; });

    activity.setLevel ("gain", 1);
    s.frame (0);
    const afterFirst = activity.getLevel ("gain");
    check (Math.abs (afterFirst - Math.pow (0.5, (1000 / 60) / activityHalfLifeMs)) < 1e-6, "decays by the first frame's dt");

    // the same time in big or small frames gives the same level
    const a = new BoundaryActivity (makeManualScheduler(), null), b = new BoundaryActivity (makeManualScheduler(), null);
    a.setLevel ("x", 1); b.setLevel ("x", 1);
    a.update (90);
    for (let i = 0; i < 9; ++i) b.update (10);
    check (Math.abs (a.getLevel ("x") - 0.5) < 1e-6 && Math.abs (a.getLevel ("x") - b.getLevel ("x")) < 1e-6, "halves in activityHalfLifeMs, whatever the frame rate");

    for (let t = 16; t < 2000 && s.isRunning; t += 16)
        s.frame (t);

    check (! s.isRunning && activity.getLevel ("gain") === 0 && lastLevels?.size === 0, "goes idle, with a last frame showing nothing lit");
    check (numFrames < 40, `...within a few half-lives (${numFrames} frames)`);
});

//==============================================================================
await test ("level meter: release and idle", () =>
{
    const s = makeManualScheduler();
    const display = new LevelDisplay (2, { scheduler: s, themeElement: document.body });
    document.body.append (display.element);

    display.addMinMax ({ min: [-1, 0], max: [1, 0.5] });
    const later = performance.now() + 5000;    // (past the peak hold)

    // the same time in big or small frames gives the same level
    const other = new LevelDisplay (2, { scheduler: makeManualScheduler(), themeElement: document.body });
    other.addMinMax ({ min: [-1, 0], max: [1, 0.5] });
    display.release (100, later);
    for (let i = 0; i < 10; ++i) other.release (10, later);
    check (Math.abs (display.levels[0] - other.levels[0]) < 1e-5 && Math.abs (display.peaks[0] - other.peaks[0]) < 1e-5,
           "falls at the same rate whatever the frame rate");
    other.dispose();

    let steps = 1;
    while (display.release (100, later) && steps < 100)
        ++steps;

    check (steps < 100 && display.levels.every (l => l === 0) && display.peaks.every (p => p === 0),
           `falls to silence (${silenceDB} dB) and stops moving, so the scheduler can idle (${steps} steps)`);

    // a new peak restarts the hold
    display.addMinMax ({ min: [0, 0], max: [0.5, 0] });
    display.peakTimes[0] = 0;
    display.addMinMax ({ min: [0, 0], max: [0.5, 0] });
    check (display.peakTimes[0] > 0, "an equal new peak restarts the hold");

    // the aria value is clamped to the meter's range, and only written when it changes
    display.addMinMax ({ min: [0, 0], max: [8, 0] });
    display.drawFrame (16);
    check (display.meterCanvas.getAttribute ("aria-valuenow") === "6.0", `aria-valuenow is clamped (${display.meterCanvas.getAttribute ("aria-valuenow")})`);

    let writes = 0;
    const setAttribute = display.meterCanvas.setAttribute.bind (display.meterCanvas);
    display.meterCanvas.setAttribute = (k, v) => { if (k === "aria-valuenow") ++writes; setAttribute (k, v); };
    display.drawFrame (16);
    display.drawFrame (16);
    check (writes === 0, "...and not rewritten when it hasn't changed");

    display.dispose();
    display.element.remove();
});

await test ("scope lanes split all the channels", () =>
{
    const lanes = (n, compact) => getScopeLanes (n, compact).map (l => `${l.firstChannel}-${l.endChannel}`).join();
    check (lanes (1) === "0-1", "mono: one lane");
    check (lanes (2) === "0-1,1-2", "stereo: one channel per lane");
    check (lanes (4) === "0-2,2-4", "4 channels: two per lane");
    check (lanes (5) === "0-3,3-5", "5 channels: three and two");
    check (lanes (6, true) === "0-6", "compact: every channel in one lane");

    for (let n = 1; n <= 9; ++n)
    {
        const l = getScopeLanes (n);

        if (l[0].firstChannel !== 0 || l.at (-1).endChannel !== n || l.some ((x, i) => i > 0 && x.firstChannel !== l[i - 1].endChannel))
            check (false, `${n} channels: the lanes don't cover every channel once`);
    }
});

//==============================================================================
function makeWidgetOptions (extra = {})
{
    const connection = new FakeConnection();
    const hub = new EndpointDataHub (connection);
    const group = hub.createGroup();
    group.setActive (true);
    return { connection, hub, options: { patchConnection: connection, group, scheduler: makeManualScheduler(), ...extra } };
}

await test ("slider: one gesture per drag, ignoring the server's echoes meanwhile", () =>
{
    const { connection, hub, options } = makeWidgetOptions();
    const endpoint = { endpointID: "gain", purpose: "parameter", endpointType: "value", dataType: { type: "float32" },
                       annotation: { min: 0, max: 1, init: 0.5 } };
    const widget = new ValueControlWidget (endpoint, options);
    document.body.append (widget.element);

    const gestures = () => connection.sent.filter (m => m.type.startsWith ("send_gesture")).map (m => m.type.slice (13)).join();
    const echo = (value) => connection.deliverMessageFromServer ({ type: "param_value", message: { endpointID: "gain", value } });
    const pointer = (type, extra = {}) => widget.control.dispatchEvent (new PointerEvent (type, { pointerId: 7, button: 0, bubbles: true, ...extra }));

    pointer ("pointerdown");
    pointer ("pointerdown");
    check (gestures() === "start", "a press starts one gesture");
    echo (0.9);
    check (widget.value === 0.5 && widget.control.value === "0.5", "an echo doesn't move it while it's dragged");
    pointer ("pointermove", { pointerId: 8 });
    pointer ("pointercancel", { pointerId: 8 });
    check (gestures() === "start", "another pointer's events are ignored");
    pointer ("pointercancel");
    check (gestures() === "start,end", "pointercancel ends the gesture");
    check (widget.value === 0.9, "...and then it shows where the server has it");
    pointer ("pointerup");
    check (gestures() === "start,end", "no second end");

    pointer ("pointerdown");
    widget.send (0.2, true);
    check (gestures() === "start,end,start", "a change during a drag joins its gesture");
    pointer ("pointerup");
    check (gestures() === "start,end,start,end", "pointerup ends it");

    pointer ("pointerdown", { button: 2 });
    check (gestures() === "start,end,start,end", "a right-click isn't a drag");

    pointer ("pointerdown");
    widget.dispose();
    check (gestures().endsWith ("start,end"), "disposing it mid-drag ends the gesture");
    hub.dispose();
});

await test ("graph view: panning stops at the graph's edges, and an axis that fits doesn't move", () =>
{
    const container = el ("div", { style: "position: absolute; left: 0; top: 0; width: 800px; height: 600px" });
    document.body.append (container);
    const view = new PatchGraphView (container, { fitTopMargin: 40 });
    view.positioned = { width: 2000, height: 400, nodes: [], edges: [] };   // (layout units, including any widget gutters)
    const near = (a, b) => Math.abs (a - b) < 1e-6;

    view.transform = { x: 0, y: 0, k: 1 };
    view.panBy (100000, 100000);
    check (near (view.transform.x, 20) && near (view.transform.y, 40), "the top-left can't be pulled further in than the margins (20 across, 40 at the top)");
    view.panBy (-100000, -100000);
    check (near (view.transform.x, 800 - 20 - 2000) && near (view.transform.y, 40),
           "the right edge stops at the margin; it fits the height (400 < 600 - 40 - 20), so that doesn't move");

    view.zoomBy (2, 0, 0);
    check (near (view.transform.y, 600 - 20 - 800) || (view.transform.y <= 40 && view.transform.y >= 600 - 20 - 800),
           "zoomed in so it's taller than the container, it can be panned up and down, within the limits");

    view.zoomBy (0.3 / view.transform.k);
    check (near (view.transform.x, (800 - 600) / 2) && near (view.transform.y, 40), "zoomed out so it all fits, it's centred across and at the top");
    view.panBy (-50, 50);
    check (near (view.transform.x, 100) && near (view.transform.y, 40), "...and can't be panned at all");

    container.style.display = "none";
    view.transform = { x: 1234, y: 5678, k: 1 };
    view.applyTransform();
    check (view.transform.x === 1234 && view.transform.y === 5678, "a hidden container (no size) leaves the transform alone");

    view.dispose();
    container.remove();
});

await test ("options: a few short ones are buttons, the rest a drop-down list", () =>
{
    const optionsOf = (text) => text.split ("|").map ((t, i) => ({ value: i, text: t }));
    check (isShortOptionList (optionsOf ("Off|On")) && isShortOptionList (optionsOf ("Sine|Saw|Square")) && isShortOptionList (optionsOf ("Old|New|New AA")),
           "Off|On, Sine|Saw|Square and Old|New|New AA are short");
    check (! isShortOptionList (optionsOf ("Sine|Triangle|Square|Ramp Up|Ramp Down|Random")) && ! isShortOptionList (optionsOf ("Bypass|Enabled|Inverted"))
             && ! isShortOptionList (optionsOf ("A|B|C|D|E")) && ! isShortOptionList (optionsOf ("Only")),
           `too many, too long (over ${maxOptionButtonsTextLength} characters) or too few (one) aren't`);

    const { connection, hub, options } = makeWidgetOptions();
    const endpoint = { endpointID: "mode", purpose: "parameter", endpointType: "value", dataType: { type: "float32" },
                       annotation: { text: "Off|On", init: 1 } };
    const widget = new ValueControlWidget (endpoint, options);
    document.body.append (widget.element);

    const pressed = () => widget.optionButtons.map (b => (b.classList.contains ("cp-on") ? "+" : "-") + (b.getAttribute ("aria-pressed") === "true" ? "+" : "-")).join();
    check (widget.model.hasOptionButtons && ! widget.element.querySelector ("select") && widget.optionButtons.length === 2, "Off|On is two buttons");
    check (pressed() === "--,++", "the default's button is pressed");

    widget.optionButtons[0].click();
    check (connection.sent.filter (m => m.type === "send_value").at (-1)?.value === 0 && pressed() === "++,--",
           "clicking one sends its value (as a gesture), and presses it");
    check (connection.sent.filter (m => m.type.startsWith ("send_gesture")).length === 2, "...as one gesture");

    connection.deliverMessageFromServer ({ type: "param_value", message: { endpointID: "mode", value: 1 } });
    check (pressed() === "--,++", "the server's value presses its button");

    const sentBefore = connection.sent.length;
    widget.optionButtons[0].dispatchEvent (new MouseEvent ("dblclick", { bubbles: true }));
    check (connection.sent.length === sentBefore, "a double-click on a button doesn't reset it");

    widget.dispose();
    hub.dispose();
});

await test ("audio input: file drops", async () =>
{
    const logs = [], sources = [];
    const session = { setAudioInputSource: (...args) => sources.push (args), addAudioInputModeListener() {}, removeAudioInputModeListener() {},
                      requestAudioInputMode() {} };
    const { hub, options } = makeWidgetOptions ({ session, log: (...args) => logs.push (args), size: "compact" });
    const endpoint = { endpointID: "in", purpose: "audio in", endpointType: "stream", numAudioChannels: 2, dataType: { type: "float32<2>" } };
    const widget = new AudioInputWidget (endpoint, options);
    document.body.append (widget.element);

    const fileDrag = new DataTransfer();
    fileDrag.items.add (new File ([new Uint8Array ([1, 2, 3])], "loop.wav", { type: "audio/wav" }));
    const textDrag = new DataTransfer();
    textDrag.setData ("text/plain", "hello");

    const drag = (type, target, dataTransfer) => target.dispatchEvent (new DragEvent (type, { dataTransfer, bubbles: true, cancelable: true }));
    const isHighlighted = () => widget.element.classList.contains ("cp-drop-target");
    const child = widget.bodyElement;

    drag ("dragenter", widget.element, textDrag);
    check (! isHighlighted(), "dragging text over it doesn't highlight it");
    drag ("dragenter", widget.element, fileDrag);
    drag ("dragenter", child, fileDrag);
    drag ("dragleave", widget.element, fileDrag);
    check (isHighlighted(), "a file drag stays highlighted as it crosses the widget's children");
    drag ("dragleave", child, fileDrag);
    check (! isHighlighted(), "...until it leaves");

    drag ("dragenter", widget.element, fileDrag);
    drag ("drop", child, fileDrag);
    check (! isHighlighted(), "a drop clears the highlight");
    await tick (20);
    check (sources.length === 1 && sources[0][0] === "in" && sources[0][1] === false && sources[0][2]?.length === 3, "the dropped file becomes the input");

    widget.useFile ({ name: "huge.wav", size: 60 * 1024 * 1024, arrayBuffer: () => Promise.resolve (new ArrayBuffer (0)) });
    widget.useFile ({ name: "bad.wav", size: 10, arrayBuffer: () => Promise.reject (new Error ("unreadable")) });
    await tick (20);
    check (logs.length === 2 && logs[0][0] === "error" && logs[0][2].includes ("huge.wav") && logs[1][2].includes ("unreadable"),
           "a file that's too big or unreadable is reported (to the console, from a compact widget)");
    check (sources.length === 1, "...and not used");

    widget.dispose();
    hub.dispose();
});

//==============================================================================
await test ("code view: caching, one request at a time, timeout and Retry", async () =>
{
    const requests = [];
    const store = new Store ({ codeGenTargets: ["graph", "cpp", "wast"], loaded: true, buildNumber: 1, patchSettings: 0 });
    const ctx = { store, settings: { patch: { get: (_k, d) => d, set() {} } }, bridge: { canOpenTextDocuments: false },
                  session: { requestGeneratedCode: (target) => new Promise (resolve => requests.push ({ target, resolve })) } };
    const view = new CodeView (ctx, { timeoutMs: 60 });
    const message = () => view.message.hidden ? "" : view.messageText.textContent;

    check (requests.length === 0, "nothing is generated until the tab is shown");
    view.setActive (true);
    view.setActive (true);
    store.set ({ loaded: true, codeGenTargets: ["graph", "cpp", "wast"] });
    check (requests.length === 1 && requests[0].target === "cpp", "one request for the target and build, however often it updates");
    check (message().startsWith ("Generating"), "says it's generating");

    await tick (100);
    check (message().includes ("in time") && ! view.retryButton.hidden, `a timeout shows an error with Retry (${message()})`);
    requests[0].resolve ({ code: "late" });
    await tick();
    check (view.text.value !== "late" && message().includes ("in time"), "a reply after the timeout is ignored");
    view.update();
    check (requests.length === 1, "...and it doesn't ask again by itself");

    view.retryButton.click();
    check (requests.length === 2, "Retry asks again");
    requests[1].resolve ({ code: "int main() {}" });
    await tick();
    check (view.text.value === "int main() {}" && ! view.text.hidden && view.retryButton.hidden, "the code is shown");

    view.setActive (false);
    view.setActive (true);
    check (requests.length === 2, "cached for the same build");

    store.set ({ buildNumber: 2 });
    check (requests.length === 3, "a new build generates it again");
    store.set ({ buildNumber: 3 });
    requests[2].resolve ({ code: "old build" });
    await tick();
    check (requests.length === 4 && view.text.value !== "old build", "an older build's reply doesn't replace a newer request");
    requests[3].resolve ({ code: "", messages: [{ fullDescription: "error: bad" }] });
    await tick();
    check (view.text.value === "error: bad", "messages are shown when there's no code");

    view.dispose();
});

//==============================================================================
await test ("server session: connection options and CPU rate", () =>
{
    class TestSession  extends ServerSession
    {
        constructor() { super ("test"); this.sent = []; }
        sendMessageToServer (m) { this.sent.push (m); }
    }

    const session = new TestSession();
    session.status = { connected: true, loaded: true, httpRootURL: "http://host/session/" };

    const plain = session.createPatchConnection();
    const connection = session.createPatchConnection ({
        transformResourceAddress: (address, path) => `${address}#${path}`,
        filterMessage: (msg) => msg.type !== "dropped",
    });

    check (plain.getResourceAddress ("/a/b.js") === "http://host/session/a/b.js", "no options: the plain address");
    check (connection.getResourceAddress ("/a/b.js") === "http://host/session/a/b.js#/a/b.js", "transformResourceAddress gets the address and the path");

    const got = [];
    connection.addEventListener ("kept", (m) => got.push (m));
    connection.addEventListener ("dropped", (m) => got.push (m));
    session.handleMessageFromServer ({ type: "kept", message: 1 });
    session.handleMessageFromServer ({ type: "dropped", message: 2 });
    check (got.join() === "1", "filterMessage drops what it returns false for");

    const rates = () => session.sent.filter (m => m.type === "set_cpu_info_rate").map (m => m.framesPerCallback);
    const cpu = () => {};
    session.addCPUListener (cpu, 4800);
    check (rates().at (-1) === 4800, "addCPUListener (listener, framesPerUpdate) sets the rate");
    session.removeCPUListener (cpu);
    check (rates().at (-1) === 0, "removing the last listener turns it off");

    plain.dispose();
    connection.dispose();
    check (session.activePatchConnections.size === 0, "disposed connections are forgotten");
    session.dispose();
});

//==============================================================================
await test ("fuzzy: matching and ranking", () =>
{
    check (fuzzyMatch ("rst", "Reset")?.indices.join() === "0,2,4", "finds the characters in order");
    check (fuzzyMatch ("RST", "reset") !== null, "ignores case");
    check (fuzzyMatch ("tsr", "Reset") === null, "no match when the order is wrong");
    check (fuzzyMatch ("resets", "Reset") === null, "no match when the query is longer");
    check (fuzzyMatch ("", "Anything")?.score === 0, "an empty query matches everything");

    const score = (q, t) => fuzzyMatch (q, t)?.score ?? -Infinity;
    check (score ("sg", "Show the graph") > score ("sg", "Messages"), "word starts beat letters inside words");
    check (score ("graph", "Show the graph") > score ("graph", "Generated code: rapid path"), "a consecutive run beats scattered letters");
    check (score ("cut", "cutoffFrequency") > score ("cut", "acoustic"), "an early match beats a late one");
    check (score ("of", "cutoffFrequency") < score ("fr", "cutoffFrequency"), "camelCase boundaries count as word starts");
    check (score ("reset", "Reset") > score ("reset", "Reset the graph view"), "a shorter text wins a tie");
    check (score ("graph", "Show the graph") > score ("graph", "Fit the graph to the view"), "a word-start match pays per skipped word, so the shorter title wins");

    const runs = splitByMatches ("Reset", [0, 2, 4]);
    check (runs.map (r => (r.isMatch ? "[" : "") + r.text + (r.isMatch ? "]" : "")).join ("") === "[R]e[s]e[t]", "splitByMatches gives runs for highlighting");
});

await test ("commands: registry, search and shortcuts", () =>
{
    const registry = new CommandRegistry ({ isMac: true });
    const ran = [];
    let enabled = false;

    const remove = registry.register ({ id: "a.reset", title: "Reset the patch", category: "Patch", shortcut: "R", run: () => ran.push ("reset") });
    registry.register ({ id: "a.graph", title: "Show the graph", category: "View", shortcut: "2", run: () => ran.push ("graph") });
    registry.register ({ id: "a.palette", title: "Show all commands", category: "Panel", shortcut: "Mod+K", run: () => ran.push ("palette") });
    registry.register ({ id: "a.off", title: "Disabled thing", shortcut: "D", isEnabled: () => enabled, run: () => ran.push ("off") });
    const removeProvider = registry.addProvider (() => [{ id: "endpoint:cutoff", kind: "parameter", title: "Cutoff", detail: "cutoff", category: "Parameter", run: () => ran.push ("cutoff") }]);

    const key = (k, mods = {}) => ({ key: k, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false, ...mods });

    check (registry.findShortcut (key ("r"))?.id === "a.reset", "a single-key shortcut is found");
    check (registry.findShortcut (key ("R", { shiftKey: true }))?.id === "a.reset", "...with Shift too, as before");
    check (registry.findShortcut (key ("r", { metaKey: true })) === null, "...but not with Cmd");
    check (registry.findShortcut (key ("r", { altKey: true })) === null, "...or Alt");
    check (registry.findShortcut (key ("k", { metaKey: true }))?.id === "a.palette", "Mod+K is Cmd+K on a Mac");
    check (registry.findShortcut (key ("k", { ctrlKey: true })) === null, "...and not Ctrl+K");
    check (shortcutMatches ("Mod+K", key ("k", { ctrlKey: true }), false), "Mod+K is Ctrl+K elsewhere");
    check (registry.findShortcut (key ("d")) === null, "a disabled command's shortcut does nothing");
    enabled = true;
    check (registry.findShortcut (key ("d"))?.id === "a.off", "...until it's enabled");

    check (formatShortcut ("Mod+K", true) === "⌘K" && formatShortcut ("Mod+K", false) === "Ctrl+K" && formatShortcut ("Space", false) === "Space",
           "shortcuts are shown the platform's way");

    check (registry.search ("graph")[0]?.command.id === "a.graph", "search ranks the best title first");
    const removeFit = registry.register ({ id: "a.fit", title: "Fit the graph to the view", category: "Graph", run: () => {} });
    check (registry.search ("graph")[0]?.command.id === "a.graph", "...and a category match doesn't outrank a title match");
    removeFit();
    check (registry.search ("@cut").length === 1 && registry.search ("@cut")[0].command.id === "endpoint:cutoff", "@ searches only endpoints and parameters");
    check (registry.search ("@reset").length === 0, "...and not actions");
    check (registry.search ("patch").some (r => r.command.id === "a.reset"), "the category is searched too");

    registry.run ("a.graph");
    check (ran.at (-1) === "graph" && registry.recent[0] === "a.graph", "run() runs it and remembers it");
    check (registry.search ("")[0]?.command.id === "a.graph", "recent commands come first with an empty query");
    check (registry.run ("endpoint:cutoff") && ran.at (-1) === "cutoff", "providers' commands can be run by id");

    remove();
    removeProvider();
    check (registry.findShortcut (key ("r")) === null && registry.search ("@cut").length === 0, "removing a command and a provider");
});

await test ("commands: the panel's shortcuts are the ones it always had", async () =>
{
    // (the keys handleKeyDown used to handle itself, and which command each must run)
    const expected = { " ": "audio.toggle", "r": "patch.reset", "1": "view.gui", "2": "view.graph", "3": "view.io",
                       "f": "graph.fit", "0": "graph.resetView", "g": "gui.float", "/": "console.filter" };

    const frame = document.createElement ("iframe");
    frame.src = "/vscode.html?fixture=Tremolo";
    frame.style.cssText = "width: 1000px; height: 700px; position: absolute; left: -2000px";
    document.body.append (frame);

    let panel = null;

    for (let i = 0; i < 200 && ! panel?.ctx?.commands?.get ("audio.toggle"); ++i)
    {
        await tick (25);
        panel = frame.contentDocument?.querySelector ("iframe")?.contentDocument?.querySelector ("cmaj-patch-panel");
    }

    for (let i = 0; i < 200 && ! panel?.store?.get ("connection"); ++i)
        await tick (25);

    check (!! panel?.ctx?.commands, "the panel has a command registry");

    if (panel?.ctx?.commands)
    {
        for (const [k, id] of Object.entries (expected))
            check (panel.ctx.commands.findShortcut ({ key: k, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false })?.id === id,
                   `"${k === " " ? "Space" : k}" runs ${id}`);
    }

    frame.remove();
});

//==============================================================================
await test ("cpu history: the ring keeps 120 s", () =>
{
    let now = 0;
    const h = new CPUHistory ({ now: () => now });
    check (cpuHistorySeconds === 120, "the history is 120 s long");

    for (let i = 0; i <= 200; ++i)
    {
        now = i * 1000;
        h.add (0.1, 0.2);
    }

    check (h.length === 121, `120 s of samples at 1 s are kept (${h.length})`);
    check (h.get (0).time === 80000 && h.latest.time === 200000, "the oldest is 120 s before the newest");

    // a fixed capacity: the oldest are overwritten, in order
    const small = new CPUHistory ({ capacity: 10, now: () => now });

    for (let i = 0; i < 25; ++i)
        small.add (i / 100, undefined, i);

    check (small.length === 10, "the capacity caps it");
    check (small.get (0).time === 15 && small.get (9).time === 24, "the ring wraps, oldest first");

    const seen = [];
    small.forEach ((t) => seen.push (t), 20);
    check (seen.join () === "20,21,22,23,24", "forEach from a time");
});

await test ("cpu history: stats over a window", () =>
{
    let now = 0;
    const h = new CPUHistory ({ now: () => now });
    check (h.getStats().current === null && h.getStats().count === 0, "no stats when empty");

    // 90 s ago (outside a 60 s window), then four reports in it
    h.add (0.9, 1.5, 10000);
    h.add (0.1, 0.2, 50000);
    h.add (0.2, 0.3, 60000);
    h.add (0.3, 1.2, 70000);
    h.add (0.4, 1.0, 80000);
    now = 100000;

    const s = h.getStats (60);
    check (s.count === 4, `the window has 4 reports (${s.count})`);
    check (Math.abs (s.current - 0.4) < 1e-6, "current is the latest level");
    check (Math.abs (s.average - 0.25) < 1e-6, `average is the mean level (${s.average})`);
    check (Math.abs (s.max - 1.2) < 1e-6, `max is the highest peak (${s.max})`);
    check (s.overBudget === 2, `peaks at or over 1.0 are over budget (${s.overBudget})`);

    const all = h.getStats (120);
    check (all.count === 5 && all.overBudget === 3 && Math.abs (all.max - 1.5) < 1e-6, "a longer window includes the older one");
});

await test ("cpu history: a missing peak is the level, and gaps", () =>
{
    let now = 0;
    const h = new CPUHistory ({ now: () => now });
    let notified = 0;
    const stop = h.addListener (() => ++notified);

    h.handleCPUInfo ({ level: 0.3 });     // (an older server)
    check (h.latest.peak === h.latest.level && Math.abs (h.latest.peak - 0.3) < 1e-6, "no peak: the level is used");

    h.handleCPUInfo ({ level: 1.1 });
    check (h.getStats().overBudget === 1, "a level-only report over budget counts");

    h.handleCPUInfo ({ level: 0.2, peak: 0.5 });
    check (Math.abs (h.latest.peak - 0.5) < 1e-6, "a peak is kept");
    h.handleCPUInfo ({});
    h.handleCPUInfo (undefined);
    check (h.length === 3, "a report without a level is ignored");

    now = 1000;
    h.addGap();
    h.addGap();
    check (h.length === 4 && Number.isNaN (h.get (3).level), "a gap is added once");
    check (h.latest.level === h.get (2).level, "latest skips the gap");
    check (h.getStats().count === 3, "stats skip the gap");
    check (notified === 4, `listeners hear each sample and gap (${notified})`);

    stop();
    h.clear();
    check (h.length === 0 && notified === 4, "clear empties it; a removed listener isn't called");
    h.addGap();
    check (h.length === 0, "no gap at the start");
});

await test ("cpu history: chart redraws only for new data, while active", () =>
{
    const scheduled = [];
    const scheduler = { schedule: (fn) => { scheduled.push (fn); return () => {}; }, cancel: () => {} };
    const h = new CPUHistory();
    const chart = new CPUHistoryChart (h, { scheduler });
    const initial = scheduled.length;

    h.add (0.1, 0.2);
    check (scheduled.length === initial + 1, "a new sample schedules a redraw");
    check (scheduled.every (fn => fn() === false), "a redraw doesn't ask to run again");

    chart.setActive (false);
    h.add (0.1, 0.2);
    check (scheduled.length === initial + 1, "inactive: no redraw");

    chart.setActive (true);
    check (scheduled.length === initial + 2, "re-activating redraws");
    chart.dispose();
    h.add (0.1, 0.2);
    check (scheduled.length === initial + 2, "disposed: no redraw");

    check (new CPUHistoryChart (h, { scheduler, fixedScale: true }).getScaleTop (0.05) === 1.25, "the fixed scale always shows the budget");
    check (new CPUHistoryChart (h, { scheduler, fixedScale: true }).getScaleTop (1.6) === 2, "and grows for peaks over it");
    check (new CPUHistoryChart (h, { scheduler, fixedScale: false }).getScaleTop (0.04) === 0.1, "the sparkline fits a light load");
});

await test ("cpu history: block budget and load text", () =>
{
    check (describeBlockBudget ({ rate: 48000, blockSize: 128 }) === "2.67 ms (128 frames at 48 kHz)", describeBlockBudget ({ rate: 48000, blockSize: 128 }));
    check (describeBlockBudget ({ rate: 44100, blockSize: 512 }) === "11.6 ms (512 frames at 44.1 kHz)", describeBlockBudget ({ rate: 44100, blockSize: 512 }));
    check (describeBlockBudget ({ rate: 48000 }) === "" && describeBlockBudget (null) === "", "no budget without both");
    check (formatLoad (0.034) === "3.4%" && formatLoad (1.234) === "123%" && formatLoad (null) === "–", "formatLoad");
});


//==============================================================================
await test ("midi controls: message encoding", () =>
{
    check (encodeMIDIMessage (0x90, 1, 60, 100) === 0x903c64, "note-on, channel 1");
    check (createNoteOn (10, 60, 100) === 0x993c64, "channel 10 goes in the status byte's low nibble");
    check (createNoteOn (16, 127, 127) === 0x9f7f7f, "channel 16, top note and velocity");
    check ((createNoteOn (1, 60, 0) & 0x7f) === 1, "a note-on's velocity is at least 1 (0 would be a note-off)");
    check (createNoteOff (3, 64) === 0x824040 && createNoteOff (3, 64, 0) === 0x824000, "note-off, release velocity 64 by default");
    check (createControlChange (1, 64, 127) === 0xb0407f && createControlChange (2, 1, 300) === 0xb1017f, "CCs, with the value clamped");
    check (encodeMIDIMessage (0x90, 0, 1, 1) >> 16 === 0x90 && encodeMIDIMessage (0x90, 99, 1, 1) >> 16 === 0x9f, "channels are clamped to 1-16");
    check (createPitchBend (1, 8192) === 0xe00040, "bend centre: LSB 0, MSB 64");
    check (createPitchBend (1, 0) === 0xe00000 && createPitchBend (2, 16383) === 0xe17f7f, "bend ends");
    check (createPitchBend (1, 8193) === 0xe00140, "the low 7 bits go first");

    const decode = (m) => ((m >> 8) & 0x7f) | ((m & 0x7f) << 7);
    check ([0, 1, 127, 128, 4097, 8192, 12345, 16383].every (v => decode (createPitchBend (1, v)) === v), "every 14-bit value round-trips");
    check (getPitchBendValue (0) === 8192 && getPitchBendValue (-1) === 0 && getPitchBendValue (1) === 16383 && getPitchBendValue (5) === 16383, "bend amount → 14 bits");
    check (getPitchBendValue (-0.5) === 4096 && getPitchBendValue (NaN) === pitchBendCentre, "half down; a bad amount is the centre");
});

await test ("midi controls: velocity from position, octaves, settings", () =>
{
    check (getVelocityFromPosition (0) === 1 && getVelocityFromPosition (1) === 127 && getVelocityFromPosition (0.5) === 64, "top soft, bottom loud");
    check (getVelocityFromPosition (-3) === 1 && getVelocityFromPosition (7) === 127, "clamped");
    check (getVelocityFromPosition (0.2) < getVelocityFromPosition (0.8), "monotonic");
    check (getRootNoteForOctave (0) === 36 && getRootNoteForOctave (2) === 60 && getRootNoteForOctave (-3) === 0 && getRootNoteForOctave (9) === 60, "root notes, kept in range");
    check (formatOctaveShift (1) === "+1" && formatOctaveShift (0) === "0" && formatOctaveShift (-2) === "−2", "octave text");

    const d = normaliseMIDIControlSettings (null);
    check (d.velocityMode === "fixed" && d.velocity === 100 && d.channel === 1 && d.octave === 0, "defaults");
    const n = normaliseMIDIControlSettings ({ velocityMode: "position", velocity: 500, channel: "7", octave: -9 });
    check (n.velocityMode === "position" && n.velocity === 127 && n.channel === 7 && n.octave === -3, "stored values are clamped");
    check (normaliseMIDIControlSettings ({ velocityMode: "x", velocity: "abc" }).velocityMode === "fixed"
           && normaliseMIDIControlSettings ({ velocity: "abc" }).velocity === 100, "junk falls back to the defaults");
});

await test ("midi controls: panic messages", () =>
{
    const m = getPanicMessages ([{ channel: 2, note: 60 }, { channel: 1, note: 64 }]);
    check (m.length === 2 + 16 * 3, "a note-off per held note, then three CCs on each of 16 channels");
    check (m[0] === 0x813c00 && m[1] === 0x804000, "the note-offs first, on their own channels");
    const ccs = m.slice (2);
    check (ccs.every (x => (x >> 20) === 0xb && (x & 0x7f) === 0), "the rest are CCs with value 0");
    check ([0, 1, 2].map (i => (ccs[i] >> 8) & 0x7f).join () === "64,123,120", "sustain off, all notes off, all sound off");
    check (new Set (ccs.map (x => (x >> 16) & 0xf)).size === 16, "on all 16 channels");
    check (getPanicMessages().length === 48, "with no held notes, just the CCs");
});

await test ("midi controls: the widget's keyboard, channel, panic and settings", async () =>
{
    const sent = [];
    const patchConnection = { sendMIDIInputEvent: (id, m) => sent.push ({ id, m }), addEndpointListener() {}, removeEndpointListener() {} };
    const group = { subscribe: () => () => {} };
    const scopes = new PanelSettingsScopes ({ getItem: () => null, setItem: () => {}, removeItem: () => {} });
    scopes.setPatch ("/x/Synth.cmajorpatch");
    const store = new Store ({ patchSettings: null });
    const frames = [];
    const scheduler = { schedule: (fn) => { frames.push (fn); return () => {}; }, cancel: (fn) => { const i = frames.indexOf (fn); if (i >= 0) frames.splice (i, 1); } };

    const w = new MIDIInputWidget ({ endpointID: "midiIn", endpointType: "event", purpose: "midi in" },
                                   { patchConnection, group, settings: scopes, store, scheduler, size: "full" });
    document.body.append (w.element);
    const kb = w.keyboard, c = w.controls;
    check (!! c && c.element.parentElement === w.bodyElement, "the full widget has the controls");

    kb.dispatchEvent (new CustomEvent ("note-down", { detail: { note: 60 } }));
    check (sent.at (-1).m === 0x903c64 && sent.at (-1).id === "midiIn", "a key plays at the fixed velocity on channel 1");

    c.channelSelect.value = "5";
    c.channelSelect.dispatchEvent (new Event ("change"));
    kb.dispatchEvent (new CustomEvent ("note-down", { detail: { note: 62 } }));
    check (sent.at (-1).m === 0x943e64, "the channel applies to the keyboard's notes");
    kb.dispatchEvent (new CustomEvent ("note-up", { detail: { note: 60 } }));
    check (sent.at (-1).m === 0x803c40, "a note-off goes to the channel its note-on went to");

    c.velocityInput.value = "30";
    c.velocityInput.dispatchEvent (new Event ("change"));
    kb.dispatchEvent (new CustomEvent ("note-down", { detail: { note: 64 } }));
    check (sent.at (-1).m === 0x94401e, "the fixed velocity");

    // octave: shifts root-note, and the keyboard re-draws its keys
    c.octaveUp.click();
    check (kb.getAttribute ("root-note") === "48" && !! kb.shadowRoot.getElementById ("note108") && ! kb.shadowRoot.getElementById ("note36"),
           "octave up shifts root-note by 12 and redraws the keys");
    check (c.octaveText.textContent === "+1", "the octave readout");
    for (let i = 0; i < 5; ++i) c.octaveUp.click();
    check (kb.getAttribute ("root-note") === "60" && c.octaveUp.disabled, "it stops at the top");

    // settings: saved per patch and endpoint, and another instance follows
    check (JSON.stringify (loadMIDIControlSettings (scopes, "midiIn")) === JSON.stringify ({ velocityMode: "fixed", velocity: 30, channel: 5, octave: 2 }), "saved");
    const other = new MIDIControls ({ endpointID: "midiIn", patchConnection, settings: scopes, scheduler });
    check (other.channelSelect.value === "5", "a new instance reads them");
    c.octaveDown.click();
    check (other.octaveText.textContent === "+1", "and follows changes made in another");
    other.dispose();

    // panic
    sent.length = 0;
    c.setSustain (true);
    c.setBend (0.5);
    c.panic();
    const panic = sent.slice (2).map (s => s.m);
    const held = panic.filter (m => (m >> 20) === 8);
    check (held.length === 2 && held.includes (0x843e00) && held.includes (0x844000), "panic: note-offs for the held notes (62 and 64, on channel 5)");
    check (panic.filter (m => (m >> 20) === 0xb).length === 48, "...then the 48 CCs");
    check (! c.isSustainOn && c.sustainSwitch.getAttribute ("aria-checked") === "false", "...and releases the sustain switch");
    check (c.bendAmount === 0 && sent.at (-1).m === createPitchBend (5, 8192), "...and centres the bend");
    check (c.heldNotes.size === 0, "nothing is held after it");

    // bend: springs back through the scheduler, ending at the centre
    sent.length = 0;
    c.bendStrip.element.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowRight" }));
    c.bendStrip.element.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowRight" }));
    check (c.bendAmount === 0.25 && c.bendStrip.element.getAttribute ("aria-valuenow") === String (2048), "arrows bend it");
    c.bendStrip.element.dispatchEvent (new KeyboardEvent ("keyup", { key: "ArrowRight" }));
    let n = 0;
    while (frames.length && n++ < 200) { const fn = frames.shift(); if (fn (0, 16)) frames.push (fn); }
    check (c.bendAmount === 0 && sent.at (-1).m === createPitchBend (5, 8192) && sent.length > 3, `releasing springs back, sending the values on the way (${sent.length} messages)`);

    // mod
    sent.length = 0;
    c.modStrip.element.dispatchEvent (new KeyboardEvent ("keydown", { key: "End" }));
    check (sent.at (-1).m === createControlChange (5, 1, 127) && c.modStrip.element.getAttribute ("aria-valuenow") === "127", "mod: End sends CC1 127");
    c.modStrip.element.dispatchEvent (new KeyboardEvent ("keyup", { key: "End" }));
    check (c.modValue === 127, "...and stays there");

    // commands
    const commands = createMIDICommands ({ getMIDIInputs: () => [{ endpointID: "midiIn" }], getPatchConnection: () => patchConnection, settings: scopes });
    const none = createMIDICommands ({ getMIDIInputs: () => [], getPatchConnection: () => patchConnection, settings: scopes });
    check (commands.every (x => x.isEnabled()) && none.every (x => ! x.isEnabled()), "the commands are disabled without a MIDI input");
    commands.find (x => x.id === "midi.octaveDown").run();
    check (kb.getAttribute ("root-note") === "36", "octave down, through the command");
    sent.length = 0;
    commands.find (x => x.id === "midi.panic").run();
    check (sent.length === 48, "panic, through the command");

    w.dispose();
    sent.length = 0;
    commands.find (x => x.id === "midi.panic").run();
    check (sent.length === 48, "without a widget, panic still sends the CCs");
});

//==============================================================================
// Audio input test signals (widgets/signal-generator.js, audio-input-source.js)

await test ("signal generator: the log slider mapping", () =>
{
    check (sliderPositionToFrequency (0) === 20 && sliderPositionToFrequency (1) === 20000, "the ends are exactly 20 Hz and 20 kHz");
    check (frequencyToSliderPosition (20) === 0 && Math.abs (frequencyToSliderPosition (20000) - 1) < 1e-12, "...and map back to 0 and 1");
    check (Math.abs (frequencyToSliderPosition (632.455532) - 0.5) < 1e-6, "it's logarithmic: the middle is the geometric mean");
    check (frequencyToSliderPosition (5) === 0 && frequencyToSliderPosition (40000) === 1, "values outside the range stick at the ends");

    let worst = 0;

    for (let i = 0; i <= 240; ++i)
    {
        const hz = sliderPositionToFrequency (i / 240);
        worst = Math.max (worst, Math.abs (frequencyToSliderPosition (hz) * 240 - i));
    }

    check (worst < 0.25, `every one of the 240 slider steps comes back to itself (the 3-figure rounding moves it by at most ${worst.toFixed (3)} of a step)`);
    check (sliderPositionToFrequency (frequencyToSliderPosition (440)) === 440 && sliderPositionToFrequency (0.37) === roundToSignificantFigures (sliderPositionToFrequency (0.37)),
           "values come out at 3 significant figures (440 stays 440)");

    const level = generatorSliderRanges.level;
    check (sliderPositionToValue (0, level) === -80 && sliderPositionToValue (1, level) === 0 && sliderPositionToValue (0.85, level) === -12
           && valueToSliderPosition (-12, level) === 0.85, "the level slider is linear in dB, in 0.5 dB steps");
    check (roundToSignificantFigures (1234.5) === 1230 && roundToSignificantFigures (0.012345) === 0.0123, "significant figures");
});

await test ("signal generator: formatting and parsing", () =>
{
    check (formatFrequency (440) === "440 Hz" && formatFrequency (27.5) === "27.5 Hz" && formatFrequency (2) === "2 Hz" && formatFrequency (0.5) === "0.5 Hz",
           `Hz (${formatFrequency (27.5)}, ${formatFrequency (0.5)})`);
    check (formatFrequency (1250) === "1.25 kHz" && formatFrequency (20000) === "20 kHz" && formatFrequency (12500) === "12.5 kHz" && formatFrequency (1000) === "1 kHz",
           `kHz (${formatFrequency (1250)}, ${formatFrequency (20000)})`);
    check (formatGeneratorLevel (-12) === "-12.0 dBFS" && formatGeneratorLevel (0) === "0.0 dBFS" && formatGeneratorLevel (-120) === "-120.0 dBFS", "dBFS");
    check (formatSeconds (5) === "5 s" && formatSeconds (0.5) === "0.5 s" && formatSeconds (120) === "120 s", "seconds");

    check (parseGeneratorValue ("440", "frequency") === 440 && parseGeneratorValue ("1.5k", "frequency") === 1500 && parseGeneratorValue ("1.25 kHz", "frequency") === 1250
           && parseGeneratorValue ("880hz", "startFrequency") === 880, "frequencies, with or without units");
    check (parseGeneratorValue ("-6", "level") === -6 && parseGeneratorValue ("-6 dBFS", "level") === -6 && parseGeneratorValue ("−3dB", "level") === -3, "levels");
    check (parseGeneratorValue ("250 ms", "sweepSeconds") === 0.25 && parseGeneratorValue ("10s", "sweepSeconds") === 10, "seconds");
    check (parseGeneratorValue ("abc", "frequency") === null && parseGeneratorValue ("", "level") === null && parseGeneratorValue ("5 s", "frequency") === null
           && parseGeneratorValue ("3 kHz", "level") === null, "nonsense and the wrong unit are rejected");
    check (parseGeneratorValue (formatFrequency (1250), "frequency") === 1250 && parseGeneratorValue (formatGeneratorLevel (-12), "level") === -12
           && parseGeneratorValue (formatSeconds (0.5), "sweepSeconds") === 0.5, "the formatted values parse back");
});

await test ("signal generator: merging and clamping, as the server does", () =>
{
    check (JSON.stringify (normaliseGeneratorSettings ({})) === JSON.stringify (defaultGeneratorSettings)
           && JSON.stringify (normaliseGeneratorSettings (null)) === JSON.stringify (defaultGeneratorSettings), "nothing gives the defaults");
    const s = mergeGeneratorSettings (defaultGeneratorSettings, { type: "sweep", level: 6, frequency: 0.2, endFrequency: 96000, sweepSeconds: 0.01, impulseRate: 5000 });
    check (s.type === "sweep" && s.level === 0 && s.frequency === 1 && s.endFrequency === 48000 && s.sweepSeconds === 0.1 && s.impulseRate === 1000, "values are clamped to the server's limits");
    const t = mergeGeneratorSettings (s, { type: "triangle", level: "loud", frequency: NaN, startFrequency: Infinity, sweepSeconds: null });
    check (JSON.stringify (t) === JSON.stringify (s), "an unknown type, and non-numeric or non-finite values, leave the field as it was");
    check (mergeGeneratorSettings (s, { level: -120.5 }).level === -120 && mergeGeneratorSettings (s, { level: -3 }).type === "sweep", "a merge keeps the other fields");
    check (Object.keys (mergeGeneratorSettings ({ junk: 1 }, {})).length === 7, "the result always has the seven fields, and nothing else");
    check (JSON.stringify (getChangedGeneratorFields (defaultGeneratorSettings, { ...defaultGeneratorSettings, level: -6 })) === '{"level":-6}', "the changed fields");
    check (generatorFieldsForType.sine.join() === "frequency" && generatorFieldsForType.sweep.length === 3 && generatorFieldsForType.white.length === 0, "the fields of each type");
});

await test ("signal generator: the throttle", () =>
{
    let now = 1000;
    const sent = [];
    const scheduler = makeManualScheduler();
    const sender = new ThrottledSender ((c) => sent.push (c), { scheduler, now: () => now, intervalMs: 33 });

    sender.queue ({ level: -10 });
    check (sent.length === 1 && sent[0].level === -10 && ! scheduler.hasFrameRequested(), "the first change goes at once");

    for (let i = 0; i < 10; ++i) { now += 2; sender.queue ({ level: -10 - i }); }
    check (sent.length === 1 && scheduler.hasFrameRequested(), "the next ones wait");
    scheduler.frame (now);
    check (sent.length === 1, "...until the interval has passed");
    now += 20;
    scheduler.frame (now);
    check (sent.length === 2 && sent[1].level === -19 && ! scheduler.hasFrameRequested(), "then the latest value goes, and it idles");

    now += 5;
    sender.queue ({ frequency: 100 });
    sender.queue ({ level: -3 });
    now += 40;
    scheduler.frame (now);
    check (sent.length === 3 && JSON.stringify (sent[2]) === '{"frequency":100,"level":-3}', "changes to different fields are merged");

    now += 5;
    sender.queue ({ level: -1 });
    sender.cancel();
    now += 40;
    scheduler.frame (now);
    check (sent.length === 3 && ! scheduler.hasFrameRequested(), "cancel() drops what's waiting");
    sender.queue ({ level: -2 });
    sender.flush();
    check (sent.length === 4, "flush() sends at once");
});

/// A session with just the audio input methods, which records what's sent and lets a test play the server
function makeGeneratorSession ({ withGenerator = true } = {})
{
    const listeners = new Map();
    const session = {
        sent: [],
        listen: (type, fn) => { if (! listeners.has (type)) listeners.set (type, new Set()); listeners.get (type).add (fn); },
        unlisten: (type, fn) => listeners.get (type)?.delete (fn),
        emit: (type, message) => { for (const fn of [...(listeners.get (type) ?? [])]) fn (message); },
        count: (type) => listeners.get (type)?.size ?? 0,
        setAudioInputSource (id, mute, file)     { this.sent.push ({ type: "source", id, mute, file }); },
        addAudioInputModeListener (id, fn)       { this.listen ("mode_" + id, fn); },
        removeAudioInputModeListener (id, fn)    { this.unlisten ("mode_" + id, fn); },
        requestAudioInputMode (id)               { this.sent.push ({ type: "req_mode", id }); },
    };

    if (withGenerator)
        Object.assign (session, {
            setAudioInputGenerator (id, settings)      { this.sent.push ({ type: "generator", id, settings }); },
            addAudioInputGeneratorListener (id, fn)    { this.listen ("gen_" + id, fn); },
            removeAudioInputGeneratorListener (id, fn) { this.unlisten ("gen_" + id, fn); },
            requestAudioInputGenerator (id)            { this.sent.push ({ type: "req_generator", id }); },
        });

    return session;
}

await test ("audio input: Signal mode", async () =>
{
    const session = makeGeneratorSession();
    const data = new Map();
    const scopes = new PanelSettingsScopes ({ getItem: k => data.get (k) ?? null, setItem: (k, v) => data.set (k, v), removeItem: k => data.delete (k) });
    scopes.setPatch ("/x/Signal.cmajorpatch");
    const { hub, options } = makeWidgetOptions ({ session, settings: scopes, generatorReplyTimeoutMs: 30 });
    const endpoint = { endpointID: "in", purpose: "audio in", endpointType: "stream", numAudioChannels: 2, dataType: { type: "float32<2>" } };
    const widget = new AudioInputWidget (endpoint, options);
    document.body.append (widget.element);
    const form = widget.generatorForm;
    const signalButton = widget.buttons.find (b => b.dataset.mode === "generator");
    const generatorSends = () => session.sent.filter (m => m.type === "generator");
    const reply = (settings) => session.emit ("gen_in", mergeGeneratorSettings (defaultGeneratorSettings, settings));

    check (session.sent.some (m => m.type === "req_generator") && session.sent.some (m => m.type === "req_mode"), "it asks for the mode and the generator settings");
    check (signalButton && widget.buttons.map (b => b.textContent).join() === "Live,Signal,File…,Mute", "the source buttons are Live, Signal, File…, Mute");
    check (form && form.element.hidden, "the form is hidden until the input plays a generator");

    // the server's remembered settings fill the form before Signal is chosen
    reply ({ type: "sweep", startFrequency: 100 });
    check (form.settings.type === "sweep" && form.settings.startFrequency === 100 && getGeneratorSupport (session) === true,
           "the server's remembered settings fill the form (and show it supports generators)");

    signalButton.click();
    check (generatorSends().length === 1 && generatorSends()[0].settings.type === "sweep" && generatorSends()[0].settings.startFrequency === 100,
           "choosing Signal sends the remembered settings");
    session.emit ("mode_in", "generator");
    reply ({ type: "sweep", startFrequency: 100 });
    check (! form.element.hidden && widget.element.dataset.inputMode === "generator" && signalButton.getAttribute ("aria-pressed") === "true",
           "the generator mode shows the form, and presses Signal");

    const visibleFields = () => [...form.rows].filter (([, r]) => ! r.row.hidden).map (([f]) => f).join();
    check (visibleFields() === "level,startFrequency,endFrequency,sweepSeconds", `a sweep shows the level, start, end and length (${visibleFields()})`);

    // changing the type sends just that
    session.sent.length = 0;
    form.typeSelect.value = "sine";
    form.typeSelect.dispatchEvent (new Event ("change"));
    check (generatorSends().length === 1 && JSON.stringify (generatorSends()[0].settings) === '{"type":"sine"}', "changing the type sends only the type");
    check (visibleFields() === "level,frequency", "...and shows the sine's fields");

    // a slider: only its field, throttled
    const slider = form.rows.get ("frequency").slider;
    await tick (40);
    session.sent.length = 0;

    for (let i = 0; i < 12; ++i)
    {
        slider.value = String (100 + i);
        slider.dispatchEvent (new Event ("input"));
    }

    check (generatorSends().length === 1 && Object.keys (generatorSends()[0].settings).join() === "frequency", "a burst of slider moves sends one message at once, with only the frequency");
    await tick (40);
    options.scheduler.frame (performance.now());
    const last = generatorSends().at (-1).settings.frequency;
    check (generatorSends().length === 2 && last === sliderPositionToFrequency (111 / 240), `...then the last value (${last} Hz), so ${generatorSends().length} messages for 12 moves`);
    check (form.rows.get ("frequency").input.value === formatFrequency (last) && slider.getAttribute ("aria-valuetext") === formatFrequency (last), "the readout follows");

    // typing a value
    const input = form.rows.get ("frequency").input;
    await tick (40);
    session.sent.length = 0;
    input.value = "1.5k";
    input.dispatchEvent (new Event ("change"));
    check (generatorSends().length === 1 && JSON.stringify (generatorSends()[0].settings) === '{"frequency":1500}', "typing 1.5k sends 1500 Hz");
    input.value = "rubbish";
    input.dispatchEvent (new Event ("change"));
    check (generatorSends().length === 1 && input.value === "1.5 kHz", "nonsense is put back");

    // another client's change shows without being sent back (this fake server never echoes, so
    // the form holds the user's own unconfirmed changes for localChangeHoldMs first)
    await tick (localChangeHoldMs + 50);
    session.sent.length = 0;
    session.emit ("mode_in", "generator");
    reply ({ type: "pink", level: -20 });
    await tick (40);
    options.scheduler.frame (performance.now());
    check (form.settings.type === "pink" && form.typeSelect.value === "pink" && form.rows.get ("level").input.value === "-20.0 dBFS" && visibleFields() === "level",
           "a broadcast from the server updates the form");
    check (generatorSends().length === 0, "...and isn't echoed back");
    check (loadSavedGeneratorSettings (scopes, "in")?.type === "pink", "the settings are saved for the patch");

    // while a slider is dragged, the server's (lagging) echoes don't move it
    const levelSlider = form.rows.get ("level").slider;
    levelSlider.dispatchEvent (new PointerEvent ("pointerdown", { pointerId: 3, button: 0, bubbles: true }));
    levelSlider.value = "150";
    levelSlider.dispatchEvent (new Event ("input"));
    reply ({ type: "pink", level: -30 });
    check (levelSlider.value === "150" && form.settings.level === -5, `an echo doesn't move a dragged slider (${levelSlider.value}, ${form.settings.level})`);
    levelSlider.dispatchEvent (new PointerEvent ("pointerup", { pointerId: 3, button: 0, bubbles: true }));
    form.stopDrag?.();

    // Live: back, and a change that's still waiting isn't sent after it
    await tick (40);
    session.sent.length = 0;
    levelSlider.value = "140";
    levelSlider.dispatchEvent (new Event ("input"));
    levelSlider.value = "141";
    levelSlider.dispatchEvent (new Event ("input"));
    widget.buttons.find (b => b.dataset.mode === "live").click();
    session.emit ("mode_in", "live");
    await tick (40);
    options.scheduler.frame (performance.now());
    check (generatorSends().length === 1 && session.sent.at (-1).type === "source" && session.sent.at (-1).mute === false,
           "Live goes back, dropping a change that was still waiting");
    check (form.element.hidden, "...and hides the form");

    // the compact one says what it's playing
    const compact = new AudioInputWidget (endpoint, { ...options, size: "compact" });
    session.emit ("mode_in", "generator");
    reply ({ type: "sine", level: -6 });
    check (! compact.generatorForm && compact.formatLevelText ("-6.0 dB") === "sine -6.0 dB", `the compact widget shows the signal's type (${compact.formatLevelText ("-6.0 dB")})`);
    reply ({ type: "silence" });
    check (compact.formatLevelText ("-∞ dB") === "silence", "...or that it's silent");
    compact.dispose();

    // commands
    const commands = createAudioInputCommands ({ getAudioInputs: () => [endpoint], getSession: () => session, settings: scopes });
    const none = createAudioInputCommands ({ getAudioInputs: () => [], getSession: () => session, settings: scopes });
    check (commands.every (c => c.isEnabled()) && none.every (c => ! c.isEnabled()), "the commands are disabled without an audio input");
    session.sent.length = 0;
    commands.find (c => c.id === "audio.useTestSignal").run();
    check (generatorSends().length === 1 && generatorSends()[0].settings.type === "silence", "the test signal command uses the widget's last settings");
    commands.find (c => c.id === "audio.useLiveInput").run();
    check (session.sent.at (-1).type === "source" && session.sent.at (-1).mute === false, "the live input command goes live");

    widget.dispose();
    check (session.count ("gen_in") === 0 && session.count ("mode_in") === 0, "disposing them removes their listeners");
    session.sent.length = 0;
    commands.find (c => c.id === "audio.useTestSignal").run();
    check (generatorSends()[0]?.settings.type === "silence", "without a widget, the command sends the saved settings");
    hub.dispose();
});

await test ("audio input: an older server", async () =>
{
    const session = makeGeneratorSession();
    const { hub, options } = makeWidgetOptions ({ session, generatorReplyTimeoutMs: 30 });
    const endpoint = { endpointID: "in", purpose: "audio in", endpointType: "stream", numAudioChannels: 1, dataType: { type: "float32" } };
    const widget = new AudioInputWidget (endpoint, options);
    document.body.append (widget.element);
    const signalButton = widget.buttons.find (b => b.dataset.mode === "generator");

    // it answers the mode request, but not the generator one
    session.emit ("mode_in", "live");
    check (! signalButton.disabled, "Signal is offered at first");
    await tick (60);
    check (signalButton.disabled && getGeneratorSupport (session) === false && widget.generatorNote.hidden, "no answer in time: Signal is disabled (quietly)");
    signalButton.click();
    check (! session.sent.some (m => m.type === "generator"), "...and does nothing");

    const commands = createAudioInputCommands ({ getAudioInputs: () => [endpoint], getSession: () => session, settings: null });
    check (! commands[0].isEnabled() && commands[1].isEnabled(), "the test signal command is disabled, the live one isn't");

    // a server that does answer later is believed
    session.emit ("gen_in", defaultGeneratorSettings);
    check (! signalButton.disabled && getGeneratorSupport (session) === true, "a later answer enables it again");

    widget.dispose();

    // Signal chosen before the server has been heard from: it takes it as "live"
    const session2 = makeGeneratorSession();
    const w1 = new AudioInputWidget (endpoint, { ...options, session: session2 });
    w1.buttons.find (b => b.dataset.mode === "generator").click();
    check (session2.sent.some (m => m.type === "generator"), "Signal before any reply sends the request");
    session2.emit ("mode_in", "live");
    await tick (60);
    check (w1.generatorButton.disabled && ! w1.generatorNote.hidden && w1.generatorNote.textContent.includes ("can't play test signals"),
           "when it ends up live, Signal is disabled, with a note saying why");
    w1.dispose();

    // a session with no generator methods at all has no Signal button
    const oldSession = makeGeneratorSession ({ withGenerator: false });
    const w2 = new AudioInputWidget (endpoint, { ...options, session: oldSession });
    check (w2.buttons.length === 3 && ! w2.generatorForm, "a session without the generator methods gets no Signal button");
    w2.dispose();
    hub.dispose();
});

//==============================================================================
// core/fft.js and core/audio-streams.js (feature 5, spectrum analyser)
const { fft, isValidFFTSize, getHannWindow, getCoherentGain, amplitudeToDB, RealSpectrum, SpectrumAnalyser, spectrumFloorDB }
    = await import ("/panel_api/core/fft.js");
const { fullAudioGranularity, fullAudioStreamOptions, subscribeToFullAudio, normaliseFullAudioMessage }
    = await import ("/panel_api/core/audio-streams.js");

/// A deterministic pseudo-random sequence in [-1, 1)
function makeRandom (seed)
{
    let s = seed >>> 0;
    return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 2147483648 - 1; };
}

function makeSine (numFrames, cyclesPerFrame, amplitude = 1, startFrame = 0)
{
    return Float64Array.from ({ length: numFrames }, (_, i) => amplitude * Math.sin (2 * Math.PI * cyclesPerFrame * (startFrame + i)));
}

await test ("fft: matches a naive DFT, inverts, and keeps Parseval", () =>
{
    const random = makeRandom (1234);

    for (const size of [32, 64, 256, 1024])
    {
        const re = Float64Array.from ({ length: size }, random), im = Float64Array.from ({ length: size }, random);
        const inRe = re.slice(), inIm = im.slice();
        fft (re, im);

        let maxError = 0;

        for (let k = 0; k < size; ++k)
        {
            let sr = 0, si = 0;

            for (let n = 0; n < size; ++n)
            {
                const angle = -2 * Math.PI * ((k * n) % size) / size;
                sr += inRe[n] * Math.cos (angle) - inIm[n] * Math.sin (angle);
                si += inRe[n] * Math.sin (angle) + inIm[n] * Math.cos (angle);
            }

            maxError = Math.max (maxError, Math.abs (sr - re[k]), Math.abs (si - im[k]));
        }

        check (maxError < 1e-9, `size ${size}: matches the DFT (max error ${maxError.toExponential (2)})`);

        let timeEnergy = 0, freqEnergy = 0;

        for (let i = 0; i < size; ++i)
        {
            timeEnergy += inRe[i] * inRe[i] + inIm[i] * inIm[i];
            freqEnergy += re[i] * re[i] + im[i] * im[i];
        }

        check (Math.abs (timeEnergy - freqEnergy / size) < 1e-9 * timeEnergy, `size ${size}: Parseval`);

        fft (re, im, true);
        check (re.every ((v, i) => Math.abs (v - inRe[i]) < 1e-12) && im.every ((v, i) => Math.abs (v - inIm[i]) < 1e-12),
               `size ${size}: the inverse gives the input back`);
    }

    // Float32 arrays and the largest size
    const size = 32768;
    const re = new Float32Array (size), im = new Float32Array (size);
    re[1] = 1;
    fft (re, im);
    check (Math.abs (re[size / 4]) < 1e-6 && Math.abs (im[size / 4] + 1) < 1e-6, "Float32Array at size 32768: an impulse at n=1 gives e^(-2πik/N)");

    check (isValidFFTSize (32) && isValidFFTSize (32768) && ! isValidFFTSize (16) && ! isValidFFTSize (65536) && ! isValidFFTSize (1000),
           "isValidFFTSize: powers of two from 32 to 32768");

    let threw = false;
    try { fft (new Float64Array (100), new Float64Array (100)); } catch { threw = true; }
    check (threw, "a size that isn't a power of two throws");
});

await test ("fft: Hann window and gain correction", () =>
{
    const size = 4096;
    const window = getHannWindow (size);
    check (getHannWindow (size) === window, "the window is cached");
    check (Math.abs (getCoherentGain (window) - 0.5) < 1e-12 && window[0] === 0 && Math.abs (window[size / 2] - 1) < 1e-12,
           "periodic Hann: coherent gain 0.5, 0 at the start, 1 in the middle");

    const spectrum = new RealSpectrum (size);
    const amplitudes = new Float64Array (spectrum.binCount);
    check (spectrum.binCount === size / 2 + 1, "binCount is size / 2 + 1");

    spectrum.getAmplitudes (makeSine (size, 100 / size), amplitudes);
    check (Math.abs (amplitudeToDB (amplitudes[100])) < 0.1, `a full-scale sine at bin 100 reads 0 dBFS (${amplitudeToDB (amplitudes[100]).toFixed (3)})`);
    check (amplitudes.every ((a, k) => k === 100 || a <= amplitudes[100] * 0.51), "...and it's the biggest bin (Hann's neighbours are −6 dB)");
    check (amplitudeToDB (amplitudes[110]) < -150, "...with nothing far from it");

    spectrum.getAmplitudes (makeSine (size, 37 / size, 0.25), amplitudes);
    check (Math.abs (amplitudeToDB (amplitudes[37]) - amplitudeToDB (0.25)) < 0.1, "a −12 dB sine at bin 37 reads −12 dB");

    spectrum.getAmplitudes (makeSine (size, 200.5 / size), amplitudes);
    const best = Math.max (amplitudeToDB (amplitudes[200]), amplitudeToDB (amplitudes[201]));
    check (best < 0 && best > -1.5, `a sine half way between bins reads within the Hann scalloping loss (${best.toFixed (3)} dB)`);

    // DC and Nyquist aren't doubled
    spectrum.getAmplitudes (new Float64Array (size).fill (0.5), amplitudes);
    check (Math.abs (amplitudeToDB (amplitudes[0]) - amplitudeToDB (0.5)) < 0.01, `a DC offset of 0.5 reads −6.02 dB at bin 0 (${amplitudeToDB (amplitudes[0]).toFixed (3)})`);
    check (amplitudes.slice (3).every (a => amplitudeToDB (a) < -150), "...and nothing above bin 2");

    spectrum.getAmplitudes (Float64Array.from ({ length: size }, (_, i) => (i & 1) ? -0.5 : 0.5), amplitudes);
    check (Math.abs (amplitudeToDB (amplitudes[size / 2]) - amplitudeToDB (0.5)) < 0.01, "a signal at Nyquist reads its level at the last bin");

    // a ring buffer read from an offset is the same as the unwrapped frame
    const frame = makeSine (size, 300.3 / size, 0.7);
    const ring = new Float64Array (size);
    const offset = 1234;
    frame.forEach ((v, i) => ring[(i + offset) % size] = v);
    const direct = spectrum.getPowers (frame), fromRing = spectrum.getPowers (ring, undefined, offset);
    check (direct.every ((p, k) => Math.abs (p - fromRing[k]) < 1e-12), "reading a ring from its oldest sample matches the unwrapped frame");

    check (amplitudeToDB (0) === spectrumFloorDB && amplitudeToDB (-0) === spectrumFloorDB, "−Infinity dB is clamped to the floor");
});

await test ("fft: SpectrumAnalyser", () =>
{
    const size = 1024, sampleRate = 48000;
    const bin = 64, cyclesPerFrame = bin / size;
    const analyser = new SpectrumAnalyser ({ fftSize: size, sampleRate, numChannels: 2, peakDecayDBPerSecond: 20 });
    check (analyser.binCount === size / 2 + 1 && analyser.getBinFrequency (bin) === bin * sampleRate / size
             && analyser.getBinForFrequency (analyser.getBinFrequency (bin)) === bin, "bin frequencies");
    check (! analyser.isFull && analyser.levelsDB.every (v => v === spectrumFloorDB), "starts empty, at the floor");

    // blocks of odd sizes that straddle the ring, including one longer than the ring
    const signal = makeSine (5000, 10.37 / size, 0.6);
    const blockSizes = [37, 1, 500, 1001, 1203, 211];
    let position = 0;

    for (const n of blockSizes.concat ([signal.length]))
    {
        const end = Math.min (signal.length, position + n);
        const block = Float32Array.from (signal.subarray (position, end));
        analyser.addBlock ([block, block]);
        position = end;

        if (position === signal.length) break;
    }

    check (analyser.isFull, "full after enough frames");
    analyser.update();

    const reference = new RealSpectrum (size).getPowers (Float32Array.from (signal.subarray (signal.length - size)));
    check (reference.every ((p, k) => Math.abs (Math.max (spectrumFloorDB, 10 * Math.log10 (p)) - analyser.currentDB[k]) < 1e-3),
           "after odd-sized blocks, the spectrum is that of the last fftSize frames");

    // a block longer than the ring keeps only its last frames
    const long = Float32Array.from (signal);
    const analyser2 = new SpectrumAnalyser ({ fftSize: size, sampleRate });
    analyser2.addBlock ([long]);
    analyser2.update();
    check (analyser2.currentDB.every ((v, k) => Math.abs (v - analyser.currentDB[k]) < 1e-3), "...and so does one block longer than the ring");

    // channels: average power, or one channel
    const tone = Float32Array.from (makeSine (size, cyclesPerFrame));
    const silence = new Float32Array (size);
    const averaging = new SpectrumAnalyser ({ fftSize: size, sampleRate, numChannels: 2 });
    averaging.addBlock ([tone, silence]);
    averaging.update();
    check (Math.abs (averaging.levelsDB[bin] + 3.0103) < 0.01, "average mode: a tone on one of two channels reads −3 dB");
    averaging.addBlock ([tone, tone]);
    averaging.update();
    check (Math.abs (averaging.levelsDB[bin]) < 0.01, "...and on both, 0 dB");
    averaging.channelMode = 1;
    averaging.addBlock ([tone, silence]);
    averaging.update();
    check (averaging.levelsDB[bin] === spectrumFloorDB, "choosing channel 1 shows only that channel");

    const growing = new SpectrumAnalyser ({ fftSize: size, numChannels: 1 });
    growing.addBlock ([tone, tone, tone]);
    check (growing.numChannels === 3, "a block with more channels grows numChannels");
    growing.update();
    check (Math.abs (growing.levelsDB[bin]) < 0.01, "...and reads them all");

    // averaging with an alpha converges geometrically
    const quiet = Float32Array.from (makeSine (size, cyclesPerFrame, 0.1));
    const alphaAnalyser = new SpectrumAnalyser ({ fftSize: size, sampleRate, averagingAlpha: 0.25 });
    alphaAnalyser.addBlock ([tone]);
    alphaAnalyser.update();
    check (Math.abs (alphaAnalyser.levelsDB[bin]) < 0.01, "the first frame isn't averaged against silence");

    const levels = [];

    for (let i = 0; i < 40; ++i)
    {
        alphaAnalyser.addBlock ([quiet]);
        alphaAnalyser.update();
        levels.push (alphaAnalyser.levelsDB[bin]);
    }

    const expectedAfterOne = 10 * Math.log10 (0.25 * 0.01 + 0.75 * 1);
    check (Math.abs (levels[0] - expectedAfterOne) < 0.01, `alpha 0.25: one update moves a quarter of the way in power (${levels[0].toFixed (2)} dB)`);
    check (levels.every ((v, i) => i === 0 || v < levels[i - 1]), "...falls steadily");
    check (Math.abs (levels[39] + 20) < 0.05, `...and converges on −20 dB (${levels[39].toFixed (3)})`);

    // averaging with a time constant
    const timeAnalyser = new SpectrumAnalyser ({ fftSize: size, sampleRate, averagingTime: 0.1 });
    timeAnalyser.addBlock ([tone]);
    timeAnalyser.update (0.05);
    timeAnalyser.addBlock ([quiet]);
    timeAnalyser.update (0.1);
    const alpha = 1 - Math.exp (-1);
    check (Math.abs (timeAnalyser.levelsDB[bin] - 10 * Math.log10 (alpha * 0.01 + (1 - alpha))) < 0.01, "time constant: dt = τ moves 1 − 1/e of the way");
    timeAnalyser.update (0);
    check (Math.abs (timeAnalyser.levelsDB[bin] - 10 * Math.log10 (alpha * 0.01 + (1 - alpha))) < 0.01, "...and dt = 0 doesn't move it");
    timeAnalyser.reset();
    check (timeAnalyser.levelsDB[bin] === spectrumFloorDB && timeAnalyser.peaksDB[bin] === spectrumFloorDB, "reset() clears everything");

    // peak hold decays at the given rate, but never below the level
    const peakAnalyser = new SpectrumAnalyser ({ fftSize: size, sampleRate, peakDecayDBPerSecond: 20 });
    peakAnalyser.addBlock ([tone]);
    peakAnalyser.update (0.01);
    check (Math.abs (peakAnalyser.peaksDB[bin]) < 0.01, "the peak starts at the level");
    peakAnalyser.addBlock ([quiet]);
    peakAnalyser.update (0.25);
    check (Math.abs (peakAnalyser.peaksDB[bin] + 5) < 0.01 && Math.abs (peakAnalyser.levelsDB[bin] + 20) < 0.01,
           `after 0.25s at 20 dB/s the peak has fallen 5 dB (${peakAnalyser.peaksDB[bin].toFixed (3)}) while the level is at −20`);
    peakAnalyser.update (0.5);
    check (Math.abs (peakAnalyser.peaksDB[bin] + 15) < 0.01, "...and after another 0.5s, 15 dB");
    peakAnalyser.update (10);
    check (Math.abs (peakAnalyser.peaksDB[bin] + 20) < 0.01, "...but it stops at the level");
    check (peakAnalyser.peaksDB.every ((p, k) => p >= peakAnalyser.levelsDB[k]), "no peak is ever below its level");

    // the default dt is the time the new frames cover
    const dtAnalyser = new SpectrumAnalyser ({ fftSize: size, sampleRate: 1024, peakDecayDBPerSecond: 20 });
    dtAnalyser.addBlock ([tone]);
    check (dtAnalyser.update() === true, "update() says it had new frames");
    dtAnalyser.addBlock ([quiet.subarray (0, 512)]);
    dtAnalyser.addBlock ([quiet.subarray (512)]);
    dtAnalyser.update();
    check (Math.abs (dtAnalyser.peaksDB[bin] + 20) < 0.01, "without a dt, 1024 frames at 1024Hz count as one second");
    check (dtAnalyser.update() === false, "...and an update with nothing new says so");

    let threw = false;
    try { new SpectrumAnalyser ({ fftSize: 1000 }); } catch { threw = true; }
    check (threw, "a bad fftSize throws");
});

await test ("fft: performance (logged, not asserted)", () =>
{
    const size = 4096, runs = 200;
    const random = makeRandom (99);
    const re = Float64Array.from ({ length: size }, random), im = new Float64Array (size);
    fft (re, im);   // warm up, and build the tables

    const start = performance.now();

    for (let i = 0; i < runs; ++i)
        fft (re, im, (i & 1) === 1);

    const ms = performance.now() - start;
    window.fftTiming = { size, runs, totalMs: ms, msPerFFT: ms / runs };
    console.log (`fft: ${runs} × ${size}-point FFTs took ${ms.toFixed (1)} ms (${(ms / runs * 1000).toFixed (0)} µs each)`);
    check (re.every (Number.isFinite), "the FFTs ran and stayed finite");
});

await test ("audio-streams: full audio subscriptions and normalisation", () =>
{
    check (fullAudioGranularity === 1024 && fullAudioStreamOptions.granularity === 1024 && fullAudioStreamOptions.fullAudioData === true,
           "the shared options are 1024 frames of full audio data");

    // normalisation
    const message = { data: [[0.5, -0.25, 1], [0, 0.125, -1]] };
    const chunk = normaliseFullAudioMessage (message);
    check (chunk.channels.length === 2 && chunk.channels.every (c => c instanceof Float32Array) && chunk.numFrames === 3,
           "data becomes one Float32Array per channel");
    check (chunk.channels[0][1] === -0.25 && chunk.channels[1][2] === -1, "...with the samples in order");
    check (chunk.frame === undefined && "frame" in chunk, "no frame from an older server: frame is undefined");
    check (normaliseFullAudioMessage (message) === chunk, "a message is converted once and cached");
    check (normaliseFullAudioMessage ({ frame: 4096, data: [[1, 2]] }).frame === 4096, "a frame is passed through");
    check (normaliseFullAudioMessage ({ frame: "x", data: [[1]] }).frame === undefined, "a non-numeric frame is ignored");
    check (normaliseFullAudioMessage ({ min: [0], max: [1] }) === null && normaliseFullAudioMessage (null) === null
             && normaliseFullAudioMessage ({ data: [[1], "nope"] }) === null, "a message without sample data gives null");
    check (normaliseFullAudioMessage ({ data: [[NaN, 1]] }).channels[0][0] === 0, "non-finite samples become 0");
    const typed = new Float32Array ([1, 2]);
    check (normaliseFullAudioMessage ({ data: [typed] }).channels[0] === typed, "a Float32Array channel is used as it is");

    // a fake hub: checks the options and the release
    const subscriptions = [];
    const fakeHub = {
        subscribe (endpointID, listener, options)
        {
            const s = { endpointID, listener, options, released: false };
            subscriptions.push (s);
            return () => { s.released = true; };
        }
    };

    const received = [];
    const unsubscribe = subscribeToFullAudio (fakeHub, "out", c => received.push (c));
    check (subscriptions.length === 1 && subscriptions[0].endpointID === "out"
             && subscriptions[0].options.granularity === 1024 && subscriptions[0].options.fullAudioData === true,
           "subscribes to the hub with { granularity: 1024, fullAudioData: true }");

    subscriptions[0].listener ({ frame: 0, data: [[1, 2, 3]] });
    subscriptions[0].listener ({ min: [0], max: [1] });
    check (received.length === 1 && received[0].frame === 0 && received[0].channels[0][2] === 3, "the listener gets normalised chunks, and skips others");
    unsubscribe();
    check (subscriptions[0].released, "unsubscribing releases the hub subscription");

    // a real hub: the analyser and the recorder share one server listener, and get the same chunk
    const connection = {
        listeners: [],
        addEndpointListener (endpointID, fn, granularity, fullAudioData) { this.listeners.push ({ endpointID, fn, granularity, fullAudioData }); },
        removeEndpointListener (endpointID, fn) { this.listeners = this.listeners.filter (l => l.fn !== fn); }
    };

    const hub = new EndpointDataHub (connection);
    const a = [], b = [];
    const unsubscribeA = subscribeToFullAudio (hub, "out", c => a.push (c));
    const unsubscribeB = subscribeToFullAudio (hub, "out", c => b.push (c));
    check (hub.numServerListeners === 1 && connection.listeners.length === 1
             && connection.listeners[0].granularity === 1024 && connection.listeners[0].fullAudioData === true,
           "two subscribers share one server listener, at 1024 frames of full data");

    connection.listeners[0].fn ({ frame: 2048, data: [[0.1], [0.2]] });
    check (a.length === 1 && b.length === 1 && a[0] === b[0] && a[0].frame === 2048, "both get the same normalised chunk");

    unsubscribeA();
    check (connection.listeners.length === 1, "the listener stays while one subscriber remains");
    unsubscribeB();
    check (connection.listeners.length === 0 && hub.numServerListeners === 0, "...and goes with the last");

    const group = hub.createGroup();
    subscribeToFullAudio (group, "out", () => {});
    check (connection.listeners.length === 1, "works with a SubscriptionGroup too");
    group.setActive (false);
    check (connection.listeners.length === 0, "...which releases it when paused");
    group.dispose();
    hub.dispose();
});

//==============================================================================
// The transport (shell/transport-control.js, and the session's transport methods)
import { hasHostTimelineInputs, clampTempo, nudgeTempo, parseTempo, formatTempo, clampNumerator, roundDenominator, getBarsBeatsTicks,
         formatBarsBeatsTicks, formatTransportTime, extrapolateTransport, normaliseTransportState, defaultTransportState, TransportControl,
         createTransportCommands, transportSupportTimeoutMs } from "/panel_api/shell/transport-control.js";

await test ("transport: bars.beats.ticks", () =>
{
    check (formatBarsBeatsTicks (0, 0, 4, 4) === "1.1.000", "the start is 1.1.000");
    check (formatBarsBeatsTicks (1.5, 0, 4, 4) === "1.2.480", "4/4: 1.5 quarter notes is beat 2, half way");
    check (formatBarsBeatsTicks (4, 4, 4, 4) === "2.1.000", "4/4: 4 quarter notes is bar 2");
    check (formatBarsBeatsTicks (7.5, 6, 3, 4) === "3.2.480", "3/4: 7.5 quarter notes is bar 3, beat 2, half way");
    check (formatBarsBeatsTicks (3.25, 3, 6, 8) === "2.1.480", "6/8: the beat is an eighth note");
    check (formatBarsBeatsTicks (5.75, 3, 6, 8) === "2.6.480", "6/8: ...and there are 6 of them");
    check (JSON.stringify (getBarsBeatsTicks (9, 4, 4, 4)) === JSON.stringify ({ bar: 3, beat: 2, tick: 0 }),
           "a position past its bar start (extrapolated) counts on into the next bar");
    check (getBarsBeatsTicks (5, 1, 4, 4).bar === 2 && getBarsBeatsTicks (5, 1, 4, 4).beat === 1,
           "bars are counted from the given bar start (a signature change at qn 1)");
    check (formatBarsBeatsTicks (3.99999, 0, 4, 4) === "1.4.959" && formatBarsBeatsTicks (3.99999999999, 0, 4, 4) === "2.1.000",
           "a rounding error just under a bar line doesn't show beat 4.960 (or 5)");
    check (formatBarsBeatsTicks (2, 99, 4, 4) === "1.3.000", "a bar start after the position is ignored");
    check (formatBarsBeatsTicks (2, 0, 4, 12) === "2.1.000", "the denominator is rounded as the server does (12 → 8)");
});

await test ("transport: time formatting", () =>
{
    check (formatTransportTime (0, 48000) === "00:00.000", "zero");
    check (formatTransportTime (72000, 48000) === "00:01.500", "1.5 s");
    check (formatTransportTime (44100 * 61.25, 44100) === "01:01.250", "over a minute");
    check (formatTransportTime (48000 * 3723.004, 48000) === "1:02:03.004", "over an hour");
    check (formatTransportTime (47, 48000) === "00:00.000", "rounded down, not up");
    check (formatTransportTime (100, 0) === "--:--.---", "no sample rate: dashes");
});

await test ("transport: extrapolation", () =>
{
    const state = { ...defaultTransportState, tempo: 120, frame: 48000, quarterNote: 2, barStartQuarterNote: 0, sampleRate: 48000 };
    const later = extrapolateTransport (state, 1500);
    check (later.frame === 48000 + 72000 && Math.abs (later.quarterNote - 5) < 1e-9 && later.barStartQuarterNote === 4,
           "1.5 s at 120 BPM: 3 quarter notes on, into the next bar");
    check (state.frame === 48000, "the state isn't changed");
    check (extrapolateTransport ({ ...state, playing: false }, 1000).frame === 48000, "a stopped transport doesn't move");
    check (extrapolateTransport ({ ...state, sampleRate: 0 }, 1000).frame === 48000, "nor one without a sample rate");
    check (extrapolateTransport (state, 5000, 250).frame === 48000 + 12000, "it's limited to maxElapsedMs");
    check (extrapolateTransport (state, -20).frame === 48000, "a negative time doesn't go backwards");
    const waltz = extrapolateTransport ({ ...state, numerator: 3, quarterNote: 2.5, barStartQuarterNote: 0 }, 500);
    check (waltz.barStartQuarterNote === 3 && formatBarsBeatsTicks (waltz.quarterNote, waltz.barStartQuarterNote, 3, 4) === "2.1.480",
           "3/4: the bar start follows the signature");
});

await test ("transport: tempo and time signature values", () =>
{
    check (nudgeTempo (120, 1) === 121 && nudgeTempo (120, -1) === 119, "a step is 1 BPM");
    check (nudgeTempo (120, 1, true) === 130 && nudgeTempo (120.5, -1, true) === 110.5, "a large step is 10");
    check (nudgeTempo (995, 1, true) === 999 && nudgeTempo (5, -1, true) === 1, "nudges are clamped to 1-999");
    check (clampTempo (0) === 1 && clampTempo (5000) === 999 && clampTempo (97.123) === 97.12 && clampTempo ("x") === null, "clampTempo");
    check (parseTempo ("128") === 128 && parseTempo (" 97.5 bpm ") === 97.5 && parseTempo ("2000") === 999 && parseTempo ("fast") === null && parseTempo ("") === null,
           "parseTempo reads a number, with or without BPM, clamped");
    check (formatTempo (120) === "120" && formatTempo (97.5) === "97.5" && formatTempo (97.125) === "97.13", "formatTempo");
    check (roundDenominator (12) === 8 && roundDenominator (0.4) === 1 && roundDenominator (64) === 32 && roundDenominator (3) === 2 && roundDenominator (4) === 4,
           "denominators round down to a power of two, in 1-32 (as the server does)");
    check (clampNumerator (0) === 1 && clampNumerator (100) === 64 && clampNumerator (6.6) === 7, "numerators are rounded and clamped to 1-64");
    check (hasHostTimelineInputs ([{ purpose: "audio in" }, { purpose: "tempo" }]) && ! hasHostTimelineInputs ([{ purpose: "audio in" }]) && ! hasHostTimelineInputs (null),
           "hasHostTimelineInputs");
    const n = normaliseTransportState ({ tempo: 90, playing: "yes", frame: NaN, recording: true });
    check (n.tempo === 90 && n.playing === true && n.frame === 0 && n.recording === true && n.numerator === 4, "normaliseTransportState fills in bad fields");
});

await test ("transport: the session's methods", () =>
{
    class TestSession  extends ServerSession
    {
        constructor() { super ("t"); this.sent = []; }
        sendMessageToServer (m) { this.sent.push (m); }
    }

    const session = new TestSession();
    const states = [];
    const a = (s) => states.push (s), b = () => {};

    session.addTransportStateListener (a);
    check (JSON.stringify (session.sent) === JSON.stringify ([{ type: "set_transport_info_rate", intervalMs: 50 }]), "the first listener asks for updates every 50 ms");
    session.addTransportStateListener (b);
    check (session.sent.length === 1, "a second one doesn't ask again");
    session.handleMessageFromServer ({ type: "transport_state", message: { tempo: 99 } });
    check (states.length === 1 && states[0].tempo === 99, "transport_state goes to the listeners");
    session.removeTransportStateListener (b);
    check (session.sent.length === 1, "removing one of two sends nothing");
    session.removeTransportStateListener (a);
    check (JSON.stringify (session.sent.at (-1)) === JSON.stringify ({ type: "set_transport_info_rate", intervalMs: 0 }), "removing the last withdraws the request");
    session.addTransportStateListener (a, 200);
    check (session.sent.at (-1).intervalMs === 200, "an interval can be given");

    session.sent.length = 0;
    session.setNewStatus ({ connected: false });
    session.handleMessageFromServer ({ type: "session_status", message: { loaded: false } });
    check (session.sent.some (m => m.type === "set_transport_info_rate" && m.intervalMs === 200), "the request is sent again after a reconnection");
    session.sent.length = 0;
    session.handleMessageFromServer ({ type: "session_status", message: { loaded: false } });
    check (! session.sent.some (m => m.type === "set_transport_info_rate"), "...but not with every status");

    session.setTransport ({ tempo: 90, playing: false });
    session.requestTransportState();
    check (JSON.stringify (session.sent.filter (m => m.type.includes ("transport")))
             === JSON.stringify ([{ type: "set_transport", tempo: 90, playing: false }, { type: "req_transport_state" }]), "setTransport and requestTransportState");
    session.dispose();
});

await test ("transport: the control", async () =>
{
    class TestSession  extends ServerSession
    {
        constructor() { super ("t"); this.sent = []; }
        sendMessageToServer (m) { this.sent.push (m); }
        reply (message) { this.handleMessageFromServer ({ type: "transport_state", message }); }
    }

    const frames = [];
    const scheduler = new FrameScheduler ((fn) => { frames.push (fn); return frames.length; }, () => { frames.length = 0; });
    const runFrame = (t) => { const fns = frames.splice (0); fns.forEach (fn => fn (t)); };
    const session = new TestSession();
    const store = new Store ({ connection: null, inputs: [], alwaysShowTransport: false, playing: true });
    const settings = new PanelSettingsScopes (null);
    const ctx = { session, store, scheduler, settings };
    const control = new TransportControl (ctx);
    document.body.append (control.element);
    const sent = (type) => session.sent.filter (m => m.type === type);

    check (control.element.hidden && sent ("set_transport_info_rate").length === 0, "hidden without a patch, and not subscribed");
    store.set ({ connection: {}, inputs: [{ purpose: "audio in" }] });
    check (control.element.hidden, "hidden for a patch with no timeline inputs");
    store.set ({ alwaysShowTransport: true });
    check (! control.element.hidden && sent ("set_transport_info_rate").at (-1)?.intervalMs === 50 && sent ("req_transport_state").length === 1,
           "the setting shows it, and it subscribes and asks for the state");
    store.set ({ alwaysShowTransport: false, inputs: [{ purpose: "transport state" }] });
    check (! control.element.hidden, "a timeline input shows it");

    const playing = { tempo: 120, numerator: 4, denominator: 4, playing: true, recording: false, looping: false, frame: 48000,
                      quarterNote: 2, barStartQuarterNote: 0, sampleRate: 48000 };
    session.reply (playing);
    check (control.isSupported === true && control.positionText.textContent === "1.3.000" && control.tempoInput.value === "120"
             && control.playButton.getAttribute ("aria-pressed") === "true", "a state shows the position, tempo and play state");
    check (control.isAnimating && frames.length === 1, "while playing, it animates through the scheduler");
    control.stateTime -= 125;
    runFrame (performance.now());
    check (/^1\.3\.2[4-9]\d$/.test (control.positionText.textContent) && frames.length === 1,
           `...extrapolating the position from the last state (125 ms at 120 BPM: a quarter of a beat on; ${control.positionText.textContent})`);
    control.stateTime -= 1000;
    runFrame (performance.now());
    check (! control.isAnimating && frames.length === 0, "...and going idle if the server stops sending");
    session.reply (playing);
    check (control.isAnimating, "...until the next state");

    store.set ({ playing: false });
    check (! control.isAnimating && frames.length === 0, "with the audio off, the scheduler idles");
    store.set ({ playing: true });
    session.reply ({ ...playing, playing: false });
    check (! control.isAnimating && frames.length === 0 && control.positionText.textContent === "1.3.000", "a stopped transport doesn't animate, and shows the reported position");

    // the buttons send only their field
    session.sent.length = 0;
    control.playButton.click();
    control.recordButton.click();
    control.loopButton.click();
    control.rewindButton.click();
    check (JSON.stringify (sent ("set_transport").map (({ type, ...m }) => m))
             === JSON.stringify ([{ playing: true }, { recording: true }, { looping: true }, { rewind: true }]), "the buttons send partial set_transports");

    // the tempo field
    session.sent.length = 0;
    control.tempoInput.focus();
    control.tempoInput.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowUp", bubbles: true }));
    control.tempoInput.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowDown", shiftKey: true, bubbles: true }));
    control.tempoInput.value = "2000";
    control.tempoInput.dispatchEvent (new Event ("input"));
    control.tempoInput.dispatchEvent (new KeyboardEvent ("keydown", { key: "Enter", bubbles: true }));
    check (JSON.stringify (sent ("set_transport").map (m => m.tempo)) === "[121,111,999]", "arrows nudge the tempo (Shift by 10), and a typed one is clamped");
    control.tempoInput.value = "abc";
    control.tempoInput.dispatchEvent (new Event ("input"));
    control.tempoInput.dispatchEvent (new Event ("blur"));
    check (sent ("set_transport").length === 3 && control.tempoInput.value === "120", "a bad tempo isn't sent, and the field shows the tempo again");

    control.numeratorSelect.value = "7";
    control.numeratorSelect.dispatchEvent (new Event ("change"));
    control.denominatorSelect.value = "8";
    control.denominatorSelect.dispatchEvent (new Event ("change"));
    check (JSON.stringify (sent ("set_transport").slice (3).map (({ type, ...m }) => m)) === JSON.stringify ([{ numerator: 7 }, { denominator: 8 }]),
           "the time signature selects send their field");

    // the time format, remembered
    control.positionButton.click();
    check (control.positionText.textContent === "00:01.000" && settings.global.get ("transportTimeFormat") === "time", "clicking the position shows the time, and remembers it");
    const second = new TransportControl (ctx);
    check (second.timeFormat === "time", "...for the next control");
    second.dispose();
    control.positionButton.click();

    // the commands
    const commands = createTransportCommands (() => control);
    check (commands.length === 4 && commands.every (c => c.category === "Transport" && c.isEnabled()), "four Transport commands, enabled");
    check (commands.find (c => c.id === "transport.playStop").shortcut === "P" && commands.find (c => c.id === "transport.rewind").shortcut === "Home",
           "P plays/stops, Home rewinds");
    check (formatShortcut ("Home", true) === "Home" && shortcutMatches ("Home", { key: "Home" }, true) && shortcutMatches ("P", { key: "p" }, true),
           "a named key is shown as its name, and matches");

    // hiding it unsubscribes
    session.sent.length = 0;
    store.set ({ connection: null });
    check (control.element.hidden && sent ("set_transport_info_rate").at (-1)?.intervalMs === 0, "losing the patch hides it and withdraws the request");
    check (! commands[0].isEnabled(), "...and disables the commands");

    // an older server: no answer
    const oldSession = new TestSession();
    const old = new TransportControl ({ ...ctx, session: oldSession });
    store.set ({ connection: {} });
    await tick (transportSupportTimeoutMs + 100);
    check (old.isSupported === false && old.playButton.disabled && old.tempoInput.disabled && old.element.title.includes ("too old")
             && old.playButton.title.includes ("too old"), "an older server: after 2 s, disabled, with a tooltip saying why");
    check (! createTransportCommands (() => old)[0].isEnabled(), "...and the commands are disabled");
    oldSession.reply (playing);
    check (old.isSupported === true && ! old.playButton.disabled && old.playButton.title.includes ("Play"), "a late answer enables it again");

    old.dispose();
    store.set ({ connection: null });
    control.dispose();
    check (! control.element.isConnected && frames.length === 0, "dispose removes it");
    session.dispose();
    oldSession.dispose();
});

//==============================================================================
// Server echoes that lag behind the user's own changes (keys pressed faster than the round trip)
import { SignalGeneratorForm, localChangeHoldMs } from "/panel_api/widgets/signal-generator.js";
import { localTempoHoldMs } from "/panel_api/shell/transport-control.js";

await test ("echoes: a stale server value doesn't undo the user's later change", async () =>
{
    const sent = [];
    const form = new SignalGeneratorForm ({ onChange: (c) => sent.push (c) });
    const server = { ...form.settings };

    form.userChanged ({ level: -12.5 });
    form.userChanged ({ level: -13 });
    form.setSettings ({ ...server, level: -12.5 });
    check (form.settings.level === -13 && form.rows.get ("level").slider.getAttribute ("aria-valuetext").startsWith ("-13.0"),
           "the signal form keeps the level the user last set when an older echo arrives");
    form.setSettings ({ ...server, level: -13, frequency: 880 });
    check (form.settings.level === -13 && form.settings.frequency === 880, "...takes the echo that matches it (and the other fields)");
    form.setSettings ({ ...server, level: -20 });
    check (form.settings.level === -20, "...and then another client's change shows");
    form.userChanged ({ level: -30 });
    await tick (localChangeHoldMs + 50);
    form.setSettings ({ ...server, level: -40 });
    check (form.settings.level === -40, "a change the server never confirmed gives way after the hold time");
    form.dispose();

    class TestSession  extends ServerSession
    {
        constructor() { super ("t"); this.sent = []; }
        sendMessageToServer (m) { this.sent.push (m); }
        reply (message) { this.handleMessageFromServer ({ type: "transport_state", message }); }
    }

    const session = new TestSession();
    const store = new Store ({ connection: {}, inputs: [{ purpose: "tempo" }], alwaysShowTransport: false, playing: false });
    const control = new TransportControl ({ session, store, scheduler: new FrameScheduler (() => 1, () => {}), settings: new PanelSettingsScopes (null) });
    const state = { ...defaultTransportState, sampleRate: 48000 };
    session.reply (state);
    control.tempoInput.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowUp" }));
    session.reply (state);                                     // (a periodic state sent before the nudge arrived)
    check (control.tempoInput.value === "121" && control.tempoInput.getAttribute ("aria-valuenow") === "121", "the tempo field keeps a nudge through an older state");
    control.tempoInput.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowUp" }));
    check (session.sent.filter (m => m.type === "set_transport").map (m => m.tempo).join() === "121,122", "...so the next nudge starts from it");
    session.reply ({ ...state, tempo: 122 });
    session.reply ({ ...state, tempo: 140 });
    check (control.tempoInput.value === "140", "...and once the server agrees, another client's tempo shows");
    control.setTempo (90);
    await tick (localTempoHoldMs + 50);
    session.reply ({ ...state, tempo: 140 });
    check (control.tempoInput.value === "140", "an unconfirmed tempo gives way after the hold time");
    control.dispose();
    session.dispose();
});

//==============================================================================
// WAV encoder (feature 4). (Imported here, so this block stands alone.)
const { encodeWAV, encodeWAVFromBlocks, generateWAVParts, blockSourceFromArrays, getWAVFileSize, getWAVHeaderSize,
        getChannelMask, maxWAVFileSize, WAVTooLargeError } = await import ("/panel_api/core/wav-encoder.js");

/// A test-only WAV reader: every header field, and the samples as floats (PCM divided by 2^(bits-1))
function readWAV (buffer)
{
    const view = new DataView (buffer instanceof ArrayBuffer ? buffer : buffer.buffer);
    const fourCC = (o) => String.fromCharCode (...[0, 1, 2, 3].map (i => view.getUint8 (o + i)));
    const wav = { riff: fourCC (0), riffSize: view.getUint32 (4, true), wave: fourCC (8), chunks: [] };

    for (let o = 12; o + 8 <= view.byteLength;)
    {
        const id = fourCC (o), size = view.getUint32 (o + 4, true);
        wav.chunks.push (id);

        if (id === "fmt ")
        {
            Object.assign (wav, { fmtSize: size, tag: view.getUint16 (o + 8, true), numChannels: view.getUint16 (o + 10, true),
                                  sampleRate: view.getUint32 (o + 12, true), byteRate: view.getUint32 (o + 16, true),
                                  blockAlign: view.getUint16 (o + 20, true), bitsPerSample: view.getUint16 (o + 22, true) });

            if (size >= 18) wav.cbSize = view.getUint16 (o + 24, true);

            if (size >= 40)
            {
                wav.validBits = view.getUint16 (o + 26, true);
                wav.channelMask = view.getUint32 (o + 28, true);
                wav.guid = [...new Uint8Array (view.buffer, o + 32, 16)].map (b => b.toString (16).padStart (2, "0")).join ("");
            }
        }
        else if (id === "fact")    wav.factFrames = view.getUint32 (o + 8, true);
        else if (id === "data")    { wav.dataOffset = o + 8; wav.dataSize = size; }

        o += 8 + size + (size & 1);
    }

    const isFloat = wav.tag === 3 || (wav.tag === 0xfffe && wav.guid?.startsWith ("03000000"));
    const bytes = wav.bitsPerSample / 8, frames = wav.dataSize / wav.blockAlign;
    wav.channels = [];

    for (let c = 0; c < wav.numChannels; ++c)
    {
        const out = new Float32Array (frames), ints = [];

        for (let i = 0; i < frames; ++i)
        {
            const p = wav.dataOffset + i * wav.blockAlign + c * bytes;
            const v = isFloat ? view.getFloat32 (p, true)
                    : bytes === 2 ? view.getInt16 (p, true)
                    : bytes === 3 ? (view.getUint16 (p, true) | (view.getInt8 (p + 2) << 16))
                    : view.getInt32 (p, true);
            ints.push (v);
            out[i] = isFloat ? v : v / 2 ** (wav.bitsPerSample - 1);
        }

        wav.channels.push (out);
        wav.ints = wav.ints ?? [];
        wav.ints.push (ints);
    }

    return wav;
}

const pcmGUID = "01000000000010008000" + "00aa00389b71", floatGUID = "03000000000010008000" + "00aa00389b71";

/// n frames of a test signal per channel, with full scale, beyond full scale and silence in it
function makeTestChannels (numChannels, n)
{
    return Array.from ({ length: numChannels }, (_, c) => Float32Array.from ({ length: n }, (_, i) =>
        i === 0 ? 1 : i === 1 ? -1 : i === 2 ? 1.5 : i === 3 ? -3 : i === 4 ? 0 : Math.sin (i * 0.05 * (c + 1)) * 0.8));
}

await test ("wav encoder: headers", () =>
{
    const expected = {
        //               channels  tag     fmt  header bits  guid
        "pcm16/1":    [1, 1,      16, 44, 16, null],
        "pcm16/2":    [2, 1,      16, 44, 16, null],
        "pcm16/3":    [3, 0xfffe, 40, 68, 16, pcmGUID],
        "pcm24/1":    [1, 0xfffe, 40, 68, 24, pcmGUID],
        "pcm24/2":    [2, 0xfffe, 40, 68, 24, pcmGUID],
        "pcm32/2":    [2, 0xfffe, 40, 68, 32, pcmGUID],
        "pcm32/6":    [6, 0xfffe, 40, 68, 32, pcmGUID],
        "float32/1":  [1, 3,      18, 58, 32, null],
        "float32/2":  [2, 3,      18, 58, 32, null],
        "float32/8":  [8, 0xfffe, 40, 80, 32, floatGUID],
        "pcm24/11":   [11, 0xfffe, 40, 68, 24, pcmGUID]
    };

    for (const [key, [numChannels, tag, fmtSize, headerSize, bits, guid]] of Object.entries (expected))
    {
        const format = key.split ("/")[0];
        const n = 101;   // (odd, so pcm24 with an odd channel count needs a pad byte)
        const buffer = encodeWAV (makeTestChannels (numChannels, n), 44100, { format });
        const w = readWAV (buffer);
        const dataSize = n * numChannels * bits / 8;

        check (w.riff === "RIFF" && w.wave === "WAVE" && w.riffSize === buffer.byteLength - 8, `${key}: RIFF header and size`);
        check (buffer.byteLength === headerSize + dataSize + (dataSize & 1) && getWAVFileSize (numChannels, n, format) === buffer.byteLength,
               `${key}: file size ${buffer.byteLength} (padded to even)`);
        check (getWAVHeaderSize (numChannels, format) === headerSize && w.dataOffset === headerSize, `${key}: header size ${headerSize}`);
        check (w.tag === tag && w.fmtSize === fmtSize, `${key}: format tag ${tag}, fmt size ${fmtSize}`);
        check (w.numChannels === numChannels && w.sampleRate === 44100 && w.bitsPerSample === bits, `${key}: channels, rate, bits`);
        check (w.blockAlign === numChannels * bits / 8 && w.byteRate === 44100 * w.blockAlign, `${key}: block align and byte rate`);
        check (w.dataSize === dataSize, `${key}: data size`);
        check (format === "float32" ? (w.factFrames === n && w.chunks.join() === "fmt ,fact,data") : (w.factFrames === undefined && w.chunks.join() === "fmt ,data"),
               `${key}: fact chunk only for float`);

        if (fmtSize === 18) check (w.cbSize === 0, `${key}: cbSize 0`);

        if (guid)
            check (w.cbSize === 22 && w.validBits === bits && w.guid === guid && w.channelMask === getChannelMask (numChannels),
                   `${key}: extensible fields (cbSize, valid bits, sub-format, mask ${w.channelMask})`);
    }

    check ([1, 2, 3, 4, 6, 8, 11].map (getChannelMask).join() === [4, 3, 7, 0x33, 0x3f, 0x63f, 0].join(), "channel masks");
    check (readWAV (encodeWAV ([new Float32Array (10)], 192000)).sampleRate === 192000, "other sample rates");

    const empty = readWAV (encodeWAV ([new Float32Array (0), new Float32Array (0)], 48000, { format: "float32" }));
    check (empty.dataSize === 0 && empty.factFrames === 0 && empty.channels[0].length === 0, "an empty recording");

    let threw = 0;
    for (const bad of [() => encodeWAV ([], 48000), () => encodeWAV ([new Float32Array (2), new Float32Array (3)], 48000),
                       () => encodeWAV ([new Float32Array (2)], 48000, { format: "pcm8" }), () => encodeWAV ([new Float32Array (2)], 44100.5)])
        try { bad(); } catch { ++threw; }

    check (threw === 4, "bad arguments throw (no channels, ragged channels, unknown format, fractional rate)");
});

await test ("wav encoder: samples", () =>
{
    const channels = makeTestChannels (3, 2000);
    const clip = (x) => Math.max (-1, Math.min (1, x));

    for (const [format, bits] of [["pcm16", 16], ["pcm24", 24], ["pcm32", 32]])
    {
        const w = readWAV (encodeWAV (channels, 48000, { format }));
        const max = 2 ** (bits - 1) - 1;
        let worst = 0, exactInts = true;

        for (let c = 0; c < 3; ++c)
            for (let i = 0; i < 2000; ++i)
            {
                const expectedInt = Math.round (clip (channels[c][i]) * max);
                exactInts &&= w.ints[c][i] === expectedInt;
                worst = Math.max (worst, Math.abs (w.channels[c][i] * 2 ** (bits - 1) / max - clip (channels[c][i])));
            }

        check (exactInts, `${format}: every sample is round (clip (x) * ${max})`);
        check (worst <= 0.5 / max + 1e-9, `${format}: round trip within half an LSB (worst ${worst.toExponential (2)})`);
        check (w.ints[0][0] === max && w.ints[0][1] === -max && w.ints[0][2] === max && w.ints[0][3] === -max && w.ints[0][4] === 0,
               `${format}: +-1 -> +-${max}, beyond full scale clips, 0 stays 0`);
        check (w.ints[1][5] !== w.ints[0][5] && w.ints[2][5] !== w.ints[1][5], `${format}: channels are interleaved in order`);
    }

    const nan = readWAV (encodeWAV ([Float32Array.of (NaN, Infinity, -Infinity)], 48000, { format: "pcm16" }));
    check (nan.ints[0].join() === "0,32767,-32767", "pcm: NaN -> 0, infinities clip");

    // float32 is bit for bit, including out-of-range values, -0, denormals and NaN payloads
    const special = new Float32Array (8);
    new Uint32Array (special.buffer).set ([0x7fc00001, 0xff800000, 0x80000000, 0x00000001, 0x3f800000, 0x40400000, 0xc1200000, 0x3eaaaaab]);
    const fw = readWAV (encodeWAV ([special, special.map (x => -x)], 44100, { format: "float32" }));
    const fileBits = new Uint32Array (8);
    const fv = new DataView (encodeWAV ([special], 44100, { format: "float32" }));

    for (let i = 0; i < 8; ++i)
        fileBits[i] = fv.getUint32 (58 + i * 4, true);

    check (fileBits.join() === new Uint32Array (special.buffer).join(), "float32: bit-exact, even NaN payloads, -0 and denormals");
    check (Object.is (fw.channels[1][2], 0) && fw.channels[0][5] === 3, "float32: values beyond +-1 are not clipped");

    const many = makeTestChannels (5, 777);
    const mw = readWAV (encodeWAV (many, 48000, { format: "float32" }));
    check (many.every ((c, i) => c.every ((x, j) => Object.is (x, mw.channels[i][j]))), "float32: 5 channels round-trip exactly");

    // dither: off by default; on, it's TPDF of +-1 LSB, so values stay within one LSB of plain rounding
    const quiet = [Float32Array.from ({ length: 4000 }, (_, i) => Math.sin (i * 0.01) * 0.001)];
    const plain = readWAV (encodeWAV (quiet, 48000)).ints[0];
    const plain2 = readWAV (encodeWAV (quiet, 48000)).ints[0];
    const dithered = readWAV (encodeWAV (quiet, 48000, { format: "pcm16", dither: true })).ints[0];
    const differences = dithered.map ((v, i) => v - plain[i]);
    check (plain.join() === plain2.join(), "no dither by default (repeatable)");
    check (differences.some (d => d !== 0) && differences.every (d => Math.abs (d) <= 1), "pcm16 dither changes samples by at most 1 LSB");
    check (readWAV (encodeWAV ([Float32Array.of (1, -1)], 48000, { dither: true, random: () => 0.999 })).ints[0].join() === "32767,-32767",
           "dither never goes past full scale");
    check (readWAV (encodeWAV (quiet, 48000, { format: "pcm24", dither: true })).ints[0].join()
             === readWAV (encodeWAV (quiet, 48000, { format: "pcm24" })).ints[0].join(), "dither is only for pcm16");
});

await test ("wav encoder: blocks", async () =>
{
    // blocks as the recorder keeps them (shrunk to 64 frames here), with a short last block
    const blockSize = 64, numFrames = 64 * 5 + 17;
    const channels = makeTestChannels (2, numFrames);
    const blocks = channels.map (c => Array.from ({ length: 6 }, (_, b) => c.slice (b * blockSize, (b + 1) * blockSize)));

    for (const format of ["pcm16", "pcm24", "float32"])
    {
        const whole = new Uint8Array (encodeWAV (channels, 48000, { format, dither: false }));
        const fromBlocks = new Uint8Array (encodeWAVFromBlocks (blockSourceFromArrays (blocks, blockSize), 48000, { format }));
        const parts = [...generateWAVParts (blockSourceFromArrays (blocks, blockSize), 48000, { format })];
        const joined = new Uint8Array (await new Blob (parts).arrayBuffer());

        check (whole.join() === fromBlocks.join(), `${format}: encodeWAVFromBlocks matches encodeWAV`);
        check (whole.join() === joined.join() && parts.length === 1 + 6, `${format}: generateWAVParts' parts make the same file (header + one per block)`);
    }

    // full-size blocks, longer than needed: frames past numFrames are ignored
    const big = new Float32Array (65536).fill (0.5);
    const src = { numChannels: 1, numFrames: 65536 + 100, blockSize: 65536, getBlock: () => big };
    const w = readWAV (encodeWAVFromBlocks (src, 48000, { format: "pcm24" }));
    check (w.channels[0].length === 65636 && w.ints[0].every (v => v === Math.round (0.5 * 8388607)), "a long last block is cut at numFrames");

    const odd = [...generateWAVParts (blockSourceFromArrays ([[Float32Array.of (0.1)]], 1), 48000, { format: "pcm24" })];
    check (odd.length === 3 && odd[2].length === 1, "generateWAVParts yields the pad byte");

    let threw = false;
    try { encodeWAVFromBlocks ({ numChannels: 1, numFrames: 100, blockSize: 64, getBlock: (_c, i) => i === 0 ? new Float32Array (64) : undefined }, 48000); }
    catch (e) { threw = /block 1 of channel 0/.test (e.message); }
    check (threw, "a missing block is a clear error");
});

await test ("wav encoder: 4 GB limit", () =>
{
    // (arithmetic only: nothing this size is allocated)
    const largest = Math.floor ((maxWAVFileSize - 44) / 4);
    check (getWAVFileSize (2, largest, "pcm16") <= maxWAVFileSize && getWAVFileSize (2, largest, "pcm16") - 8 <= 0xffffffff,
           "the largest stereo pcm16 file fits the RIFF size field");

    let error;
    try { getWAVFileSize (2, largest + 1, "pcm16"); } catch (e) { error = e; }
    check (error instanceof WAVTooLargeError && /4 GB/.test (error.message) && error.fileSize > maxWAVFileSize, "one frame more is refused, saying why");

    let calls = 0, encodeError;
    const huge = { numChannels: 8, numFrames: 48000 * 60 * 60 * 4, blockSize: 65536, getBlock: () => { ++calls; return new Float32Array (65536); } };
    try { encodeWAVFromBlocks (huge, 48000, { format: "float32" }); } catch (e) { encodeError = e; }
    check (encodeError instanceof WAVTooLargeError && calls === 0, "encodeWAVFromBlocks refuses 4 hours of 8 channels before reading or allocating");

    encodeError = undefined;
    try { generateWAVParts (huge, 48000, { format: "pcm24" }).next(); } catch (e) { encodeError = e; }
    check (encodeError instanceof WAVTooLargeError && calls === 0, "...and so does generateWAVParts");
});

await test ("wav encoder: the browser decodes it", async () =>
{
    const rate = 44100, n = 3000;
    const channels = makeTestChannels (2, n);
    const clip = (x) => Math.max (-1, Math.min (1, x));

    for (const [format, tolerance] of [["pcm16", 2 / 32768], ["pcm24", 2 / 8388608], ["pcm32", 2 ** -23], ["float32", 0]])
    {
        let decoded;

        try { decoded = await new OfflineAudioContext (2, 1, rate).decodeAudioData (encodeWAV (channels, rate, { format })); }
        catch (e) { check (false, `${format}: decodeAudioData failed: ${e}`); continue; }

        let worst = 0;

        for (let c = 0; c < 2; ++c)
        {
            const d = decoded.getChannelData (c);

            for (let i = 0; i < n; ++i)
                worst = Math.max (worst, Math.abs (d[i] - (format === "float32" ? channels[c][i] : clip (channels[c][i]))));
        }

        check (decoded.numberOfChannels === 2 && decoded.length === n && decoded.sampleRate === rate, `${format}: decodes to 2 x ${n} frames at ${rate}`);
        check (worst <= tolerance, `${format}: decoded samples match (worst error ${worst.toExponential (2)})`);
    }

    // extensible with more channels
    const four = makeTestChannels (4, 500);
    try
    {
        const d = await new OfflineAudioContext (4, 1, 48000).decodeAudioData (encodeWAV (four, 48000, { format: "pcm24" }));
        check (d.numberOfChannels === 4 && Math.abs (d.getChannelData (3)[100] - four[3][100]) < 1e-6, "pcm24 quad (extensible, mask 0x33) decodes");
    }
    catch (e) { check (false, "pcm24 quad: decodeAudioData failed: " + e); }
});

//==============================================================================
// HostBridge.forwardUnusedKeys: VS Code's keybindings while the panel has the focus
const { shouldForwardKey } = await import ("/panel_api/core/host-bridge.js");

await test ("host bridge: forwarding unused keys to VS Code", async () =>
{
    const key = (k, mods = {}) => ({ key: k, ctrlKey: false, metaKey: false, shiftKey: false, altKey: false, ...mods });

    check (shouldForwardKey (key ("P", { metaKey: true, shiftKey: true })), "Cmd+Shift+P is forwarded");
    check (shouldForwardKey (key ("p", { ctrlKey: true })) && shouldForwardKey (key ("w", { metaKey: true })), "Ctrl+P and Cmd+W are forwarded");
    check (shouldForwardKey (key ("F1")) && shouldForwardKey (key ("F12", { shiftKey: true })), "function keys are forwarded");
    check (! ["a", "c", "v", "x", "z", "y", "Z"].some (k => shouldForwardKey (key (k, { metaKey: true }))), "editing keys (copy, paste, undo...) aren't");
    check (! shouldForwardKey (key ("Meta", { metaKey: true })) && ! shouldForwardKey (key ("Shift", { shiftKey: true })), "modifier keys alone aren't");
    check (! shouldForwardKey (key ("p")) && ! shouldForwardKey (key (" ")) && ! shouldForwardKey (key ("Escape")), "plain keys aren't");

    const listeners = [], posted = [];
    const fakeWindow = { sendMessageToVSCode: (m) => posted.push (m),
                         addEventListener: (type, l) => listeners.push (l),
                         removeEventListener: (type, l) => listeners.splice (listeners.indexOf (l), 1) };
    const bridge = new HostBridge (fakeWindow);
    const stop = bridge.forwardUnusedKeys();
    const press = (e) => listeners.forEach (l => l ({ code: "", keyCode: 0, repeat: false, defaultPrevented: false, ...e }));

    press (key ("P", { metaKey: true, shiftKey: true, code: "KeyP", keyCode: 80 }));
    check (posted.length === 1 && posted[0].keyDown?.code === "KeyP" && posted[0].keyDown.keyCode === 80 && posted[0].keyDown.shiftKey, "sends { keyDown } with the key's details");
    press (key ("k", { metaKey: true, defaultPrevented: true }));
    check (posted.length === 1, "a key the panel used (default prevented) isn't sent");
    stop();
    check (listeners.length === 0, "the returned function stops it");
});

//==============================================================================
// HostBridge.saveFile (feature 4)
const { saveFileChunkSize, saveFileTimeoutMs, bytesToBase64 } = await import ("/panel_api/core/host-bridge.js");

await test ("host bridge: saveFile in VS Code", async () =>
{
    const posted = [];
    const bridge = new HostBridge ({ sendMessageToVSCode: (m) => posted.push (m) });
    const fromBase64 = (s) => Uint8Array.from (atob (s), c => c.charCodeAt (0));

    const wav = new Uint8Array (encodeWAV (makeTestChannels (2, 1000), 48000, { format: "pcm24" }));
    const saving = bridge.saveFile ({ suggestedName: "take 1.wav", data: wav.buffer, mimeType: "audio/wav" });
    await tick();
    const request = posted.at (-1).saveFile;
    check (request && request.suggestedName === "take 1.wav" && request.mimeType === "audio/wav" && request.size === wav.length
             && request.chunkIndex === 0 && request.numChunks === 1, "sends { saveFile } with name, type, size and one chunk");
    check (fromBase64 (request.dataBase64).join() === wav.join(), "the base64 holds the file's bytes");
    check (bridge.handleMessage ({ saveFileResult: { requestID: request.requestID, saved: true, path: "/x/take 1.wav" } }) === true, "the reply is consumed");
    const result = await saving;
    check (result.saved === true && result.path === "/x/take 1.wav" && ! result.cancelled, "resolves saved, with the path");
    check (bridge.handleMessage ({ saveFileResult: { requestID: request.requestID, saved: true } }) === false, "an unknown reply is left alone");

    // cancelled, errors, and concurrent requests answered out of order
    posted.length = 0;
    const a = bridge.saveFile ({ suggestedName: "a.wav", data: new Uint8Array ([1, 2, 3]) });
    const b = bridge.saveFile ({ suggestedName: "b.wav", data: new Blob ([new Uint8Array ([4, 5])]) });
    const c = bridge.saveFile ({ suggestedName: "c.bin", data: new Uint16Array ([0x0201, 0x0403]).subarray (1) });
    await tick (10);
    const [ra, rb, rc] = ["a.wav", "b.wav", "c.bin"].map (name => posted.find (m => m.saveFile.suggestedName === name).saveFile);
    check (new Set ([ra.requestID, rb.requestID, rc.requestID]).size === 3, "each request has its own ID");
    check (fromBase64 (rb.dataBase64).join() === "4,5" && fromBase64 (rc.dataBase64).join() === "3,4", "Blob and typed-array views are sent as their bytes");
    bridge.handleMessage ({ saveFileResult: { requestID: rc.requestID, saved: false, error: "disk full" } });
    bridge.handleMessage ({ saveFileResult: { requestID: ra.requestID, saved: false, cancelled: true } });
    bridge.handleMessage ({ saveFileResult: { requestID: rb.requestID, saved: true, path: "/b.wav" } });
    const [resA, resB, resC] = await Promise.all ([a, b, c]);
    check (resA.saved === false && resA.cancelled === true && resB.path === "/b.wav" && resC.error === "disk full" && ! resC.saved,
           "out-of-order replies go to the right callers (cancelled / saved / error)");

    // a big file is sent in chunks
    posted.length = 0;
    const bigFile = new Uint8Array (saveFileChunkSize * 2 + 5).map ((_, i) => i * 7);
    const bigSave = bridge.saveFile ({ suggestedName: "big.wav", data: bigFile });
    for (let i = 0; i < 20 && posted.length < 3; ++i) await tick (5);
    check (posted.length === 3 && posted.every ((m, i) => m.saveFile.chunkIndex === i && m.saveFile.numChunks === 3
                                                          && m.saveFile.requestID === posted[0].saveFile.requestID), "a big file goes as 3 ordered chunks");
    const rejoined = new Uint8Array (bigFile.length);
    posted.reduce ((o, m) => { const part = fromBase64 (m.saveFile.dataBase64); rejoined.set (part, o); return o + part.length; }, 0);
    check (rejoined.every ((v, i) => v === bigFile[i]), "the chunks join back into the file");
    bridge.handleMessage ({ saveFileResult: { requestID: posted[0].saveFile.requestID, saved: true, path: "/big.wav" } });
    check ((await bigSave).saved, "...and it resolves");

    check ((await bridge.saveFile ({ suggestedName: "x", data: "not bytes" })).error?.includes ("ArrayBuffer"), "bad data resolves with an error");
    check (bytesToBase64 (new Uint8Array ([0, 255, 128, 1])) === "AP+AAQ==", "bytesToBase64");
    check (saveFileTimeoutMs >= 5 * 60 * 1000, "the timeout allows for a long-open dialog");
});

await test ("host bridge: saveFile timeout and browser download", async () =>
{
    // timeout (with the real timers shortened, via a fake setTimeout)
    const realSetTimeout = window.setTimeout;
    const bridge = new HostBridge ({ sendMessageToVSCode: () => {} });
    let timeoutDelay;
    window.setTimeout = (fn, ms) => { if (ms === saveFileTimeoutMs) { timeoutDelay = ms; ms = 1; } return realSetTimeout (fn, ms); };

    try
    {
        const r = await bridge.saveFile ({ suggestedName: "t.wav", data: new Uint8Array (4) });
        check (timeoutDelay === saveFileTimeoutMs && r.saved === false && /didn't answer/.test (r.error), "no answer resolves with an error");
    }
    finally { window.setTimeout = realSetTimeout; }

    // browser: an <a download> with an object URL, clicked, then revoked later
    const clicks = [], created = [], revoked = [];
    const fakeDocument = { body: { appendChild: (a) => (fakeDocument.appended = a) },
                           createElement: (tag) => ({ tag, style: {}, click() { clicks.push ({ href: this.href, download: this.download }); }, remove() { this.removed = true; } }) };
    const fakeURL = { createObjectURL: (blob) => { created.push (blob); return "blob:fake/" + created.length; }, revokeObjectURL: (u) => revoked.push (u) };
    const browser = new HostBridge ({ document: fakeDocument, URL: fakeURL });
    const result = await browser.saveFile ({ suggestedName: "rec.wav", data: new Uint8Array ([9, 8, 7]), mimeType: "audio/wav" });

    check (result.saved === true && result.path === undefined && ! result.cancelled, "browser: resolves saved, with no path");
    check (fakeDocument.appended?.tag === "a" && clicks[0]?.download === "rec.wav" && clicks[0].href === "blob:fake/1" && fakeDocument.appended.removed,
           "browser: clicks a hidden <a download> link, then removes it");
    check (created[0].type === "audio/wav" && new Uint8Array (await created[0].arrayBuffer()).join() === "9,8,7", "browser: the Blob holds the bytes and type");
    check (revoked.length === 0, "browser: the URL isn't revoked straight away");
    const blob = new Blob (["x"]);
    await browser.saveFile ({ suggestedName: "b.txt", data: blob });
    check (created[1] === blob, "browser: a Blob is used as it is");
});

//==============================================================================
// Parameter fuzzing (feature 3). (Imported here, so this block stands alone.)
const { ParameterFuzzer, createFuzzCommands, getRandomLegalValue, toLegalValue, getAutomationWindow, getWaveValue, normaliseAutomationSettings,
        isLikelyOutputLevel, sendIntervalMs, maxStepMs, fuzzExclusionsSettingName } = await import ("/panel_api/core/parameter-fuzzer.js");
const { FuzzToolbar, AutomationEditor, parseAutomationRate, formatAutomationRate, parseDepth } = await import ("/panel_api/widgets/fuzz-controls.js");
const { describeParameter } = await import ("/panel_api/widgets/parameter-model.js");

const fuzzParameters = [
    { endpointID: "x",      purpose: "parameter", endpointType: "value", dataType: { type: "float32" }, annotation: { min: 0, max: 1, init: 0.5 } },
    { endpointID: "cutoff", purpose: "parameter", endpointType: "value", dataType: { type: "float32" }, annotation: { min: 20, max: 20000, init: 1000 } },
    { endpointID: "steps",  purpose: "parameter", endpointType: "value", dataType: { type: "float32" }, annotation: { min: 0, max: 10, step: 0.5, init: 0 } },
    { endpointID: "bypass", purpose: "parameter", endpointType: "value", dataType: { type: "bool" }, annotation: { boolean: true, init: 0 } },
    { endpointID: "shape",  purpose: "parameter", endpointType: "value", dataType: { type: "int32" }, annotation: { text: "sine|saw|square", init: 0 } },
    { endpointID: "voices", purpose: "parameter", endpointType: "value", dataType: { type: "int32" }, annotation: { min: 1, max: 8, step: 1, discrete: true, init: 4 } },
    { endpointID: "volume", purpose: "parameter", endpointType: "value", dataType: { type: "float32" }, annotation: { name: "Volume", min: 0, max: 1, init: 0.8 } },
];

/// A repeatable random() (an LCG)
function makeSeededRandom (seed = 1)
{
    let s = seed >>> 0;
    return () => { s = (Math.imul (s, 1664525) + 1013904223) >>> 0; return s / 4294967296; };
}

function makeFuzzSettings()
{
    const values = {};
    return { values, get: (k, d) => Object.hasOwn (values, k) ? values[k] : d, set: (k, v) => { if (v === undefined) delete values[k]; else values[k] = v; } };
}

function makeFuzzer ({ parameters = fuzzParameters, sampleRate = 48000, values = { x: 0.5, cutoff: 1000, steps: 2, bypass: 0, shape: 1, voices: 4, volume: 0.8 } } = {})
{
    const connection = new FakeConnection();
    const hub = new EndpointDataHub (connection);
    const scheduler = makeManualScheduler();
    const settings = makeFuzzSettings();
    const fuzzer = new ParameterFuzzer ({ patchConnection: connection, hub, scheduler, settings, getSampleRate: () => sampleRate, random: makeSeededRandom (7) });
    fuzzer.setParameters (parameters);
    const echo = (endpointID, value) => connection.deliverMessageFromServer ({ type: "param_value", message: { endpointID, value } });

    for (const [id, v] of Object.entries (values))
        echo (id, v);

    let time = 1000;
    // runs ms of frames at a frame rate, through the scheduler
    const run = (ms, fps = 60) => { for (let t = 0; t < ms - 1e-9; t += 1000 / fps) { time += 1000 / fps; scheduler.frame (time); } };
    const gestures = (id) => connection.sent.filter (m => m.type.startsWith ("send_gesture") && (! id || m.id === id)).map (m => m.type.slice (13));
    const sends = (id) => connection.sent.filter (m => m.type === "send_value" && (! id || m.id === id));
    const balanced = () => fuzzParameters.every (p => { const g = gestures (p.endpointID); return g.filter (x => x === "start").length === g.filter (x => x === "end").length; });
    return { connection, hub, scheduler, settings, fuzzer, echo, run, gestures, sends, balanced, frameAt: (t) => scheduler.frame (time = t), getTime: () => time };
}

const near = (a, b, tolerance = 1e-9) => Math.abs (a - b) <= tolerance;

await test ("fuzzer: random legal values", () =>
{
    const random = makeSeededRandom (3);
    const models = Object.fromEntries (fuzzParameters.map (p => [p.endpointID, describeParameter (p)]));
    const seen = {};

    for (let i = 0; i < 400; ++i)
        for (const [id, model] of Object.entries (models))
            (seen[id] ??= new Set()).add (getRandomLegalValue (model, random));

    const all = (id, fn) => [...seen[id]].every (fn);
    check (all ("cutoff", v => v >= 20 && v <= 20000) && seen.cutoff.size > 300, "a continuous parameter: anywhere in its range");
    check (all ("steps", v => near (v * 2, Math.round (v * 2)) && v >= 0 && v <= 10) && seen.steps.size === 21, "a stepped one: on its steps, all 21 of them (both ends too)");
    check ([...seen.bypass].sort().join() === "0,1", "a boolean: on or off");
    check ([...seen.shape].sort().join() === "0,1,2", "options: each of the three");
    check ([...seen.voices].sort().join() === "1,2,3,4,5,6,7,8", "discrete steps: each of 1..8");
    check (toLegalValue (models.shape, 1.4) === 1 && toLegalValue (models.bypass, 0.7) === 1 && toLegalValue (models.steps, 3.3) === 3.5 && toLegalValue (models.x, 1.7) === 1,
           "toLegalValue: the nearest option, on/off, step, or the range's end");
});

await test ("fuzzer: randomise brackets each change, and skips the excluded and automated", () =>
{
    const { fuzzer, connection, settings, sends } = makeFuzzer();
    connection.sent.length = 0;
    fuzzer.setExcluded ("volume", true);
    check (settings.values[fuzzExclusionsSettingName]?.join() === "volume" && fuzzer.isExcluded ("volume"), "an exclusion is kept in the patch settings");

    const count = fuzzer.randomiseAll();
    const relevant = connection.sent.filter (m => m.type !== "req_param_value");
    let bracketed = true;

    for (let i = 0; i < relevant.length; i += 3)
        bracketed &&= relevant[i].type === "send_gesture_start" && relevant[i + 1]?.type === "send_value" && relevant[i + 2]?.type === "send_gesture_end"
                       && relevant[i].id === relevant[i + 1].id && relevant[i + 1].id === relevant[i + 2].id && relevant[i + 1].rampFrames === undefined;

    check (count === 6 && relevant.length === 18 && bracketed, `6 parameters, each as start, value, end (${relevant.map (m => m.type).join()})`);
    check (! sends ("volume").length, "the excluded one isn't touched");

    // the exclusions are read afresh (e.g. another panel, or a reload, changed them)
    settings.values[fuzzExclusionsSettingName] = ["volume", "cutoff"];
    connection.sent.length = 0;
    fuzzer.automate ("x");
    connection.sent.length = 0;
    check (fuzzer.randomiseAll() === 4 && ! sends ("cutoff").length && ! sends ("x").length, "the settings are re-read; an automated parameter is left to its automation");
    settings.values[fuzzExclusionsSettingName] = fuzzParameters.map (p => p.endpointID);
    check (! fuzzer.canRandomise && fuzzer.randomiseAll() === 0, "with everything excluded, there's nothing to randomise");
    fuzzer.dispose();
});

await test ("fuzzer: the automation waveforms", () =>
{
    const valueAt = (mode, startValue, depth, times, extra = {}) =>
    {
        const { fuzzer } = makeFuzzer ({ values: { x: startValue } });
        fuzzer.automate ("x", { mode, rate: 1, depth, ...extra });
        const a = fuzzer.automations.get ("x");
        let t = 0;
        const out = times.map (target => { fuzzer.advance (target - t); t = target; return a.value; });
        fuzzer.dispose();
        return out;
    };

    const sine = valueAt ("sine", 0.5, 0.5, [0, 250, 500, 750, 1000]);
    check ([0.5, 0.75, 0.5, 0.25, 0.5].every ((v, i) => near (sine[i], v)), `sine at 1 Hz, depth 50% around 0.5: ${sine.map (v => v.toFixed (3))}`);
    const triangle = valueAt ("triangle", 0.5, 0.5, [125, 250, 500, 750, 1000]);
    check ([0.625, 0.75, 0.5, 0.25, 0.5].every ((v, i) => near (triangle[i], v)), `triangle: ${triangle.map (v => v.toFixed (3))}`);
    const ramp = valueAt ("ramp", 0.5, 0.5, [250, 500, 1000, 1500, 2500]);
    check ([0.625, 0.75, 0.5, 0.25, 0.75].every ((v, i) => near (ramp[i], v)), `ramp: up from 0.5 to 0.75, down to 0.25 over a second, and back: ${ramp.map (v => v.toFixed (3))}`);

    const edge = valueAt ("sine", 0.95, 0.5, [1, 250, 750]);
    check (near (edge[0], 0.95, 0.01) && edge.every (v => v >= 0.5 - 1e-9 && v <= 1 + 1e-9), `a window that would stick out is moved in, and the wave starts at the value (${edge.map (v => v.toFixed (3))})`);
    const w = getAutomationWindow (describeParameter (fuzzParameters[1]), 19000, 1);
    check (w.lo === 20 && w.hi === 20000, "depth 1 is the full range, wherever it starts");
    check (near (getWaveValue ("triangle", 0.25), 1) && near (getWaveValue ("ramp", 1), 1) && near (getWaveValue ("ramp", 2), 0), "getWaveValue");

    // random: smoothed from one random target to the next, one per period, inside the window
    const { fuzzer, sends } = makeFuzzer ({ values: { x: 0.5 } });
    fuzzer.automate ("x", { mode: "random", rate: 2, depth: 0.5, smooth: true });
    const a = fuzzer.automations.get ("x");
    const firstTarget = a.stepTo;
    fuzzer.advance (250);
    check (near (a.value, 0.5 + (firstTarget - 0.5) / 2), "random, smoothed: halfway through a period it's halfway to the target");
    fuzzer.advance (251);
    const secondTarget = a.stepTo;
    check (secondTarget !== firstTarget && near (a.stepFrom, firstTarget) && near (a.value, firstTarget + (secondTarget - firstTarget) * 0.002, 1e-6), "...and then heads for a new one");
    const targets = [];
    for (let i = 0; i < 200; ++i) { fuzzer.advance (100); targets.push (a.value); }
    check (targets.every (v => v >= 0.25 - 1e-9 && v <= 0.75 + 1e-9), "random values stay in the window");
    check (sends ("x").every (m => m.rampFrames === 1600), "smoothed random values are sent with a ramp");

    fuzzer.automate ("x", { smooth: false });
    const before = sends ("x").length;
    fuzzer.advance (100);
    const steppedValue = a.value;
    fuzzer.advance (100);
    check (near (a.value, a.stepTo) && near (steppedValue, a.stepTo) === (Math.floor (a.phase) === Math.floor (a.phase - 0.2)), "random, stepped: it holds each value through its period");
    check (sends ("x").slice (before).every (m => m.rampFrames === 0), "...and its steps are sent without a ramp");
    fuzzer.dispose();

    // a discrete parameter only gets its legal values, without a ramp, and only when they change
    const d = makeFuzzer();
    d.fuzzer.automate ("shape", { mode: "sine", rate: 1, depth: 1 });
    for (let i = 0; i < 60; ++i) d.fuzzer.advance (sendIntervalMs);
    const shapes = d.sends ("shape");
    check (shapes.length > 2 && shapes.every (m => [0, 1, 2].includes (m.value) && m.rampFrames === 0), `options: only 0, 1, 2, unramped (${shapes.map (m => m.value)})`);
    check (shapes.every ((m, i) => i === 0 || m.value !== shapes[i - 1].value), "...sent only when it changes");
    d.fuzzer.dispose();
});

await test ("fuzzer: at most about 30 sends a second, with ramp frames from the sample rate", () =>
{
    for (const fps of [60, 144, 30, 240])
    {
        const { fuzzer, run, sends } = makeFuzzer();
        fuzzer.automate ("cutoff", { mode: "sine", rate: 1, depth: 1 });
        run (2000, fps);
        const n = sends ("cutoff").length / 2;
        check (n <= 31 && n >= (fps >= 60 ? 28 : 29), `${fps} fps: ${n} sends a second`);
        check (sends ("cutoff").every (m => m.rampFrames === 1600), `${fps} fps: rampFrames 1600 (48 kHz / 30)`);
        fuzzer.dispose();
    }

    const { fuzzer, run, sends } = makeFuzzer ({ sampleRate: 0 });
    fuzzer.automate ("x");
    run (200);
    check (sends ("x").length > 0 && sends ("x").every (m => m.rampFrames === 1470), "without a known sample rate, 44.1 kHz is assumed (1470 frames)");
    fuzzer.dispose();
});

await test ("fuzzer: gestures stay balanced", () =>
{
    const { fuzzer, gestures, balanced, run, sends, scheduler } = makeFuzzer();
    fuzzer.automate ("x");
    check (gestures ("x").join() === "start", "an automation opens one gesture");
    run (100);
    fuzzer.automate ("x", { rate: 2, mode: "triangle" });
    check (gestures ("x").join() === "start", "changing its settings doesn't open another");
    fuzzer.stopAutomation ("x");
    check (gestures ("x").join() === "start,end", "stopping it ends it");
    const sentAfterStop = sends ("x").length;
    run (200);
    check (sends ("x").length === sentAfterStop && ! scheduler.hasFrameRequested(), "...and nothing more is sent, and the frames stop");
    check (fuzzer.stopAutomation ("x") === false && gestures ("x").join() === "start,end", "stopping it again does nothing");

    fuzzer.automate ("x");
    fuzzer.automate ("cutoff");
    check (fuzzer.stopAll() === 2 && balanced() && fuzzer.numAutomations === 0, "Stop all ends every gesture");

    fuzzer.automate ("x");
    fuzzer.automate ("steps");
    fuzzer.setParameters (fuzzParameters.filter (p => p.endpointID !== "steps"));
    check (gestures ("steps").join() === "start,end" && fuzzer.isAutomating ("x") && ! fuzzer.isAutomating ("steps"),
           "a rebuild that removes a parameter ends its gesture; the others keep running");
    fuzzer.setParameters (fuzzParameters);
    fuzzer.dispose();
    check (balanced() && gestures ("x").at (-1) === "end", "dispose ends the rest");
    check (! fuzzer.automate ("x") && fuzzer.randomiseAll() === 0, "a disposed fuzzer does nothing");
});

await test ("fuzzer: a user grab ends the fuzzer's gesture before the user's starts", () =>
{
    const { fuzzer, connection, hub, run, sends } = makeFuzzer();
    const group = hub.createGroup();
    const widget = new ValueControlWidget (fuzzParameters[0], { patchConnection: connection, group, scheduler: makeManualScheduler(),
                                                                onUserGesture: (id) => fuzzer.handleUserGesture (id) });
    document.body.append (widget.element);

    fuzzer.automate ("x");
    run (100);
    connection.sent.length = 0;
    widget.control.dispatchEvent (new PointerEvent ("pointerdown", { pointerId: 3, button: 0, bubbles: true }));
    const order = connection.sent.filter (m => m.id === "x").map (m => m.type.replace ("send_", ""));
    check (order.join() === "gesture_end,gesture_start" && ! fuzzer.isAutomating ("x"), `a slider drag: the fuzzer's end, then the user's start (${order})`);
    run (200);
    check (! sends ("x").length, "...and the fuzzer sends no more");
    widget.control.dispatchEvent (new PointerEvent ("pointerup", { pointerId: 3, button: 0, bubbles: true }));

    // a key press (a gesture of its own) stops it too
    fuzzer.automate ("x");
    connection.sent.length = 0;
    widget.control.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowUp", bubbles: true }));
    const keyOrder = connection.sent.filter (m => m.id === "x").map (m => m.type.replace ("send_", ""));
    check (keyOrder.join() === "gesture_end,gesture_start,value,gesture_end" && ! fuzzer.isAutomating ("x"), `an arrow key: the same (${keyOrder})`);

    check (fuzzer.handleUserGesture ("cutoff") === false, "grabbing a parameter that isn't automated changes nothing");
    widget.dispose();
    fuzzer.dispose();
});

await test ("fuzzer: snapshot and restore", () =>
{
    const { fuzzer, connection, echo, sends, balanced, run } = makeFuzzer();
    let notified = 0;
    fuzzer.addListener (() => ++notified);
    fuzzer.setExcluded ("volume", true);
    check (! fuzzer.hasSnapshot, "no snapshot before fuzzing");

    fuzzer.randomiseAll();
    check (fuzzer.hasSnapshot && fuzzer.snapshotSize === 7 && notified >= 2, "the first randomise takes a snapshot of every parameter's value");

    // the server echoes the new values, and someone else moves the excluded one
    for (const m of sends()) echo (m.id, m.value);
    echo ("volume", 0.3);
    fuzzer.randomiseAll();
    fuzzer.automate ("x", { mode: "sine", depth: 1 });
    run (300);

    connection.sent.length = 0;
    const restored = fuzzer.restore();
    const restoredValues = Object.fromEntries (sends().map (m => [m.id, m.value]));
    check (! fuzzer.isAutomating ("x") && fuzzer.numAutomations === 0, "Restore stops the automation");
    check (restored === 6 && JSON.stringify (restoredValues) === JSON.stringify ({ x: 0.5, cutoff: 1000, steps: 2, bypass: 0, shape: 1, voices: 4 }),
           `...and sends back the values from before the first fuzz action, to what it changed (${JSON.stringify (restoredValues)})`);
    check (! ("volume" in restoredValues), "...leaving alone what it didn't change (another client's change stays)");
    const afterEnd = connection.sent.slice (1).map (m => m.type.replace ("send_", "")).join();
    check (connection.sent[0].type === "send_gesture_end" && connection.sent[0].id === "x"
             && afterEnd === Array (6).fill ("gesture_start,value,gesture_end").join(), "...with the automation's gesture ended first, and each value as a gesture");
    check (! fuzzer.hasSnapshot && fuzzer.restore() === 0, "the snapshot is gone afterwards");

    echo ("x", 0.1);
    fuzzer.automate ("x");
    check (fuzzer.hasSnapshot && fuzzer.snapshot.get ("x") === 0.1, "the next fuzz action (here an automation) takes a new snapshot");
    fuzzer.clearSnapshot();
    check (! fuzzer.hasSnapshot, "clearSnapshot() forgets it");
    fuzzer.dispose();
});

await test ("fuzzer: time-based, and no jump after a hidden page", () =>
{
    const { fuzzer, frameAt, getTime } = makeFuzzer();
    fuzzer.automate ("x", { mode: "sine", rate: 0.25, depth: 0.5 });
    const a = fuzzer.automations.get ("x");
    frameAt (getTime() + 16);
    const phase = a.phase;
    frameAt (getTime() + 60000);      // (rAF stopped for a minute)
    check (a.phase - phase <= 0.25 * maxStepMs / 1000 + 1e-12 && a.phase > phase, `a minute's gap moves it on by one clamped frame at most (${(a.phase - phase).toFixed (4)} cycles)`);
    const p2 = a.phase;
    fuzzer.handleFrame (100000);
    check (near (a.phase - p2, 0.25 * maxStepMs / 1000), "the fuzzer clamps dt itself too");

    // the same time at different frame rates reaches the same phase
    const phases = [30, 60, 144].map (fps =>
    {
        const f = makeFuzzer();
        f.fuzzer.automate ("x", { mode: "sine", rate: 0.5 });
        f.run (1000, fps);
        const p = f.fuzzer.automations.get ("x").phase;
        f.fuzzer.dispose();
        return p;
    });
    check (phases.every (p => Math.abs (p - phases[0]) < 0.02), `the phase follows time, not frames (${phases.map (p => p.toFixed (3))})`);

    // changes from elsewhere don't disturb it
    const { fuzzer: f2, echo } = makeFuzzer();
    f2.automate ("x", { mode: "sine", rate: 1, depth: 0.5 });
    f2.advance (100);
    echo ("x", 0.01);
    f2.advance (150);
    check (near (f2.automations.get ("x").value, 0.75), "another client's change to the value doesn't move the automation's phase");
    f2.dispose();
    fuzzer.dispose();
});

await test ("fuzzer: settings, commands and helpers", () =>
{
    const { fuzzer, settings } = makeFuzzer();
    fuzzer.saveAutomationSettings ("x", { mode: "ramp", rate: 0.2 });
    check (fuzzer.getAutomationSettings ("x").mode === "ramp" && settings.values.fuzzAutomation.x.rate === 0.2, "automation settings are remembered per parameter");
    check (normaliseAutomationSettings ({ mode: "zigzag", rate: 1000, depth: -1 }).mode === "sine" && normaliseAutomationSettings ({ rate: 1000 }).rate === 20
             && normaliseAutomationSettings ({ depth: -1 }).depth === 0, "settings are normalised and clamped");
    check (isLikelyOutputLevel (fuzzParameters[6]) && ! isLikelyOutputLevel (fuzzParameters[1]), "volume/gain-like names are spotted (for a suggestion only)");
    check (! fuzzer.isExcluded ("volume"), "...but nothing is excluded unless the user says so");

    let current = null;
    const commands = new CommandRegistry();
    commands.registerAll (createFuzzCommands (() => current));
    const enabled = () => commands.getAll().map (c => c.id).filter (id => id.startsWith ("parameters.")).join();
    check (enabled() === "", "without a patch, the commands are disabled");
    current = fuzzer;
    check (enabled() === "parameters.randomise", "with parameters: Randomise only");
    check (commands.run ("parameters.randomise") && enabled() === "parameters.randomise,parameters.restore", "after a randomise: Restore too");
    fuzzer.automate ("x");
    check (enabled() === "parameters.randomise,parameters.restore,parameters.stopAutomation", "with an automation: Stop all too");
    check (commands.run ("parameters.stopAutomation") && fuzzer.numAutomations === 0, "Stop all runs");
    check (commands.run ("parameters.restore") && ! fuzzer.hasSnapshot, "Restore runs");
    check (commands.getAll().filter (c => c.id.startsWith ("parameters.")).every (c => c.category === "Parameters" && ! c.shortcut), "category Parameters, no shortcuts");

    check (parseAutomationRate ("sine", "2") === 2 && parseAutomationRate ("ramp", "2") === 0.5 && parseAutomationRate ("ramp", "500 ms") === 2
             && parseAutomationRate ("sine", "4 s") === 0.25 && parseAutomationRate ("sine", "x") === null && parseAutomationRate ("sine", "100") === 20,
           "parseAutomationRate: Hz for LFOs, seconds for ramp/random, with units, clamped");
    check (formatAutomationRate ("sine", 0.5) === "0.5 Hz" && formatAutomationRate ("random", 0.5) === "2 s", "formatAutomationRate");
    check (parseDepth ("25%") === 0.25 && parseDepth ("150") === 1 && parseDepth ("") === null, "parseDepth");
    fuzzer.dispose();
});

await test ("fuzzer: the toolbar and the automation editor", () =>
{
    const { fuzzer } = makeFuzzer();
    const toolbar = new FuzzToolbar (fuzzer);
    const editor = new AutomationEditor (fuzzer);
    document.body.append (toolbar.element, editor.element);

    check (! toolbar.randomiseButton.disabled && toolbar.restoreButton.disabled && toolbar.snapshotChip.hidden
             && toolbar.element.querySelector (".cp-fuzz-running").hidden, "at first: Randomise only, no snapshot, nothing running");
    check (toolbar.element.getAttribute ("role") === "toolbar" && toolbar.randomiseButton.getAttribute ("aria-label"), "a labelled toolbar");

    editor.setParameter (fuzzParameters[0]);
    const modeButton = (mode) => editor.modeButtons.find (b => b.dataset.mode === mode);
    check (modeButton ("off").getAttribute ("aria-pressed") === "true", "the editor shows Off for a parameter that isn't automated");
    modeButton ("triangle").click();
    check (fuzzer.getAutomation ("x")?.mode === "triangle" && modeButton ("triangle").getAttribute ("aria-pressed") === "true", "choosing Triangle starts it");
    check (toolbar.countText.textContent === "1 automated" && ! toolbar.element.querySelector (".cp-fuzz-running").hidden && ! toolbar.restoreButton.disabled && ! toolbar.snapshotChip.hidden,
           "the toolbar shows it running, and the snapshot");

    editor.rateInput.value = "3 Hz";
    editor.rateInput.dispatchEvent (new Event ("change"));
    editor.depthInput.value = "80";
    editor.depthInput.dispatchEvent (new Event ("change"));
    check (fuzzer.getAutomation ("x").rate === 3 && fuzzer.getAutomation ("x").depth === 0.8, "typing a rate and depth changes the running automation");
    modeButton ("ramp").click();
    check (editor.rateLabel.textContent === "Period" && editor.rateInput.value === "0.33 s", `ramp shows a period (${editor.rateInput.value})`);
    check (editor.smoothRow.hidden, "smoothing only shows for random");
    modeButton ("random").click();
    check (! editor.smoothRow.hidden, "...like this");

    editor.excludeSwitch.click();
    check (fuzzer.isExcluded ("x") && editor.excludeSwitch.getAttribute ("aria-checked") === "true", "the exclude switch excludes it");
    editor.excludeSwitch.click();
    editor.setParameter (fuzzParameters[6]);
    check (! editor.excludeHint.hidden, "a volume parameter gets the suggestion to exclude it");
    editor.setParameter (fuzzParameters[0]);

    fuzzer.handleUserGesture ("x");
    check (modeButton ("off").getAttribute ("aria-pressed") === "true" && toolbar.element.querySelector (".cp-fuzz-running").hidden, "a grab elsewhere shows as Off");
    editor.rateInput.value = "5 s";
    editor.rateInput.dispatchEvent (new Event ("change"));
    check (! fuzzer.isAutomating ("x") && fuzzer.getAutomationSettings ("x").rate === 0.2, "changing settings while Off keeps them for next time, without starting it");
    modeButton ("off").click();

    toolbar.restoreButton.click();
    check (toolbar.restoreButton.disabled && toolbar.snapshotChip.hidden, "Restore, and it's disabled again");

    toolbar.dispose();
    editor.dispose();
    fuzzer.dispose();
});

//==============================================================================
// core/recorder.js (feature 4, recording)
const { RecordingManager, RecordingStopReason, recordingBlockSize, bytesPerRecordingBlock, alignmentWindowFrames,
        defaultRecordingCapBytes, maxRecordingMarkers } = await import ("/panel_api/core/recorder.js");
const recorderWAV = await import ("/panel_api/core/wav-encoder.js");

// A fake hub: subscribe() / subscribeParameter() like EndpointDataHub's, plus send() and setParameter() to drive them
function createFakeRecorderHub()
{
    const feeds = new Map(), parameterFeeds = new Map(), parameterValues = new Map();
    const addTo = (map, id, item) => { if (! map.has (id)) map.set (id, new Set()); map.get (id).add (item); return () => map.get (id).delete (item); };

    return {
        subscriptions: [],
        subscribe (endpointID, listener, options) { this.subscriptions.push ({ endpointID, options }); return addTo (feeds, endpointID, listener); },
        subscribeParameter (endpointID, listener)
        {
            const remove = addTo (parameterFeeds, endpointID, listener);

            if (parameterValues.has (endpointID))
                listener (parameterValues.get (endpointID));

            return remove;
        },
        send (endpointID, message)          { for (const l of [...(feeds.get (endpointID) ?? [])]) l (message); },
        setParameter (endpointID, value)    { parameterValues.set (endpointID, value); for (const l of [...(parameterFeeds.get (endpointID) ?? [])]) l (value); },
        numListeners (endpointID)           { return feeds.get (endpointID)?.size ?? 0; },
        numParameterListeners (endpointID)  { return parameterFeeds.get (endpointID)?.size ?? 0; }
    };
}

// A deterministic test signal, exactly representable as float32
const recorderSignal = (frame, channel) => Math.fround (Math.sin (frame * 0.0123 * (channel + 1)) * 0.9 + ((frame * 7919 + channel * 104729) % 1000) / 10000);

// A full-audio message for frames [start, start + n) of the signal (frame omitted when it's null)
function recorderMessage (frame, numFrames, numChannels = 2, signalStart = frame)
{
    const data = [];

    for (let ch = 0; ch < numChannels; ++ch)
    {
        const samples = new Float32Array (numFrames);

        for (let i = 0; i < numFrames; ++i)
            samples[i] = recorderSignal (signalStart + i, ch);

        data.push (samples);
    }

    return frame === null ? { data } : { frame, data };
}

function createTestRecordingManager (options = {})
{
    const queue = [];
    const manager = new RecordingManager ({ scheduleNotification: fn => queue.push (fn), ...options });
    const flush = () => { for (const fn of queue.splice (0)) fn(); };
    return { manager, queue, flush };
}

// true if the recording's frames [recStart, recStart + n) of a channel match the signal from signalStart
function recordingMatchesSignal (recording, endpointID, channel, recStart, n, signalStart)
{
    const samples = recording.getSamples (endpointID, channel, recStart, n);

    for (let i = 0; i < n; ++i)
        if (samples[i] !== recorderSignal (signalStart + i, channel))
            return false;

    return true;
}

const isAllZero = (a) => a.every (v => v === 0);

await test ("recorder: contiguous chunks give the exact samples back", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });

    check (hub.subscriptions.length === 1 && hub.subscriptions[0].options.granularity === 1024 && hub.subscriptions[0].options.fullAudioData,
           "one shared full-audio subscription per endpoint");
    check (recording.isRecording && recording.state === "recording" && recording.stopReason === null, "it starts recording");
    check (/^Recording 1 – \d\d:\d\d:\d\d$/.test (recording.name), "the default name has a number and the time: " + recording.name);

    for (let i = 0; i < 10; ++i)
        hub.send ("out", recorderMessage (5000 + i * 1024, 1024));

    check (recording.originFrame === 5000 && recording.numFrames === 10240, "the first chunk's frame is recording frame 0");
    check (Math.abs (recording.durationSeconds - 10240 / 48000) < 1e-12, "durationSeconds");
    check (recordingMatchesSignal (recording, "out", 0, 0, 10240, 5000) && recordingMatchesSignal (recording, "out", 1, 0, 10240, 5000),
           "both channels come back exactly");
    check (recording.dropouts.length === 0 && ! recording.isUnaligned, "no dropouts, aligned");

    const info = recording.endpoints[0];
    check (info.endpointID === "out" && info.numChannels === 2 && info.numFrames === 10240 && info.firstServerFrame === 5000
             && info.leadingSilenceFrames === 0 && info.overlaps.count === 0, "the endpoint info");
    check (isAllZero (recording.getSamples ("out", 0, 10240, 100)) && isAllZero (recording.getSamples ("out", 0, -100, 100)),
           "outside the recording reads as silence");
    check (isAllZero (recording.getSamples ("nope", 0, 0, 10)) && isAllZero (recording.getSamples ("out", 5, 0, 10)), "...as do unknown endpoints and channels");

    hub.send ("out", { min: [0], max: [1], frame: 99999 });
    check (recording.numFrames === 10240, "min/max messages are ignored");

    recording.stop();
    check (! recording.isRecording && recording.stopReason.code === RecordingStopReason.user && hub.numListeners ("out") === 0,
           "stop() stops it and releases the subscription");
    hub.send ("out", recorderMessage (5000 + 10240, 1024));
    check (recording.numFrames === 10240, "nothing is added after stopping");

    let threw = false;
    try { manager.start ({ endpoints: [], hub, sampleRate: 48000 }); } catch (e) { threw = true; }
    check (threw, "start() throws without endpoints");
    threw = false;
    try { manager.start ({ endpoints: ["out"], sampleRate: 48000 }); } catch (e) { threw = true; }
    check (threw, "...or without a hub");
    check (manager.recordings.length === 1, "failed starts don't add recordings");
    manager.dispose();
});

await test ("recorder: gaps, overlaps and backwards chunks", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 44100 });

    for (let i = 0; i < 4; ++i)
        hub.send ("out", recorderMessage (i * 1024, 1024));

    hub.send ("out", recorderMessage (4096 + 300, 1024));
    check (recording.numFrames === 4096 + 300 + 1024, "a gap moves the chunk to its frame");
    check (recording.dropouts.length === 1 && recording.dropouts[0].startFrame === 4096 && recording.dropouts[0].numFrames === 300
             && recording.dropouts[0].endpointID === "out", "...and is a dropout with the right frames");
    check (isAllZero (recording.getSamples ("out", 0, 4096, 300)) && isAllZero (recording.getSamples ("out", 1, 4096, 300)), "the gap is silence");
    check (recordingMatchesSignal (recording, "out", 0, 4396, 1024, 4396), "the chunk after it is in the right place");

    // an exact repeat, a partial overlap, and one going backwards
    hub.send ("out", recorderMessage (4396, 1024));
    check (recording.numFrames === 5420 && recording.endpoints[0].overlaps.count === 1 && recording.endpoints[0].overlaps.frames === 1024,
           "a repeated chunk is dropped and counted");
    hub.send ("out", recorderMessage (5420 - 512, 1024));
    check (recording.numFrames === 5420 + 512 && recording.endpoints[0].overlaps.frames === 1024 + 512,
           "a partial overlap keeps only its new frames");
    check (recordingMatchesSignal (recording, "out", 0, 0, 4096, 0) && recordingMatchesSignal (recording, "out", 1, 4396, 1536, 4396),
           "...and they're the right ones");
    hub.send ("out", recorderMessage (0, 1024));
    check (recording.numFrames === 5932 && recording.endpoints[0].overlaps.count === 3, "a chunk going backwards is dropped and counted");
    check (recording.dropouts.length === 1, "overlaps aren't dropouts");
    manager.dispose();
});

await test ("recorder: chunks without frame (an older server)", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });

    for (let i = 0; i < 5; ++i)
        hub.send ("out", recorderMessage (null, 1000, 1, i * 1000));

    check (recording.numFrames === 5000 && recording.isUnaligned && recording.endpoints[0].isUnaligned, "appended back to back, and unaligned");
    check (recording.originFrame === undefined && recording.dropouts.length === 0, "no origin and no dropouts");
    check (recordingMatchesSignal (recording, "out", 0, 0, 5000, 0), "the samples are exact");
    check (recording.endpoints[0].numChannels === 1, "mono");

    // an aligned recording that later loses its frames carries on and becomes unaligned
    const second = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    recording.stop();
    hub.send ("out", recorderMessage (8192, 1024, 1));
    check (! second.isUnaligned, "aligned while frames come");
    hub.send ("out", recorderMessage (null, 1024, 1, 8192 + 1024));
    check (second.numFrames === 2048 && second.isUnaligned && recordingMatchesSignal (second, "out", 0, 0, 2048, 8192),
           "a chunk without frame is appended, and marks it unaligned");
    manager.dispose();
});

await test ("recorder: several endpoints share one timeline", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out", { endpointID: "in" }], hub, sampleRate: 48000 });

    hub.send ("out", recorderMessage (2048 + 100, 1024));
    check (recording.numFrames === 0 && recording.isRecording, "waits for every endpoint's first chunk");
    hub.send ("in", recorderMessage (1024 + 100, 1024, 1));
    check (recording.originFrame === 1124, "the earliest first frame is the origin");
    check (recording.getEndpointInfo ("out").leadingSilenceFrames === 1024 && recording.getEndpointInfo ("in").leadingSilenceFrames === 0,
           "the later endpoint starts with leading silence");
    check (recording.dropouts.length === 0, "...which isn't a dropout");
    check (isAllZero (recording.getSamples ("out", 0, 0, 1024)) && recordingMatchesSignal (recording, "out", 0, 1024, 1024, 2148)
             && recordingMatchesSignal (recording, "in", 0, 0, 1024, 1124), "both are on the same timeline");

    for (let i = 1; i < 4; ++i)
    {
        hub.send ("out", recorderMessage (2148 + i * 1024, 1024));
        hub.send ("in", recorderMessage (1124 + i * 1024, 1024, 1));
    }

    check (recording.numFrames === 5 * 1024 && recording.getEndpointInfo ("in").numFrames === 4096, "numFrames is the longest endpoint");
    check (recordingMatchesSignal (recording, "out", 1, 1024, 4096, 2148) && recordingMatchesSignal (recording, "in", 0, 0, 4096, 1124),
           "the samples line up");
    check (recording.endpoints.map (e => e.numChannels).join() === "2,1", "each endpoint has its own channels");

    const wav = recording.getWAVBlockSource ("in");
    check (wav.numFrames === 5 * 1024 && wav.numChannels === 1, "every endpoint's WAV covers the common timeline");
    recording.stop();

    // an endpoint that never sends doesn't hold the others up for ever
    const second = manager.start ({ endpoints: ["out", "silent"], hub, sampleRate: 48000 });
    let frame = 50000;

    while (frame - 50000 < alignmentWindowFrames)
    {
        hub.send ("out", recorderMessage (frame, 1024));
        frame += 1024;
    }

    check (second.numFrames === frame - 50000 && second.originFrame === 50000, "it starts without it after alignmentWindowFrames");
    hub.send ("silent", recorderMessage (frame - 1024, 1024, 1));
    check (second.getEndpointInfo ("silent").leadingSilenceFrames === frame - 1024 - 50000 && second.dropouts.length === 0,
           "a late endpoint gets leading silence");
    hub.send ("silent", recorderMessage (40000, 1024, 1));
    check (second.getEndpointInfo ("silent").overlaps.count === 1, "a chunk before the origin from it is dropped");

    // stopping while still waiting keeps what's held
    const third = manager.start ({ endpoints: ["out", "silent"], hub, sampleRate: 48000 });
    hub.send ("out", recorderMessage (90000, 1024));
    third.stop();
    check (third.numFrames === 1024 && recordingMatchesSignal (third, "out", 0, 0, 1024, 90000), "stopping before alignment keeps the held chunks");
    manager.dispose();
});

await test ("recorder: chunks straddling block boundaries", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    const total = 2 * recordingBlockSize + 5000;

    for (let frame = 0; frame < total; frame += 1000)
        hub.send ("out", recorderMessage (frame, 1000));

    check (recording.numFrames === Math.ceil (total / 1000) * 1000, "all the frames arrived");
    check (recordingMatchesSignal (recording, "out", 0, 0, recording.numFrames, 0) && recordingMatchesSignal (recording, "out", 1, 0, recording.numFrames, 0),
           "every sample is right across the 65536-frame blocks");
    check (recordingMatchesSignal (recording, "out", 1, recordingBlockSize - 3, 6, recordingBlockSize - 3)
             && recordingMatchesSignal (recording, "out", 0, 2 * recordingBlockSize - 1, 2, 2 * recordingBlockSize - 1), "...including at the boundaries");
    check (recording.usedBytes === 3 * 2 * bytesPerRecordingBlock && manager.usedBytes === recording.usedBytes, "three blocks per channel are counted");
    manager.dispose();
});

await test ("recorder: getMinMax matches brute force at every zoom", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    let frame = 0;

    // 3+ blocks, with a gap inside a block, one across a whole block and its mip levels, and an odd chunk size
    while (frame < 230000)
    {
        hub.send ("out", recorderMessage (frame, 777));
        frame += 777;

        if (frame > 30000 && frame < 31000)     frame += 1234;
        if (frame > 70000 && frame < 71000)     frame += 2 * recordingBlockSize + 5000;
    }

    check (recording.dropouts.length === 2 && recording.dropouts[1].numFrames === 2 * recordingBlockSize + 5000, "two dropouts, one longer than two blocks");
    check (recording.usedBytes < 4 * 2 * bytesPerRecordingBlock, "a block wholly inside a gap isn't allocated");

    const n = recording.numFrames;
    const all = [recording.getSamples ("out", 0, 0, n), recording.getSamples ("out", 1, 0, n)];

    const bruteForce = (channel, start, end, numBuckets) =>
    {
        const min = new Float32Array (numBuckets), max = new Float32Array (numBuckets);
        const span = (end - start) / numBuckets;

        for (let i = 0; i < numBuckets; ++i)
        {
            const a = Math.floor (start + i * span), b = Math.max (a + 1, Math.floor (start + (i + 1) * span));
            let mn = Infinity, mx = -Infinity;

            for (let f = a; f < b; ++f)
            {
                const v = f >= 0 && f < n ? all[channel][f] : 0;
                mn = Math.min (mn, v);
                mx = Math.max (mx, v);
            }

            min[i] = mn;
            max[i] = mx;
        }

        return { min, max };
    };

    const zooms = [[0, n, 10], [0, n, 1000], [0, n, 37], [1000, 140000, 333], [4000, 4200, 3], [4090, 4110, 20], [65000, 66100, 7],
                   [10, 20, 40], [-500, n + 1000, 50], [30000, 32000, 9], [60000, 160000, 1], [255, 4097 * 3, 5], [131000, 200000, 17]];

    for (const [start, end, numBuckets] of zooms)
    {
        for (let channel = 0; channel < 2; ++channel)
        {
            const result = recording.getMinMax ("out", channel, start, end, numBuckets);
            const expected = bruteForce (channel, start, end, numBuckets);
            let same = result.min.length === numBuckets;

            for (let i = 0; i < numBuckets; ++i)
                same = same && result.min[i] === expected.min[i] && result.max[i] === expected.max[i];

            check (same, `getMinMax (${start}, ${end}, ${numBuckets}) on channel ${channel}`);
        }
    }

    const out = { min: new Float32Array (8), max: new Float32Array (8) };
    const reused = recording.getMinMax ("out", 0, 0, n, 8, out);
    check (reused.min === out.min && reused.max === out.max, "out arrays are reused");
    check (isAllZero (recording.getMinMax ("nope", 0, 0, n, 4).max), "an unknown endpoint is silence");

    // the zoomed-out query is fast: it reads the mip levels, not the samples
    const start = performance.now();

    for (let i = 0; i < 100; ++i)
        recording.getMinMax ("out", 0, 0, n, 1000, out);

    const ms = performance.now() - start;
    console.log (`recorder: getMinMax of ${n} frames into 1000 buckets took ${(ms / 100).toFixed (3)} ms`);
    check (ms < 2000, "100 zoomed-out queries run quickly");
    manager.dispose();
});

await test ("recorder: the memory cap stops the recording, and discard frees it", () =>
{
    const hub = createFakeRecorderHub();
    const { manager, flush } = createTestRecordingManager ({ capBytes: 3 * bytesPerRecordingBlock });
    check (new RecordingManager().capBytes === defaultRecordingCapBytes && defaultRecordingCapBytes === 256 * 1024 * 1024, "the default cap is 256 MB");

    let managerCalls = 0;
    manager.addListener (() => ++managerCalls);
    const stereo = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    check (managerCalls === 1, "the manager reports a new recording straight away");

    for (let frame = 0; frame < recordingBlockSize + 4096; frame += 1000)
        hub.send ("out", recorderMessage (frame, 1000));

    check (! stereo.isRecording && stereo.stopReason.code === RecordingStopReason.memoryLimit, "it stops when the next block doesn't fit");
    check (/memory limit/.test (stereo.stopReason.message) && /MB/.test (stereo.stopReason.message), "with a message the UI can show: " + stereo.stopReason.message);
    check (stereo.numFrames === recordingBlockSize, "it keeps everything up to the cap");
    check (recordingMatchesSignal (stereo, "out", 1, 0, recordingBlockSize, 0), "...exactly");
    check (stereo.usedBytes === 2 * bytesPerRecordingBlock && manager.usedBytes === 2 * bytesPerRecordingBlock && hub.numListeners ("out") === 0,
           "usedBytes, and the subscription is released");

    const mono = manager.start ({ endpoints: ["mono"], hub, sampleRate: 48000 });
    hub.send ("mono", recorderMessage (0, 1024, 1));
    check (mono.isRecording && manager.usedBytes === 3 * bytesPerRecordingBlock && manager.remainingBytes === 0, "the cap is shared");
    hub.send ("mono", recorderMessage (1024, 1024, 1));
    check (mono.isRecording, "...and the block it has keeps filling");

    stereo.discard();
    check (stereo.isDiscarded && manager.usedBytes === bytesPerRecordingBlock && manager.recordings.length === 1 && manager.recordings[0] === mono,
           "discard frees its blocks and removes it");
    check (isAllZero (stereo.getSamples ("out", 0, 0, 100)), "a discarded recording holds no samples");

    manager.setCapBytes (bytesPerRecordingBlock / 2);
    hub.send ("mono", recorderMessage (2048, recordingBlockSize, 1));
    check (mono.stopReason?.code === RecordingStopReason.memoryLimit && mono.numFrames === recordingBlockSize, "lowering the cap stops it at its next block");

    manager.discardAll();
    flush();
    check (manager.usedBytes === 0 && manager.recordings.length === 0, "discardAll frees everything");
    manager.dispose();
});

await test ("recorder: markers", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });

    for (let i = 0; i < 3; ++i)
        hub.send ("out", recorderMessage (7000 + i * 1024, 1024));

    const marker = recording.addMarker ({ label: "here" });
    check (marker.frame === 3072 && marker.isApproximate && marker.kind === "user" && marker.label === "here" && typeof marker.time === "number",
           "a marker is stamped with the latest frame received, and approximate");
    const exact = recording.addMarker ({ frame: 100, label: "exact" });
    check (exact.frame === 100 && ! exact.isApproximate && recording.markers[0] === exact, "a marker with a frame is exact, and kept in frame order");

    hub.setParameter ("gain", 0.5);
    const unwatch = recording.watchEvents ({ events: [{ endpointID: "midiOut", kind: "midi" }, { endpointID: "trigger", name: "Trigger" }],
                                             parameters: [{ endpointID: "gain", name: "Gain" }] });
    check (recording.markers.length === 2, "a parameter's current value isn't a change");

    hub.send ("midiOut", { message: 0x903c64 });
    const midiMarker = recording.markers[recording.markers.length - 1];
    check (midiMarker.kind === "midi" && midiMarker.endpointID === "midiOut" && /Note-On/.test (midiMarker.label) && midiMarker.frame === 3072,
           "a MIDI event becomes a marker: " + midiMarker.label);
    hub.send ("trigger", 3);
    check (recording.markers[recording.markers.length - 1].label === "Trigger = 3", "an event becomes a marker");

    hub.setParameter ("gain", 0.25);
    hub.setParameter ("gain", 0.75);
    const parameterMarkers = recording.markers.filter (m => m.kind === "parameter");
    check (parameterMarkers.length === 1 && parameterMarkers[0].value === 0.75 && parameterMarkers[0].label === "Gain = 0.75",
           "parameter changes within one chunk share one marker");
    hub.send ("out", recorderMessage (7000 + 3072, 1024));
    hub.setParameter ("gain", 0.1);
    check (recording.markers.filter (m => m.kind === "parameter").length === 2 && recording.markers[recording.markers.length - 1].frame === 4096,
           "a change in a later chunk is a new one, at the new latest frame");

    unwatch();
    check (hub.numListeners ("midiOut") === 0 && hub.numParameterListeners ("gain") === 0, "the unsubscribe function removes them");
    recording.watchEvents ({ events: ["trigger"] });
    recording.stop();
    check (hub.numListeners ("trigger") === 0, "stopping removes the event subscriptions");
    const count = recording.markers.length;
    hub.send ("trigger", 1);
    check (recording.markers.length === count, "...so nothing more is added");

    recording.removeMarker (exact);
    check (! recording.markers.includes (exact), "removeMarker");
    check (recording.addMarker ({ frame: 2000, label: "later" })?.frame === 2000, "user markers can still be added after stopping");

    for (let i = recording.markers.length; i < maxRecordingMarkers; ++i)
        recording.addMarker ({ frame: i });

    check (recording.addMarker ({ frame: 1 }) === null && recording.droppedMarkerCount === 1 && recording.markers.length === maxRecordingMarkers,
           "markers are capped, and the dropped ones counted");
    manager.dispose();
});

await test ("recorder: WAV block sources round-trip through encodeWAVFromBlocks", () =>
{
    const hub = createFakeRecorderHub();
    const { manager } = createTestRecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });

    for (let frame = 0; frame < 140000; frame += 1024)
        hub.send ("out", recorderMessage (frame, 1024));

    recording.stop();
    const n = recording.numFrames;
    const sameBytes = (a, b) => { const x = new Uint8Array (a), y = new Uint8Array (b); return x.length === y.length && x.every ((v, i) => v === y[i]); };
    const concatenated = (start, end) => [0, 1].map (ch => recording.getSamples ("out", ch, start, end - start));

    for (const format of ["pcm16", "float32"])
    {
        const source = recording.getWAVBlockSource ("out");
        check (source.numChannels === 2 && source.numFrames === n && source.blockSize === recordingBlockSize, "the whole recording's source");
        check (sameBytes (recorderWAV.encodeWAVFromBlocks (source, 48000, { format }), recorderWAV.encodeWAV (concatenated (0, n), 48000, { format })),
               `the whole recording (${format}) gives the same bytes as encodeWAV`);

        for (const [start, end] of [[1000, 70000], [recordingBlockSize, 140000], [65530, 65540], [5, 5]])
        {
            const selection = recording.getWAVBlockSource ("out", { startFrame: start, endFrame: end });
            check (selection.numFrames === end - start, `the selection ${start}…${end} has the right length`);
            check (sameBytes (recorderWAV.encodeWAVFromBlocks (selection, 48000, { format }), recorderWAV.encodeWAV (concatenated (start, end), 48000, { format })),
                   `the selection ${start}…${end} (${format}) gives the same bytes`);
        }
    }

    const parts = [...recorderWAV.generateWAVParts (recording.getWAVBlockSource ("out", { startFrame: 1000, endFrame: 70000 }), 48000)];
    const joined = new Uint8Array (parts.reduce ((t, p) => t + p.length, 0));
    parts.reduce ((offset, p) => { joined.set (p, offset); return offset + p.length; }, 0);
    check (sameBytes (joined.buffer, recorderWAV.encodeWAV (concatenated (1000, 70000), 48000)), "generateWAVParts gives the same file for a mid-block selection");

    const clamped = recording.getWAVBlockSource ("out", { startFrame: -100, endFrame: n + 100 });
    check (clamped.numFrames === n, "a selection is clamped to the recording");
    const aligned = recording.getWAVBlockSource ("out");
    check (aligned.getBlock (0, 0) === aligned.getBlock (0, 0), "an aligned source hands out the stored blocks without copying");

    let threw = false;
    try { recording.getWAVBlockSource ("nope"); } catch (e) { threw = true; }
    check (threw, "an unknown endpoint throws");
    manager.dispose();
});

await test ("recorder: a rebuild in the middle, and the connection going away", () =>
{
    const connection = {
        listeners: [],
        addEndpointListener (endpointID, fn, granularity, fullAudioData) { this.listeners.push ({ endpointID, fn, granularity, fullAudioData }); },
        removeEndpointListener (endpointID, fn) { this.listeners = this.listeners.filter (l => l.fn !== fn); },
        addParameterListener() {}, removeParameterListener() {}, requestParameterValue() {}
    };

    const hub = new EndpointDataHub (connection);
    const panelConnection = { patchConnection: connection, hub };
    const { manager } = createTestRecordingManager();
    const analyserChunks = [];
    const unsubscribeAnalyser = subscribeToFullAudio (hub, "out", c => analyserChunks.push (c));
    const recording = manager.start ({ endpoints: ["out"], connection: panelConnection, sampleRate: 48000 });
    const send = (message) => { for (const l of connection.listeners) l.fn (message); };

    check (hub.numServerListeners === 1, "the recorder shares the analyser's server listener");

    for (let i = 0; i < 3; ++i)
        send (recorderMessage (1000 + i * 1024, 1024));

    // a rebuild: the old build's part-filled chunk is lost, and the new build starts at an odd frame, with 3 channels
    hub.handleNewBuild();
    manager.handleConnectionChanged (panelConnection);
    check (recording.isRecording, "a rebuild (same connection) doesn't stop it");
    const resume = 1000 + 3 * 1024 + 1500;

    for (let i = 0; i < 3; ++i)
        send (recorderMessage (resume + i * 1024, 1024, 3));

    check (recording.dropouts.length === 1 && recording.dropouts[0].startFrame === 3072 && recording.dropouts[0].numFrames === 1500,
           "the rebuild gap is a dropout");
    check (recording.numFrames === 3072 + 1500 + 3072 && recordingMatchesSignal (recording, "out", 0, 4572, 3072, resume)
             && recordingMatchesSignal (recording, "out", 1, 0, 3072, 1000), "the recording carries on after it");
    check (recording.endpoints[0].numChannels === 3 && isAllZero (recording.getSamples ("out", 2, 0, 4572))
             && recordingMatchesSignal (recording, "out", 2, 4572, 3072, resume), "a channel added by the rebuild is silent before it");
    check (analyserChunks.length === 6, "the analyser got every chunk too");

    manager.handleConnectionChanged ({ patchConnection: connection, hub: new EndpointDataHub (connection) });
    check (! recording.isRecording && recording.stopReason.code === RecordingStopReason.connectionLost && /unloaded or changed/.test (recording.stopReason.message),
           "a different connection stops it, with a reason");
    check (hub.numServerListeners === 1, "...and releases its subscription (the analyser's remains)");
    unsubscribeAnalyser();

    const second = manager.start ({ endpoints: ["out"], hub: hub.createGroup(), sampleRate: 48000 });
    manager.stopRecordingsFor (hub);
    check (second.stopReason?.code === RecordingStopReason.connectionLost, "stopRecordingsFor matches a recording made through one of the hub's groups");

    const third = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    manager.handleConnectionChanged (null);
    check (third.stopReason?.code === RecordingStopReason.connectionLost && hub.numServerListeners === 0, "unloading the patch stops it");
    manager.dispose();
    check (manager.recordings.length === 0, "dispose() discards them all");
});

await test ("recorder: change notifications are throttled", () =>
{
    const hub = createFakeRecorderHub();
    const { manager, queue, flush } = createTestRecordingManager();
    let managerCalls = 0, recordingCalls = 0;
    manager.addListener (() => ++managerCalls);
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    recording.addListener ((r) => { if (r === recording) ++recordingCalls; });
    managerCalls = 0;

    for (let i = 0; i < 50; ++i)
        hub.send ("out", recorderMessage (i * 1024, 1024));

    check (recordingCalls === 0 && managerCalls === 0, "50 chunks notify nobody synchronously");
    check (queue.length === 2, "...and schedule one notification each for the recording and the manager");
    flush();
    check (recordingCalls === 1 && managerCalls === 1, "which is delivered once");
    flush();
    check (recordingCalls === 1, "nothing more until something changes");

    hub.send ("out", recorderMessage (50 * 1024, 1024));
    recording.stop();
    check (recordingCalls === 2 && managerCalls === 2, "stopping notifies straight away");
    flush();
    check (recordingCalls === 2, "...and replaces the pending notification");
    manager.dispose();
});

await test ("recorder: default notifications are coalesced on a timer", async () =>
{
    const hub = createFakeRecorderHub();
    const manager = new RecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    let calls = 0;
    recording.addListener (() => ++calls);

    for (let i = 0; i < 20; ++i)
        hub.send ("out", recorderMessage (i * 1024, 1024));

    await tick (40);
    check (calls === 1, "one notification for a batch of chunks");
    manager.dispose();
});

await test ("recorder: appending 60 s of stereo at 48 kHz", () =>
{
    const hub = createFakeRecorderHub();
    const manager = new RecordingManager();
    const recording = manager.start ({ endpoints: ["out"], hub, sampleRate: 48000 });
    const left = new Float32Array (1024).map ((_, i) => Math.sin (i * 0.05));
    const right = new Float32Array (1024).map ((_, i) => Math.cos (i * 0.03));
    const numChunks = Math.ceil (60 * 48000 / 1024);
    const start = performance.now();

    for (let i = 0; i < numChunks; ++i)
        hub.send ("out", { frame: i * 1024, data: [left, right] });

    const ms = performance.now() - start;
    window.recorderTiming = { numChunks, totalMs: ms, msPerChunk: ms / numChunks };
    console.log (`recorder: appending ${numChunks} stereo 1024-frame chunks (60 s at 48 kHz) took ${ms.toFixed (1)} ms (${(ms / numChunks * 1000).toFixed (1)} µs per chunk), ${(manager.usedBytes / 2 ** 20).toFixed (1)} MB`);
    check (recording.numFrames === numChunks * 1024 && recording.isRecording, "all of it was recorded");
    check (recording.getSamples ("out", 1, 30 * 48000 + 5, 1)[0] === right[(30 * 48000 + 5) % 1024], "...correctly");
    check (ms < 5000, "in reasonable time");
    manager.dispose();
});

//==============================================================================
// The spectrum analyser display (widgets/spectrum-analyser.js): its drawing maths, then the view
// with a fake subscription group and a manual frame scheduler

const spectrumUI = await import ("/panel_api/widgets/spectrum-analyser.js");

await test ("spectrum: the log frequency axis and the dB scale", () =>
{
    const { frequencyToX, xToFrequency, dbToY, yToDB } = spectrumUI;
    const near = (a, b, e = 1e-9) => Math.abs (a - b) < e;

    check (near (frequencyToX (20, 300, 20, 20000), 0) && near (frequencyToX (20000, 300, 20, 20000), 300), "the ends of the axis");
    check (near (frequencyToX (200, 300, 20, 20000), 100) && near (frequencyToX (2000, 300, 20, 20000), 200), "a decade per third, on 20 Hz…20 kHz");
    check (near (xToFrequency (150, 300, 20, 20000), Math.sqrt (20 * 20000), 1e-6), "the middle is the geometric mean");
    check ([31, 440, 1000, 15000].every (f => near (xToFrequency (frequencyToX (f, 517, 20, 22050), 517, 20, 22050), f, 1e-6)), "x ↔ frequency round trips");
    check (frequencyToX (0, 300, 20, 20000) === 0, "0 Hz goes to the left edge");

    check (dbToY (0, 200) === 0 && dbToY (-100, 200) === 200 && dbToY (-50, 200) === 100, "0 dB at the top, −100 at the bottom");
    check (dbToY (6, 200) === 0 && dbToY (-150, 200) === 200 && dbToY (NaN, 200) === 200 && dbToY (-Infinity, 200) === 200, "clamped to the scale");
    check (near (yToDB (dbToY (-37.5, 180), 180), -37.5), "y ↔ dB round trips");
    check (dbToY (-30, 100, -60, 0) === 50, "another range");
});

await test ("spectrum: collapsing bins onto pixel columns", () =>
{
    const { collapseBinsToColumns, frequencyToX } = spectrumUI;

    // dense: 1 Hz bins over 20 Hz…24 kHz in 100 columns: a single loud bin must survive
    const dense = new Float32Array (24001).fill (-100);
    dense[5000] = -3;
    dense[5001] = -40;
    const columns = collapseBinsToColumns (dense, 1, 100, 20, 24000);
    const column = Math.floor (frequencyToX (5000, 100, 20, 24000));
    check (columns.length === 100 && columns[column] === -3, "a column takes the highest of its bins");
    check (columns.filter (v => v === -3).length === 1 && columns.filter (v => v > -100).length <= 2, "...and only that column shows it");

    // every bin goes to exactly one column: the maxima over all the columns match
    const noisy = new Float32Array (24001).map ((_, k) => -60 - 30 * Math.abs (Math.sin (k * 0.37)));
    const noisyColumns = collapseBinsToColumns (noisy, 1, 100, 20, 24000);
    check (Math.max (...noisyColumns) === Math.max (...noisy.subarray (20, 24000)), "no bin is lost");

    // sparse: 100 Hz bins (level −k at bin k) in 400 columns over 20 Hz…2 kHz: interpolated
    const sparse = new Float32Array (101).map ((_, k) => -k);
    const sparseColumns = collapseBinsToColumns (sparse, 100, 400, 20, 2000);
    const centreOf = (i) => 20 * Math.pow (100, (i + 0.5) / 400);
    const errors = [10, 100, 250, 300].map (i => Math.abs (sparseColumns[i] - (-centreOf (i) / 100)));
    check (errors.every (e => e < 1e-4), `less than one bin per column: interpolated linearly (errors ${errors.map (e => e.toExponential (1))})`);

    let isMonotonic = true;

    for (let i = 1; i < 400; ++i)
        if (sparseColumns[i] > sparseColumns[i - 1] + 1e-6)
            isMonotonic = false;

    check (isMonotonic, "...smoothly, with no steps back");

    // above the last bin (the axis goes past Nyquist): the floor
    const short = collapseBinsToColumns (new Float32Array (11).fill (-20), 100, 50, 20, 5000, undefined, -200);
    check (short[49] === -200 && short[0] === -20, "columns above the last bin get the floor");

    const out = new Float32Array (64);
    check (collapseBinsToColumns (dense, 1, 64, 20, 24000, out) === out, "it fills the array it's given");
});

await test ("spectrum: gridlines", () =>
{
    const { getFrequencyGridlines, getLevelGridlines, formatFrequencyLabel } = spectrumUI;
    const lines = getFrequencyGridlines (20, 22050);
    const labels = lines.filter (l => l.isMajor).map (l => l.label);

    check (JSON.stringify (labels) === JSON.stringify (["50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"]),
           `1-2-5 major lines, labelled (${labels.join (" ")})`);
    check (lines.every (l => l.frequency > 20 && l.frequency < 22050), "none on the edges");
    check ([30, 40, 60, 70, 80, 90, 300, 3000, 9000].every (f => lines.some (l => l.frequency === f && ! l.isMajor && l.label === null)),
           "the other multiples are minor, unlabelled");
    check (lines.every ((l, i) => i === 0 || l.frequency > lines[i - 1].frequency), "in order");
    check (getFrequencyGridlines (20, 24000).some (l => l.frequency === 20000) && ! getFrequencyGridlines (20, 8000).some (l => l.frequency >= 8000),
           "up to Nyquist");
    check (formatFrequencyLabel (2500) === "2.5k" && formatFrequencyLabel (500) === "500", "labels");

    const tall = getLevelGridlines (-100, 0, 200);
    const short = getLevelGridlines (-100, 0, 100);
    check (JSON.stringify (tall.map (l => l.db)) === JSON.stringify ([0, -10, -20, -30, -40, -50, -60, -70, -80, -90]), "every 10 dB when there's room");
    check (JSON.stringify (tall.filter (l => l.label).map (l => l.label)) === JSON.stringify (["0", "−20", "−40", "−60", "−80"]),
           "labelled every 20 dB, with a real minus sign");
    check (JSON.stringify (short.map (l => l.db)) === JSON.stringify ([0, -20, -40, -60, -80]) && short.every (l => l.label), "every 20 dB in a short plot");
});

await test ("spectrum: note names and the readout", () =>
{
    const { getNoteName, formatSpectrumFrequency, formatSpectrumLevel, formatSpectrumReadout, describeSpectrum } = spectrumUI;

    check (getNoteName (440) === "A4" && getNoteName (261.63) === "C4" && getNoteName (1000) === "B5" && getNoteName (27.5) === "A0",
           "A4, middle C, 1 kHz (B5), A0");
    check (getNoteName (466.16) === "A#4" && getNoteName (452) === "A4" && getNoteName (8.18) === "C-1", "sharps, rounding, the lowest note");
    check (getNoteName (0) === "" && getNoteName (-5) === "" && getNoteName (30000) === "", "nothing out of range");

    const f = formatSpectrumFrequency;
    check (f (1000) === "1.00 kHz" && f (440) === "440 Hz" && f (62.5) === "62.5 Hz" && f (12500) === "12.5 kHz" && f (2345) === "2.35 kHz",
           "frequencies");
    check (f (999.7) === "1.00 kHz" && f (99.97) === "100 Hz" && f (9999) === "10.0 kHz", "...at the unit boundaries");

    check (formatSpectrumLevel (-12.07) === "−12.1 dB" && formatSpectrumLevel (-0.01) === "0.0 dB" && formatSpectrumLevel (-150) === "< −100 dB",
           "levels");
    check (formatSpectrumReadout (1000, -12.07) === "1.00 kHz · −12.1 dB · B5", `the readout (${formatSpectrumReadout (1000, -12.07)})`);
    check (formatSpectrumReadout (5, -50) === "5.0 Hz · −50.0 dB", "...with no note where there's none");

    check (describeSpectrum ({ frequency: 1000, db: -12.2 }) === "Spectrum: peak −12 dB at 1.00 kHz", "the accessible summary");
    check (describeSpectrum (null) === "Spectrum: no signal" && describeSpectrum ({ frequency: 100, db: -180 }) === "Spectrum: no signal", "...of silence");
});

await test ("spectrum: finding the peak, the channel choices, the saved options", () =>
{
    const { findSpectrumPeak, getSpectrumChannelOptions, normaliseSpectrumOptions, defaultSpectrumOptions } = spectrumUI;
    const sampleRate = 44100, size = 4096;
    const analyser = new SpectrumAnalyser ({ fftSize: size, sampleRate });
    const gain = Math.pow (10, -12 / 20);
    analyser.addBlock ([new Float32Array (size).map ((_, i) => gain * Math.sin (2 * Math.PI * 1000 * i / sampleRate))]);
    analyser.update();

    const peak = findSpectrumPeak (analyser.levelsDB, sampleRate / size, 20, sampleRate / 2);
    check (Math.abs (peak.frequency - 1000) < 2 && Math.abs (peak.db + 12) < 0.3, `a 1 kHz sine at −12 dBFS (${peak.frequency.toFixed (1)} Hz, ${peak.db.toFixed (2)} dB)`);
    check (findSpectrumPeak (analyser.levelsDB, sampleRate / size, 2000, 3000).db < -100, "a range without it");
    check (findSpectrumPeak (new Float32Array ([-50, -10, -50]), 1).frequency === 1, "a symmetric peak stays on its bin");
    check (findSpectrumPeak (new Float32Array (4), 1, 10, 20) === null, "no bins in range: null");

    check (getSpectrumChannelOptions (1).length === 0, "no channel choice for mono");
    check (getSpectrumChannelOptions (2).map (c => c.label).join () === "Average,L,R" && getSpectrumChannelOptions (2)[2].value === 1, "Average, L, R for stereo");
    check (getSpectrumChannelOptions (4).map (c => c.label).join () === "Average,Ch 1,Ch 2,Ch 3,Ch 4", "numbered beyond that");

    check (JSON.stringify (normaliseSpectrumOptions ({ averaging: "glacial", peakHold: "yes", fftSize: 1000, channel: -1 })) === JSON.stringify (defaultSpectrumOptions),
           "invalid options get the defaults");
    check (JSON.stringify (normaliseSpectrumOptions ({ averaging: "slow", peakHold: false, fftSize: 16384, channel: 1 }))
             === JSON.stringify ({ averaging: "slow", peakHold: false, fftSize: 16384, channel: 1 }), "valid ones are kept");
});

await test ("spectrum: SpectrumAnalyserView", () =>
{
    const { SpectrumAnalyserView } = spectrumUI;
    const frames = [];
    const scheduler = new FrameScheduler ((fn) => { frames.push (fn); return frames.length; }, () => { frames.length = 0; });
    let time = 0;
    const runFrame = () => { time += 16; const fns = frames.splice (0); fns.forEach (fn => fn (time)); };
    const subscriptions = [];
    const group = { subscribe: (id, fn, options) => { const s = { id, fn, options }; subscriptions.push (s); return () => subscriptions.splice (subscriptions.indexOf (s), 1); } };
    const store = new Store ({ deviceProperties: { rate: 44100 } });
    const settings = new PanelSettingsScopes (null);
    const endpoint = { endpointID: "out", purpose: "audio out", numAudioChannels: 2, annotation: { name: "Output" } };

    const view = new SpectrumAnalyserView ({ endpoint, group, scheduler, store, settings });
    document.body.append (view.element);
    view.canvas.width = 600;
    view.canvas.height = 160;

    check (subscriptions.length === 1 && subscriptions[0].id === "out" && subscriptions[0].options.fullAudioData === true
             && subscriptions[0].options.granularity === 1024, "it subscribes to the shared full-audio stream");
    check (view.sampleRate === 44100 && view.analyser.fftSize === 4096 && view.analyser.numChannels === 2, "the sample rate from the device, 4096 points, 2 channels");
    check (view.channelSelect?.options.length === 3 && view.toolbar.getAttribute ("role") === "toolbar" && view.canvas.getAttribute ("role") === "img",
           "a channel choice, a toolbar, and an image role");

    // a 1 kHz sine at −12 dBFS, in 1024-frame chunks
    const gain = Math.pow (10, -12 / 20);
    let frame = 0;
    const sendChunk = () =>
    {
        const left = Array.from ({ length: 1024 }, (_, i) => gain * Math.sin (2 * Math.PI * 1000 * (frame + i) / 44100));
        subscriptions[0]?.fn ({ data: [left, left], frame });
        frame += 1024;
    };

    for (let i = 0; i < 6; ++i) { sendChunk(); runFrame(); }

    check (view.numChunks === 6 && view.analyser.isFull, "the chunks reach the analyser");
    check (Math.abs (view.peak.frequency - 1000) < 11 && Math.abs (view.peak.db + 12) < 1, `the peak (${view.peak.frequency.toFixed (1)} Hz, ${view.peak.db.toFixed (2)} dB)`);
    check (view.levelColumns.length === view.getPlotArea().width && view.getPlotArea().width < 600 && Math.max (...view.levelColumns) > -13.5, "the trace is collapsed onto the plot's columns");

    // the summary and the keyboard cursor
    view.lastSummaryTime = -Infinity;     // (it's updated at most every 500 ms)
    sendChunk();
    runFrame();
    check (view.canvas.getAttribute ("aria-label") === "Spectrum of Output: peak −12 dB at 1.00 kHz", `the aria-label (${view.canvas.getAttribute ("aria-label")})`);
    view.canvas.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowRight", bubbles: true }));
    check (Math.abs (view.cursorFrequency - view.peak.frequency) < 1e-6, "the first arrow puts the cursor on the peak");
    view.canvas.dispatchEvent (new KeyboardEvent ("keydown", { key: "ArrowRight", shiftKey: true, bubbles: true }));
    check (Math.abs (view.cursorFrequency / view.peak.frequency - 2) < 1e-6, "Shift-arrow moves it an octave");
    runFrame();
    check (/^2\.00 kHz · (−\d+\.\d|< −100) dB · B6$/.test (view.readout.textContent) && view.readout.getAttribute ("aria-live") === "polite",
           `...and the readout follows it (${view.readout.textContent})`);
    view.canvas.dispatchEvent (new KeyboardEvent ("keydown", { key: "Escape", bubbles: true }));
    check (view.cursorFrequency === null, "Escape hides it");

    // the options, remembered
    view.averagingButtons[2].click();
    view.peakButton.click();
    view.fftSelect.value = "8192";
    view.fftSelect.dispatchEvent (new Event ("change"));
    check (view.analyser.averagingTime === 1 && view.analyser.peakDecayDBPerSecond === Infinity && view.analyser.fftSize === 8192,
           "slow averaging, no peak hold, 8192 points");
    check (view.averagingButtons[2].getAttribute ("aria-pressed") === "true" && view.peakButton.getAttribute ("aria-pressed") === "false" && view.resetButton.disabled,
           "...shown on the buttons");
    view.channelSelect.value = "1";
    view.channelSelect.dispatchEvent (new Event ("change"));
    check (view.analyser.channelMode === 1, "the R channel");
    check (JSON.stringify (settings.global.get ("spectrumOptions")) === JSON.stringify ({ averaging: "slow", peakHold: false, fftSize: 8192, channel: 1 }),
           "the options are saved");

    // it goes idle when no more data comes
    view.setOption ("averaging", "off");

    for (let i = 0; i < 10; ++i) { sendChunk(); runFrame(); }

    const wait = performance.now() + 300;
    while (performance.now() < wait) {}
    let n = 0;

    while (frames.length > 0 && n < 400) { runFrame(); ++n; }

    check (frames.length === 0 && ! view.isAnimating && ! scheduler.isRunning, `it stops drawing once the data stops and the trace settles (after ${n} frames)`);

    const second = new SpectrumAnalyserView ({ endpoint, group, scheduler, store, settings });
    check (second.analyser.fftSize === 8192 && second.options.averaging === "off" && second.channelSelect.value === "1", "a new analyser starts with the saved options");
    second.dispose();

    store.set ({ deviceProperties: { rate: 48000 } });
    check (view.sampleRate === 48000, "it follows a change of sample rate");

    view.dispose();
    check (subscriptions.length === 0 && ! view.element.isConnected, "disposing it unsubscribes and removes it");
    check (frames.length === 0 || (runFrame(), true), "...and cancels its drawing");
});

await test ("spectrum: the header toggle and the commands", () =>
{
    const { addSpectrumToggle, getShownSpectrumEndpoints, createSpectrumCommandProvider } = spectrumUI;
    const subscriptions = new Set();
    const group = { subscribe: (id, fn, options) => { const s = { id, options }; subscriptions.add (s); return () => subscriptions.delete (s); } };
    const settings = new PanelSettingsScopes (null);
    settings.setPatch ("/a/Spectrum.cmajorpatch");
    const store = new Store ({ deviceProperties: null });

    const makeWidget = () => new AudioInputWidget ({ endpointID: "in", purpose: "audio in", numAudioChannels: 1 },
                                                   { group, settings, store, size: "full", session: null });
    const widget = makeWidget();
    const toggle = addSpectrumToggle (widget, "spectrumEndpoints");
    document.body.append (widget.element);
    const fullAudioSubscriptions = () => [...subscriptions].filter (s => s.options?.fullAudioData).length;

    check (toggle.button.getAttribute ("aria-pressed") === "false" && toggle.button.getAttribute ("aria-label") === "Spectrum" && ! toggle.isShown,
           "an icon button in the header, off");
    check (widget.headerElement.contains (toggle.button) && widget.headerElement.contains (widget.valueElement), "...beside the value");
    toggle.button.click();
    check (toggle.isShown && toggle.button.getAttribute ("aria-pressed") === "true" && widget.display.element.nextSibling === toggle.view.element,
           "clicking it shows the analyser, under the meter");
    check (fullAudioSubscriptions() === 1 && JSON.stringify (getShownSpectrumEndpoints (settings, "spectrumEndpoints")) === '["in"]', "...subscribed, and remembered");
    widget.dispose();
    check (fullAudioSubscriptions() === 0, "disposing the widget disposes the analyser");

    const again = makeWidget();
    const toggle2 = addSpectrumToggle (again, "spectrumEndpoints");
    check (toggle2.isShown && fullAudioSubscriptions() === 1, "a new widget for the endpoint opens it again");
    toggle2.setShown (false);
    check (! toggle2.isShown && fullAudioSubscriptions() === 0 && getShownSpectrumEndpoints (settings, "spectrumEndpoints").length === 0, "closing it forgets it");
    again.dispose();

    const shown = [];
    let endpoints = [{ endpointID: "in", purpose: "audio in" }, { endpointID: "gain", purpose: "parameter" }, { endpointID: "out", purpose: "audio out", annotation: { name: "Main" } }];
    const provider = createSpectrumCommandProvider ({ getAudioEndpoints: () => endpoints, showSpectrum: (id) => shown.push (id) });
    const commands = provider();
    check (commands.map (c => c.title).join ("|") === "Show spectrum for in|Show spectrum for Main" && commands.every (c => c.category === "Audio"),
           "a command per audio endpoint, in Audio");
    commands[1].run();
    check (shown.join () === "out", "...which shows its spectrum");
    endpoints = [];
    check (provider().length === 0, "none without a patch");
});

//==============================================================================
import { frameToX, xToFrame, fitViewport, clampViewport, zoomViewport, panViewport, viewportAtEnd, isViewportAtEnd, isViewportFitted,
         getWheelZoomFactor, chooseWaveformDrawMode, niceStep, getRulerTicks, formatRulerTime, normaliseSelection, extendSelection,
         describeSelection, formatSampleValue, getWaveformLanes, minFramesPerPixel } from "/panel_api/widgets/waveform-view.js";
import { sanitiseFileName, getRecordingFileName, formatBytes, formatRecordingTime, describeSaveResult, normaliseCapMB,
         createWAVBlob } from "/panel_api/views/recordings-view.js";


await test ("waveform: time ↔ x, zoom and pan", () =>
{
    const view = { start: 1000, framesPerPixel: 10 };
    check (frameToX (1500, view) === 50 && xToFrame (50, view) === 1500, "frameToX / xToFrame are inverses");

    const fit = fitViewport (48000, 400);
    check (fit.start === 0 && fit.framesPerPixel === 120, "fit shows everything");
    check (fitViewport (10, 400).framesPerPixel === minFramesPerPixel, "...but no deeper than the deepest zoom");

    const z = zoomViewport (fit, 4, 100, 48000, 400);
    check (Math.abs (xToFrame (100, z) - xToFrame (100, fit)) < 1e-6 && z.framesPerPixel === 30, "zooming keeps the frame under the pointer");
    const zl = zoomViewport (fit, 4, 0, 48000, 400), zr = zoomViewport (fit, 4, 400, 48000, 400);
    check (zl.start === 0 && Math.abs (zr.start + 400 * zr.framesPerPixel - 48000) < 1e-6, "...at either edge");
    check (zoomViewport (z, 1e9, 200, 48000, 400).framesPerPixel === minFramesPerPixel, "zoom in stops at the deepest zoom");
    check (zoomViewport (z, 1e-9, 200, 48000, 400).framesPerPixel === 120 && zoomViewport (z, 1e-9, 200, 48000, 400).start === 0, "zoom out stops at the fit");

    const p = panViewport (z, 50, 48000, 400);
    check (p.start === z.start + 50 * 30, "panning moves by pixels × frames per pixel");
    check (panViewport (z, -1e9, 48000, 400).start === 0 && panViewport (z, 1e9, 48000, 400).start === 48000 - 400 * 30, "panning is clamped to the timeline");
    check (clampViewport ({ start: NaN, framesPerPixel: NaN }, 48000, 400).framesPerPixel === 120, "clamping fixes nonsense");

    const end = viewportAtEnd ({ start: 0, framesPerPixel: 10 }, 48000, 400);
    check (end.start === 44000 && isViewportAtEnd (end, 48000, 400) && ! isViewportAtEnd (z, 48000, 400), "the live end");
    check (isViewportFitted (fit, 48000, 400) && ! isViewportFitted (z, 48000, 400), "fitted or not");
    check (getWheelZoomFactor (-100) > 1.4 && getWheelZoomFactor (100) < 0.7 && Math.abs (getWheelZoomFactor (-100) * getWheelZoomFactor (100) - 1) < 1e-9
             && getWheelZoomFactor (-3, 1) === getWheelZoomFactor (-48), "the wheel zooms in for negative deltas, symmetrically, lines × 16");
});

await test ("waveform: ruler ticks", () =>
{
    const rate = 48000;
    const majors = (view, w) => getRulerTicks (view, w, rate).ticks.filter (t => t.isMajor);

    for (const fpp of [0.25, 0.5, 3, 40, 480, 5000, 48000, 400000])
    {
        const view = { start: 12345, framesPerPixel: fpp };
        const { ticks, step, unit } = getRulerTicks (view, 800, rate);
        const m = ticks.filter (t => t.isMajor);
        const spacing = m.length > 1 ? m[1].x - m[0].x : Infinity;
        check (m.length >= 1 && spacing >= 84 - 1e-6 && spacing < 84 * 6, `fpp ${fpp}: labelled ticks ${spacing.toFixed (1)} px apart (${unit}, step ${step})`);
        check (ticks.every (t => t.x >= -1e-6 && t.x <= 800 + 1e-6) && ticks.every ((t, i) => i === 0 || t.frame > ticks[i - 1].frame), `fpp ${fpp}: in view, in order`);
        check ((unit === "samples") === (fpp < 1), `fpp ${fpp}: counts ${unit}`);
    }

    check (majors ({ start: 0, framesPerPixel: 96 }, 400).map (t => t.label).join () === "0.0 s,0.2 s,0.4 s,0.6 s,0.8 s", "0.8 s across 400 px: 0.2 s steps");
    check (majors ({ start: 0, framesPerPixel: 0.5 }, 200).map (t => t.label).join () === "0,50,100", "deep in: sample numbers");
    check (formatRulerTime (90, 60) === "1:30" && formatRulerTime (1.5, 0.5) === "1.5 s" && formatRulerTime (0.125, 0.005) === "0.125 s" && formatRulerTime (3, 1) === "3 s", "time labels");
    check (niceStep (0.003) === 0.005 && niceStep (7) === 10 && niceStep (2) === 2 && niceStep (0) === 1, "niceStep");
    check (chooseWaveformDrawMode (1.9) === "samples" && chooseWaveformDrawMode (2) === "minmax", "samples below 2 frames per pixel, min/max above");
});

await test ("waveform: selection, lanes, readouts", () =>
{
    check (JSON.stringify (normaliseSelection (300.4, 100.6, 1000)) === '{"startFrame":101,"endFrame":300}', "a drag either way, in whole frames");
    check (normaliseSelection (5, 5.2, 1000) === null && JSON.stringify (normaliseSelection (-50, 2000, 1000)) === '{"startFrame":0,"endFrame":1000}', "empty is null; clamped to the recording");
    const s = { startFrame: 100, endFrame: 300 };
    check (JSON.stringify (extendSelection (s, 400, 1000)) === '{"startFrame":100,"endFrame":400}' && JSON.stringify (extendSelection (s, 50, 1000)) === '{"startFrame":50,"endFrame":300}',
           "Shift-click moves the nearer edge");
    check (JSON.stringify (extendSelection (null, 400, 1000, 250)) === '{"startFrame":250,"endFrame":400}', "...or selects from the last click");
    check (describeSelection (s, 1000) === "Selection from 0.100 s to 0.300 s: 0.200 s (200 samples)" && describeSelection (null, 1000).startsWith ("No selection"), "described for screen readers");
    check (formatSampleValue (-0.25) === "\u22120.2500" && formatSampleValue (0.00001) === "0.0000", "sample values");
    const lanes = getWaveformLanes ([{ endpointID: "out", numChannels: 2 }, { endpointID: "in", numChannels: 1 }], id => id.toUpperCase());
    check (lanes.map (l => l.label).join ("|") === "OUT · L|OUT · R|IN" && lanes[1].channel === 1, "one lane per channel, labelled");
});

await test ("recordings: file names and formatting", async () =>
{
    check (sanitiseFileName ('a/b\\c:d*e?f"g<h>i|j') === "a-b-c-d-e-f-g-h-i-j", "path separators and forbidden characters");
    check (sanitiseFileName ("  ..hidden. ") === "hidden" && sanitiseFileName ("x\u0001\ty") === "x-y" && sanitiseFileName ("") === "untitled", "dots, control characters, empty");
    check (sanitiseFileName ("CON") === "_CON" && sanitiseFileName ("a".repeat (300)).length === 100, "reserved names and the length limit");
    check (getRecordingFileName ("Tremolo", "Recording 1 – 12:34:56", "out") === "Tremolo-Recording 1 – 12-34-56-out.wav", "patch-recording-endpoint.wav");
    check (getRecordingFileName ("", "", "out") === "out.wav", "empty parts are left out");
    check (formatBytes (0) === "0 MB" && formatBytes (528384) === "516 KB" && formatBytes (5.5 * 1048576) === "5.5 MB" && formatBytes (256 * 1048576) === "256 MB", "byte sizes");
    check (formatRecordingTime (1.96) === "0:01.9" && formatRecordingTime (62) === "1:02.0" && formatRecordingTime (3723) === "1:02:03", "durations");
    check (normaliseCapMB ("128") === 128 && normaliseCapMB (100) === 256, "the memory limit setting");
    check (describeSaveResult ({ saved: true, path: "/x/a.wav" }, "a.wav").text === "Saved to /x/a.wav" && describeSaveResult ({ saved: true }, "a.wav").text === "Downloaded a.wav"
             && describeSaveResult ({ saved: false, cancelled: true }).text === "Cancelled" && describeSaveResult ({ saved: false, error: "x" }, "a.wav").state === "error", "save results");

    const ch = Float32Array.from ({ length: 1000 }, (_, i) => Math.sin (i / 10) * 0.5);
    let lastProgress = 0;
    const blob = await createWAVBlob (blockSourceFromArrays ([[ch.subarray (0, 256), ch.subarray (256, 512), ch.subarray (512, 768), ch.subarray (768)]], 256, 1000), 48000, "pcm24", p => lastProgress = p);
    const expected = new Uint8Array (encodeWAV ([ch], 48000, { format: "pcm24" }));
    const actual = new Uint8Array (await blob.arrayBuffer());
    check (blob.type === "audio/wav" && lastProgress === 1 && actual.length === expected.length && actual.every ((b, i) => b === expected[i]), "createWAVBlob makes the same file as encodeWAV");
});

//==============================================================================
window.testResults = results;
const failed = results.filter (r => ! r.ok);
document.body.textContent = `${results.length - failed.length}/${results.length} passed` + failed.map (f => `\nFAIL ${f.test}: ${f.what}`).join ("");
