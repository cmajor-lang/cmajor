//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

import { Disposer } from "./dom.js";
import { describeParameter, sendParameterChange } from "../widgets/parameter-model.js";

/*
    Parameter fuzzing, for testing how a patch copes with its parameters moving: one
    ParameterFuzzer per connection (the panel keeps it as store.get ("connection").fuzzer).

      - randomiseAll(): every parameter that isn't excluded (or being automated) gets a random
        legal value (in range, on its step, one of its options, on/off), each change sent as a
        complete gesture (start, value, end). The exclusions are kept per patch, in the patch
        settings' "fuzzExclusions" (an array of endpoint IDs), and read afresh every time.

      - automate (endpointID, { mode, rate, depth, smooth }): moves one parameter continuously:
          ramp      linearly from one end of its window to the other, taking 1 / rate seconds,
                    and back (back and forth)
          sine      a sine LFO at rate Hz
          triangle  a triangle LFO at rate Hz
          random    a new random value every 1 / rate seconds: ramped from the last one if
                    smooth, else stepped
        The window is depth (0 to 1) of the parameter's range, centred on its value when the
        automation started, and moved in from the ends if it would stick out (so depth 1 is the
        full range, and a window always keeps its width). Each wave starts from the starting
        value (the phase is chosen so that it does), so starting one doesn't make it jump.

        It runs on the frame scheduler and keeps its own clock from the frames' dt, clamped to
        maxStepMs: while the page is hidden, rAF stops and so does the automation, which then
        carries on from where it was (a phase step of one frame at most). A parameter is sent at
        most every sendIntervalMs (~30 Hz), with rampFrames covering the time to the next send
        (from the sample rate), so the server smooths between sends. It never reads the value back:
        another client (or the patch's GUI) moving the parameter doesn't change its phase.

        An automation holds a gesture open from its start to its stop. A user grabbing the
        parameter (handleUserGesture(), which the widgets call before they start their own
        gesture) stops the automation first, so its gesture ends before theirs begins. Nothing is
        left open by stopAll(), dispose(), or a rebuild that removes the parameter.

      - The snapshot: the first fuzz action (a randomise, or the first automation started) takes
        a copy of every parameter's value. restore() stops all the automation and sends the
        snapshot's values back (as gestures) to the parameters the fuzzer changed, then forgets
        the snapshot; the next fuzz action takes a new one.

    It follows every parameter's value through the hub (the server sends param_value to every
    client anyway), so that the snapshot and a starting value are at hand when they're needed.

        const fuzzer = new ParameterFuzzer ({ patchConnection, hub, scheduler, settings: ctx.settings.patch,
                                              getSampleRate: () => 48000 });
        fuzzer.setParameters (getParameters (store.state));
        fuzzer.randomiseAll();
        fuzzer.automate ("cutoff", { mode: "sine", rate: 0.5, depth: 0.5 });
        const removeListener = fuzzer.addListener (() => updateTheUI());
*/

export const automationModes = Object.freeze (["ramp", "sine", "triangle", "random"]);

export const automationModeNames = Object.freeze ({ ramp: "Ramp", sine: "Sine", triangle: "Triangle", random: "Random" });

export const defaultAutomationSettings = Object.freeze ({ mode: "sine", rate: 0.5, depth: 0.5, smooth: true });

/// The rate (Hz) limits: an LFO from 100 s to 20 Hz per cycle; a ramp or random step the same
export const automationRateLimits = Object.freeze ({ min: 0.01, max: 20 });

/// The shortest time between two sends of one parameter (so, about 30 Hz)
export const sendIntervalMs = 1000 / 30;

/// A frame's time step is clamped to this, so nothing jumps after a stall or a hidden page
export const maxStepMs = 100;

/// The patch settings key for the parameters that Randomise skips (an array of endpoint IDs)
export const fuzzExclusionsSettingName = "fuzzExclusions";

