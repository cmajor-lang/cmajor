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

export default class LevelMeter extends HTMLElement
{
    constructor()
    {
        super();

        this.setScaleRange (-65.0, 0.0);
        this.setDecayTime (700, 1000);

        this.root = this.attachShadow({ mode: "open" });
        this.setNumChans (1);
    }

    connectedCallback()
    {
        this.timer = setInterval (() => this.refreshAllChannelLevels(), 25);
    }

    disconnectedCallback()
    {
        clearInterval (this.timer);
    }

    setScaleRange (lowDB, highDB)
    {
        this.scaleMinDB = lowDB;
        this.scaleMaxDB = highDB;
    }

    setDecayTime (decayMs, peakHoldMs)
    {
        this.peakHoldMs = peakHoldMs;
        this.percentageDecayPerMs = 100.0 / decayMs;
    }

    setNumChans (numChans)
    {
        if (this.numChans != numChans)
        {
            this.numChans = numChans;
            this.refreshContent();

            this.channels = [];

            for (let i = 0; i < numChans; ++i)
                this.channels.push ({
                    currentLevel: 0,
                    currentBar: 0,
                    lastPeakPercentage: 0,
                    lastPeakTime: Date.now(),
                    peakBarPercentage: 0,
                    peakBarTime: Date.now(),
                    element: this.root.querySelector(`[data-chan="${i}"]`)
                });
        }
    }

    setChannelLevel (channel, newLevel)
    {
        const now = Date.now();
        const chan = this.channels[channel];
        const newPercent = this.levelToPercentageHeight (newLevel);

        if (newPercent > chan.currentLevel)
        {
            chan.currentLevel = newPercent;
            chan.lastPeakPercentage = newPercent;
            chan.lastPeakTime = now;
        }

        if (newPercent > chan.peakBarPercentage)
        {
            chan.peakBarPercentage = newPercent;
            chan.peakBarTime = now;
        }

        this.refreshChannelLevel (channel, now);
    }

    setChannelMinMax (channel, minSample, maxSample)
    {
        this.setChannelLevel (channel, Math.max (Math.abs (minSample), Math.abs (maxSample)));
    }

    levelToPercentageHeight (level)
    {
        if (level <= 0)
            return 0;

        const dB = Math.log10 (level) * 20;
        const percentage = 100 * (dB - this.scaleMinDB) / (this.scaleMaxDB - this.scaleMinDB);

        return Math.max (0, Math.min (100.0, percentage));
    }

    refreshChannelLevel (channel, currentTime)
    {
        const chan = this.channels[channel];
        const elem = chan.element;

        if (elem)
        {
            const timeSincePeak = currentTime - chan.lastPeakTime;
            const initialDecayDelay = 100;

            if (timeSincePeak > initialDecayDelay)
            {
                let percent = chan.lastPeakPercentage - this.percentageDecayPerMs * (timeSincePeak - initialDecayDelay);

                if (percent < 0.5)
                    percent = 0;

                chan.currentLevel = percent;
            }

            const timeSincePeakBar = currentTime - chan.peakBarTime;

            if (timeSincePeakBar > this.peakHoldMs)
            {
                chan.peakBarTime = currentTime;
                chan.peakBarPercentage = chan.currentLevel;
            }

            const topGap = 2;
            const w = elem.clientWidth;
            const h = elem.clientHeight - topGap;
            const bottom = topGap + h;
            const levelY = topGap + h * (100 - chan.currentLevel) / 100.0;

            let path = `M0 ${levelY} L ${w}, ${levelY} L${w} ${bottom} L 0 ${bottom} Z`;

            if (chan.peakBarPercentage > 0)
            {
                const peakY1 = topGap + h * (100 - chan.peakBarPercentage) / 100.0;
                const peakY2 = Math.min (bottom, peakY1 + 2);
                path += ` M0 ${peakY1} L ${w} ${peakY1} L ${w} ${peakY2} L 0 ${peakY2} Z`;
            }

            elem.style.clipPath = `path("${path}")`;
        }
    }

    refreshAllChannelLevels()
    {
        const now = Date.now();

        for (let i = 0; i < this.numChans; ++i)
            this.refreshChannelLevel (i, now);
    }

    refreshContent()
    {
        let bars = "";

        for (let chan = 0; chan < this.numChans; ++chan)
            bars += `<div class="meter-bar" data-chan="${chan}"></div>`;

        this.root.innerHTML = `<style>${this.getCSS()}</style><div class="holder">${bars}</div>`;
    }

    getCSS (numChans)
    {
        return `
            * {
                box-sizing: border-box;
                pointer-events: none;
                user-select: none;
                -webkit-user-select: none;
                -moz-user-select: none;
                -ms-user-select: none;
                padding: 0;
                margin: 0;
            }

            :host {
                --background-color: #222222;
                display: block;
                overflow: hidden;
                background: var(--background-color);
                padding: 0.1rem;
            }

            .meter-bar {
                display: flex;
                flex-flow: row nowrap;
                align-items: flex-end;
                justify-content: center;
                background-image: linear-gradient(to bottom, #f22 0%, #f22 17%, #ff2 18%, #ff2 35%, #2f2 36%, #2f2 100%);
                background-position: bottom;
                background-size: 100% 100%;
                background-repeat: no-repeat;
                clip-path: polygon(0% 0%, 0% 0%, 0% 0%);
                top: 0%;
                width: 1.4rem;
                height: 100%;
                color: #777;
            }

            .holder {
                display: flex;
                min-width: 2.5rem;
                height: 100%;
                flex-flow: row nowrap;
                align-items: flex-end;
                justify-content: space-around;
            }
`;
    }
}
