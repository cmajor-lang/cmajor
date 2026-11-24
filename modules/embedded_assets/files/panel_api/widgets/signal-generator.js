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

import { el, Disposer, trackPointer } from "../core/dom.js";
import { frameScheduler } from "../core/frame-scheduler.js";

/*
    The test signal that the server can play into an audio input (its SignalGeneratorSource):
    the settings, their limits and formatting, and the form that edits them.

    The server owns the signal: it merges each change into the running generator in place, and
    smooths the level and frequency itself, so the form just sends the field that changed (at
    most about 30 times a second, through a ThrottledSender) and shows what the server reports.

    The settings, the slider mappings, the formatting and parsing, and the throttle are plain
    functions and classes, so that they can be tested on their own.
*/

//==============================================================================
export const generatorTypes = Object.freeze ([
    { type: "sine",    label: "Sine",        shortName: "sine",    title: "A sine wave" },
    { type: "sweep",   label: "Sweep",       shortName: "sweep",   title: "A logarithmic sine sweep, which repeats" },
    { type: "white",   label: "White noise", shortName: "white",   title: "White noise" },
    { type: "pink",    label: "Pink noise",  shortName: "pink",    title: "Pink noise (the same RMS level as white, so its peaks go higher)" },
    { type: "impulse", label: "Impulses",    shortName: "impulse", title: "Single-sample impulses at a regular rate" },
    { type: "silence", label: "Silence",     shortName: "silence", title: "Exact zeros" },
]);

/// The server's defaults
export const defaultGeneratorSettings = Object.freeze ({ type: "sine", level: -12, frequency: 440, startFrequency: 20, endFrequency: 20000,
                                                         sweepSeconds: 5, impulseRate: 2 });

/// The server clamps every value to these, and so does the client
export const generatorLimits = Object.freeze ({
    level:          Object.freeze ([-120, 0]),
    frequency:      Object.freeze ([1, 48000]),
    startFrequency: Object.freeze ([1, 48000]),
    endFrequency:   Object.freeze ([1, 48000]),
    sweepSeconds:   Object.freeze ([0.1, 600]),
    impulseRate:    Object.freeze ([0.1, 1000]),
});

/// The fields that each type of signal uses, besides its level
export const generatorFieldsForType = Object.freeze ({
    sine:    ["frequency"],
    sweep:   ["startFrequency", "endFrequency", "sweepSeconds"],
    white:   [],
    pink:    [],
    impulse: ["impulseRate"],
    silence: [],
});

/// The patch setting that remembers each audio input's last generator settings
export const generatorSettingName = "audioInputGenerators";

const clamp = (v, lo, hi) => Math.max (lo, Math.min (hi, v));

/// Merges changes into a set of settings the way the server does: a field that's missing,
/// not a number or not finite is left as it was, an unknown type is ignored, and the numbers
/// are clamped to generatorLimits. Returns a new, complete set.
export function mergeGeneratorSettings (settings, changes)
{
    const result = { ...defaultGeneratorSettings };
    const apply = (source) =>
    {
        if (! source || typeof source !== "object")
            return;

        if (typeof source.type === "string" && generatorFieldsForType[source.type])
            result.type = source.type;

        for (const [field, [lo, hi]] of Object.entries (generatorLimits))
        {
            const v = source[field];

            if (typeof v === "number" && Number.isFinite (v))
                result[field] = clamp (v, lo, hi);
        }
    };

    apply (settings);
    apply (changes);
    return result;
}

/// A complete, valid set of settings from whatever was stored or received
export function normaliseGeneratorSettings (settings)
{
    return mergeGeneratorSettings (defaultGeneratorSettings, settings);
}

/// The fields of b that differ from a
export function getChangedGeneratorFields (a, b)
{
    return Object.fromEntries (Object.entries (b).filter (([k, v]) => a?.[k] !== v));
}

export function getGeneratorTypeInfo (type)
{
    return generatorTypes.find (t => t.type === type) ?? generatorTypes[0];
}

//==============================================================================
/// What the sliders cover (a typed value can go anywhere within generatorLimits). The log ones
/// take equal steps in ratio; `steps` is how many positions the slider has.
export const generatorSliderRanges = Object.freeze ({
    level:          Object.freeze ({ min: -80, max: 0, isLog: false, steps: 160 }),
    frequency:      Object.freeze ({ min: 20, max: 20000, isLog: true, steps: 240 }),
    startFrequency: Object.freeze ({ min: 20, max: 20000, isLog: true, steps: 240 }),
    endFrequency:   Object.freeze ({ min: 20, max: 20000, isLog: true, steps: 240 }),
    sweepSeconds:   Object.freeze ({ min: 0.5, max: 60, isLog: true, steps: 120 }),
    impulseRate:    Object.freeze ({ min: 0.5, max: 100, isLog: true, steps: 120 }),
});

