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

import { el, Disposer, setPressed } from "../core/dom.js";
import { getThemeColours, addThemeChangeListener } from "../core/theme.js";
import { SpectrumAnalyser, isValidFFTSize } from "../core/fft.js";
import { subscribeToFullAudio } from "../core/audio-streams.js";
import { frameScheduler } from "../core/frame-scheduler.js";
import { observeCanvasSize, getEndpointDisplayName } from "./widget-base.js";

/*
    The spectrum analyser display: an audio endpoint's spectrum, on a log frequency axis (20 Hz to
    Nyquist) and a −100…0 dBFS scale, as an averaged level line (with a soft fill) and a decaying
    peak-hold line, with a hover/keyboard readout of the frequency, level and note.

    - It subscribes to the endpoint's shared full-audio stream (core/audio-streams.js) through a
      SubscriptionGroup, so it shares one server listener with every other analyser or recorder of
      the same endpoint, and it stops receiving when its view pauses the group (a hidden tab).
    - The samples go into a core/fft.js SpectrumAnalyser; drawing is done by the frame scheduler,
      only while data is arriving or the trace is still settling (a peak falling, an average
      converging), so it's idle when nothing changes.
    - The bins are collapsed onto the canvas's pixel columns (collapseBinsToColumns()): where a
      column covers several bins it takes their maximum (so a narrow peak isn't lost), and where
      it covers less than one it interpolates between the neighbouring bins.
    - The controls (averaging, peak hold and its reset, FFT size, channel) are a toolbar row
      above the canvas; their choices are remembered for every analyser (settings.global
      "spectrumOptions").

    addSpectrumToggle (widget, settingsKey) gives a full-size audio widget a "Spectrum" button in
    its header, which shows an analyser under its meter; settingsKey names a per-patch setting
    (a list of endpoint IDs) that remembers which ones are open.

    The drawing maths is in plain functions (frequencyToX(), dbToY(), collapseBinsToColumns(),
    getFrequencyGridlines(), getLevelGridlines(), getNoteName(), formatSpectrumReadout()...),
    which have no DOM, for the tests.
*/

/// The display's range
export const spectrumMinFrequency = 20;
export const spectrumMinDB = -100;
export const spectrumMaxDB = 0;

/// The FFT sizes offered, and the default
export const spectrumFFTSizes = [2048, 4096, 8192, 16384];
export const defaultSpectrumFFTSize = 4096;

/// The averaging choices: the time constant of each, in seconds
export const spectrumAveragingTimes = Object.freeze ({ off: 0, fast: 0.125, slow: 1 });

/// How fast the held peaks fall, while peak hold is on
export const spectrumPeakDecayDBPerSecond = 10;

export const defaultSpectrumOptions = Object.freeze ({ averaging: "fast", peakHold: true, fftSize: defaultSpectrumFFTSize, channel: "average" });

//==============================================================================
/// The x position (0 … width) of a frequency on a log axis from minHz to maxHz
export function frequencyToX (frequency, width, minHz, maxHz)
{
    if (! (frequency > 0))
        return 0;

    return width * Math.log (frequency / minHz) / Math.log (maxHz / minHz);
}

/// The frequency at x (0 … width) on a log axis from minHz to maxHz
export function xToFrequency (x, width, minHz, maxHz)
{
    return minHz * Math.pow (maxHz / minHz, x / width);
}

/// The y position of a level (0 at maxDB, height at minDB, clamped to that range)
export function dbToY (db, height, minDB = spectrumMinDB, maxDB = spectrumMaxDB)
{
    const clamped = Math.max (minDB, Math.min (maxDB, Number.isFinite (db) ? db : minDB));
    return height * (maxDB - clamped) / (maxDB - minDB);
}

/// The level at y (the inverse of dbToY(), unclamped)
export function yToDB (y, height, minDB = spectrumMinDB, maxDB = spectrumMaxDB)
{
    return maxDB - (y / height) * (maxDB - minDB);
}

