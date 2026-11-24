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

/// The session's CPU load, as a small bar with a percentage
export class CPUMeter
{
    constructor (session)
    {
        this.bar = el ("div", { class: "cp-cpu-bar" });
        this.text = el ("span", { class: "cp-cpu-text" }, "–");
        this.element = el ("div", { class: "cp-cpu", role: "meter", "aria-label": "CPU load", "aria-valuemin": 0, "aria-valuemax": 100,
                                    title: "Audio thread CPU load" },
                           el ("span", { class: "cp-cpu-label" }, "CPU"),
                           el ("div", { class: "cp-cpu-track" }, this.bar), this.text);

        this.listener = (e) => this.setLevel (e?.level ?? 0);
        this.setSession (session);
    }

    dispose()
    {
        this.setSession (undefined);
    }

    setSession (session)
    {
        this.session?.removeCPUListener (this.listener);
        this.session = session;
        this.session?.setCPULevelUpdateRate (20000);
        this.session?.addCPUListener (this.listener);
    }

    setLevel (level)
    {
        const percent = Math.min (100, Math.max (0, level * 100));
        this.bar.style.width = percent.toFixed (1) + "%";
        this.bar.classList.toggle ("cp-high", level >= 0.8);
        this.text.textContent = percent.toFixed (percent < 10 ? 1 : 0) + "%";
        this.element.setAttribute ("aria-valuenow", percent.toFixed (0));
    }

    reset()
    {
        this.bar.style.width = "0%";
        this.text.textContent = "–";
    }
}
