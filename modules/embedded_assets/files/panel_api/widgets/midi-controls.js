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

import * as midi from "../../cmaj_api/cmaj-midi-helpers.js";
import { el, Disposer, trackPointer, setPressed } from "../core/dom.js";
import { frameScheduler } from "../core/frame-scheduler.js";

/*
    The control strip of a full-size MIDI input widget: how the on-screen keyboard plays
    (velocity, channel, octave), the performance controls (pitch bend, mod wheel, sustain) and a
    panic button.

    The velocity, channel and octave are remembered per patch and endpoint (the patch setting
    "midiControls"); the bend, mod and sustain are momentary, and start at rest.

    The message encoding, the panic list and the velocity mapping are plain functions below,
    so that they can be tested on their own.
*/

//==============================================================================
export const midiChannelCount = 16;
export const pitchBendCentre = 8192;
export const pitchBendMax = 16383;
export const defaultRootNote = 36;
export const minOctaveShift = -3;      // (the 61-note keyboard then starts at C-1, note 0...)
export const maxOctaveShift = 2;       // (...or ends at C9, note 120, at the top)
export const midiControlsSettingName = "midiControls";

export const defaultMIDIControlSettings = Object.freeze ({ velocityMode: "fixed", velocity: 100, channel: 1, octave: 0 });

const clamp = (v, lo, hi) => Math.max (lo, Math.min (hi, v));

/// Packs a short MIDI message for sendMIDIInputEvent(): status (the high nibble, e.g. 0x90),
/// channel 1-16, and two data bytes
export function encodeMIDIMessage (status, channel, data1 = 0, data2 = 0)
{
    const statusByte = (status & 0xf0) | ((clamp (Math.round (channel), 1, midiChannelCount) - 1) & 0x0f);
    return (statusByte << 16) | ((data1 & 0x7f) << 8) | (data2 & 0x7f);
}

export function createNoteOn (channel, note, velocity)       { return encodeMIDIMessage (0x90, channel, note, clamp (Math.round (velocity), 1, 127)); }
export function createNoteOff (channel, note, velocity = 64) { return encodeMIDIMessage (0x80, channel, note, velocity); }
export function createControlChange (channel, controller, value) { return encodeMIDIMessage (0xb0, channel, controller, clamp (Math.round (value), 0, 127)); }

/// A 14-bit pitch-bend message (0 to 16383, centre 8192): the low 7 bits go first
export function createPitchBend (channel, value)
{
    const v = clamp (Math.round (value), 0, pitchBendMax);
    return encodeMIDIMessage (0xe0, channel, v & 0x7f, v >> 7);
}

/// The 14-bit bend value for an amount from -1 (all the way down) to 1 (all the way up)
export function getPitchBendValue (amount)
{
    const a = clamp (Number (amount) || 0, -1, 1);
    return Math.round (pitchBendCentre + a * (a < 0 ? pitchBendCentre : pitchBendMax - pitchBendCentre));
}

/// The velocity for a click at a fraction of the way down a key (0 = its top, 1 = its bottom):
/// soft at the top, loud at the bottom, as where you'd strike a real key
export function getVelocityFromPosition (fraction)
{
    return Math.round (1 + clamp (Number (fraction) || 0, 0, 1) * 126);
}

/// The messages that silence everything: a note-off for each held note ({ channel, note }),
/// then, on every channel, sustain off (CC64), all notes off (CC123) and all sound off (CC120)
export function getPanicMessages (heldNotes = [])
{
    const messages = heldNotes.map (n => createNoteOff (n.channel, n.note, 0));

    for (let channel = 1; channel <= midiChannelCount; ++channel)
        messages.push (createControlChange (channel, 64, 0), createControlChange (channel, 123, 0), createControlChange (channel, 120, 0));

    return messages;
}

/// The keyboard's lowest note for an octave shift
export function getRootNoteForOctave (octave)
{
    return defaultRootNote + 12 * clamp (Math.round (octave), minOctaveShift, maxOctaveShift);
}