/// Collapses a spectrum's bins (levels in dB, bin k centred on k × binWidth Hz) onto numColumns
/// columns of a log axis from minHz to maxHz: column i covers the frequencies from x = i to i + 1.
/// A column holding one or more bin centres takes the highest of them; one holding none (where the
/// bins are wider than the columns) interpolates linearly between the bins either side of its
/// centre. Columns above the last bin get floorDB. Returns out (a Float32Array of numColumns).
export function collapseBinsToColumns (levels, binWidth, numColumns, minHz, maxHz,
                                       out = new Float32Array (numColumns), floorDB = -200)
{
    const lastBin = levels.length - 1;

    for (let i = 0; i < numColumns; ++i)
    {
        const startBin = xToFrequency (i, numColumns, minHz, maxHz) / binWidth;
        const endBin = xToFrequency (i + 1, numColumns, minHz, maxHz) / binWidth;
        const first = Math.ceil (startBin);
        const last = Math.min (lastBin, Math.ceil (endBin) - 1);   // (the bins with startBin ≤ k < endBin)

        if (first > lastBin)
        {
            out[i] = floorDB;
        }
        else if (last >= first)
        {
            let highest = levels[first];

            for (let k = first + 1; k <= last; ++k)
                if (levels[k] > highest)
                    highest = levels[k];

            out[i] = highest;
        }
        else
        {
            const centre = Math.min (lastBin, xToFrequency (i + 0.5, numColumns, minHz, maxHz) / binWidth);
            const k = Math.min (lastBin - 1, Math.floor (centre));
            const t = centre - k;
            out[i] = k < 0 ? levels[0] : levels[k] + (levels[k + 1] - levels[k]) * t;
        }
    }

    return out;
}

/// The frequency gridlines between minHz and maxHz (not on either edge): the 1-2-5 steps of each
/// decade are major and labelled ("50", "100", … "1k", "20k"), and the other whole multiples
/// (30, 40, 60…) are minor, with no label. Returns [{ frequency, isMajor, label }] in order.
export function getFrequencyGridlines (minHz, maxHz)
{
    const lines = [];

    for (let decade = Math.pow (10, Math.floor (Math.log10 (minHz))); decade <= maxHz; decade *= 10)
    {
        for (let m = 1; m <= 9; ++m)
        {
            const frequency = Math.round (m * decade * 1000) / 1000;

            if (frequency <= minHz * 1.0001 || frequency >= maxHz * 0.9999)
                continue;

            const isMajor = m === 1 || m === 2 || m === 5;
            lines.push ({ frequency, isMajor, label: isMajor ? formatFrequencyLabel (frequency) : null });
        }
    }

    return lines;
}

/// A gridline's label: "50", "500", "1k", "2.5k", "20k"
export function formatFrequencyLabel (frequency)
{
    return frequency >= 1000 ? `${Math.round (frequency / 100) / 10}k` : `${Math.round (frequency)}`;
}

/// The level gridlines from maxDB down to (not including) minDB: every 10 dB if they'd be at least
/// minSpacing pixels apart in a plot plotHeight tall, otherwise every 20 dB. The multiples of 20
/// are labelled ("0", "−20"…). Returns [{ db, label }] from the top.
export function getLevelGridlines (minDB = spectrumMinDB, maxDB = spectrumMaxDB, plotHeight = 200, minSpacing = 14)
{
    const step = plotHeight * 10 / (maxDB - minDB) >= minSpacing ? 10 : 20;
    const lines = [];

    for (let db = Math.floor (maxDB / step) * step; db > minDB; db -= step)
        lines.push ({ db, label: db % 20 === 0 ? formatMinus (String (db)) : null });

    return lines;
}

//==============================================================================
const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/// The name of the nearest equal-tempered note (A4 = 440 Hz, middle C = C4), e.g. "B5" for
/// 1 kHz, or "" outside the MIDI range
export function getNoteName (frequency)
{
    if (! (frequency > 0))
        return "";

    const note = Math.round (69 + 12 * Math.log2 (frequency / 440));

    if (note < 0 || note > 127)
        return "";

    return noteNames[note % 12] + (Math.floor (note / 12) - 1);
}

/// A frequency for the readout: "62.5 Hz", "440 Hz", "1.00 kHz", "12.5 kHz"
export function formatSpectrumFrequency (frequency)
{
    if (! Number.isFinite (frequency))
        return "";

    if (frequency < 99.95)
        return `${frequency.toFixed (1)} Hz`;

    if (Math.round (frequency) < 1000)
        return `${Math.round (frequency)} Hz`;

    const kHz = frequency / 1000;
    return kHz < 9.995 ? `${kHz.toFixed (2)} kHz` : `${kHz.toFixed (1)} kHz`;
}

/// A level for the readout: "−12.1 dB" (with a real minus sign), or "< −100 dB" below the scale
export function formatSpectrumLevel (db, minDB = spectrumMinDB)
{
    if (! Number.isFinite (db) || db < minDB)
        return `< ${formatMinus (String (minDB))} dB`;

    const text = db.toFixed (1);
    return `${formatMinus (text === "-0.0" ? "0.0" : text)} dB`;
}

