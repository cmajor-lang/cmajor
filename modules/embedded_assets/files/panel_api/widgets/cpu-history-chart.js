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

import { el, Disposer } from "../core/dom.js";
import { getThemeColours, addThemeChangeListener } from "../core/theme.js";
import { cpuOverBudgetLevel } from "../core/cpu-history.js";
import { observeCanvasSize } from "./widget-base.js";

/// The seconds shown by the top bar's sparkline, and by the popover's chart
export const sparklineSeconds = 30;
export const chartSeconds = 60;

/// (the server reports a steady level only every few seconds, so a line is carried on to "now"
/// for up to this long after its last sample; beyond that, a missing report counts as a gap)
const maxSampleSpacingMs = 6000;

/*
    A CPU history (core/cpu-history.js) drawn on a canvas, from the frame scheduler. It's redrawn
    only when a sample arrives, the canvas resizes or the theme changes: nothing animates.

    - The smoothed level is a line over a faint area, in --cp-cpu-trace.
    - Where a report's peak (its slowest block) is above its level, a tick runs up to it in --cp-cpu-peak.
    - A report with a block over budget (a peak of 100% or more) gets a mark along the top in --cp-cpu-over.
    - fixedScale: the y axis always runs to at least the budget (100%), with a dashed budget line, and
      grows when the peaks go over it. Otherwise (the sparkline) it fits the data, so that a light
      load still shows.
*/
export class CPUHistoryChart
{
    constructor (history, { scheduler, seconds = chartSeconds, fixedScale = false, className = "cp-cpu-chart-canvas", now = () => performance.now() })
    {
        this.history = history;
        this.scheduler = scheduler;
        this.windowMs = seconds * 1000;
        this.fixedScale = fixedScale;
        this.now = now;
        this.yMax = 1;
        this.canvas = el ("canvas", { class: className, "aria-hidden": "true" });
        this.disposer = new Disposer();

        this.draw = () => { this.drawNow(); return false; };
        this.redraw = () => this.scheduler.schedule (this.draw);
        this.disposer.add (observeCanvasSize (this.canvas, this.redraw));
        this.disposer.add (addThemeChangeListener (this.redraw));
        this.disposer.add (() => this.scheduler.cancel (this.draw));
        this.setActive (true);
    }

    dispose()
    {
        this.setActive (false);
        this.disposer.dispose();
    }

    /// While inactive (e.g. hidden), new samples don't schedule a redraw
    setActive (isActive)
    {
        if (isActive === !! this.stopListening)
            return;

        if (isActive)
        {
            this.stopListening = this.history.addListener (this.redraw);
            this.redraw();
        }
        else
        {
            this.stopListening();
            this.stopListening = null;
        }
    }

    /// The scale's top, as a load (1 = the budget), for what's in the window
    getScaleTop (maxValue)
    {
        if (this.fixedScale)
            return Math.min (8, Math.max (1.25, niceCeiling (maxValue * 1.1)));

        return Math.min (8, Math.max (0.1, niceCeiling (maxValue * 1.15)));
    }

