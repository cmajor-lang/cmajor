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

import { el, Disposer, setPressed, trackPointer } from "../core/dom.js";
import { hostTimelinePurposes } from "../widgets/registry.js";

/*
    The top bar's transport: play/stop, record, loop, rewind, the tempo, the time signature and
    the position (bars.beats.ticks, or a time), for patches that read the host's timeline.

    The session's transport (set_transport / transport_state, see the session's
    addTransportStateListener()) is the truth: the buttons send only the field they change, and
    show what the server broadcasts back. Between the server's updates (every
    transportUpdateIntervalMs while playing), the position is extrapolated from the last state,
    through the frame scheduler, and only while the transport and the audio are both running.

    It shows when the patch has an input with a host-timeline purpose, or always (with a patch
    loaded) if the global setting "alwaysShowTransport" is on. A server that doesn't answer within
    transportSupportTimeoutMs is too old to have a transport to control: the control is then
    disabled, with a tooltip saying why.
*/

export const transportUpdateIntervalMs = 50;
export const transportSupportTimeoutMs = 2000;
export const ticksPerBeat = 960;
export const localTempoHoldMs = 500;
export const tempoRange = Object.freeze ({ min: 1, max: 999 });
export const numeratorRange = Object.freeze ({ min: 1, max: 64 });
export const denominatorRange = Object.freeze ({ min: 1, max: 32 });

/// The server's defaults (and an older server's fixed transport)
export const defaultTransportState = Object.freeze ({ tempo: 120, numerator: 4, denominator: 4, playing: true, recording: false, looping: false,
                                                      frame: 0, quarterNote: 0, barStartQuarterNote: 0, sampleRate: 0 });

const olderServerMessage = "This server is too old to control the transport: it always plays at 120 BPM in 4/4";

//==============================================================================
// The pure parts (tested in core-tests.js)

/// True if any of the endpoints reads the host's timeline (tempo, time signature, transport or position)
export function hasHostTimelineInputs (inputs)
{
    return (inputs ?? []).some (e => hostTimelinePurposes.has (e?.purpose ?? ""));
}

/// A tempo clamped to the server's range (to 2 decimal places), or null if it isn't a number
export function clampTempo (tempo)
{
    tempo = Number (tempo);

    if (! Number.isFinite (tempo))
        return null;

    return Math.round (Math.max (tempoRange.min, Math.min (tempoRange.max, tempo)) * 100) / 100;
}

/// The tempo moved by a number of steps: 1 BPM each, or 10 if isLarge
export function nudgeTempo (tempo, steps, isLarge = false)
{
    return clampTempo ((clampTempo (tempo) ?? defaultTransportState.tempo) + steps * (isLarge ? 10 : 1));
}

/// Reads a typed tempo ("128", "97.5 bpm"), clamped; null if it isn't one
export function parseTempo (text)
{
    const match = /^\s*(-?\d+(?:\.\d*)?|-?\.\d+)\s*(?:bpm)?\s*$/i.exec (String (text ?? ""));
    return match ? clampTempo (parseFloat (match[1])) : null;
}

export function formatTempo (tempo)
{
    return Number.isFinite (tempo) ? String (Math.round (tempo * 100) / 100) : "";
}

/// As the server does it: rounded, then clamped
export function clampNumerator (numerator)
{
    numerator = Math.round (Number (numerator));
    return Number.isFinite (numerator) ? Math.max (numeratorRange.min, Math.min (numeratorRange.max, numerator)) : null;
}

/// As the server does it: rounded, clamped to 1-32, then rounded down to a power of two (12 → 8)
export function roundDenominator (denominator)
{
    denominator = Math.round (Number (denominator));

    if (! Number.isFinite (denominator))
        return null;

    denominator = Math.max (denominatorRange.min, Math.min (denominatorRange.max, denominator));
    return 2 ** Math.floor (Math.log2 (denominator));
}

