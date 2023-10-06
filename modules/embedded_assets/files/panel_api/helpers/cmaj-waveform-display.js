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

export default class WaveformDisplay extends HTMLElement
{
    constructor()
    {
        super();

        this.root = this.attachShadow({ mode: "open" });

        this.setNumFrames (100);
        this.setNumChans (1);
    }

    connectedCallback()
    {
    }

    disconnectedCallback()
    {
    }

    setNumFrames (numFrames)
    {
        if (this.numFrames != numFrames)
        {
            this.numFrames = numFrames;
            this.refreshChannelData();
        }
    }

    setNumChans (numChans)
    {
        if (this.numChans != numChans)
        {
            this.numChans = numChans;
            this.refreshChannelData();
        }
    }

    refreshChannelData()
    {
        this.channels = [];
        this.refreshContent();

        if (this.numFrames)
        {
            for (let i = 0; i < this.numChans; ++i)
                this.channels.push ({
                    samples: new Float32Array(this.numFrames * 2),
                    index: 0,
                    canvas: this.root.querySelector(`[data-chan="${i}"]`)
                });
        }
    }

    setChannelMinMax (channel, minSample, maxSample)
    {
        const chan = this.channels[channel];
        chan.samples[chan.index++] = minSample;
        chan.samples[chan.index++] = maxSample;

        if (chan.index >= this.numFrames * 2)
            chan.index = 0;

        this.updateChannelCurve (chan);
    }

    updateChannelCurve (channel)
    {
        const ctx = channel.canvas.getContext("2d");

        const canvasW = channel.canvas.width;
        const canvasH = channel.canvas.height;
        const centreY = canvasH * 0.5;
        const vScale = (canvasH - 2) * -0.5;
        const xScale = canvasW / this.numFrames;

        ctx.clearRect (0, 0, canvasW, canvasH);
        ctx.fillStyle = "#aca";

        let index = channel.index;

        for (let i = 0; i < this.numFrames; ++i)
        {
            const minLevel = channel.samples[index++];
            const maxLevel = channel.samples[index++];

            if (index >= this.numFrames * 2)
                index = 0;

            const y1 = centreY + vScale * Math.max (-1.0, Math.min (1.0, maxLevel));
            const y2 = centreY + vScale * Math.max (-1.0, Math.min (1.0, minLevel));

            if (y2 > y1 + 0.5)
            {
                const peak = Math.max (Math.abs (minLevel), Math.abs (maxLevel));

                if (peak > 0.98)
                    ctx.fillStyle = "#f22";
                else if (peak > 0.8)
                    ctx.fillStyle = "#ff2";
                else
                    ctx.fillStyle = "#2f2";

                ctx.fillRect (xScale * i, y1, xScale, y2 - y1);
            }
        }
    }

    sampleToY (sample)
    {
        return 20.0 - 20.0 * sample;
    }

    refreshContent()
    {
        let waveforms = "";

        for (let chan = 0; chan < this.numChans; ++chan)
            waveforms += `<canvas class="waveform" data-chan="${chan}"></canvas>`;

        this.root.innerHTML = `<style>${this.getCSS()}</style> <div class="waveforms">${waveforms}</div>`;
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

            .waveforms {
                display: flex;
                min-width: 3rem;
                height: 100%;
                flex-flow: column nowrap;
                align-items: stretch;
                align-content: stretch;
                justify-content: space-around;
            }

            .waveform {
                left: 0%;
                width: 100%;
                min-height: 1rem;
                flex: 1 1 auto;
            }
`;
    }
}