/// The readout for a point on the spectrum: "1.00 kHz · −12.1 dB · B5"
export function formatSpectrumReadout (frequency, db)
{
    const note = getNoteName (frequency);
    return [formatSpectrumFrequency (frequency), formatSpectrumLevel (db), note].filter (Boolean).join (" · ");
}

/// The analyser's accessible summary: "Spectrum: peak −12 dB at 1.00 kHz" (peak: { frequency, db } or null)
export function describeSpectrum (peak, minDB = spectrumMinDB)
{
    if (! peak || ! (peak.db >= minDB))
        return "Spectrum: no signal";

    const db = Math.round (peak.db);
    return `Spectrum: peak ${formatMinus (String (db === 0 ? 0 : db))} dB at ${formatSpectrumFrequency (peak.frequency)}`;
}

/// The highest bin between minHz and maxHz, refined by fitting a parabola through it and its
/// neighbours (in dB): { frequency, db, bin }, or null if there are no bins in that range
export function findSpectrumPeak (levels, binWidth, minHz = 0, maxHz = Infinity)
{
    const lastBin = levels.length - 1;
    const first = Math.max (0, Math.ceil (minHz / binWidth));
    const last = Math.min (lastBin, Math.floor (maxHz / binWidth));
    let bin = -1;

    for (let k = first; k <= last; ++k)
        if (bin < 0 || levels[k] > levels[bin])
            bin = k;

    if (bin < 0)
        return null;

    let offset = 0, db = levels[bin];

    if (bin > 0 && bin < lastBin)
    {
        const a = levels[bin - 1], b = levels[bin], c = levels[bin + 1];
        const curvature = a - 2 * b + c;

        if (curvature < 0)
        {
            offset = Math.max (-0.5, Math.min (0.5, 0.5 * (a - c) / curvature));
            db = b - 0.25 * (a - c) * offset;
        }
    }

    return { frequency: (bin + offset) * binWidth, db, bin };
}

/// The channel choices for an endpoint with numChannels channels: Average, then L/R for stereo,
/// or 1, 2, 3… for more. (None for a mono one.) Returns [{ value: "average" | index, label }].
export function getSpectrumChannelOptions (numChannels)
{
    if (! (numChannels > 1))
        return [];

    const names = numChannels === 2 ? ["L", "R"] : Array.from ({ length: numChannels }, (_, i) => `Ch ${i + 1}`);
    return [{ value: "average", label: "Average" }, ...names.map ((label, value) => ({ value, label }))];
}

/// Cleans up saved options (anything missing or invalid gets the default)
export function normaliseSpectrumOptions (options)
{
    const o = (options && typeof options === "object") ? options : {};

    return {
        averaging: Object.hasOwn (spectrumAveragingTimes, o.averaging) ? o.averaging : defaultSpectrumOptions.averaging,
        peakHold:  typeof o.peakHold === "boolean" ? o.peakHold : defaultSpectrumOptions.peakHold,
        fftSize:   spectrumFFTSizes.includes (o.fftSize) && isValidFFTSize (o.fftSize) ? o.fftSize : defaultSpectrumOptions.fftSize,
        channel:   o.channel === "average" || (Number.isInteger (o.channel) && o.channel >= 0) ? o.channel : "average",
    };
}

function formatMinus (text)     { return text.replace ("-", "\u2212"); }

//==============================================================================
/// The canvas's margins around the plot, in CSS pixels (the labels go in them)
const plotMargins = { left: 30, right: 6, top: 6, bottom: 15 };

/// How long after the last data the trace keeps being redrawn regardless
const dataTimeoutMs = 250;

/// The longest the peaks and the average keep moving after the data stops (then the display freezes)
const maxSettlingMs = 3000;