/// Rounds to a number of significant figures (so a log slider gives 440 rather than 439.86)
export function roundToSignificantFigures (value, figures = 3)
{
    if (! Number.isFinite (value) || value === 0)
        return value;

    const scale = Math.pow (10, figures - 1 - Math.floor (Math.log10 (Math.abs (value))));
    return Math.round (value * scale) / scale;
}

/// A value's position (0 to 1) along a slider range, clamped to the ends
export function valueToSliderPosition (value, range)
{
    const v = clamp (Number (value), range.min, range.max);

    if (! Number.isFinite (v))
        return 0;

    return range.isLog ? Math.log (v / range.min) / Math.log (range.max / range.min)
                       : (v - range.min) / (range.max - range.min);
}

/// The value at a position (0 to 1) along a slider range. The ends give exactly min and max;
/// in between, a log range rounds to 3 significant figures, and a linear one to its step.
export function sliderPositionToValue (position, range)
{
    const p = clamp (Number (position) || 0, 0, 1);

    if (p === 0) return range.min;
    if (p === 1) return range.max;

    if (range.isLog)
        return clamp (roundToSignificantFigures (range.min * Math.pow (range.max / range.min, p)), range.min, range.max);

    const step = (range.max - range.min) / (range.steps || 100);
    return clamp (range.min + Math.round (p * (range.max - range.min) / step) * step, range.min, range.max);
}

export function frequencyToSliderPosition (hz)      { return valueToSliderPosition (hz, generatorSliderRanges.frequency); }
export function sliderPositionToFrequency (position) { return sliderPositionToValue (position, generatorSliderRanges.frequency); }

//==============================================================================
/// Strips the trailing zeros: 3 digits or so, so the mono readouts don't jitter much
function formatDigits (value)
{
    const magnitude = Math.abs (value);
    const decimals = magnitude >= 100 ? 0 : magnitude >= 10 ? 1 : 2;
    return Number (value.toFixed (decimals)).toString();
}

/// "440 Hz", "27.5 Hz", "1.25 kHz", "20 kHz"
export function formatFrequency (hz)
{
    if (! Number.isFinite (hz))
        return "";

    return Math.abs (hz) >= 1000 ? `${formatDigits (hz / 1000)} kHz` : `${formatDigits (hz)} Hz`;
}

/// "-12.0 dBFS" (the generator's level is its peak level)
export function formatGeneratorLevel (db)
{
    return Number.isFinite (db) ? `${db.toFixed (1)} dBFS` : "";
}

/// "5 s", "0.5 s", "12.5 s", "120 s"
export function formatSeconds (seconds)
{
    return Number.isFinite (seconds) ? `${formatDigits (seconds)} s` : "";
}

/// How each numeric field is labelled, formatted and parsed
export const generatorFieldInfo = Object.freeze ({
    level:          { label: "Level",  title: "The peak level",                format: formatGeneratorLevel, unit: "dB" },
    frequency:      { label: "Freq",   title: "The frequency",                 format: formatFrequency, unit: "Hz" },
    startFrequency: { label: "From",   title: "The sweep's start frequency",   format: formatFrequency, unit: "Hz" },
    endFrequency:   { label: "To",     title: "The sweep's end frequency",     format: formatFrequency, unit: "Hz" },
    sweepSeconds:   { label: "Length", title: "How long each sweep takes",     format: formatSeconds, unit: "s" },
    impulseRate:    { label: "Rate",   title: "The number of impulses per second", format: formatFrequency, unit: "Hz" },
});

/// Parses what's typed into a field: a number, with an optional unit ("1.5k", "1.5 kHz", "440hz",
/// "250 ms", "-6 dB", "-6 dBFS"). Returns the value in the field's own unit (Hz, s or dB), or null.
export function parseGeneratorValue (text, field)
{
    const match = /^\s*([-+−]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)\s*([a-z]*)\s*$/i.exec (String (text ?? ""));

    if (! match)
        return null;

    const value = Number (match[1].replace ("−", "-"));
    const suffix = match[2].toLowerCase();
    const unit = generatorFieldInfo[field]?.unit;

    if (! Number.isFinite (value))
        return null;

    if (unit === "Hz")
    {
        if (suffix === "" || suffix === "hz")   return value;
        if (suffix === "k" || suffix === "khz") return value * 1000;
    }
    else if (unit === "s")
    {
        if (suffix === "" || suffix === "s" || suffix === "sec") return value;
        if (suffix === "ms")                                     return value / 1000;
    }
    else if (unit === "dB")
    {
        if (suffix === "" || suffix === "db" || suffix === "dbfs") return value;
    }

    return null;
}

