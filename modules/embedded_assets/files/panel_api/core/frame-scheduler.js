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

/*
    One requestAnimationFrame loop for everything in the panel that animates (meters, scopes,
    activity glows), so that N meters cost one callback per frame rather than N timers.

    A widget calls schedule (drawFunction) whenever it has something new to draw. Each function
    runs at most once per frame, however many times it was scheduled. If it returns true, it
    is run again on the next frame (e.g. while a peak-hold is decaying). When nothing is
    scheduled, the loop stops completely.

    Each function is called with (time, dt): the frame's time, and the milliseconds since the
    previous frame, so that anything that decays can do so at the same rate whatever the
    display's frame rate. dt is at most maxFrameInterval (a frame after a stall, or in a
    throttled background tab, doesn't make everything jump), and the first frame after the
    loop has been idle counts as one 60 Hz frame.

        const unschedule = frameScheduler.schedule (this.draw);
*/
export class FrameScheduler
{
    constructor (requestFrame = (fn) => globalThis.requestAnimationFrame (fn),
                 cancelFrame = (id) => globalThis.cancelAnimationFrame (id))
    {
        this.requestFrame = requestFrame;
        this.cancelFrame = cancelFrame;
        this.pending = new Set();
        this.spare = null;
        this.frameID = null;
        this.lastFrameTime = null;      // (null while it's idle)
        this.runFrame = (time) => this.handleFrame (time);
    }

    static get maxFrameInterval()       { return 100; }
    static get idleFrameInterval()      { return 1000 / 60; }

    /// Asks for fn (time, dt) to be called on the next frame. Returns a function that cancels it.
    schedule (fn)
    {
        this.pending.add (fn);

        if (this.frameID === null)
            this.frameID = this.requestFrame (this.runFrame);

        return () => this.cancel (fn);
    }

    cancel (fn)
    {
        this.pending.delete (fn);

        if (this.pending.size === 0 && this.frameID !== null)
        {
            this.cancelFrame (this.frameID);
            this.frameID = null;
            this.lastFrameTime = null;
        }
    }

    get isRunning()             { return this.frameID !== null; }

    handleFrame (time)
    {
        this.frameID = null;
        const dt = this.lastFrameTime === null ? FrameScheduler.idleFrameInterval
                                               : Math.max (0, Math.min (FrameScheduler.maxFrameInterval, time - this.lastFrameTime));
        this.lastFrameTime = time;

        // (the two sets swap, rather than making a new one each frame)
        const toRun = this.pending;
        this.pending = this.spare ?? new Set();
        this.spare = null;

        for (const fn of toRun)
        {
            let again = false;

            try { again = fn (time, dt) === true; }
            catch (e) { console.error (e); }

            if (again)
                this.pending.add (fn);
        }

        toRun.clear();
        this.spare = toRun;

        if (this.pending.size > 0)
        {
            if (this.frameID === null)
                this.frameID = this.requestFrame (this.runFrame);
        }
        else if (this.frameID === null)
        {
            this.lastFrameTime = null;  // (idle)
        }
    }
}

/// The scheduler that all of the panel's widgets share
export const frameScheduler = new FrameScheduler();