/// The patch settings key for each parameter's last automation settings ({ [endpointID]: settings })
export const fuzzAutomationSettingName = "fuzzAutomation";

const defaultSampleRate = 44100;

// (a little slack, so that a 60 Hz display sends on every second frame rather than every third)
const sendIntervalSlackMs = 1;

//==============================================================================
/// Fills in and clamps automation settings
export function normaliseAutomationSettings (settings)
{
    const s = { ...defaultAutomationSettings, ...(settings ?? {}) };
    const number = (x, fallback) => (typeof x === "number" && isFinite (x)) ? x : fallback;

    return {
        mode:   automationModes.includes (s.mode) ? s.mode : defaultAutomationSettings.mode,
        rate:   Math.max (automationRateLimits.min, Math.min (automationRateLimits.max, number (s.rate, defaultAutomationSettings.rate))),
        depth:  Math.max (0, Math.min (1, number (s.depth, defaultAutomationSettings.depth))),
        smooth: s.smooth !== false,
    };
}

/// Whether a mode's rate reads better as a period (ramp, random) than as a frequency (LFOs)
export function isPeriodMode (mode)         { return mode === "ramp" || mode === "random"; }

/// The nearest value that a parameter can really take: one of its options, on or off, or a
/// value on its step within its range
export function toLegalValue (model, value)
{
    if (model.kind === "options")
        return model.options[model.toIndex (value)].value;

    if (model.kind === "toggle")
        return model.isOn (value) ? model.max : model.min;

    return model.snap (value);
}

/// A random legal value: an option, on/off, or a point in the range (on its step), all equally likely
export function getRandomLegalValue (model, random = Math.random)
{
    if (model.kind === "options")
        return model.options[Math.min (model.options.length - 1, Math.floor (random() * model.options.length))].value;

    if (model.kind === "toggle")
        return random() < 0.5 ? model.min : model.max;

    if (model.step > 0)
    {
        // (whole steps from min, so the ends are as likely as any other)
        const first = Math.ceil (model.min / model.step - 1e-9), last = Math.floor (model.max / model.step + 1e-9);

        if (last >= first)
            return model.snap ((first + Math.min (last - first, Math.floor (random() * (last - first + 1)))) * model.step);
    }

    return model.snap (model.min + random() * (model.max - model.min));
}

/// The window an automation moves in: depth of the range, centred on the starting value, moved
/// in from the ends of the range if it would stick out
export function getAutomationWindow (model, startValue, depth)
{
    const range = model.max - model.min;
    const width = Math.max (0, Math.min (1, depth)) * range;
    const centre = Math.max (model.min, Math.min (model.max, Number.isFinite (startValue) ? startValue : model.init));
    const lo = Math.max (model.min, Math.min (model.max - width, centre - width / 2));
    return { lo, hi: lo + width };
}

/// A periodic wave's value from -1 to 1 (ramp: 0 to 1, rising over phase 0..1 and falling over 1..2)
export function getWaveValue (mode, phase)
{
    if (mode === "sine")
        return Math.sin (2 * Math.PI * phase);

    if (mode === "triangle")
        return 1 - 4 * Math.abs (fract (phase + 0.25) - 0.5);

    if (mode === "ramp")
    {
        const p = fract (phase / 2) * 2;
        return p <= 1 ? p : 2 - p;
    }

    return 0;
}

/// The phase at which a wave passes through a position (0 to 1 across the window), rising
function getStartPhase (mode, position)
{
    const s = Math.max (-1, Math.min (1, 2 * position - 1));

    if (mode === "sine")        return Math.asin (s) / (2 * Math.PI);
    if (mode === "triangle")    return s / 4;
    if (mode === "ramp")        return Math.max (0, Math.min (1, position));
    return 0;
}

/// A name that suggests the parameter sets the output's level, which it may be wiser to leave out
/// of a randomise (only ever offered as a suggestion: nothing is excluded unless the user says so)
export function isLikelyOutputLevel (endpoint)
{
    const text = `${endpoint?.endpointID ?? ""} ${endpoint?.annotation?.name ?? ""}`;
    return /volume|gain|output|master|level/i.test (text);
}