//==============================================================================
/// The settings saved for an endpoint (a PanelSettingsScopes, or none), or null if there are none
export function loadSavedGeneratorSettings (settings, endpointID)
{
    const saved = settings?.patch.get (generatorSettingName, {})?.[endpointID];
    return saved && typeof saved === "object" ? normaliseGeneratorSettings (saved) : null;
}

export function saveGeneratorSettings (settings, endpointID, values)
{
    if (! settings)
        return;

    const all = { ...(settings.patch.get (generatorSettingName, {}) ?? {}) };
    all[endpointID] = normaliseGeneratorSettings (values);
    settings.patch.set (generatorSettingName, all);
}

//==============================================================================
/*
    Sends changes at most once per interval (default 33 ms, i.e. about 30 Hz): the first change
    goes at once, and the ones that come in during the interval are merged and sent at its end,
    from a frame function on the scheduler. So the last value of a drag always arrives.

    send (changes) is called with the merged object; now() gives the time in ms.
*/
export class ThrottledSender
{
    constructor (send, { intervalMs = 33, scheduler = frameScheduler, now = () => performance.now() } = {})
    {
        this.send = send;
        this.intervalMs = intervalMs;
        this.scheduler = scheduler;
        this.now = now;
        this.pending = null;
        this.lastSendTime = -Infinity;
        this.isScheduled = false;

        this.onFrame = () =>
        {
            if (this.pending && this.now() - this.lastSendTime >= this.intervalMs)
                this.flush();

            this.isScheduled = this.pending !== null;
            return this.isScheduled;
        };
    }

    queue (changes)
    {
        this.pending = { ...(this.pending ?? {}), ...changes };

        if (this.now() - this.lastSendTime >= this.intervalMs)
        {
            this.flush();
        }
        else if (! this.isScheduled)
        {
            this.isScheduled = true;
            this.scheduler.schedule (this.onFrame);
        }
    }

    /// Sends anything that's waiting, now
    flush()
    {
        const changes = this.pending;
        this.pending = null;

        if (changes)
        {
            this.lastSendTime = this.now();
            this.send (changes);
        }
    }

    cancel()
    {
        this.pending = null;
        this.isScheduled = false;
        this.scheduler.cancel (this.onFrame);
    }
}