/// A complete, valid set of the persistent settings, from whatever was stored
export function normaliseMIDIControlSettings (stored)
{
    const s = (stored && typeof stored === "object") ? stored : {};
    const d = defaultMIDIControlSettings;
    const int = (v, lo, hi, fallback) => Number.isFinite (Number (v)) && v !== null && v !== "" ? clamp (Math.round (Number (v)), lo, hi) : fallback;

    return {
        velocityMode: s.velocityMode === "position" ? "position" : "fixed",
        velocity:     int (s.velocity, 1, 127, d.velocity),
        channel:      int (s.channel, 1, midiChannelCount, d.channel),
        octave:       int (s.octave, minOctaveShift, maxOctaveShift, d.octave),
    };
}

/// "+1", "0", "−2"
export function formatOctaveShift (octave)
{
    return octave > 0 ? `+${octave}` : octave < 0 ? `−${-octave}` : "0";
}

//==============================================================================
/// Reads an endpoint's saved settings from a PanelSettingsScopes (or none: the defaults)
export function loadMIDIControlSettings (settings, endpointID)
{
    return normaliseMIDIControlSettings (settings?.patch.get (midiControlsSettingName, {})?.[endpointID]);
}

/// Saves an endpoint's settings, and tells the other controls showing it (e.g. the I/O view's
/// and a popover's) to re-read them
export function saveMIDIControlSettings (settings, endpointID, values)
{
    if (! settings)
        return;

    const all = { ...(settings.patch.get (midiControlsSettingName, {}) ?? {}) };
    all[endpointID] = normaliseMIDIControlSettings (values);
    settings.patch.set (midiControlsSettingName, all);
    settingsChanges.dispatchEvent (new CustomEvent ("change", { detail: { settings, endpointID } }));
}

const settingsChanges = new EventTarget();

/// The MIDIControls that exist now, so that the commands can reach them
const liveControls = new Set();

/// The live controls for a connection's endpoint
export function findMIDIControls (patchConnection, endpointID)
{
    return [...liveControls].filter (c => c.patchConnection === patchConnection && c.endpointID === endpointID);
}