    drawNow()
    {
        const canvas = this.canvas;
        const g = canvas.getContext ("2d");

        if (! g || ! canvas.isConnected)
            return;

        const w = canvas.width, h = canvas.height;
        const dpr = globalThis.devicePixelRatio || 1;
        const colours = getThemeColours (canvas);
        const now = this.now();
        const start = now - this.windowMs;

        // the samples in view, and the one before it, so the line comes in from the left edge
        const times = [], levels = [], peaks = [];
        let maxLevel = 0, maxPeak = 0;

        this.history.forEach ((t, level, peak) =>
        {
            times.push (t); levels.push (level); peaks.push (peak);

            if (t >= start && ! Number.isNaN (level))
            {
                maxLevel = Math.max (maxLevel, level);
                maxPeak = Math.max (maxPeak, peak);
            }
        }, start - maxSampleSpacingMs);

        // (the sparkline fits the level, with some room for the peaks, which may go off the top)
        const yMax = this.yMax = this.getScaleTop (this.fixedScale ? Math.max (maxLevel, maxPeak) : maxLevel * 1.5);
        const x = (t) => (t - start) / this.windowMs * w;
        const y = (v) => h - Math.min (1, v / yMax) * h;

        g.clearRect (0, 0, w, h);
        g.fillStyle = colours.meterTrack;
        g.fillRect (0, 0, w, h);

        // gridlines at quarters of the budget (halves, on a taller scale), and the budget itself
        if (this.fixedScale)
        {
            g.fillStyle = colours.divider;
            const gridStep = yMax > 1.5 ? 0.5 : 0.25;

            for (let v = gridStep; v < yMax; v += gridStep)
                if (Math.abs (v - 1) > 0.01)
                    g.fillRect (0, Math.round (y (v)), w, Math.max (1, Math.round (dpr)));

            g.save();
            g.strokeStyle = colours.cpuOver;
            g.globalAlpha = 0.7;
            g.lineWidth = Math.max (1, Math.round (dpr));
            g.setLineDash ([4 * dpr, 3 * dpr]);
            g.beginPath();
            const budgetY = Math.round (y (1)) + 0.5;
            g.moveTo (0, budgetY);
            g.lineTo (w, budgetY);
            g.stroke();
            g.restore();
        }

        // the level: runs of samples, broken at gaps, carried on to "now" if the last one is recent
        const runs = [];
        let run = null;

        for (let i = 0; i < times.length; ++i)
        {
            if (Number.isNaN (levels[i]))
            {
                run = null;
                continue;
            }

            if (run && times[i] - run[run.length - 1].t > maxSampleSpacingMs * 2)
                run = null;

            if (! run)
                runs.push (run = []);

            run.push ({ t: times[i], v: levels[i] });
        }

        const last = run?.[run.length - 1];

        if (last && now - last.t < maxSampleSpacingMs)
            run.push ({ t: now, v: last.v });

        g.lineWidth = Math.max (1, (this.fixedScale ? 1.5 : 1) * dpr);
        g.lineJoin = "round";

        for (const r of runs)
        {
            g.beginPath();
            g.moveTo (x (r[0].t), h);

            for (const p of r)
                g.lineTo (x (p.t), y (p.v));

            g.lineTo (x (r[r.length - 1].t), h);
            g.closePath();
            g.globalAlpha = this.fixedScale ? 0.18 : 0.3;
            g.fillStyle = colours.cpuTrace;
            g.fill();

            g.beginPath();

            for (const [i, p] of r.entries())
                i === 0 ? g.moveTo (x (p.t), y (p.v)) : g.lineTo (x (p.t), y (p.v));

            g.globalAlpha = 1;
            g.strokeStyle = colours.cpuTrace;
            g.stroke();
        }

        // the peaks: a mark at each one (with a faint stem down to the level, on the full chart),
        // and the over-budget marks along the top
        const tick = Math.max (1, Math.round (dpr));
        const overHeight = Math.max (2, Math.round ((this.fixedScale ? 4 : 2) * dpr));
        g.fillStyle = colours.cpuPeak;

        for (let i = 0; i < times.length; ++i)
        {
            if (Number.isNaN (levels[i]) || times[i] < start || ! (peaks[i] > levels[i] + yMax * 0.02))
                continue;

            const px = Math.round (x (times[i])), top = Math.round (y (peaks[i]));

            if (this.fixedScale)
            {
                g.globalAlpha = 0.3;
                g.fillRect (px, top, tick, Math.max (0, y (levels[i]) - top));
            }

            g.globalAlpha = 1;
            g.fillRect (px - tick, Math.max (0, top - (tick >> 1)), tick * 3, tick);
        }

        g.fillStyle = colours.cpuOver;

        for (let i = 0; i < times.length; ++i)
            if (times[i] >= start && peaks[i] >= cpuOverBudgetLevel)
                g.fillRect (Math.round (x (times[i])) - tick, 0, tick * 3, overHeight);
    }
}

/// The smallest of 1, 1.25, 2, 2.5 or 5 times a power of ten that's at least v
function niceCeiling (v)
{
    if (! (v > 0))
        return 0;

    const p = Math.pow (10, Math.floor (Math.log10 (v)));

    for (const m of [1, 1.25, 2, 2.5, 5, 10])
        if (m * p >= v * 0.9999)
            return m * p;

    return 10 * p;
}

//==============================================================================
/// The top bar's sparkline: the last sparklineSeconds, scaled to fit
export class CPUSparkline extends CPUHistoryChart
{
    constructor (history, { scheduler, now })
    {
        super (history, { scheduler, now, seconds: sparklineSeconds, className: "cp-cpu-spark" });
        this.element = this.canvas;
    }
}