/** The position as { bar, beat, tick } (bar and beat from 1, ticksPerBeat ticks to a beat),
    where a beat is the time signature's denominator (an eighth in 6/8).
    barStartQuarterNote is where the bar holding quarterNote starts: bars are counted from
    there, so a position that has moved on past it (when it's extrapolated) still comes out right. */
export function getBarsBeatsTicks (quarterNote, barStartQuarterNote, numerator, denominator)
{
    numerator = clampNumerator (numerator) ?? 4;
    denominator = roundDenominator (denominator) ?? 4;
    quarterNote = Math.max (0, Number (quarterNote) || 0);

    const beatLength = 4 / denominator;
    const barLength = beatLength * numerator;
    const epsilon = 1e-9;
    let barStart = Number (barStartQuarterNote);

    if (! Number.isFinite (barStart) || barStart > quarterNote + epsilon)
        barStart = Math.floor (quarterNote / barLength + epsilon) * barLength;

    const barsAfterStart = Math.floor ((quarterNote - barStart) / barLength + epsilon);
    const positionInBar = Math.max (0, quarterNote - (barStart + barsAfterStart * barLength));
    const beatIndex = Math.min (numerator - 1, Math.floor (positionInBar / beatLength + epsilon));
    const tick = Math.floor ((positionInBar - beatIndex * beatLength) / beatLength * ticksPerBeat + epsilon);

    return { bar: Math.round (barStart / barLength) + barsAfterStart + 1,
             beat: beatIndex + 1,
             tick: Math.max (0, Math.min (ticksPerBeat - 1, tick)) };
}

/// "bar.beat.tick", e.g. "3.2.480"
export function formatBarsBeatsTicks (quarterNote, barStartQuarterNote, numerator, denominator)
{
    const { bar, beat, tick } = getBarsBeatsTicks (quarterNote, barStartQuarterNote, numerator, denominator);
    return `${bar}.${beat}.${String (tick).padStart (3, "0")}`;
}

/// "mm:ss.mmm" ("h:mm:ss.mmm" from an hour), or dashes without a sample rate
export function formatTransportTime (frame, sampleRate)
{
    if (! (sampleRate > 0) || ! Number.isFinite (frame))
        return "--:--.---";

    const totalMs = Math.floor (Math.max (0, frame) / sampleRate * 1000 + 1e-6);
    const ms = totalMs % 1000, seconds = Math.floor (totalMs / 1000) % 60;
    const minutes = Math.floor (totalMs / 60000) % 60, hours = Math.floor (totalMs / 3600000);
    const pad = (n, digits = 2) => String (n).padStart (digits, "0");

    return (hours > 0 ? `${hours}:` : "") + `${pad (minutes)}:${pad (seconds)}.${pad (ms, 3)}`;
}

/** Where the transport will have got to elapsedMs after it reported state: if it's playing (and
    has a sample rate), the frame and quarter note move on at its rate and tempo, and the bar
    start follows. elapsedMs is limited to maxElapsedMs, so a stalled server doesn't run away. */
export function extrapolateTransport (state, elapsedMs, maxElapsedMs = Infinity)
{
    if (! state?.playing || ! (state.sampleRate > 0) || ! (state.tempo > 0))
        return state;

    const seconds = Math.max (0, Math.min (maxElapsedMs, elapsedMs)) / 1000;
    const quarterNote = state.quarterNote + seconds * state.tempo / 60;
    const barLength = 4 * (clampNumerator (state.numerator) ?? 4) / (roundDenominator (state.denominator) ?? 4);
    const barStart = state.barStartQuarterNote + Math.floor ((quarterNote - state.barStartQuarterNote) / barLength + 1e-9) * barLength;

    return { ...state, frame: state.frame + seconds * state.sampleRate, quarterNote, barStartQuarterNote: barStart };
}

/// A transport_state message with every field present and of the right type (missing or
/// invalid ones take the defaults)
export function normaliseTransportState (message)
{
    const result = { ...defaultTransportState };

    if (! message || typeof message !== "object")
        return result;

    for (const [key, defaultValue] of Object.entries (defaultTransportState))
    {
        const value = message[key];

        if (typeof defaultValue === "boolean" ? typeof value === "boolean" : Number.isFinite (value))
            result[key] = value;
    }

    return result;
}