function fract (x)          { return x - Math.floor (x); }

//==============================================================================
export class ParameterFuzzer
{
    /// options: { patchConnection, hub (an EndpointDataHub, to follow the values), scheduler,
    ///            settings (the patch scope: get/set), getSampleRate(), random() }
    constructor ({ patchConnection, hub = null, scheduler, settings = null, getSampleRate = null, random = Math.random })
    {
        this.patchConnection = patchConnection;
        this.hub = hub;
        this.scheduler = scheduler;
        this.settings = settings;
        this.getSampleRate = getSampleRate;
        this.random = random;
        this.disposer = new Disposer();

        this.parameters = new Map();        // endpointID -> { endpoint, model }
        this.values = new Map();            // endpointID -> the last value the server reported
        this.valueSubscriptions = new Map();// endpointID -> unsubscribe
        this.automations = new Map();       // endpointID -> the running automation's state
        this.openGestures = new Set();      // the endpoint IDs whose gesture the fuzzer has open
        this.snapshot = null;               // Map endpointID -> value, or null
        this.touched = new Set();           // what the fuzzer has changed since the snapshot
        this.listeners = new Set();
        this.isDisposed = false;
        this.isFrameScheduled = false;
        this.unschedule = null;
        this.runFrame = (time, dt) => this.handleFrame (dt);
    }

    dispose()
    {
        if (this.isDisposed)
            return;

        this.stopAll();
        this.isDisposed = true;

        // (belt and braces: nothing should be open now)
        for (const id of [...this.openGestures])
            this.endGesture (id);

        for (const unsubscribe of this.valueSubscriptions.values())
            unsubscribe();

        this.valueSubscriptions.clear();
        this.listeners.clear();
        this.disposer.dispose();
    }

    //==============================================================================
    /// Sets the patch's parameters (on a connection, and after every rebuild). Automations of
    /// parameters that have gone are stopped (ending their gestures); the others keep running.
    setParameters (endpoints)
    {
        if (this.isDisposed)
            return;

        const next = new Map();

        for (const endpoint of endpoints ?? [])
            if (endpoint?.endpointID && endpoint.purpose === "parameter")
                next.set (endpoint.endpointID, { endpoint, model: describeParameter (endpoint) });

        for (const id of [...this.automations.keys()])
            if (! next.has (id))
                this.stopAutomation (id, false);

        for (const [id, unsubscribe] of [...this.valueSubscriptions])
        {
            if (! next.has (id))
            {
                unsubscribe();
                this.valueSubscriptions.delete (id);
                this.values.delete (id);
            }
        }

        this.parameters = next;

        for (const [id, automation] of this.automations)
            automation.model = next.get (id).model;

        if (this.hub)
            for (const id of next.keys())
                if (! this.valueSubscriptions.has (id))
                    this.valueSubscriptions.set (id, this.hub.subscribeParameter (id, (value) => this.handleValue (id, value)));

        this.notify();
    }

    getParameters()                 { return [...this.parameters.values()].map (p => p.endpoint); }
    get numParameters()             { return this.parameters.size; }
    getModel (endpointID)           { return this.parameters.get (endpointID)?.model ?? null; }

    /// The last value the server reported for a parameter (undefined if none yet)
    getValue (endpointID)           { return this.values.get (endpointID); }

    /// Without a hub, the panel (or a test) can pass the values on itself
    handleValue (endpointID, value)
    {
        const v = Number (value);

        if (Number.isFinite (v))
            this.values.set (endpointID, v);
    }

    //==============================================================================
    /// Calls fn() whenever something the UI shows changes (automations, snapshot, exclusions).
    /// Returns a function that removes it.
    addListener (fn)
    {
        this.listeners.add (fn);
        return () => this.listeners.delete (fn);
    }