//==============================================================================
/*
    The CPU history popover's content: a chart of the last chartSeconds (with a time axis and the
    budget line), the stats over that time, and the time budget per block from the audio device's
    properties (the store's deviceProperties: { rate, blockSize }).
*/
export class CPUHistoryView
{
    constructor ({ store, scheduler, now = () => performance.now() }, history)
    {
        this.store = store;
        this.history = history;
        this.now = now;
        this.disposer = new Disposer();

        this.chart = new CPUHistoryChart (history, { scheduler, now, fixedScale: true });
        this.chart.setActive (false);
        this.disposer.add (() => this.chart.dispose());

        const axisLabel = (text, position) => el ("span", { class: "cp-cpu-axis-label", style: `left: ${position}%`, dataset: { edge: position === 0 ? "start" : (position === 100 ? "end" : "") } }, text);
        const step = chartSeconds / 4;

        this.budgetLabel = el ("span", { class: "cp-cpu-budget-label" }, "100%");
        this.scaleLabel = el ("span", { class: "cp-cpu-scale-label" });
        this.emptyLabel = el ("p", { class: "cp-empty cp-cpu-chart-empty" }, "No CPU reports yet: start the audio");

        this.chartElement = el ("div", { class: "cp-cpu-chart", role: "img" },
                                this.chart.canvas, this.budgetLabel, this.scaleLabel, this.emptyLabel,
                                el ("div", { class: "cp-cpu-axis", "aria-hidden": "true" },
                                    ...[0, 1, 2, 3].map (i => axisLabel (`−${chartSeconds - i * step}s`, i * 25)), axisLabel ("now", 100)));

        const stat = (label, title) =>
        {
            const value = el ("dd", { class: "cp-cpu-stat-value" }, "–");
            const element = el ("div", { class: "cp-cpu-stat", title }, el ("dt", null, label), value);
            return { value, element };
        };

        this.stats = {
            current:    stat ("Current", "The latest (smoothed) load: the time spent processing, as a proportion of the time the audio allows"),
            average:    stat ("Average", `The mean of the smoothed load over the last ${chartSeconds} seconds`),
            peak:       stat ("Peak", `The slowest single block in the last ${chartSeconds} seconds, as a proportion of its time budget`),
            overBudget: stat ("Over budget", `How many CPU reports in the last ${chartSeconds} seconds included a block that took longer than its time budget (a likely dropout)`),
        };

        this.budgetText = el ("span", { class: "cp-cpu-budget-value" }, "–");
        this.clearButton = el ("button", { class: "cp-small-button", title: "Forget the history so far", onclick: () => history.clear() }, "Clear");

        this.element = el ("div", { class: "cp-cpu-history" },
                           el ("h3", { class: "cp-section-title" }, "CPU history", el ("span", { class: "cp-count" }, `last ${chartSeconds} s`)),
                           this.chartElement,
                           el ("dl", { class: "cp-cpu-stats" }, ...Object.values (this.stats).map (s => s.element)),
                           el ("div", { class: "cp-cpu-footer" },
                               el ("span", { class: "cp-dim" }, "Budget per block"), this.budgetText,
                               el ("span", { class: "cp-spacer" }), this.clearButton));

        this.update = () => this.updateStats();
        this.disposer.add (store.subscribe (["deviceProperties"], () => this.updateBudget(), true));
    }

    dispose()
    {
        this.setActive (false);
        this.disposer.dispose();
        this.element.remove();
    }

    /// Call when it's shown (to start following the history) and hidden
    setActive (isActive)
    {
        if (isActive === !! this.stopListening)
            return;

        this.chart.setActive (isActive);

        if (isActive)
        {
            this.stopListening = this.history.addListener (this.update);
            this.updateBudget();
            this.updateStats();
        }
        else
        {
            this.stopListening();
            this.stopListening = null;
        }
    }

    updateStats()
    {
        const s = this.history.getStats (chartSeconds, this.now());
        const set = (stat, text) => { if (stat.value.textContent !== text) stat.value.textContent = text; };

        set (this.stats.current, formatLoad (s.current));
        set (this.stats.average, formatLoad (s.average));
        set (this.stats.peak, formatLoad (s.max));
        set (this.stats.overBudget, s.count ? String (s.overBudget) : "–");
        this.stats.overBudget.element.classList.toggle ("cp-over", s.overBudget > 0);
        this.stats.peak.element.classList.toggle ("cp-over", (s.max ?? 0) >= cpuOverBudgetLevel);

        this.emptyLabel.hidden = s.count > 0;

        // (the chart works out its scale as it draws, so place the labels by the scale it will use)
        const yMax = this.chart.getScaleTop (s.max ?? 0);
        this.budgetLabel.style.top = `${(1 - 1 / yMax) * 100}%`;
        this.scaleLabel.textContent = formatLoad (yMax);
        this.scaleLabel.hidden = yMax <= 1.25;     // (the budget line is enough, and the labels would touch)

        this.chartElement.setAttribute ("aria-label", s.count
            ? `CPU over the last ${chartSeconds} seconds: average ${formatLoad (s.average)}, peak ${formatLoad (s.max)}, ${s.overBudget} over budget`
            : "No CPU reports yet");
    }

    updateBudget()
    {
        const text = describeBlockBudget (this.store.get ("deviceProperties"));
        this.budgetText.textContent = text || "–";
    }
}

/// e.g. "2.67 ms (128 frames at 48 kHz)", or "" without both the rate and block size
export function describeBlockBudget (deviceProperties)
{
    const rate = Number (deviceProperties?.rate), blockSize = Number (deviceProperties?.blockSize);

    if (! (rate > 0 && blockSize > 0))
        return "";

    const ms = blockSize / rate * 1000;
    const kHz = rate / 1000;
    return `${ms.toFixed (ms < 10 ? 2 : 1)} ms (${blockSize} frames at ${kHz.toFixed (rate % 1000 ? 1 : 0)} kHz)`;
}

/// A load (1 = the budget) as a percentage, or "–"
export function formatLoad (load)
{
    if (load === null || load === undefined || Number.isNaN (load))
        return "–";

    const percent = load * 100;
    return percent.toFixed (percent < 10 ? 1 : 0) + "%";
}
