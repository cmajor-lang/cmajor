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

import { el } from "../core/dom.js";
import { getThemeColours } from "../core/theme.js";
import { EndpointWidget, observeCanvasSize, gainToDB, formatDB } from "./widget-base.js";

/// The number of frames per level update requested from the server (about 110 per second)
export const audioGranularity = 400;

const minDB = -60, maxDB = 6;
const peakHoldMs = 1500;

/// How fast the bars fall, and the peak markers once their hold time is over (in dB per second,
/// whatever the frame rate). The bars fall at about a peak programme meter's rate: much faster,
/// and they dip between one level update and the next (each is the peak of about 9 ms), so a
/// steady signal makes them flicker.
export const levelReleaseDBPerSecond = 20;
export const peakReleaseDBPerSecond = 16;

/// Anything below this counts as silence, so a meter that's had no data for a while goes idle
export const silenceDB = -80;
const silenceGain = Math.pow (10, silenceDB / 20);

/*
    A level meter (one bar per channel, with peak hold and a clip light) and a scope (a scrolling
    history of each channel's min/max), drawn on canvases at the screen's pixel ratio, from
    the shared frame scheduler. Used by both the audio output and audio input widgets. The
    compact one (for the graph's live mode) is just the meter strip.

    Data arriving only ever raises the levels and peaks; they fall in drawFrame(), at a rate that
    doesn't depend on how often frames or data arrive, and stop at silence (silenceDB), so that
    the frame scheduler can go idle.
*/
export class LevelDisplay
{
    constructor (numChannels, { compact = false, scheduler, themeElement, onLevelText })
    {
        this.numChannels = Math.max (1, numChannels | 0);
        this.compact = compact;
        this.scheduler = scheduler;
        this.themeElement = themeElement;
        this.onLevelText = onLevelText;

        this.levels = new Float32Array (this.numChannels);     // current (decaying) level, linear
        this.peaks = new Float32Array (this.numChannels);      // held peak, linear
        this.peakTimes = new Float64Array (this.numChannels);
        this.isClipping = false;

        this.historySize = compact ? 80 : 240;
        this.historyMin = Array.from ({ length: this.numChannels }, () => new Float32Array (this.historySize));
        this.historyMax = Array.from ({ length: this.numChannels }, () => new Float32Array (this.historySize));
        this.historyPos = 0;
        this.lastDataTime = 0;
        this.ariaValue = null;

        this.meterCanvas = el ("canvas", { class: "cp-meter-canvas", role: "meter", "aria-valuemin": minDB, "aria-valuemax": maxDB,
                                           "aria-label": "Level" });
        this.scopeCanvas = el ("canvas", { class: "cp-scope-canvas", "aria-hidden": "true" });
        this.clipElement = el ("button", { class: "cp-clip", title: "Clipped (click to reset)", "aria-label": "Clip indicator",
                                           onclick: () => this.resetClip() });

        this.element = el ("div", { class: "cp-level-display" }, this.meterCanvas, this.clipElement, compact ? null : this.scopeCanvas);

        this.draw = (_time, dt) => this.drawFrame (dt);
        this.redraw = () => this.scheduler.schedule (this.draw);
        this.stopObservingMeter = observeCanvasSize (this.meterCanvas, this.redraw);
        this.stopObservingScope = compact ? () => {} : observeCanvasSize (this.scopeCanvas, this.redraw);

        this.meterCanvas.addEventListener ("pointermove", () =>
            this.meterCanvas.title = [...this.peaks].map ((p, i) => `${this.numChannels > 1 ? `ch ${i + 1}: ` : ""}peak ${formatDB (gainToDB (p))}`).join ("\n"));
    }

    dispose()
    {
        this.stopObservingMeter();
        this.stopObservingScope();
        this.scheduler.cancel (this.draw);
    }