    notify()
    {
        for (const fn of [...this.listeners])
        {
            try { fn (this); }
            catch (e) { console.error (e); }
        }
    }

    //==============================================================================
    // Exclusions (from randomise), kept in the patch's settings
    getExclusions()
    {
        const list = this.settings?.get (fuzzExclusionsSettingName, []);
        return new Set (Array.isArray (list) ? list.filter (id => typeof id === "string") : []);
    }

    isExcluded (endpointID)         { return this.getExclusions().has (endpointID); }

    setExcluded (endpointID, shouldExclude)
    {
        const exclusions = this.getExclusions();

        if (exclusions.has (endpointID) === !! shouldExclude)
            return;

        shouldExclude ? exclusions.add (endpointID) : exclusions.delete (endpointID);
        this.settings?.set (fuzzExclusionsSettingName, [...exclusions]);
        this.notify();
    }

    /// The parameters that a randomise would change
    getRandomisableIDs()
    {
        const exclusions = this.getExclusions();
        return [...this.parameters.keys()].filter (id => ! exclusions.has (id) && ! this.automations.has (id));
    }

    get canRandomise()              { return ! this.isDisposed && this.getRandomisableIDs().length > 0; }

    /// Gives every parameter that isn't excluded or automated a random legal value, each as a
    /// complete gesture. Returns the number changed.
    randomiseAll()
    {
        const ids = this.isDisposed ? [] : this.getRandomisableIDs();

        if (ids.length === 0)
            return 0;

        this.takeSnapshotIfNeeded();

        for (const id of ids)
        {
            const value = getRandomLegalValue (this.parameters.get (id).model, this.random);
            sendParameterChange (this.patchConnection, id, value);
            this.touched.add (id);
        }

        this.notify();
        return ids.length;
    }

    //==============================================================================
    // The snapshot
    get hasSnapshot()               { return this.snapshot !== null; }
    get snapshotSize()              { return this.snapshot?.size ?? 0; }

    takeSnapshotIfNeeded()
    {
        if (this.snapshot)
            return;

        this.snapshot = new Map();
        this.touched.clear();

        for (const id of this.parameters.keys())
            if (this.values.has (id))
                this.snapshot.set (id, this.values.get (id));
    }

    /// Stops all the automation and sends the values from before fuzzing back (to the parameters
    /// the fuzzer changed that still exist), then forgets the snapshot. Returns the number sent.
    restore()
    {
        if (! this.snapshot)
            return 0;

        const snapshot = this.snapshot;
        const touched = [...this.touched];
        this.snapshot = null;
        this.touched.clear();
        this.stopAll (false);
        let count = 0;

        for (const id of touched)
        {
            if (this.parameters.has (id) && snapshot.has (id))
            {
                sendParameterChange (this.patchConnection, id, snapshot.get (id));
                ++count;
            }
        }

        this.notify();
        return count;
    }

    /// Forgets the snapshot without sending anything
    clearSnapshot()
    {
        if (this.snapshot)
        {
            this.snapshot = null;
            this.touched.clear();
            this.notify();
        }
    }

    //==============================================================================
    // Automation
    get numAutomations()            { return this.automations.size; }
    isAutomating (endpointID)       { return this.automations.has (endpointID); }
    getAutomatedIDs()               { return [...this.automations.keys()]; }

    /// The running automation's settings, or null
    getAutomation (endpointID)
    {
        const a = this.automations.get (endpointID);
        return a ? { ...a.settings } : null;
    }

    /// The settings the popover should show for a parameter: the running ones, or the last ones
    /// used for it in this patch, or the defaults
    getAutomationSettings (endpointID)
    {
        return this.getAutomation (endpointID) ?? normaliseAutomationSettings (this.getSavedAutomationSettings()[endpointID]);
    }

    getSavedAutomationSettings()
    {
        const saved = this.settings?.get (fuzzAutomationSettingName, {});
        return (saved && typeof saved === "object" && ! Array.isArray (saved)) ? saved : {};
    }