//==============================================================================
export class TransportControl
{
    /// ctx: the panel's (session, store, scheduler, settings, commands)
    constructor (ctx)
    {
        this.ctx = ctx;
        this.session = ctx.session;
        this.disposer = new Disposer();
        this.state = null;              // the last transport_state, normalised
        this.stateTime = 0;             // performance.now() when it arrived
        this.isSupported = undefined;   // true once a state has arrived; false if none came in time
        this.isShown = false;
        this.isSubscribed = false;
        this.isAnimating = false;
        this.numAnimationFrames = 0;    // (for tests: frames drawn by the scheduler)
        this.timeFormat = ctx.settings?.global.get ("transportTimeFormat", "bars") === "time" ? "time" : "bars";
        this.listener = (message) => this.handleState (message);
        this.drawFrame = () => this.handleFrame();

        this.createElements();

        this.disposer.add (ctx.store.subscribe (["connection", "inputs", "alwaysShowTransport", "playing"], () => this.update(), true));
        this.disposer.add (() => this.setSubscribed (false));
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    get isAvailable()       { return this.isShown && this.isSupported === true; }
    get currentState()      { return this.state ?? defaultTransportState; }

    //==============================================================================
    // Actions (the buttons and commands): each sends only the field it changes

    togglePlaying()         { this.send ({ playing: ! this.currentState.playing }); }
    toggleRecording()       { this.send ({ recording: ! this.currentState.recording }); }
    toggleLooping()         { this.send ({ looping: ! this.currentState.looping }); }
    rewind()                { this.send ({ rewind: true }); }

    setTempo (tempo)
    {
        tempo = clampTempo (tempo);

        if (tempo !== null)
        {
            // (states already on their way still carry the old tempo: see showState())
            this.localTempo = { value: tempo, time: performance.now() };
            this.send ({ tempo });
        }

        return tempo;
    }

    setNumerator (numerator)        { this.send ({ numerator: clampNumerator (numerator) }); }
    setDenominator (denominator)    { this.send ({ denominator: roundDenominator (denominator) }); }

    send (props)
    {
        if (this.isShown && this.isSupported !== false)
            this.session.setTransport (props);
    }

    setTimeFormat (format)
    {
        this.timeFormat = format === "time" ? "time" : "bars";
        this.ctx.settings?.global.set ("transportTimeFormat", this.timeFormat);
        this.updatePositionTitle();
        this.drawPosition (true);
    }

    //==============================================================================
    createElements()
    {
        const iconButton = (className, glyph, label, title, onclick, { isToggle = true, shortcut = null } = {}) =>
            el ("button", { class: ["cp-button", "cp-icon-button", className], type: "button", "aria-label": label, title,
                            "aria-pressed": isToggle ? "false" : null, "aria-keyshortcuts": shortcut, onclick },
                el ("span", { "aria-hidden": "true" }, glyph));

        this.playButton = iconButton ("cp-transport-play", "▶", "Play", "Play or stop the transport (P)", () => this.togglePlaying(), { shortcut: "P" });
        this.recordButton = iconButton ("cp-transport-record", "●", "Record", "Record: sets the recording flag of the patch's transport state",
                                        () => this.toggleRecording());
        this.loopButton = iconButton ("cp-transport-loop", "↻", "Loop", "Loop: sets the looping flag of the patch's transport state (there are no loop points)",
                                      () => this.toggleLooping());
        this.rewindButton = iconButton ("cp-transport-rewind", "⇤", "Rewind to the start", "Rewind to the start (Home)", () => this.rewind(),
                                        { isToggle: false, shortcut: "Home" });

        // the tempo: type it, step it with the arrow keys (Shift or Page Up/Down: by 10), or drag it up and down
        this.tempoInput = el ("input", { class: "cp-input cp-transport-tempo-input", type: "text", inputMode: "decimal", role: "spinbutton",
                                         "aria-label": "Tempo", "aria-valuemin": String (tempoRange.min), "aria-valuemax": String (tempoRange.max),
                                         autocomplete: "off", spellcheck: false,
                                         title: "Tempo in BPM: type a value, use ↑ and ↓ (with Shift, by 10), or drag up and down",
                                         onkeydown: (e) => this.handleTempoKey (e),
                                         oninput: () => { this.isEditingTempo = true; },
                                         onblur: () => this.commitTempo(),
                                         onpointerdown: (e) => this.handleTempoPointerDown (e) });
        this.tempoField = el ("span", { class: "cp-transport-tempo" }, this.tempoInput, el ("span", { class: "cp-transport-unit", "aria-hidden": "true" }, "BPM"));

        // the time signature
        const numerators = Array.from ({ length: 32 }, (_, i) => i + 1);
        const denominators = [1, 2, 4, 8, 16, 32];
        this.numeratorSelect = el ("select", { class: "cp-select cp-transport-select", "aria-label": "Time signature: beats per bar",
                                               title: "Time signature: beats per bar", onchange: () => this.setNumerator (this.numeratorSelect.value) },
                                   numerators.map (n => el ("option", { value: String (n) }, String (n))));
        this.denominatorSelect = el ("select", { class: "cp-select cp-transport-select", "aria-label": "Time signature: beat unit",
                                                 title: "Time signature: beat unit", onchange: () => this.setDenominator (this.denominatorSelect.value) },
                                     denominators.map (n => el ("option", { value: String (n) }, String (n))));
        this.signature = el ("span", { class: "cp-transport-signature", role: "group", "aria-label": "Time signature" },
                             this.numeratorSelect, el ("span", { class: "cp-transport-slash", "aria-hidden": "true" }, "/"), this.denominatorSelect);

        // the position: click for the other format
        this.positionText = el ("span", { class: "cp-transport-position-text" });
        this.positionKind = el ("span", { class: "cp-transport-position-kind" });
        this.positionButton = el ("button", { class: "cp-button cp-transport-position", type: "button",
                                              onclick: () => this.setTimeFormat (this.timeFormat === "time" ? "bars" : "time") },
                                  this.positionKind, this.positionText);
        this.updatePositionTitle();

        this.element = el ("div", { class: "cp-transport", role: "group", "aria-label": "Transport", hidden: true },
                           this.playButton, this.recordButton, this.loopButton, this.rewindButton,
                           this.tempoField, this.signature, this.positionButton);

        this.controls = [this.playButton, this.recordButton, this.loopButton, this.rewindButton, this.tempoInput,
                         this.numeratorSelect, this.denominatorSelect];

        this.showState();
    }

    updatePositionTitle()
    {
        const isTime = this.timeFormat === "time";
        // (the kind is for screen readers and the narrow layout's tooltip: the text alone is just numbers)
        this.positionKind.textContent = isTime ? "Time " : "Bar ";
        this.positionButton.title = isTime ? "The position in minutes and seconds: click to show bars, beats and ticks"
                                           : "The position in bars, beats and ticks: click to show the time";
    }

    //==============================================================================
    update()
    {
        const s = this.ctx.store.state;
        const shouldShow = !! s.connection && (hasHostTimelineInputs (s.inputs) || !! s.alwaysShowTransport);

        if (shouldShow !== this.isShown)
        {
            this.isShown = shouldShow;
            this.element.hidden = ! shouldShow;
            this.setSubscribed (shouldShow);
        }

        this.updateAnimation();
    }

    setSubscribed (shouldSubscribe)
    {
        if (shouldSubscribe === this.isSubscribed)
            return;

        this.isSubscribed = shouldSubscribe;
        clearTimeout (this.supportTimer);
        this.supportTimer = undefined;

        if (shouldSubscribe)
        {
            this.session.addTransportStateListener (this.listener, transportUpdateIntervalMs);
            this.session.requestTransportState();

            // (an older server never answers)
            if (this.isSupported !== true)
                this.supportTimer = setTimeout (() =>
                {
                    this.supportTimer = undefined;

                    if (this.isSupported !== true)
                        this.setSupported (false);
                }, transportSupportTimeoutMs);
        }
        else
        {
            this.session.removeTransportStateListener (this.listener);
            this.stopAnimation();
        }
    }

    handleState (message)
    {
        this.state = normaliseTransportState (message);
        this.stateTime = performance.now();

        if (this.isSupported !== true)
            this.setSupported (true);

        this.showState();
        this.updateAnimation();
    }

    setSupported (isSupported)
    {
        this.isSupported = isSupported;
        this.element.classList.toggle ("cp-unsupported", isSupported === false);
        this.element.title = isSupported === false ? olderServerMessage : "";

        // (each control gets the explanation too, as a disabled control's own title is what's shown)
        for (const control of this.controls)
        {
            control.dataset.title ??= control.title;
            control.disabled = isSupported === false;
            control.title = isSupported === false ? olderServerMessage : control.dataset.title;
        }

        this.positionButton.disabled = isSupported === false;

        this.showState();
    }

    showState()
    {
        const s = this.currentState;

        setPressed (this.playButton, s.playing && this.isSupported !== false);
        setPressed (this.recordButton, s.recording);
        setPressed (this.loopButton, s.looping);

        // A tempo the user has just set is shown until the server's state agrees (or for
        // localTempoHoldMs, after which another client's change shows), so a periodic state sent
        // before a nudge arrived can't undo it, and the next nudge starts from the right value
        let tempo = s.tempo;

        if (this.localTempo)
        {
            if (this.localTempo.value === s.tempo || performance.now() - this.localTempo.time > localTempoHoldMs)
                this.localTempo = null;
            else
                tempo = this.localTempo.value;
        }

        if (! this.isEditingTempo)
            this.tempoInput.value = formatTempo (tempo);

        this.tempoInput.setAttribute ("aria-valuenow", String (tempo));
        this.tempoInput.setAttribute ("aria-valuetext", `${formatTempo (tempo)} BPM`);

        if (! [...this.numeratorSelect.options].some (o => o.value === String (s.numerator)))
            this.numeratorSelect.append (el ("option", { value: String (s.numerator) }, String (s.numerator)));

        this.numeratorSelect.value = String (s.numerator);
        this.denominatorSelect.value = String (s.denominator);
        this.drawPosition (true);
    }

    /// The position now: extrapolated while the transport and the audio are running
    getDisplayedState()
    {
        if (! this.state)
            return null;

        if (! this.shouldAnimate())
            return this.state;

        return extrapolateTransport (this.state, performance.now() - this.stateTime, this.maxExtrapolationMs);
    }

    get maxExtrapolationMs()        { return Math.max (250, 5 * transportUpdateIntervalMs); }

    drawPosition (force = false)
    {
        const s = this.getDisplayedState();
        let text;

        if (! s || this.isSupported === false)
            text = this.timeFormat === "time" ? "--:--.---" : "-.-.---";
        else if (this.timeFormat === "time")
            text = formatTransportTime (s.frame, s.sampleRate);
        else
            text = formatBarsBeatsTicks (s.quarterNote, s.barStartQuarterNote, s.numerator, s.denominator);

        if (force || text !== this.positionText.textContent)
            this.positionText.textContent = text;
    }

    //==============================================================================
    // The animation: only while there's something moving to show

    shouldAnimate()
    {
        return this.isShown && this.isSupported === true && !! this.state?.playing && !! this.ctx.store.get ("playing")
                 && this.state.sampleRate > 0;
    }

    updateAnimation()
    {
        if (this.shouldAnimate())
        {
            if (! this.isAnimating)
            {
                this.isAnimating = true;
                this.unschedule = this.ctx.scheduler.schedule (this.drawFrame);
            }
        }
        else
        {
            this.stopAnimation();
            this.drawPosition();
        }
    }

    stopAnimation()
    {
        if (this.isAnimating)
        {
            this.isAnimating = false;
            this.unschedule?.();
            this.unschedule = null;
        }
    }

    handleFrame()
    {
        ++this.numAnimationFrames;
        this.drawPosition();

        // (it goes idle if the server has stopped sending: the next state starts it again)
        const keepGoing = this.shouldAnimate() && performance.now() - this.stateTime < this.maxExtrapolationMs;

        if (! keepGoing)
        {
            this.isAnimating = false;
            this.unschedule = null;
        }

        return keepGoing;
    }

    //==============================================================================
    // The tempo field

    handleTempoKey (e)
    {
        const step = { ArrowUp: 1, ArrowDown: -1, PageUp: 1, PageDown: -1 }[e.key];

        if (step !== undefined)
        {
            e.preventDefault();
            const current = parseTempo (this.tempoInput.value) ?? this.currentState.tempo;
            const tempo = nudgeTempo (current, step, e.shiftKey || e.key.startsWith ("Page"));
            this.isEditingTempo = false;
            this.tempoInput.value = formatTempo (tempo);
            this.setTempo (tempo);
        }
        else if (e.key === "Enter")
        {
            e.preventDefault();
            this.commitTempo();
            this.tempoInput.select();
        }
        else if (e.key === "Escape")
        {
            this.isEditingTempo = false;
            this.tempoInput.value = formatTempo (this.currentState.tempo);
        }
    }

    commitTempo()
    {
        if (! this.isEditingTempo)
            return;

        this.isEditingTempo = false;
        const tempo = parseTempo (this.tempoInput.value);

        if (tempo !== null && tempo !== this.currentState.tempo)
            this.setTempo (tempo);

        this.tempoInput.value = formatTempo (tempo ?? this.currentState.tempo);
    }

    /// A drag up or down on the (unfocused) field changes the tempo, 1 BPM per 3 px (10 with
    /// Shift); a click without a drag focuses it for typing
    handleTempoPointerDown (e)
    {
        if (e.button !== 0 || document.activeElement === this.tempoInput || this.tempoInput.disabled)
            return;

        e.preventDefault();
        const startY = e.clientY;
        const startTempo = this.currentState.tempo;
        let isDragging = false, lastSent = startTempo;

        trackPointer (e, this.tempoInput, (ev) =>
        {
            const dy = startY - ev.clientY;

            if (! isDragging && Math.abs (dy) < 3)
                return;

            isDragging = true;
            this.tempoInput.classList.add ("cp-dragging");
            const tempo = nudgeTempo (Math.round (startTempo), Math.round (dy / 3), ev.shiftKey);

            if (tempo !== lastSent)
            {
                lastSent = tempo;
                this.tempoInput.value = formatTempo (tempo);
                this.setTempo (tempo);
            }
        },
        () =>
        {
            this.tempoInput.classList.remove ("cp-dragging");

            if (! isDragging)
            {
                this.tempoInput.focus();
                this.tempoInput.select();
            }
        });
    }
}

//==============================================================================
/// The transport's commands, for the panel's CommandRegistry: enabled while the control is
/// shown and the server has a transport
export function createTransportCommands (getTransport)
{
    const isAvailable = () => !! getTransport()?.isAvailable;

    return [
        { id: "transport.playStop", title: "Play/stop the transport", category: "Transport", shortcut: "P", keywords: ["start", "pause", "timeline"],
          run: () => getTransport()?.togglePlaying(), isEnabled: isAvailable },
        { id: "transport.rewind",   title: "Rewind the transport",    category: "Transport", shortcut: "Home", keywords: ["start", "zero", "beginning", "position"],
          run: () => getTransport()?.rewind(), isEnabled: isAvailable },
        { id: "transport.record",   title: "Toggle recording",        category: "Transport", keywords: ["record", "arm"],
          run: () => getTransport()?.toggleRecording(), isEnabled: isAvailable },
        { id: "transport.loop",     title: "Toggle loop",             category: "Transport", keywords: ["looping", "cycle", "repeat"],
          run: () => getTransport()?.toggleLooping(), isEnabled: isAvailable },
    ];
}