    /// Takes a { min: [..], max: [..] } message from an audio endpoint listener
    addMinMax (data)
    {
        const now = performance.now();
        this.lastDataTime = now;

        for (let c = 0; c < this.numChannels; ++c)
        {
            const lo = data.min?.[c] ?? 0, hi = data.max?.[c] ?? 0;
            const level = Math.max (Math.abs (lo), Math.abs (hi));

            if (level > this.levels[c])
                this.levels[c] = level;

            // (a new peak restarts the hold; otherwise drawFrame() lets it fall)
            if (level >= this.peaks[c])
            {
                this.peaks[c] = level;
                this.peakTimes[c] = now;
            }

            if (level >= 1.0)
                this.isClipping = true;

            this.historyMin[c][this.historyPos] = lo;
            this.historyMax[c][this.historyPos] = hi;
        }

        this.historyPos = (this.historyPos + 1) % this.historySize;
        this.scheduler.schedule (this.draw);
    }

    resetClip()
    {
        this.isClipping = false;
        this.scheduler.schedule (this.draw);
    }

    clear()
    {
        this.levels.fill (0);
        this.peaks.fill (0);
        this.peakTimes.fill (0);
        this.isClipping = false;
        this.historyMin.forEach (h => h.fill (0));
        this.historyMax.forEach (h => h.fill (0));
        this.scheduler.schedule (this.draw);
    }

    /// Lets the levels and peaks fall by dt milliseconds' worth. Returns true while anything is
    /// still above silence (and so still moving).
    release (dt, now = performance.now())
    {
        const levelFactor = Math.pow (10, -levelReleaseDBPerSecond * dt / 20000);
        const peakFactor  = Math.pow (10, -peakReleaseDBPerSecond * dt / 20000);
        let stillMoving = false;

        for (let c = 0; c < this.numChannels; ++c)
        {
            this.levels[c] *= levelFactor;

            if (this.levels[c] < silenceGain)
                this.levels[c] = 0;
            else
                stillMoving = true;

            if (this.peaks[c] > this.levels[c] && now - this.peakTimes[c] > peakHoldMs)
            {
                this.peaks[c] = Math.max (this.levels[c], this.peaks[c] * peakFactor);

                if (this.peaks[c] < silenceGain)
                    this.peaks[c] = this.levels[c];
            }

            // (a held peak is waiting to fall)
            if (this.peaks[c] > this.levels[c])
                stillMoving = true;
        }

        return stillMoving;
    }

    drawFrame (dt = 1000 / 60)
    {
        const now = performance.now();
        const colours = getThemeColours (this.themeElement);
        const stillMoving = this.release (dt, now);

        this.drawMeter (colours);

        if (! this.compact)
            this.drawScope (colours);

        this.clipElement.classList.toggle ("cp-on", this.isClipping);

        let loudest = 0;

        for (let c = 0; c < this.numChannels; ++c)
            loudest = Math.max (loudest, this.peaks[c]);

        const loudestDB = gainToDB (loudest);
        const ariaValue = Math.max (minDB, Math.min (maxDB, loudestDB)).toFixed (1);

        if (ariaValue !== this.ariaValue)
        {
            this.ariaValue = ariaValue;
            this.meterCanvas.setAttribute ("aria-valuenow", ariaValue);
        }

        this.onLevelText?.(formatDB (loudestDB));

        // keep going while data is arriving or the meter is still falling
        return stillMoving || now - this.lastDataTime < 200;
    }