    /// Remembers a parameter's automation settings (merged over its current ones) for next
    /// time, without starting anything. Returns the merged settings.
    saveAutomationSettings (endpointID, settings)
    {
        const s = normaliseAutomationSettings ({ ...this.getAutomationSettings (endpointID), ...(settings ?? {}) });
        this.settings?.set (fuzzAutomationSettingName, { ...this.getSavedAutomationSettings(), [endpointID]: s });
        return s;
    }

    /// Starts automating a parameter, or changes a running automation's settings (keeping its
    /// phase and its window's centre). Returns true if it's running.
    automate (endpointID, settings)
    {
        const parameter = this.parameters.get (endpointID);

        if (this.isDisposed || ! parameter)
            return false;

        const s = this.saveAutomationSettings (endpointID, settings);
        const existing = this.automations.get (endpointID);

        if (existing)
        {
            const modeChanged = existing.settings.mode !== s.mode;
            existing.settings = s;
            Object.assign (existing, getAutomationWindow (existing.model, existing.startValue, s.depth));

            // a new wave carries on from where the old one had got to
            if (modeChanged)
                this.startWave (existing, existing.value);

            this.notify();
            return true;
        }

        this.takeSnapshotIfNeeded();

        const model = parameter.model;
        const startValue = this.values.has (endpointID) ? this.values.get (endpointID) : model.init;
        const automation = { endpointID, model, settings: s, startValue, value: startValue, lastSent: undefined,
                             sinceSendMs: Infinity, phase: 0, stepFrom: startValue, stepTo: startValue,
                             ...getAutomationWindow (model, startValue, s.depth) };

        this.startWave (automation, startValue);
        this.automations.set (endpointID, automation);
        this.touched.add (endpointID);
        this.startGesture (endpointID);
        this.scheduleFrames();
        this.notify();
        return true;
    }

    /// Stops automating a parameter (ending its gesture). Returns true if it was running.
    stopAutomation (endpointID, shouldNotify = true)
    {
        if (! this.automations.delete (endpointID))
            return false;

        this.endGesture (endpointID);

        if (this.automations.size === 0)
            this.stopFrames();

        if (shouldNotify)
            this.notify();

        return true;
    }

    stopAll (shouldNotify = true)
    {
        const ids = [...this.automations.keys()];

        for (const id of ids)
            this.stopAutomation (id, false);

        if (shouldNotify && ids.length > 0)
            this.notify();

        return ids.length;
    }

    /// Call when the user is about to start a gesture on a parameter (grabbing its slider,
    /// clicking its toggle, resetting it): an automation of it stops first, ending its gesture
    /// before the user's begins
    handleUserGesture (endpointID)
    {
        return this.stopAutomation (endpointID);
    }

    //==============================================================================
    startGesture (endpointID)
    {
        if (! this.openGestures.has (endpointID))
        {
            this.openGestures.add (endpointID);
            this.patchConnection.sendParameterGestureStart (endpointID);
        }
    }

    endGesture (endpointID)
    {
        if (this.openGestures.delete (endpointID))
            this.patchConnection.sendParameterGestureEnd (endpointID);
    }

    /// (Re)starts an automation's wave so that it passes through the given value
    startWave (automation, value)
    {
        const { lo, hi } = automation;
        const position = hi > lo ? (value - lo) / (hi - lo) : 0.5;
        automation.phase = getStartPhase (automation.settings.mode, position);
        automation.stepFrom = automation.stepTo = Math.max (lo, Math.min (hi, value));

        if (automation.settings.mode === "random")
            automation.stepTo = this.getRandomInWindow (automation);
    }

    getRandomInWindow (automation)
    {
        return automation.lo + this.random() * (automation.hi - automation.lo);
    }