//==============================================================================
/*
    The settings form under a full-size audio input in Signal mode: the type, the level, and
    the type's own fields, each a slider with an editable mono readout (type a value, with or
    without its unit, and press Enter).

    setSettings() shows the server's settings; it never calls onChange. A slider that's being
    dragged, or a readout that's being edited, keeps what the user has there. So does a field the
    user has just changed (e.g. by arrow keys, faster than the server's echoes come back): the
    server's value for it is ignored until it matches the user's, or for localChangeHoldMs after
    the last change (after which another client's change shows).

    options: { label (for the group's aria-label), onChange (changes: just the fields the user changed) }
*/
export class SignalGeneratorForm
{
    constructor ({ label = "Test signal", onChange })
    {
        this.onChange = onChange;
        this.disposer = new Disposer();
        this.settings = { ...defaultGeneratorSettings };
        this.draggingField = null;
        this.localChanges = new Map();  // field -> { value, time }: what the user last set, until the server agrees
        this.rows = new Map();          // field -> { row, slider, input, range }

        const id = `cp-gen-${uniqueID++}`;

        this.typeSelect = el ("select", { class: "cp-select cp-gen-type", id: `${id}-type`, title: "The kind of test signal",
                                          onchange: () => this.changeType (this.typeSelect.value) },
                              generatorTypes.map (t => el ("option", { value: t.type, title: t.title }, t.label)));

        this.element = el ("div", { class: "cp-gen", role: "group", "aria-label": label },
                           el ("div", { class: "cp-gen-row", dataset: { field: "type" } },
                               el ("label", { class: "cp-gen-label", for: `${id}-type` }, "Type"),
                               this.typeSelect),
                           Object.keys (generatorFieldInfo).map (field => this.createRow (field, id)));

        this.disposer.add (() => this.stopDrag?.());
        this.showSettings();
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    createRow (field, id)
    {
        const info = generatorFieldInfo[field];
        const range = generatorSliderRanges[field];
        const labelID = `${id}-${field}`;

        const slider = el ("input", { type: "range", class: "cp-slider cp-gen-slider", min: 0, max: range.steps, step: 1,
                                      "aria-labelledby": labelID, title: info.title });

        const input = el ("input", { type: "text", class: "cp-input cp-gen-value", inputMode: "decimal", spellcheck: false, autocomplete: "off",
                                     "aria-label": `${info.label} (${info.unit})`, title: `${info.title}: type a value and press Enter` });

        this.disposer.listen (slider, "input", () =>
        {
            const value = sliderPositionToValue (Number (slider.value) / range.steps, range);
            this.userChanged ({ [field]: value });
        });

        this.disposer.listen (slider, "pointerdown", (e) =>
        {
            if (e.button !== 0)
                return;

            this.stopDrag?.();
            this.draggingField = field;
            const stop = trackPointer (e, slider, null, () => { this.draggingField = null; this.stopDrag = null; });
            this.stopDrag = () => { stop(); this.draggingField = null; this.stopDrag = null; };
        });

        const commit = () =>
        {
            const value = parseGeneratorValue (input.value, field);

            if (value === null || value === this.settings[field])
                this.showField (field, true);
            else
                this.userChanged ({ [field]: value }, true);
        };

        this.disposer.listen (input, "change", commit);
        this.disposer.listen (input, "keydown", (e) =>
        {
            if (e.key === "Enter")
            {
                e.preventDefault();
                commit();
                input.select();
            }
            else if (e.key === "Escape")
            {
                this.showField (field, true);
            }
        });

        const row = el ("div", { class: "cp-gen-row", dataset: { field } },
                        el ("span", { class: "cp-gen-label", id: labelID }, info.label), slider, input);

        this.rows.set (field, { row, slider, input, range });
        return row;
    }

    /// Shows a complete set of settings (from the server); doesn't send anything
    setSettings (settings)
    {
        const previous = this.settings;
        this.settings = normaliseGeneratorSettings (settings);

        // (the server's echoes lag behind a dragged slider, so they'd make it jitter)
        if (this.draggingField)
            this.settings[this.draggingField] = previous[this.draggingField];

        // (an echo of an earlier step mustn't undo a later one that's still on its way)
        const now = performance.now();

        for (const [field, change] of [...this.localChanges])
        {
            if (isSameGeneratorValue (this.settings[field], change.value) || now - change.time > localChangeHoldMs)
                this.localChanges.delete (field);
            else
                this.settings[field] = change.value;
        }

        this.showSettings();
    }

    changeType (type)
    {
        this.userChanged ({ type });
    }

    userChanged (changes, forceShow = false)
    {
        const merged = mergeGeneratorSettings (this.settings, changes);
        const sent = Object.fromEntries (Object.keys (changes).map (k => [k, merged[k]]));
        this.settings = merged;

        for (const [field, value] of Object.entries (sent))
            this.localChanges.set (field, { value, time: performance.now() });

        this.showSettings (forceShow);
        this.onChange?.(sent);
    }

    showSettings (forceShow = false)
    {
        const s = this.settings;
        // (silence has no level either)
        const used = new Set (s.type === "silence" ? [] : ["level", ...generatorFieldsForType[s.type]]);

        if (this.typeSelect.value !== s.type)
            this.typeSelect.value = s.type;

        this.typeSelect.title = getGeneratorTypeInfo (s.type).title;
        this.element.dataset.type = s.type;

        for (const [field, { row }] of this.rows)
        {
            row.hidden = ! used.has (field);
            this.showField (field, forceShow);
        }
    }

    showField (field, forceShow = false)
    {
        const { slider, input, range } = this.rows.get (field);
        const value = this.settings[field];
        const text = generatorFieldInfo[field].format (value);
        const position = valueToSliderPosition (value, range);

        if (field !== this.draggingField)
        {
            const sliderValue = String (Math.round (position * range.steps));

            if (slider.value !== sliderValue)
                slider.value = sliderValue;
        }

        slider.setAttribute ("aria-valuetext", text);
        slider.style.setProperty ("--cp-fill", (position * 100).toFixed (2) + "%");

        if (forceShow || document.activeElement !== input)
            input.value = text;
    }
}

let uniqueID = 0;

/// How long a field the user has changed ignores server values that disagree with it
export const localChangeHoldMs = 500;

function isSameGeneratorValue (a, b)
{
    return typeof a === "number" && typeof b === "number" ? Math.abs (a - b) <= 1e-9 * Math.max (1, Math.abs (a)) : a === b;
}