//==============================================================================
/*
    options: { endpointID, patchConnection, keyboard (the PianoKeyboard element, or null),
               settings (PanelSettingsScopes, or null), store, scheduler }
*/
export class MIDIControls
{
    constructor ({ endpointID, patchConnection, keyboard = null, settings = null, store = null, scheduler = frameScheduler })
    {
        this.endpointID = endpointID;
        this.patchConnection = patchConnection;
        this.keyboard = keyboard;
        this.settings = settings;
        this.scheduler = scheduler;
        this.disposer = new Disposer();
        this.values = loadMIDIControlSettings (settings, endpointID);
        this.heldNotes = new Map();         // note -> the channel its note-on went to
        this.pointerVelocity = null;        // while a pointer is down on the keyboard, in "position" mode
        this.bendAmount = 0;
        this.modValue = 0;
        this.isSustainOn = false;
        this.isBendHeld = false;

        // (a frame function: eases the bend back to the centre, and stops there)
        this.springBack = (_time, dt) =>
        {
            if (this.isBendHeld)
                return false;

            const next = this.bendAmount * Math.exp (-(dt || 16) / springBackTimeConstantMs);
            this.setBend (Math.abs (next) < 0.01 ? 0 : next);
            return this.bendAmount !== 0;
        };

        this.createElements();
        this.attachToKeyboard();
        this.applyValues();

        this.disposer.listen (settingsChanges, "change", (e) =>
        {
            if (e.detail.settings === this.settings && e.detail.endpointID === this.endpointID)
                this.reloadSettings();
        });

        if (store)
            this.disposer.add (store.subscribe (["patchSettings"], () => this.reloadSettings()));

        liveControls.add (this);
        this.disposer.add (() => { liveControls.delete (this); this.scheduler.cancel (this.springBack); });
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    //==============================================================================
    send (message)
    {
        this.patchConnection?.sendMIDIInputEvent (this.endpointID, message);
    }

    get channel()   { return this.values.channel; }

    setValues (changes)
    {
        const previousChannel = this.values.channel;
        this.values = normaliseMIDIControlSettings ({ ...this.values, ...changes });

        // (a held sustain belongs to the channel it was sent on)
        if (this.values.channel !== previousChannel && this.isSustainOn)
        {
            this.send (createControlChange (previousChannel, 64, 0));
            this.setSustainShown (false);
        }

        this.applyValues();
        saveMIDIControlSettings (this.settings, this.endpointID, this.values);
    }

    reloadSettings()
    {
        this.values = loadMIDIControlSettings (this.settings, this.endpointID);
        this.applyValues();
    }

    shiftOctave (delta)     { this.setValues ({ octave: this.values.octave + delta }); }

    /// Silences everything that this endpoint might be holding, and puts the controls at rest
    panic()
    {
        const held = [...this.heldNotes].map (([note, channel]) => ({ note, channel }));
        this.heldNotes.clear();

        // (the keyboard forgets its pressed keys, without sending its own note-offs)
        this.isPanicking = true;
        try { this.keyboard?.allNotesOff?.(); }
        finally { this.isPanicking = false; }

        for (const message of getPanicMessages (held))
            this.send (message);

        this.setSustainShown (false);
        this.scheduler.cancel (this.springBack);
        this.isBendHeld = false;

        if (this.bendAmount !== 0)
            this.setBend (0);
    }

    //==============================================================================
    createElements()
    {
        const id = this.endpointID;

        // velocity: fixed (with its value), or from where the key is clicked
        this.velocityButtons = [["fixed", "Fixed", "Every note has the velocity set here"],
                                ["position", "By position", "The velocity comes from where you click the key: softer at the top, louder at the bottom"]]
            .map (([mode, label, title]) => el ("button", { class: "cp-seg-button", type: "button", title, dataset: { mode },
                                                            onclick: () => this.setValues ({ velocityMode: mode }) }, label));

        this.velocityInput = el ("input", { class: "cp-input cp-midi-number", type: "number", min: 1, max: 127, step: 1,
                                            "aria-label": "Fixed velocity", title: "The velocity of the keyboard's notes (1–127)",
                                            onchange: () => this.setValues ({ velocity: this.velocityInput.value }) });

        // channel
        this.channelSelect = el ("select", { class: "cp-select cp-midi-channel", "aria-label": "MIDI channel", title: "The channel of every message sent from here",
                                             onchange: () => this.setValues ({ channel: Number (this.channelSelect.value) }) },
                                 Array.from ({ length: midiChannelCount }, (_, i) => el ("option", { value: i + 1 }, String (i + 1))));

        // octave
        this.octaveDown = el ("button", { class: "cp-seg-button", type: "button", "aria-label": "Octave down", title: "Shift the keyboard down an octave",
                                          onclick: () => this.shiftOctave (-1) }, "−");
        this.octaveUp = el ("button", { class: "cp-seg-button", type: "button", "aria-label": "Octave up", title: "Shift the keyboard up an octave",
                                        onclick: () => this.shiftOctave (1) }, "+");
        this.octaveText = el ("span", { class: "cp-midi-octave-text", role: "status" });

        // performance controls
        this.bendStrip = new ControlStrip ({ label: "Pitch bend", isBipolar: true, min: -8192, max: 8191,
                                             onStart: () => { this.isBendHeld = true; this.scheduler.cancel (this.springBack); },
                                             onChange: (fraction) => this.setBend (fraction * 2 - 1),
                                             onEnd: () => { this.isBendHeld = false; this.startSpringBack(); },
                                             onStep: (steps) => { this.isBendHeld = true; this.scheduler.cancel (this.springBack); this.setBend (this.bendAmount + steps / 8); } });

        this.modStrip = new ControlStrip ({ label: "Mod wheel", min: 0, max: 127,
                                            onChange: (fraction) => this.setMod (Math.round (fraction * 127)),
                                            onStep: (steps) => this.setMod (this.modValue + steps) });

        this.sustainSwitch = el ("button", { class: "cp-toggle", type: "button", role: "switch", "aria-checked": "false", "aria-label": "Sustain",
                                             title: "Sustain pedal (CC64)", onclick: () => this.setSustain (! this.isSustainOn) },
                                 el ("span", { class: "cp-toggle-knob" }));

        this.panicButton = el ("button", { class: "cp-small-button cp-midi-panic", type: "button",
                                           title: "Turn off every note and sound: note-offs for the held notes, then sustain off, all notes off and all sound off on all 16 channels",
                                           onclick: () => this.panic() }, "Panic");

        const label = (text, control) => el ("label", { class: "cp-midi-field" }, el ("span", { class: "cp-midi-label" }, text), control);

        this.element = el ("div", { class: "cp-midi-controls", role: "group", "aria-label": "MIDI controls for " + id },
                           el ("div", { class: "cp-midi-row" },
                               el ("div", { class: "cp-midi-field" }, el ("span", { class: "cp-midi-label", id: `cp-midi-vel-${uniqueID}` }, "Velocity"),
                                   el ("div", { class: "cp-seg cp-midi-velocity-mode", role: "group", "aria-labelledby": `cp-midi-vel-${uniqueID}` }, this.velocityButtons),
                                   this.velocityInput),
                               label ("Channel", this.channelSelect),
                               el ("div", { class: "cp-midi-field" }, el ("span", { class: "cp-midi-label" }, "Octave"),
                                   el ("div", { class: "cp-seg cp-midi-octave", role: "group", "aria-label": "Octave" }, this.octaveDown, this.octaveText, this.octaveUp))),
                           el ("div", { class: "cp-midi-row" },
                               el ("div", { class: "cp-midi-field cp-midi-strip-field" }, el ("span", { class: "cp-midi-label" }, "Bend"), this.bendStrip.element),
                               el ("div", { class: "cp-midi-field cp-midi-strip-field" }, el ("span", { class: "cp-midi-label" }, "Mod"), this.modStrip.element),
                               el ("div", { class: "cp-midi-field" }, el ("span", { class: "cp-midi-label" }, "Sustain"), this.sustainSwitch),
                               this.panicButton));

        ++uniqueID;
        this.disposer.add (() => { this.bendStrip.dispose(); this.modStrip.dispose(); });
        this.bendStrip.setFraction (0.5, 0);
        this.modStrip.setFraction (0, 0);
    }

    applyValues()
    {
        const v = this.values;

        for (const b of this.velocityButtons)
            setPressed (b, b.dataset.mode === v.velocityMode);

        if (document.activeElement !== this.velocityInput)
            this.velocityInput.value = String (v.velocity);

        this.velocityInput.disabled = v.velocityMode !== "fixed";
        this.channelSelect.value = String (v.channel);

        const rootNote = getRootNoteForOctave (v.octave);
        const noteCount = Number (this.keyboard?.getAttribute ("note-count") ?? 61);
        this.octaveText.textContent = formatOctaveShift (v.octave);
        this.octaveText.title = `The keyboard plays ${midi.getNoteNameWithOctaveNumber (rootNote)} to ${midi.getNoteNameWithOctaveNumber (rootNote + noteCount - 1)}`;
        this.octaveDown.disabled = v.octave <= minOctaveShift;
        this.octaveUp.disabled = v.octave >= maxOctaveShift;

        if (this.keyboard && this.keyboard.getAttribute ("root-note") !== String (rootNote))
            this.keyboard.setAttribute ("root-note", String (rootNote));
    }

    //==============================================================================
    /// Plays the keyboard's notes with this channel and velocity, and works out the velocity
    /// from the click position before the keyboard sends its note (it plays on mousedown, which
    /// comes after pointerdown)
    attachToKeyboard()
    {
        const kb = this.keyboard;

        if (! kb)
            return;

        const updatePointerVelocity = (e) =>
        {
            const key = e.composedPath().find (n => n.classList?.contains ("note"));

            if (this.values.velocityMode === "position" && key)
            {
                const r = key.getBoundingClientRect();
                this.pointerVelocity = getVelocityFromPosition (r.height > 0 ? (e.clientY - r.top) / r.height : 0.5);
            }
        };

        this.disposer.listen (kb, "pointerdown", (e) => { this.pointerVelocity = null; updatePointerVelocity (e); }, { capture: true });
        this.disposer.listen (kb, "pointermove", (e) => { if (e.buttons !== 0) updatePointerVelocity (e); }, { capture: true });

        this.disposer.listen (kb, "note-down", (e) =>
        {
            const note = e.detail.note;
            const velocity = (this.values.velocityMode === "position" && this.pointerVelocity !== null) ? this.pointerVelocity : this.values.velocity;
            this.heldNotes.set (note, this.channel);
            this.send (createNoteOn (this.channel, note, velocity));
        });

        this.disposer.listen (kb, "note-up", (e) =>
        {
            const note = e.detail.note;
            const channel = this.heldNotes.get (note) ?? this.channel;
            this.heldNotes.delete (note);

            if (! this.isPanicking)
                this.send (createNoteOff (channel, note));
        });
    }

    //==============================================================================
    setBend (amount)
    {
        const a = clamp (amount, -1, 1);
        const before = getPitchBendValue (this.bendAmount);
        this.bendAmount = a;
        const value = getPitchBendValue (a);

        if (value !== before)
            this.send (createPitchBend (this.channel, value));

        this.bendStrip.setFraction ((a + 1) / 2, value - pitchBendCentre);
    }

    /// Eases the bend back to the centre over a few frames, sending the values on the way, and
    /// always ends with the centre itself
    startSpringBack()
    {
        if (this.bendAmount === 0)
            return;

        if (prefersReducedMotion())
            this.setBend (0);
        else
            this.scheduler.schedule (this.springBack);
    }

    setMod (value)
    {
        const v = clamp (Math.round (value), 0, 127);

        if (v !== this.modValue)
            this.send (createControlChange (this.channel, 1, v));

        this.modValue = v;
        this.modStrip.setFraction (v / 127, v);
    }

    setSustain (isOn)
    {
        this.send (createControlChange (this.channel, 64, isOn ? 127 : 0));
        this.setSustainShown (isOn);
    }

    setSustainShown (isOn)
    {
        this.isSustainOn = !! isOn;
        this.sustainSwitch.classList.toggle ("cp-on", this.isSustainOn);
        this.sustainSwitch.setAttribute ("aria-checked", this.isSustainOn ? "true" : "false");
    }
}

let uniqueID = 0;
const springBackTimeConstantMs = 40;

function prefersReducedMotion()
{
    return globalThis.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;
}

//==============================================================================
/*
    A horizontal strip (role=slider) that a pointer drags: a recessed well, filled up to the value
    (or, if it's bipolar, from the centre to it). The arrow keys step it; for a spring-loaded one,
    onEnd() is called when the pointer or the key is released.

    onChange (fraction 0-1), onStep (steps: ±1, ±8 with Page Up/Down, or ±1000 for Home/End),
    onStart(), onEnd()
*/
export class ControlStrip
{
    constructor ({ label, isBipolar = false, min, max, onChange, onStep, onStart, onEnd })
    {
        this.isBipolar = isBipolar;
        this.min = min;
        this.max = max;
        this.disposer = new Disposer();
        this.fill = el ("span", { class: "cp-midi-strip-fill" });
        this.element = el ("div", { class: ["cp-midi-strip", isBipolar && "cp-bipolar"], role: "slider", tabindex: 0, "aria-label": label,
                                    "aria-valuemin": min, "aria-valuemax": max, "aria-orientation": "horizontal" },
                           isBipolar ? el ("span", { class: "cp-midi-strip-centre" }) : null, this.fill);

        const fractionAt = (e) =>
        {
            const r = this.element.getBoundingClientRect();
            return r.width > 0 ? clamp ((e.clientX - r.left) / r.width, 0, 1) : 0.5;
        };

        this.disposer.listen (this.element, "pointerdown", (e) =>
        {
            if (e.button !== 0)
                return;

            e.preventDefault();
            this.element.focus ({ preventScroll: true });
            this.element.classList.add ("cp-dragging");
            onStart?.();
            onChange (fractionAt (e));
            this.stopTracking = trackPointer (e, this.element, (ev) => onChange (fractionAt (ev)),
                                              () => { this.element.classList.remove ("cp-dragging"); this.stopTracking = null; onEnd?.(); });
        });

        const stepKeys = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 8, PageDown: -8, End: 1000, Home: -1000 };

