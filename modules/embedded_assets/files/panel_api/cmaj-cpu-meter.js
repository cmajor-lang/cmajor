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

export default class CPUMeter extends HTMLElement
{
    constructor()
    {
        super();

        this.root = this.attachShadow({ mode: "open" });
        this.root.innerHTML = `<style>${this.getCSS()}</style>${this.getHTML()}`;
        this.bar = this.root.getElementById ("meter-bar");
        this.text = this.root.getElementById ("text");

        this.cpuListener = e => this.setLevel (e.level);
    }

    dispose()
    {
        setSession (undefined);
    }

    setSession (session)
    {
        this.session?.removeCPUListener (this.cpuListener);
        this.session = session;
        this.session?.setCPULevelUpdateRate (20000);
        this.session?.addCPUListener (this.cpuListener);
    }

    setLevel (newLevel)
    {
        const percentage = Math.min (newLevel * 100.0, 100.0).toFixed (1) + "%";
        this.text.innerText = "CPU: " + percentage;
        this.bar.style.width = percentage;
        this.bar.style.background = newLevel < 0.8 ? "var(--bar-color-low)" : "var(--bar-color-high)";
    }

    getHTML()
    {
        return `<div id="holder">
                 <div id="meter-bar"></div>
                 <p id="text">CPU: 0%</p>
                </div>`;
    }

    getCSS()
    {
        return `
            * {
                box-sizing: border-box;
                user-select: none;
                -webkit-user-select: none;
                -moz-user-select: none;
                -ms-user-select: none;
            }

            :host {
                --bar-color-low: #8c8;
                --bar-color-high: #f44;
                --background-color: #00000055;
                --text-color: #787;
                display: block;
            }

            #holder {
                display: flex;
                position: relative;
                justify-content: center;
                align-items: center;
                top: 0rem;
                left: 0rem;
                width: 100%;
                height: 100%;
                background: var(--background-color);
                border: 0.1rem solid var(--background-color);
            }

            #meter-bar {
                position: absolute;
                display: block;
                background: var(--bar-color-low);
                left: 0%;
                top: 0%;
                width: 0%;
                height: 100%;
                transition: width 0.2s ease-out, background 0.1s;
            }

            p {
                position: relative;
                pointer-events: none;
                align-self: center;
                font-size: 0.7rem;
                color: var(--text-color);
                overflow: hidden;
            }
            `;
    }
}

customElements.define ("cmaj-cpu-meter", CPUMeter);
