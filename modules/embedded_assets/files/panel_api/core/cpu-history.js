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

/// How much CPU history is kept, in seconds
export const cpuHistorySeconds = 120;

/// A load at or above this means a block took longer than its time budget
export const cpuOverBudgetLevel = 1.0;

/*
    The recent history of the session's cpu_info reports: a ring of { time, level, peak }
    samples covering the last cpuHistorySeconds.

    - level is the server's smoothed (EMA) load, and peak the highest unsmoothed per-block load
      since its previous report. An older server sends no peak, so the level stands in for it.
    - The server only reports when the level changes (or every few seconds when it's steady), so
      the samples aren't evenly spaced: every one has its own time (in ms, performance.now()).
    - addGap() marks a break (audio stopped, the patch changed), so a chart doesn't join a line
      across it. A gap's level and peak are NaN, and the stats skip it.

        const history = new CPUHistory();
        const detach = history.attach (session);
        const stopListening = history.addListener (() => redraw());
*/
export class CPUHistory
{
    constructor ({ seconds = cpuHistorySeconds, capacity = 8192, now = () => performance.now() } = {})
    {
        this.durationMs = seconds * 1000;
        this.capacity = capacity;
        this.now = now;
        this.times = new Float64Array (capacity);
        this.levels = new Float32Array (capacity);
        this.peaks = new Float32Array (capacity);
        this.start = 0;     // the index of the oldest sample
        this.size = 0;
        this.listeners = new Set();
        this.handleCPUInfo = (info) => this.add (info?.level, info?.peak);
    }

    /// Listens to a session's cpu_info messages. Returns a function that stops it.
    attach (session)
    {
        session.addCPUListener (this.handleCPUInfo);
        return () => session.removeCPUListener (this.handleCPUInfo);
    }

    /// Calls fn (history) after each new sample (or gap, or clear). Returns a function that removes it.
    addListener (fn)
    {
        this.listeners.add (fn);
        return () => this.listeners.delete (fn);
    }

    /// Adds a report. A missing peak (from an older server) is taken to be the level.
    add (level, peak, time = this.now())
    {
        level = Number (level);

        if (! Number.isFinite (level))
            return;

        peak = Number (peak);
        this.push (time, Math.max (0, level), Math.max (0, Number.isFinite (peak) ? peak : level));
    }

    /// Marks a break in the history (unless there's one already)
    addGap (time = this.now())
    {
        if (this.size > 0 && ! Number.isNaN (this.levels[this.indexOf (this.size - 1)]))
            this.push (time, NaN, NaN);
    }

    clear()
    {
        this.start = 0;
        this.size = 0;
        this.notify();
    }

    get length()            { return this.size; }

    /// The i'th sample (0 = the oldest), as { time, level, peak }
    get (i)
    {
        const n = this.indexOf (i);
        return { time: this.times[n], level: this.levels[n], peak: this.peaks[n] };
    }

    /// The most recent sample that isn't a gap, or undefined
    get latest()
    {
        for (let i = this.size - 1; i >= 0; --i)
            if (! Number.isNaN (this.levels[this.indexOf (i)]))
                return this.get (i);

        return undefined;
    }

    /// Calls fn (time, level, peak) for each sample (gaps included) from sinceTime onwards, oldest first
    forEach (fn, sinceTime = -Infinity)
    {
        for (let i = this.findFirstIndexSince (sinceTime); i < this.size; ++i)
        {
            const n = this.indexOf (i);
            fn (this.times[n], this.levels[n], this.peaks[n]);
        }
    }

    /// The stats over the last windowSeconds: { current, average, max, overBudget, count }.
    /// current is the latest level (null if there's none); average is the mean of the levels
    /// reported; max is the highest peak; overBudget counts the reports whose peak was at or
    /// over the budget (i.e. at least one block took longer than it had).
    getStats (windowSeconds = 60, now = this.now())
    {
        let sum = 0, count = 0, max = 0, overBudget = 0;

        this.forEach ((_time, level, peak) =>
        {
            if (Number.isNaN (level))
                return;

            sum += level;
            ++count;
            max = Math.max (max, peak, level);

            if (peak >= cpuOverBudgetLevel)
                ++overBudget;
        }, now - windowSeconds * 1000);

        return { current: this.latest?.level ?? null, average: count ? sum / count : null,
                 max: count ? max : null, overBudget, count };
    }

    //==============================================================================
    /** @private */
    indexOf (i)             { return (this.start + i) % this.capacity; }

    /** @private */
    findFirstIndexSince (time)
    {
        let lo = 0, hi = this.size;

        while (lo < hi)
        {
            const mid = (lo + hi) >> 1;

            if (this.times[this.indexOf (mid)] < time)
                lo = mid + 1;
            else
                hi = mid;
        }

        return lo;
    }

    /** @private */
    push (time, level, peak)
    {
        // (a clock that went backwards would break the binary search, so keep the times in order)
        if (this.size > 0)
            time = Math.max (time, this.times[this.indexOf (this.size - 1)]);

        if (this.size === this.capacity)
        {
            this.start = (this.start + 1) % this.capacity;
            --this.size;
        }

        const n = this.indexOf (this.size++);
        this.times[n] = time;
        this.levels[n] = level;
        this.peaks[n] = peak;

        // drop anything older than the history's length (and a gap left at the start)
        const oldest = time - this.durationMs;

        while (this.size > 0 && (this.times[this.start] < oldest || (this.size > 1 && Number.isNaN (this.levels[this.start]))))
        {
            this.start = (this.start + 1) % this.capacity;
            --this.size;
        }

        this.notify();
    }

    /** @private */
    notify()
    {
        for (const fn of [...this.listeners])
            fn (this);
    }
}