        this.disposer.listen (this.element, "keydown", (e) =>
        {
            const steps = stepKeys[e.key];

            if (steps === undefined || e.altKey || e.ctrlKey || e.metaKey)
                return;

            e.preventDefault();
            onStep (steps);
        });

        this.disposer.listen (this.element, "keyup", (e) => { if (stepKeys[e.key] !== undefined) onEnd?.(); });
        this.disposer.listen (this.element, "blur", () => { if (! this.stopTracking) onEnd?.(); });
        this.disposer.add (() => this.stopTracking?.());
    }

    dispose()
    {
        this.disposer.dispose();
    }

    /// Shows a value at a fraction (0-1) of the way along the strip; valueNow is the value it
    /// stands for (between min and max), for assistive technology
    setFraction (fraction, valueNow)
    {
        const f = clamp (fraction, 0, 1);
        const left = this.isBipolar ? Math.min (f, 0.5) : 0;
        const right = this.isBipolar ? Math.max (f, 0.5) : f;
        this.fill.style.left = `${(left * 100).toFixed (2)}%`;
        this.fill.style.width = `${((right - left) * 100).toFixed (2)}%`;
        this.element.setAttribute ("aria-valuenow", String (valueNow));
    }
}

//==============================================================================
/*
    The MIDI commands for the palette: panic (every MIDI input), and the octave of the first
    one's keyboard. Disabled when the patch has no MIDI input.

    getMIDIInputs(): the patch's MIDI input endpoints; getPatchConnection(); settings: the panel's
    PanelSettingsScopes
*/
export function createMIDICommands ({ getMIDIInputs, getPatchConnection, settings })
{
    const hasMIDIInput = () => !! getPatchConnection() && getMIDIInputs().length > 0;

    const panic = () =>
    {
        const patchConnection = getPatchConnection();

        for (const endpoint of getMIDIInputs())
        {
            const controls = findMIDIControls (patchConnection, endpoint.endpointID);

            if (controls.length > 0)
                controls.forEach (c => c.panic());
            else
                getPanicMessages().forEach (m => patchConnection.sendMIDIInputEvent (endpoint.endpointID, m));
        }
    };

    const shiftOctave = (delta) =>
    {
        const id = getMIDIInputs()[0]?.endpointID;
        const controls = findMIDIControls (getPatchConnection(), id);

        if (controls.length > 0)
        {
            controls[0].shiftOctave (delta);
        }
        else if (id)
        {
            const values = loadMIDIControlSettings (settings, id);
            saveMIDIControlSettings (settings, id, { ...values, octave: values.octave + delta });
        }
    };

    return [
        { id: "midi.panic",      title: "MIDI panic: all notes off", category: "MIDI", keywords: ["stuck notes", "all sound off", "silence", "reset"],
          run: panic, isEnabled: hasMIDIInput },
        { id: "midi.octaveUp",   title: "Shift the keyboard up an octave", category: "MIDI", keywords: ["octave", "transpose", "higher"],
          run: () => shiftOctave (1), isEnabled: hasMIDIInput },
        { id: "midi.octaveDown", title: "Shift the keyboard down an octave", category: "MIDI", keywords: ["octave", "transpose", "lower"],
          run: () => shiftOctave (-1), isEnabled: hasMIDIInput },
    ];
}