    drawMeter (colours)
    {
        const canvas = this.meterCanvas, g = canvas.getContext ("2d");
        const w = canvas.width, h = canvas.height;
        const dpr = globalThis.devicePixelRatio || 1;
        const n = this.numChannels;
        const gap = Math.round (2 * dpr);
        const barH = Math.max (1, Math.floor ((h - gap * (n - 1)) / n));
        const toX = (gain) => Math.max (0, Math.min (1, (gainToDB (gain) - minDB) / (maxDB - minDB))) * w;
        const zeroX = toX (1);

        g.clearRect (0, 0, w, h);

        for (let c = 0; c < n; ++c)
        {
            const y = c * (barH + gap);

            g.fillStyle = colours.meterTrack;
            g.fillRect (0, y, w, barH);

            const x = toX (this.levels[c]);
            const warnX = toX (0.5);     // -6 dB

            g.fillStyle = colours.meterLow;
            g.fillRect (0, y, Math.min (x, warnX), barH);

            if (x > warnX)
            {
                g.fillStyle = colours.meterHigh;
                g.fillRect (warnX, y, Math.min (x, zeroX) - warnX, barH);
            }

            if (x > zeroX)
            {
                g.fillStyle = colours.meterClip;
                g.fillRect (zeroX, y, x - zeroX, barH);
            }

            const px = toX (this.peaks[c]);

            if (px > 0)
            {
                g.fillStyle = this.peaks[c] >= 1 ? colours.meterClip : colours.meterPeak;
                g.fillRect (Math.max (0, px - dpr), y, Math.max (1, 1.5 * dpr), barH);
            }
        }

        // the 0 dB mark
        g.fillStyle = colours.fgMuted;
        g.fillRect (zeroX, 0, Math.max (1, dpr * 0.75), h);
    }

    drawScope (colours)
    {
        const canvas = this.scopeCanvas, g = canvas.getContext ("2d");
        const w = canvas.width, h = canvas.height;
        const lanes = getScopeLanes (this.numChannels, this.compact);
        const laneH = h / lanes.length;
        const size = this.historySize;
        const colW = w / size;

        g.clearRect (0, 0, w, h);
        g.fillStyle = colours.meterTrack;
        g.fillRect (0, 0, w, h);

        for (let lane = 0; lane < lanes.length; ++lane)
        {
            const mid = laneH * (lane + 0.5);
            const { firstChannel, endChannel } = lanes[lane];

            g.fillStyle = colours.divider;
            g.fillRect (0, Math.floor (mid), w, 1);
            g.fillStyle = colours.scopeTrace;

            for (let i = 0; i < size; ++i)
            {
                const index = (this.historyPos + i) % size;
                let lo = 0, hi = 0;

                for (let c = firstChannel; c < endChannel; ++c)
                {
                    lo = Math.min (lo, this.historyMin[c][index]);
                    hi = Math.max (hi, this.historyMax[c][index]);
                }

                const top = mid - Math.min (1, hi) * laneH * 0.48;
                const bottom = mid - Math.max (-1, lo) * laneH * 0.48;
                g.fillRect (i * colW, top, Math.max (1, colW - 0.5), Math.max (1, bottom - top));
            }
        }
    }
}

/// The scope's lanes: at most two, with the channels split between them (with 2 channels, one
/// each; with 5, three and two), each lane showing the extremes of its channels. Returns an
/// array of { firstChannel, endChannel } (end exclusive).
export function getScopeLanes (numChannels, compact = false)
{
    const n = Math.max (1, numChannels | 0);
    const numLanes = compact ? 1 : Math.min (n, 2);

    return Array.from ({ length: numLanes }, (_, lane) => ({ firstChannel: Math.ceil (lane * n / numLanes),
                                                             endChannel:   Math.ceil ((lane + 1) * n / numLanes) }));
}

//==============================================================================
/// Gives an audio widget a LevelDisplay (widget.display) in its body, fed with its endpoint's
/// levels, and disposed with it. formatLevel (text) turns the loudest level into the text that
/// the widget's header shows.
export function addLevelDisplay (widget, formatLevel = (text) => text)
{
    widget.display = new LevelDisplay (widget.endpoint.numAudioChannels ?? 1, {
        compact: widget.isCompact, scheduler: widget.scheduler, themeElement: widget.element,
        onLevelText: (text) => widget.setValueText (formatLevel (text))
    });

    widget.setValueText (formatLevel ("-∞ dB"));
    widget.bodyElement.append (widget.display.element);
    widget.disposer.add (() => widget.display.dispose());
    widget.subscribe (data => widget.display.addMinMax (data), { granularity: audioGranularity });
    return widget.display;
}

/// An audio output: level meter + scope
export class AudioOutputWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "audio-out");
        addLevelDisplay (this);
    }
}