    /// The value an automation has reached (before it's made legal)
    getAutomationValue (automation)
    {
        const { lo, hi, settings } = automation;

        if (settings.mode === "random")
            return settings.smooth ? automation.stepFrom + (automation.stepTo - automation.stepFrom) * fract (automation.phase)
                                   : automation.stepTo;

        const w = getWaveValue (settings.mode, automation.phase);
        return settings.mode === "ramp" ? lo + w * (hi - lo) : (lo + hi) / 2 + w * (hi - lo) / 2;
    }

    /// The number of frames the server should ramp over, from one send to the next
    getRampFrames()
    {
        const rate = Number (this.getSampleRate?.());
        return Math.round ((rate > 0 ? rate : defaultSampleRate) * sendIntervalMs / 1000);
    }

    scheduleFrames()
    {
        if (! this.isFrameScheduled && this.scheduler)
        {
            this.isFrameScheduled = true;
            this.unschedule = this.scheduler.schedule (this.runFrame);
        }
    }

    stopFrames()
    {
        this.isFrameScheduled = false;
        this.unschedule?.();
        this.unschedule = null;
    }

    /// Advances every automation by one frame's dt (ms), and sends the ones that are due
    handleFrame (dt)
    {
        if (this.isDisposed || this.automations.size === 0)
        {
            this.isFrameScheduled = false;
            this.unschedule = null;
            return false;
        }

        const stepMs = Math.max (0, Math.min (maxStepMs, Number (dt) || 0));
        this.advance (stepMs);
        return true;
    }

    /// Moves the automations on by stepMs of their own time, and sends the ones that are due
    advance (stepMs)
    {
        const rampFrames = this.getRampFrames();

        for (const a of this.automations.values())
        {
            const oldPhase = a.phase;
            a.phase += a.settings.rate * stepMs / 1000;

            if (a.settings.mode === "random")
            {
                // a new step for each whole period crossed (only the last matters)
                if (Math.floor (a.phase) !== Math.floor (oldPhase))
                {
                    a.stepFrom = a.settings.smooth ? a.stepTo : a.stepFrom;
                    a.stepTo = this.getRandomInWindow (a);

                    if (! a.settings.smooth)
                        a.stepFrom = a.stepTo;
                }
            }

            a.value = this.getAutomationValue (a);
            a.sinceSendMs += stepMs;

            if (a.sinceSendMs + sendIntervalSlackMs < sendIntervalMs)
                continue;

            const value = toLegalValue (a.model, a.value);

            if (value === a.lastSent)
                continue;

            // (discrete parameters, and random steps, jump; the rest are smoothed between sends)
            const isStepped = a.model.kind !== "slider" || (a.settings.mode === "random" && ! a.settings.smooth);
            this.patchConnection.sendEventOrValue (a.endpointID, value, isStepped ? 0 : rampFrames);
            a.lastSent = value;
            a.sinceSendMs = 0;
        }
    }
}

//==============================================================================
/// The fuzzer's commands for the panel's registry (category "Parameters"), each disabled when
/// it has nothing to do. getFuzzer() returns the current connection's fuzzer, or null.
export function createFuzzCommands (getFuzzer)
{
    return [
        { id: "parameters.randomise",      title: "Randomise all parameters",                category: "Parameters", keywords: ["fuzz", "random", "test"],
          run: () => getFuzzer()?.randomiseAll(), isEnabled: () => !! getFuzzer()?.canRandomise },
        { id: "parameters.restore",        title: "Restore parameters from before fuzzing",  category: "Parameters", keywords: ["fuzz", "undo", "snapshot"],
          run: () => getFuzzer()?.restore(), isEnabled: () => !! getFuzzer()?.hasSnapshot },
        { id: "parameters.stopAutomation", title: "Stop all parameter automation",           category: "Parameters", keywords: ["fuzz", "lfo", "automate"],
          run: () => getFuzzer()?.stopAll(), isEnabled: () => (getFuzzer()?.numAutomations ?? 0) > 0 },
    ];
}