/*
    The analyser display (see the comment at the top).

    options: { endpoint, group (a SubscriptionGroup or EndpointDataHub), scheduler, store (for the
               sample rate: its deviceProperties), settings (PanelSettingsScopes, for the options),
               sampleRate (if there's no store) }
*/
export class SpectrumAnalyserView
{
    constructor ({ endpoint, group, scheduler = frameScheduler, store = null, settings = null, sampleRate = 48000 })
    {
        this.endpoint = endpoint;
        this.scheduler = scheduler;
        this.settings = settings;
        this.disposer = new Disposer();
        this.numChannels = Math.max (1, endpoint?.numAudioChannels ?? 1);
        this.options = normaliseSpectrumOptions (settings?.global.get ("spectrumOptions", null));

        if (typeof this.options.channel === "number" && this.options.channel >= this.numChannels)
            this.options.channel = "average";

        this.analyser = new SpectrumAnalyser ({ fftSize: this.options.fftSize, sampleRate: getSampleRate (store) ?? sampleRate,
                                                numChannels: this.numChannels, channelMode: this.options.channel });
        this.applyOptions();

        this.levelColumns = new Float32Array (0);
        this.peakColumns = new Float32Array (0);
        this.peak = null;
        this.cursorFrequency = null;    // where the readout is (null: nowhere)
        this.pointerFrequency = null;   // where the pointer is, while it's over the plot
        this.lastDataTime = -Infinity;
        this.numChunks = 0;
        this.drawCount = 0;
        this.isAnimating = false;
        this.lastSummaryTime = -Infinity;
        this.labelFont = "";

        this.createElements();

        this.draw = (_time, dt) => this.drawFrame (dt);
        this.redraw = () => { this.isAnimating = true; this.scheduler.schedule (this.draw); };

        this.disposer.add (observeCanvasSize (this.canvas, () => { this.labelFont = ""; this.redraw(); }),
                           addThemeChangeListener (this.redraw),
                           () => this.scheduler.cancel (this.draw));

        if (store)
            this.disposer.add (store.subscribe (["deviceProperties"], () => this.setSampleRate (getSampleRate (store))));

        if (group && endpoint)
            this.disposer.add (subscribeToFullAudio (group, endpoint.endpointID, (chunk) => this.addChunk (chunk)));

        this.redraw();
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    //==============================================================================
    createElements()
    {
        const o = this.options;

        const segButton = (value, label, title) =>
            el ("button", { class: "cp-seg-button", type: "button", dataset: { averaging: value }, title, "aria-pressed": "false",
                            onclick: () => this.setOption ("averaging", value) }, label);

        this.averagingButtons = [segButton ("off", "Off", "No averaging"),
                                 segButton ("fast", "Fast", "Average over 125 ms"),
                                 segButton ("slow", "Slow", "Average over 1 s")];

        this.peakButton = el ("button", { class: "cp-small-button", type: "button", "aria-pressed": "false", title: "Hold the peaks (they fall slowly)",
                                          onclick: () => this.setOption ("peakHold", ! this.options.peakHold) }, "Peak");
        this.resetButton = el ("button", { class: "cp-small-button", type: "button", title: "Reset the held peaks", "aria-label": "Reset the peaks",
                                           onclick: () => { this.analyser.resetPeaks(); this.redraw(); } }, "Reset");

        this.fftSelect = el ("select", { class: "cp-select", "aria-label": "FFT size", title: "FFT size (frequency resolution)",
                                         onchange: () => this.setOption ("fftSize", Number (this.fftSelect.value)) },
                             spectrumFFTSizes.map (n => el ("option", { value: String (n) }, String (n))));

        const channelOptions = getSpectrumChannelOptions (this.numChannels);
        this.channelSelect = channelOptions.length === 0 ? null
            : el ("select", { class: "cp-select", "aria-label": "Channel", title: "Which channel to analyse",
                              onchange: () => this.setOption ("channel", this.channelSelect.value === "average" ? "average" : Number (this.channelSelect.value)) },
                  channelOptions.map (c => el ("option", { value: String (c.value) }, c.label)));

        this.toolbar = el ("div", { class: "cp-spectrum-toolbar", role: "toolbar", "aria-label": "Spectrum options" },
                           el ("div", { class: "cp-seg", role: "group", "aria-label": "Averaging", title: "Averaging" }, this.averagingButtons),
                           this.peakButton, this.resetButton, el ("span", { class: "cp-spacer" }), this.channelSelect, this.fftSelect);

        this.canvas = el ("canvas", { class: "cp-spectrum-canvas", role: "img", tabindex: "0", "aria-label": describeSpectrum (null),
                                      "aria-roledescription": "spectrum analyser",
                                      title: "Hover, or focus and use ← → (Shift: octaves, P: the peak), for the level at a frequency" });
        this.readout = el ("span", { class: "cp-spectrum-readout", "aria-live": "off" });
        this.element = el ("div", { class: "cp-spectrum" }, this.toolbar, el ("div", { class: "cp-spectrum-plot" }, this.canvas, this.readout));

        this.disposer.listen (this.canvas, "pointermove", (e) => this.setPointerPosition (e));
        this.disposer.listen (this.canvas, "pointerleave", () => { this.pointerFrequency = null; this.redraw(); });
        this.disposer.listen (this.canvas, "keydown", (e) => this.handleKey (e));
        this.disposer.listen (this.canvas, "blur", () => { this.cursorFrequency = null; this.redraw(); });

        this.fftSelect.value = String (o.fftSize);

        if (this.channelSelect)
            this.channelSelect.value = String (o.channel);

        this.showOptions();
    }

    showOptions()
    {
        for (const b of this.averagingButtons)
            setPressed (b, b.dataset.averaging === this.options.averaging);

        setPressed (this.peakButton, this.options.peakHold);
        this.resetButton.disabled = ! this.options.peakHold;
    }

    applyOptions()
    {
        const a = this.analyser;
        a.averagingTime = spectrumAveragingTimes[this.options.averaging] ?? 0;
        a.averagingAlpha = 0;
        a.peakDecayDBPerSecond = this.options.peakHold ? spectrumPeakDecayDBPerSecond : Infinity;
        a.channelMode = this.options.channel;

        if (a.fftSize !== this.options.fftSize)
            a.setFFTSize (this.options.fftSize);
    }

    /// Changes one of the options (averaging, peakHold, fftSize, channel), and remembers it
    setOption (name, value)
    {
        this.options = normaliseSpectrumOptions ({ ...this.options, [name]: value });
        this.applyOptions();

        if (name === "channel" || name === "peakHold")
            this.analyser.resetPeaks();

        this.showOptions();
        this.settings?.global.set ("spectrumOptions", this.options);
        this.redraw();
    }

    setSampleRate (sampleRate)
    {
        if (sampleRate > 0 && sampleRate !== this.analyser.sampleRate)
        {
            this.analyser.sampleRate = sampleRate;
            this.analyser.reset();
            this.redraw();
        }
    }

    get sampleRate()            { return this.analyser.sampleRate; }
    get maxFrequency()          { return this.analyser.sampleRate / 2; }

    //==============================================================================
    /// Takes a chunk from the full-audio stream ({ frame, numFrames, channels })
    addChunk (chunk)
    {
        this.analyser.addBlock (chunk.channels, chunk.numFrames);
        this.lastDataTime = performance.now();
        ++this.numChunks;
        this.redraw();
    }

    /// The plot's area in the canvas, in canvas (device) pixels
    getPlotArea()
    {
        const dpr = globalThis.devicePixelRatio || 1;
        const left = Math.round (plotMargins.left * dpr), top = Math.round (plotMargins.top * dpr);
        const width = Math.max (1, this.canvas.width - left - Math.round (plotMargins.right * dpr));
        const height = Math.max (1, this.canvas.height - top - Math.round (plotMargins.bottom * dpr));
        return { left, top, width, height, dpr };
    }

    /// The frequency at a canvas x (device pixels), and back
    frequencyAt (x, plot = this.getPlotArea())   { return xToFrequency (x - plot.left, plot.width, spectrumMinFrequency, this.maxFrequency); }
    xOf (frequency, plot = this.getPlotArea())   { return plot.left + frequencyToX (frequency, plot.width, spectrumMinFrequency, this.maxFrequency); }

    /// The displayed (averaged) level at a frequency, as drawn
    getLevelAt (frequency)
    {
        const plot = this.getPlotArea();
        const column = Math.floor (frequencyToX (frequency, plot.width, spectrumMinFrequency, this.maxFrequency));
        return this.levelColumns[Math.max (0, Math.min (this.levelColumns.length - 1, column))] ?? this.analyser.floorDB;
    }

    setPointerPosition (e)
    {
        const rect = this.canvas.getBoundingClientRect();
        const plot = this.getPlotArea();
        const x = (e.clientX - rect.left) * (this.canvas.width / Math.max (1, rect.width));
        this.pointerFrequency = (x >= plot.left && x <= plot.left + plot.width) ? this.frequencyAt (x, plot) : null;
        this.redraw();
    }

    /// Moves the keyboard cursor: ←/→ by a semitone (with Shift, an octave), Page Up/Down by an
    /// octave, Home/End to the ends, P to the peak, Escape to hide it
    handleKey (e)
    {
        const minHz = spectrumMinFrequency, maxHz = this.maxFrequency;
        const current = this.cursorFrequency ?? this.peak?.frequency ?? 1000;
        const step = (semitones) => Math.max (minHz, Math.min (maxHz, (this.cursorFrequency === null ? current : current * Math.pow (2, semitones / 12))));
        let frequency;

        switch (e.key)
        {
            case "ArrowRight":  frequency = step (e.shiftKey ? 12 : 1); break;
            case "ArrowLeft":   frequency = step (e.shiftKey ? -12 : -1); break;
            case "PageUp":      frequency = step (12); break;
            case "PageDown":    frequency = step (-12); break;
            case "Home":        frequency = minHz; break;
            case "End":         frequency = maxHz; break;
            case "p": case "P": frequency = this.peak?.frequency ?? current; break;
            case "Escape":      if (this.cursorFrequency === null) return; frequency = null; break;
            default:            return;
        }

        e.preventDefault();
        e.stopPropagation();
        this.cursorFrequency = frequency;
        this.redraw();
    }

    /// Where the readout is: the pointer, else the keyboard cursor (null: neither)
    get readoutFrequency()      { return this.pointerFrequency ?? this.cursorFrequency; }

    //==============================================================================
    drawFrame (dt = 1000 / 60)
    {
        const now = performance.now();
        const analyser = this.analyser;
        const hadData = analyser.framesSinceUpdate > 0;

        if (hadData || analyser.numFramesKept > 0)
            analyser.update (Math.min (0.1, dt / 1000));

        // (until the ring is full, its frames are partly silence, so the average starts again
        // from the first full one rather than climbing slowly out of them, and the peaks don't hold
        // the leakage from the edge of that silence)
        if (! analyser.isFull)
        {
            analyser.hasAverage = false;
            analyser.resetPeaks();
        }

        this.render();
        ++this.drawCount;

        const sinceData = now - this.lastDataTime;
        const keepGoing = sinceData < dataTimeoutMs || (sinceData < maxSettlingMs && ! this.hasSettled());
        this.isAnimating = keepGoing;
        return keepGoing;
    }

    /// True once the averaged levels have caught up with the frames and the peaks have fallen to them
    hasSettled()
    {
        const { levelsDB, peaksDB, currentDB, numFramesKept } = this.analyser;

        if (numFramesKept === 0)
            return true;

        // (only what can be seen counts: a bin below the scale has settled)
        const visible = (db) => Math.max (spectrumMinDB, db);

        for (let k = 0; k < levelsDB.length; ++k)
        {
            const level = visible (levelsDB[k]);

            if (visible (peaksDB[k]) - level > 0.05 || Math.abs (level - visible (currentDB[k])) > 0.05)
                return false;
        }

        return true;
    }

    render()
    {
        const canvas = this.canvas, g = canvas.getContext ("2d");
        const w = canvas.width, h = canvas.height;

        if (! g || w < 2 || h < 2)
            return;

        const colours = getThemeColours (this.canvas);
        const plot = this.getPlotArea();
        const { left, top, width, height, dpr } = plot;
        const analyser = this.analyser;
        const binWidth = analyser.sampleRate / analyser.fftSize;
        const minHz = spectrumMinFrequency, maxHz = this.maxFrequency;
        const numColumns = Math.max (1, Math.floor (width));

        if (this.levelColumns.length !== numColumns)
        {
            this.levelColumns = new Float32Array (numColumns);
            this.peakColumns = new Float32Array (numColumns);
        }

        collapseBinsToColumns (analyser.levelsDB, binWidth, numColumns, minHz, maxHz, this.levelColumns, analyser.floorDB);
        collapseBinsToColumns (analyser.peaksDB, binWidth, numColumns, minHz, maxHz, this.peakColumns, analyser.floorDB);
        this.peak = analyser.numFramesKept > 0 ? findSpectrumPeak (analyser.levelsDB, binWidth, minHz, maxHz) : null;

        if (! this.labelFont)
        {
            const family = getComputedStyle (canvas).fontFamily || "monospace";
            this.labelFont = `${Math.round (10 * dpr)}px ${family}`;
        }

        g.clearRect (0, 0, w, h);
        g.fillStyle = colours.spectrumBg;
        g.fillRect (0, 0, w, h);

        this.drawGrid (g, plot, colours, minHz, maxHz);

        const yOf = (db) => top + dbToY (db, height);

        // the averaged level: a soft fill, then the line
        g.save();
        g.beginPath();
        g.rect (left, top, width, height);
        g.clip();

        const tracePath = new Path2D();

        for (let i = 0; i < numColumns; ++i)
        {
            const x = left + i + 0.5, y = yOf (this.levelColumns[i]);
            i === 0 ? tracePath.moveTo (x, y) : tracePath.lineTo (x, y);
        }

        const fillPath = new Path2D (tracePath);
        fillPath.lineTo (left + numColumns, top + height);
        fillPath.lineTo (left, top + height);
        fillPath.closePath();
        g.globalAlpha = 0.16;
        g.fillStyle = colours.spectrumTrace;
        g.fill (fillPath);
        g.globalAlpha = 1;

        g.lineJoin = "round";
        g.lineWidth = Math.max (1, 1.25 * dpr);
        g.strokeStyle = colours.spectrumTrace;
        g.stroke (tracePath);

        if (this.options.peakHold)
        {
            g.beginPath();

            for (let i = 0; i < numColumns; ++i)
            {
                const x = left + i + 0.5, y = yOf (this.peakColumns[i]);
                i === 0 ? g.moveTo (x, y) : g.lineTo (x, y);
            }

            g.lineWidth = Math.max (1, dpr);
            g.strokeStyle = colours.spectrumPeak;
            g.stroke();
        }

        // the readout's cursor
        const cursor = this.readoutFrequency;

        if (cursor !== null)
        {
            const x = Math.round (this.xOf (cursor, plot)) + 0.5;
            const y = yOf (this.getLevelAt (cursor));
            g.globalAlpha = 0.5;
            g.fillStyle = colours.spectrumCursor;
            g.fillRect (Math.floor (x), top, Math.max (1, Math.round (dpr)), height);
            g.globalAlpha = 1;
            g.beginPath();
            g.arc (x, y, 2.5 * dpr, 0, 2 * Math.PI);
            g.fill();
        }

        g.restore();
        this.updateText (cursor);
    }

    drawGrid (g, plot, colours, minHz, maxHz)
    {
        const { left, top, width, height, dpr } = plot;
        const line = Math.max (1, Math.round (dpr));
        const gap = 6 * dpr;

        g.font = this.labelFont;
        g.fillStyle = colours.spectrumGrid;

        // levels
        g.textAlign = "right";
        g.textBaseline = "middle";

        for (const { db, label } of getLevelGridlines (spectrumMinDB, spectrumMaxDB, height / dpr))
        {
            const y = Math.round (top + dbToY (db, height));
            g.fillStyle = colours.spectrumGrid;
            g.fillRect (left, y, width, line);

            if (label)
            {
                g.fillStyle = colours.spectrumLabel;
                g.fillText (label, left - 4 * dpr, Math.max (top + 5 * dpr, y));
            }
        }

        // frequencies: the labels are left out where they'd run into the one before
        g.textAlign = "center";
        g.textBaseline = "top";
        let lastLabelEnd = -Infinity;

        for (const { frequency, isMajor, label } of getFrequencyGridlines (minHz, maxHz))
        {
            const x = Math.round (left + frequencyToX (frequency, width, minHz, maxHz));
            g.globalAlpha = isMajor ? 1 : 0.45;
            g.fillStyle = colours.spectrumGrid;
            g.fillRect (x, top, line, height);
            g.globalAlpha = 1;

            if (label)
            {
                const half = g.measureText (label).width / 2;

                if (x - half > lastLabelEnd + gap && x + half < left + width + plotMargins.right * dpr)
                {
                    g.fillStyle = colours.spectrumLabel;
                    g.fillText (label, x, top + height + 3 * dpr);
                    lastLabelEnd = x + half;
                }
            }
        }

        // the plot's bottom edge (−100 dB)
        g.fillStyle = colours.spectrumGrid;
        g.fillRect (left, top + height - line, width, line);
    }

    /// The readout (the cursor's point, or the peak) and the accessible summary (at most every 500 ms)
    updateText (cursor)
    {
        const now = performance.now();

        // (only the keyboard cursor's readings are announced: the peak changes all the time)
        const live = this.pointerFrequency === null && this.cursorFrequency !== null ? "polite" : "off";

        if (this.readout.getAttribute ("aria-live") !== live)
            this.readout.setAttribute ("aria-live", live);

        if (cursor !== null)
        {
            setText (this.readout, formatSpectrumReadout (cursor, this.getLevelAt (cursor)));
            this.readout.classList.remove ("cp-dim");
        }
        else if (now - this.lastSummaryTime > 250 || ! this.readout.classList.contains ("cp-dim"))
        {
            const peak = this.peak && this.peak.db >= spectrumMinDB ? this.peak : null;
            setText (this.readout, peak ? `Peak ${formatSpectrumReadout (peak.frequency, peak.db)}` : "");
            this.readout.classList.add ("cp-dim");
        }

        if (now - this.lastSummaryTime > 500)
        {
            this.lastSummaryTime = now;
            const name = getEndpointDisplayName (this.endpoint);
            const summary = describeSpectrum (this.peak);
            const label = name ? summary.replace ("Spectrum", `Spectrum of ${name}`) : summary;

            if (this.canvas.getAttribute ("aria-label") !== label)
                this.canvas.setAttribute ("aria-label", label);
        }
    }
}

function setText (element, text)
{
    if (element.textContent !== text)
        element.textContent = text;
}

function getSampleRate (store)
{
    const rate = store?.get ("deviceProperties")?.rate;
    return rate > 0 ? rate : null;
}

//==============================================================================
/// The header button's icon: a little spectrum curve
function createSpectrumIcon()
{
    return el ("svg:svg", { viewBox: "0 0 14 14", width: "14", height: "14", "aria-hidden": "true", class: "cp-spectrum-icon" },
               el ("svg:path", { d: "M1 12.5 L3.5 7 L5.5 9 L8 3 L10 8.5 L13 10.5", fill: "none", stroke: "currentColor",
                                 "stroke-width": "1.4", "stroke-linejoin": "round", "stroke-linecap": "round" }));
}

/// The endpoint IDs saved under a per-patch settings key
export function getShownSpectrumEndpoints (settings, settingsKey)
{
    const ids = settings?.patch.get (settingsKey, []);
    return Array.isArray (ids) ? ids.filter (id => typeof id === "string") : [];
}

/// Remembers whether an endpoint's spectrum is shown, under a per-patch settings key
export function setSpectrumEndpointShown (settings, settingsKey, endpointID, isShown)
{
    const ids = getShownSpectrumEndpoints (settings, settingsKey).filter (id => id !== endpointID);
    settings?.patch.set (settingsKey, isShown ? [...ids, endpointID] : ids);
}

/*
    Gives a full-size audio widget a "Spectrum" toggle in its header (an icon button with
    aria-pressed), which shows a SpectrumAnalyserView under the widget's meter. Whether it's open
    is remembered per patch, in the list of endpoint IDs under settingsKey; onChange (isShown) is
    called after it opens or closes. Returns
    { button, isShown, setShown (isShown), get view() }, and puts it on widget.spectrumToggle.
    The analyser is disposed with the widget.
*/
export function addSpectrumToggle (widget, settingsKey, { onChange } = {})
{
    const { endpoint, options } = widget;
    const settings = options.settings ?? null;
    const button = el ("button", { class: ["cp-icon-button", "cp-spectrum-toggle"], type: "button", "aria-pressed": "false",
                                   "aria-label": "Spectrum", title: "Show the spectrum" }, createSpectrumIcon());

    let view = null;

    const toggle = {
        button,
        get view()      { return view; },
        get isShown()   { return view !== null; },

        setShown (isShown, { remember = true } = {})
        {
            if (!! isShown === (view !== null))
                return;

            if (isShown)
            {
                view = new SpectrumAnalyserView ({ endpoint, group: widget.group, scheduler: widget.scheduler,
                                                   store: options.store ?? null, settings });
                const meter = widget.display?.element;

                if (meter?.parentNode === widget.bodyElement)
                    meter.after (view.element);
                else
                    widget.bodyElement.append (view.element);
            }
            else
            {
                view.dispose();
                view = null;
            }

            setPressed (button, isShown);
            button.title = isShown ? "Hide the spectrum" : "Show the spectrum";
            widget.element.classList.toggle ("cp-ew-has-spectrum", !! isShown);

            if (remember)
                setSpectrumEndpointShown (settings, settingsKey, endpoint.endpointID, !! isShown);

            onChange?.(!! isShown);
        },
    };

    button.addEventListener ("click", () => toggle.setShown (! toggle.isShown));

    // (the value and the button share the header's right-hand end)
    const end = el ("span", { class: "cp-ew-end" });
    widget.valueElement.replaceWith (end);
    end.append (widget.valueElement, button);

    widget.disposer.add (() => { view?.dispose(); view = null; });
    widget.spectrumToggle = toggle;

    if (getShownSpectrumEndpoints (settings, settingsKey).includes (endpoint.endpointID))
        toggle.setShown (true, { remember: false });

    return toggle;
}

/// True for an endpoint that can have a spectrum (an audio input or output)
export function isSpectrumEndpoint (endpoint)
{
    return endpoint?.purpose === "audio in" || endpoint?.purpose === "audio out";
}

/*
    The palette's spectrum commands: "Show spectrum for <name>", one per audio endpoint.
    getAudioEndpoints(): the patch's audio inputs and outputs (none without a patch);
    showSpectrum (endpointID): shows it (the panel opens it in the I/O view).
*/
export function createSpectrumCommandProvider ({ getAudioEndpoints, showSpectrum })
{
    return () => getAudioEndpoints().filter (isSpectrumEndpoint).map (endpoint => ({
        id: `spectrum:${endpoint.endpointID}`,
        title: `Show spectrum for ${getEndpointDisplayName (endpoint)}`,
        detail: endpoint.endpointID,
        category: "Audio",
        keywords: ["spectrum", "analyser", "analyzer", "FFT", "frequency"],
        run: () => showSpectrum (endpoint.endpointID),
    }));
}
