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
    "Echo Canyon" - a custom GUI for the ConvolutionReverb example.

    Everything you see is drawn from real data:

     - The canyon is the impulse response itself. The view loads impulse.wav, measures
       how loud it is over time, and draws that as a mountain range: the left edge is the
       moment a sound arrives, and the right edge is the end of the reverb tail.

     - The light moving through it is what the reverb is doing right now. The view listens
       to the level of the patch's input, and at each point in the canyon (i.e. at each time
       offset t into the impulse response) it shows:

             (input level t seconds ago) x (wet gain) x (impulse response level at t)

       ...which is roughly how loudly that part of the reverb tail is ringing. So a drum hit
       appears as a flash on the left that travels to the right, fading as the tail decays.

     - The little figure on the left is the dry signal: it glows with input x dry gain.
*/

const impulseFile       = "impulse.wav";
const levelChunkFrames  = 512;    // how many input frames each level update covers
const envelopeBins      = 512;    // resolution at which we measure the impulse response
const quietestDb        = -66;    // levels below this are drawn as flat ground
const darkestLightDb    = -60;    // reverb levels below this produce no light...
const brightestLightDb  = -20;    // ...and levels above this are at full brightness
const pixelsPerColumn   = 3;      // width of each lit column of the canyon
const bloomScale        = 4;      // the soft glow is drawn at 1/bloomScale resolution
const designSize        = { width: 560, height: 320 };  // the view size in the manifest, which sets its shape

const palettes = {
    sunset: {
        sky:        ["#2b1b4a", "#a8437a", "#ffb36b"],
        sun:        "#ffe6a8",
        layers:     ["#80325f", "#55204d", "#2c1135"],
        light:      [255, 214, 140],
        label:      "rgba(255, 233, 207, 0.55)",
        stars:      false
    },
    night: {
        sky:        ["#05071a", "#141c47", "#33397a"],
        sun:        "#e8ecff",
        layers:     ["#2c3468", "#1b2049", "#0d1029"],
        light:      [150, 215, 255],
        label:      "rgba(214, 226, 255, 0.5)",
        stars:      true
    }
};

const clamp     = (v, low, high) => Math.min (high, Math.max (low, v));
const dbToGain  = (db) => Math.pow (10, db / 20);
const gainToDb  = (gain) => gain > 0 ? 20 * Math.log10 (gain) : -200;

/** Maps a linear level to a brightness 0..1 for drawing the light. */
const levelToBrightness = (gain) => clamp ((gainToDb (gain) - darkestLightDb) / (brightestLightDb - darkestLightDb), 0, 1);


//==============================================================================
/** Loads the impulse response and measures its loudness envelope. */
async function loadImpulseEnvelope (patchConnection)
{
    const response = await fetch (patchConnection.getResourceAddress (impulseFile));
    const decoder = new OfflineAudioContext (1, 1, 44100);
    const audio = await decoder.decodeAudioData (await response.arrayBuffer());
    const samples = audio.getChannelData (0);

    // Find the peak level in each bin...
    const peaks = new Float32Array (envelopeBins);
    const samplesPerBin = samples.length / envelopeBins;
    let overallPeak = 0;

    for (let i = 0; i < samples.length; ++i)
    {
        const bin = Math.floor (i / samplesPerBin);
        const level = Math.abs (samples[i]);
        peaks[bin] = Math.max (peaks[bin], level);
        overallPeak = Math.max (overallPeak, level);
    }

    // ...and express each one relative to the loudest moment.
    const gains = peaks.map (p => overallPeak > 0 ? p / overallPeak : 0);

    return {
        duration:   audio.duration,
        gains,
        decibels:   gains.map (gainToDb)
    };
}

/** Returns a smoothed copy of an array, using a moving average of the given width. */
function smooth (values, width)
{
    const half = Math.floor (width / 2);

    return values.map ((_, i) =>
    {
        let total = 0, count = 0;

        for (let j = Math.max (0, i - half); j <= Math.min (values.length - 1, i + half); ++j, ++count)
            total += values[j];

        return total / count;
    });
}


//==============================================================================
/** A chunky vertical fader controlling one of the patch's dB parameters. */
class Fader
{
    constructor (patchConnection, endpointID, label)
    {
        this.patchConnection = patchConnection;
        this.endpointID = endpointID;
        this.min = -48;
        this.max = 6;
        this.init = 0;
        this.value = this.init;

        this.element = document.createElement ("div");
        this.element.className = "fader";
        this.element.innerHTML = `
            <div class="fader-label">${label}</div>
            <div class="fader-track" tabindex="0" role="slider" aria-label="${label} level">
                <div class="fader-fill"></div>
                <div class="fader-thumb"></div>
            </div>
            <div class="fader-value"></div>`;

        this.track   = this.element.querySelector (".fader-track");
        this.thumb   = this.element.querySelector (".fader-thumb");
        this.readout = this.element.querySelector (".fader-value");

        this.parameterListener = (newValue) => this.showValue (newValue);
        this.addMouseAndKeyHandlers();
        this.showValue (this.value);
    }

    /** The current level as a linear gain. */
    get gain()  { return dbToGain (this.value); }

    /** Picks up the range and default value from the endpoint's annotation. */
    setRange (annotation)
    {
        this.min  = annotation?.min  ?? this.min;
        this.max  = annotation?.max  ?? this.max;
        this.init = annotation?.init ?? this.init;
        this.track.setAttribute ("aria-valuemin", this.min);
        this.track.setAttribute ("aria-valuemax", this.max);
        this.showValue (this.value);
    }

    connect()
    {
        this.patchConnection.addParameterListener (this.endpointID, this.parameterListener);
        this.patchConnection.requestParameterValue (this.endpointID);
    }

    disconnect()
    {
        this.patchConnection.removeParameterListener (this.endpointID, this.parameterListener);
    }

    showValue (newValue)
    {
        this.value = clamp (newValue, this.min, this.max);
        const proportion = (this.value - this.min) / (this.max - this.min);
        this.element.style.setProperty ("--position", proportion);

        const text = (this.value > 0 ? "+" : "") + this.value.toFixed (1).replace ("-", "\u2212") + " dB";
        this.readout.textContent = text;
        this.track.setAttribute ("aria-valuenow", this.value.toFixed (1));
        this.track.setAttribute ("aria-valuetext", text);
    }

    /** Moves the fader and sends the new value to the patch. */
    sendValue (newValue)
    {
        this.showValue (newValue);
        this.patchConnection.sendEventOrValue (this.endpointID, this.value);
    }

    /** Sends a value as a complete gesture, for one-off changes like a key press. */
    sendSingleChange (newValue)
    {
        this.patchConnection.sendParameterGestureStart (this.endpointID);
        this.sendValue (newValue);
        this.patchConnection.sendParameterGestureEnd (this.endpointID);
    }

    valueAtPosition (clientY)
    {
        const track = this.track.getBoundingClientRect();
        const thumbHeight = this.thumb.offsetHeight;
        const proportion = 1 - (clientY - track.top - thumbHeight / 2) / (track.height - thumbHeight);
        return this.min + proportion * (this.max - this.min);
    }

    addMouseAndKeyHandlers()
    {
        // Click anywhere on the track to jump there, then drag. Hold shift for fine control.
        this.track.addEventListener ("pointerdown", (downEvent) =>
        {
            downEvent.preventDefault();
            this.track.focus();
            this.track.setPointerCapture (downEvent.pointerId);
            this.patchConnection.sendParameterGestureStart (this.endpointID);

            if (downEvent.target !== this.thumb)
                this.sendValue (this.valueAtPosition (downEvent.clientY));

            let lastY = downEvent.clientY;

            const onMove = (moveEvent) =>
            {
                const pixelsMoved = lastY - moveEvent.clientY;
                const dbPerPixel = (this.max - this.min) / (this.track.clientHeight - this.thumb.offsetHeight);
                lastY = moveEvent.clientY;
                this.sendValue (this.value + pixelsMoved * dbPerPixel * (moveEvent.shiftKey ? 0.1 : 1));
            };

            const onRelease = () =>
            {
                this.track.removeEventListener ("pointermove", onMove);
                this.track.removeEventListener ("pointerup", onRelease);
                this.track.removeEventListener ("pointercancel", onRelease);
                this.patchConnection.sendParameterGestureEnd (this.endpointID);
            };

            this.track.addEventListener ("pointermove", onMove);
            this.track.addEventListener ("pointerup", onRelease);
            this.track.addEventListener ("pointercancel", onRelease);
        });

        // Double-click to go back to the default level
        this.track.addEventListener ("dblclick", () => this.sendSingleChange (this.init));

        this.track.addEventListener ("wheel", (e) =>
        {
            e.preventDefault();
            this.sendSingleChange (this.value - Math.sign (e.deltaY) * (e.shiftKey ? 0.1 : 0.5));
        }, { passive: false });

        this.track.addEventListener ("keydown", (e) =>
        {
            const step = e.shiftKey ? 0.1 : 0.5;
            const changes = {
                ArrowUp:    this.value + step,
                ArrowRight: this.value + step,
                ArrowDown:  this.value - step,
                ArrowLeft:  this.value - step,
                PageUp:     this.value + 6,
                PageDown:   this.value - 6,
                Home:       this.min,
                End:        this.max
            };

            if (e.key in changes)
            {
                e.preventDefault();
                this.sendSingleChange (changes[e.key]);
            }
        });
    }
}


//==============================================================================
class EchoCanyonView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;

        const root = this.attachShadow ({ mode: "open" });
        root.innerHTML = this.getHTML();

        this.dryFader = new Fader (patchConnection, "dryLevel", "Dry");
        this.wetFader = new Fader (patchConnection, "wetLevel", "Wet");
        root.querySelector (".stage").prepend (this.dryFader.element);
        root.querySelector (".stage").append (this.wetFader.element);

        this.canvasHolder = root.querySelector (".canyon");
        this.canvas = root.querySelector ("canvas");
        this.context = this.canvas.getContext ("2d");
        this.bloomCanvas = document.createElement ("canvas");
        this.bloomContext = this.bloomCanvas.getContext ("2d");
        this.infoText = root.querySelector (".info");

        this.impulse = null;
        this.inputHistory = [];     // recent input levels, as { time, level }, newest last
        this.dryGlow = 0;
        this.uiScale = 1;
        this.stars = Array.from ({ length: 40 }, () => ({ x: Math.random(), y: Math.random() * 0.5, phase: Math.random() * 6.3 }));

        loadImpulseEnvelope (patchConnection)
            .then (impulse =>
            {
                this.impulse = { ...impulse,
                                 layers: [impulse.decibels,
                                          smooth (impulse.decibels, 9),
                                          smooth (impulse.decibels, 41)] };
                this.infoText.textContent = `impulse response \u00b7 ${impulse.duration.toFixed (2)} s`;
            })
            .catch (error =>
            {
                console.error (error);
                this.infoText.textContent = `couldn't load ${impulseFile}`;
            });
    }

    connectedCallback()
    {
        // The status tells us each parameter's range and default, from the patch's annotations
        this.statusListener = (status) =>
        {
            for (const input of status?.details?.inputs ?? [])
            {
                if (input.endpointID === "dryLevel")  this.dryFader.setRange (input.annotation);
                if (input.endpointID === "wetLevel")  this.wetFader.setRange (input.annotation);
            }
        };

        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
        this.dryFader.connect();
        this.wetFader.connect();

        // Each update gives us the min and max sample values over a chunk of the input
        this.inputLevelListener = (chunk) =>
        {
            const level = Math.max (Math.abs (chunk.min[0]), Math.abs (chunk.max[0]));
            this.inputHistory.push ({ time: performance.now() / 1000, level });
        };

        this.patchConnection.addEndpointListener ("in", this.inputLevelListener, levelChunkFrames);

        // The host gives us a fixed size from the manifest's view width and height, but we'd
        // rather fill whatever space the window has, and scale up or down to fit it.
        this.style.width = "100%";
        this.style.height = "100%";

        this.resizeObserver = new ResizeObserver (() => this.updateSize());
        this.resizeObserver.observe (this);
        this.resizeObserver.observe (this.canvasHolder);

        this.darkMode = window.matchMedia ("(prefers-color-scheme: dark)");

        const drawFrame = () =>
        {
            this.drawCanyon();
            this.animationFrame = requestAnimationFrame (drawFrame);
        };

        drawFrame();
    }

    disconnectedCallback()
    {
        cancelAnimationFrame (this.animationFrame);
        this.resizeObserver.disconnect();
        this.patchConnection.removeStatusListener (this.statusListener);
        this.patchConnection.removeEndpointListener ("in", this.inputLevelListener);
        this.dryFader.disconnect();
        this.wetFader.disconnect();
    }

    updateSize()
    {
        // The whole GUI keeps the shape of the manifest's view size: we find the largest box
        // of that shape that fits the window, and scale everything inside it to match.
        // The CSS uses this as --scale, and the canvas drawing as this.uiScale.
        const fitWidth  = this.clientWidth  / designSize.width;
        const fitHeight = this.clientHeight / designSize.height;
        this.uiScale = Math.min (fitWidth, fitHeight) || 1;
        this.style.setProperty ("--scale", this.uiScale);

        const scale = window.devicePixelRatio || 1;
        this.width  = this.canvasHolder.clientWidth;
        this.height = this.canvasHolder.clientHeight;
        this.canvas.width  = Math.round (this.width * scale);
        this.canvas.height = Math.round (this.height * scale);
        this.context.setTransform (scale, 0, 0, scale, 0, 0);
        this.bloomCanvas.width  = Math.ceil (this.width / bloomScale);
        this.bloomCanvas.height = Math.ceil (this.height / bloomScale);
    }

    //==============================================================================
    /** Returns the brightness (0..1) of the reverb at each column of the canyon. */
    getReverbLightPerColumn (numColumns, now)
    {
        const history = this.inputHistory;
        const wetGain = this.wetFader.gain;
        const light = new Float32Array (numColumns);

        // Columns further right are further into the tail, so they show older input.
        // We walk back through the history as we move right.
        let index = history.length - 1;

        for (let column = 0; column < numColumns && index >= 0; ++column)
        {
            const timeIntoTail = (column + 0.5) / numColumns * this.impulse.duration;

            while (index > 0 && history[index].time > now - timeIntoTail)
                --index;

            if (history[index].time > now - timeIntoTail)
                break; // no input this long ago

            const bin = Math.floor ((column + 0.5) / numColumns * envelopeBins);
            light[column] = levelToBrightness (history[index].level * wetGain * this.impulse.gains[bin]);
        }

        // Soften the steps between neighbouring columns
        return light.map ((v, i) => (light[Math.max (0, i - 1)] + 2 * v + light[Math.min (numColumns - 1, i + 1)]) / 4);
    }

    drawCanyon()
    {
        if (! this.width || ! this.height)
            return;

        const ctx = this.context;
        const bloom = this.bloomContext;
        const { width, height } = this;
        const palette = this.darkMode.matches ? palettes.night : palettes.sunset;
        const now = performance.now() / 1000;

        // Forget any input that's older than the whole reverb tail
        const longestTail = (this.impulse?.duration ?? 0) + 0.5;

        while (this.inputHistory.length > 0 && now - this.inputHistory[0].time > longestTail)
            this.inputHistory.shift();

        const numColumns = Math.ceil (width / pixelsPerColumn);
        const light = this.impulse ? this.getReverbLightPerColumn (numColumns, now)
                                   : new Float32Array (numColumns);
        const totalLight = light.reduce ((sum, v) => sum + v, 0) / numColumns;
        const [r, g, b] = palette.light;
        const lightColour = (alpha) => `rgba(${r}, ${g}, ${b}, ${alpha})`;

        // The sky warms up a little when there's lots of reverb around
        const sky = ctx.createLinearGradient (0, 0, 0, height);
        palette.sky.forEach ((colour, i) => sky.addColorStop (i / (palette.sky.length - 1), colour));
        ctx.fillStyle = sky;
        ctx.fillRect (0, 0, width, height);
        ctx.fillStyle = lightColour (0.3 * totalLight);
        ctx.fillRect (0, 0, width, height);

        if (palette.stars)
        {
            for (const star of this.stars)
            {
                ctx.fillStyle = `rgba(255, 255, 255, ${0.35 + 0.3 * Math.sin (now * 1.7 + star.phase)})`;
                ctx.fillRect (star.x * width, star.y * height, 1.5, 1.5);
            }
        }

        // Each layer of the canyon is drawn as a ridge whose height follows the
        // impulse response level: the back one shows the raw data, and the nearer
        // ones are smoothed and lower, to give some depth.
        const layerShapes = [{ base: 0.12, scale: 0.72, lightAmount: 1.0 },
                             { base: 0.07, scale: 0.48, lightAmount: 0.55 },
                             { base: 0.03, scale: 0.26, lightAmount: 0.3 }];

        const ridges = layerShapes.map ((shape, layerIndex) =>
            Float32Array.from ({ length: numColumns + 1 }, (_, column) =>
            {
                if (! this.impulse)
                    return height * (1 - shape.base);

                const levels = this.impulse.layers[layerIndex];
                const bin = Math.min (envelopeBins - 1, Math.floor (column / numColumns * envelopeBins));
                const proportion = clamp ((levels[bin] - quietestDb) / -quietestDb, 0, 1);
                return height * (1 - shape.base - proportion * shape.scale);
            }));

        // The sun (or moon) sinks behind the back ridge
        const sunX = width * 0.8, sunY = height * 0.58, sunRadius = Math.min (height * 0.1, width * 0.07);
        const sunGlow = ctx.createRadialGradient (sunX, sunY, sunRadius, sunX, sunY, sunRadius * 3.5);
        sunGlow.addColorStop (0, palette.sun + "66");
        sunGlow.addColorStop (1, palette.sun + "00");
        ctx.fillStyle = sunGlow;
        ctx.fillRect (0, 0, width, height);
        ctx.fillStyle = palette.sun;
        ctx.beginPath();
        ctx.arc (sunX, sunY, sunRadius, 0, Math.PI * 2);
        ctx.fill();

        bloom.clearRect (0, 0, this.bloomCanvas.width, this.bloomCanvas.height);

        layerShapes.forEach ((shape, layerIndex) =>
        {
            const ridgeY = ridges[layerIndex];
            const ridge = new Path2D();
            ridge.moveTo (0, height);

            for (let column = 0; column <= numColumns; ++column)
                ridge.lineTo (column * pixelsPerColumn, ridgeY[column]);

            ridge.lineTo (width, height);
            ridge.closePath();

            ctx.fillStyle = palette.layers[layerIndex];
            ctx.fill (ridge);

            // Light up the parts of this layer where the reverb is ringing:
            // a wash over the rock, a bright line along the ridge, and some glow above it
            ctx.save();
            ctx.globalCompositeOperation = "lighter";
            ctx.lineWidth = 2 * this.uiScale;
            ctx.lineCap = "round";

            for (let column = 0; column < numColumns; ++column)
            {
                const brightness = light[column] * shape.lightAmount;

                if (brightness > 0.01)
                {
                    const x = column * pixelsPerColumn, y = ridgeY[column];

                    ctx.fillStyle = lightColour (0.4 * brightness);
                    ctx.fillRect (x, y, pixelsPerColumn, height - y);

                    ctx.strokeStyle = lightColour (brightness);
                    ctx.beginPath();
                    ctx.moveTo (x, y);
                    ctx.lineTo (x + pixelsPerColumn, ridgeY[column + 1]);
                    ctx.stroke();

                    bloom.fillStyle = lightColour (brightness);
                    const glowHeight = 24 * this.uiScale;
                    bloom.fillRect (x / bloomScale, (y - glowHeight / 2) / bloomScale, pixelsPerColumn / bloomScale, glowHeight / bloomScale);
                }
            }

            ctx.restore();
        });

        // The soft glow: drawn small, then stretched up so it blurs
        ctx.save();
        ctx.globalCompositeOperation = "lighter";
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage (this.bloomCanvas, 0, 0, width, height);
        ctx.restore();

        const figureX = 18 * this.uiScale;
        const figureSize = Math.min (height * 0.08, 26 * this.uiScale);
        this.drawFigure (ctx, palette, lightColour, figureX, ridges[2][Math.round (figureX / pixelsPerColumn)], figureSize);
        this.drawTimeAxis (ctx, palette, width, height);
    }

    /** Draws the little figure who is shouting into the canyon, glowing with the dry signal. */
    drawFigure (ctx, palette, lightColour, x, feetY, size)
    {
        const latestInput = this.inputHistory.at (-1);
        const isRecent = latestInput && performance.now() / 1000 - latestInput.time < 0.1;
        const target = isRecent ? levelToBrightness (latestInput.level * this.dryFader.gain) : 0;
        this.dryGlow += (target - this.dryGlow) * (target > this.dryGlow ? 0.5 : 0.08);

        if (this.dryGlow > 0.01)
        {
            const halo = ctx.createRadialGradient (x, feetY - size, 0, x, feetY - size, size * 3);
            halo.addColorStop (0, lightColour (0.8 * this.dryGlow));
            halo.addColorStop (1, lightColour (0));
            ctx.fillStyle = halo;
            ctx.fillRect (x - size * 3, feetY - size * 4, size * 6, size * 6);
        }

        ctx.fillStyle = palette.layers[2];
        ctx.beginPath();
        ctx.arc (x, feetY - size * 1.15, size * 0.28, 0, Math.PI * 2);
        ctx.roundRect (x - size * 0.22, feetY - size * 0.82, size * 0.44, size * 0.82, size * 0.15);
        ctx.fill();
    }

    drawTimeAxis (ctx, palette, width, height)
    {
        if (! this.impulse)
            return;

        ctx.fillStyle = palette.label;
        ctx.font = `600 ${10 * this.uiScale}px ui-rounded, 'SF Pro Rounded', system-ui, sans-serif`;
        ctx.textBaseline = "bottom";
        ctx.textAlign = "center";

        for (let seconds = 0.5; seconds < this.impulse.duration; seconds += 0.5)
            ctx.fillText (`${seconds} s`, seconds / this.impulse.duration * width, height - 4 * this.uiScale);
    }

    //==============================================================================
    getHTML()
    {
        return `
        <style>
            :host {
                --background:   #1d0f26;
                --panel:        #2a1734;
                --text:         #ffe9cf;
                --text-dim:     rgba(255, 233, 207, 0.55);
                --dry-colour:   #ff8f6b;
                --wet-colour:   #ffd07a;

                display: block;
                width: 100%;
                height: 100%;
                user-select: none;
                -webkit-user-select: none;
            }

            @media (prefers-color-scheme: dark) {
                :host {
                    --background:   #070a1c;
                    --panel:        #121838;
                    --text:         #e3eaff;
                    --text-dim:     rgba(214, 226, 255, 0.5);
                    --dry-colour:   #ff9fc4;
                    --wet-colour:   #96d7ff;
                }
            }

            /* The frame fills the window, and centres the GUI in it at a fixed aspect ratio */
            .frame {
                display: flex;
                align-items: center;
                justify-content: center;
                width: 100%;
                height: 100%;
                overflow: hidden;
                background: var(--background);
            }

            /* All the sizes below are in units of --u, which is one pixel at the
               manifest's size, and grows or shrinks as the window is resized. */
            .main {
                --u: calc(var(--scale, 1) * 1px);

                box-sizing: border-box;
                display: flex;
                flex-direction: column;
                flex-shrink: 0;
                gap: calc(10 * var(--u));
                width: calc(${designSize.width} * var(--u));
                height: calc(${designSize.height} * var(--u));
                padding: calc(14 * var(--u)) calc(16 * var(--u)) calc(16 * var(--u));
                background: var(--background);
                color: var(--text);
                font-family: ui-rounded, "SF Pro Rounded", "Nunito", system-ui, sans-serif;
                overflow: hidden;
            }

            header {
                display: flex;
                justify-content: space-between;
                align-items: baseline;
                gap: calc(10 * var(--u));
                white-space: nowrap;
            }

            h1 {
                margin: 0;
                font-size: calc(18 * var(--u));
                font-weight: 800;
                letter-spacing: 0.12em;
            }

            .info {
                font-size: calc(12 * var(--u));
                font-weight: 600;
                color: var(--text-dim);
                overflow: hidden;
                text-overflow: ellipsis;
            }

            .stage {
                flex: 1;
                display: flex;
                gap: calc(14 * var(--u));
                min-height: 0;
            }

            .canyon {
                flex: 1;
                position: relative;
                min-width: 0;
                border-radius: calc(14 * var(--u));
                overflow: hidden;
            }

            canvas {
                position: absolute;
                width: 100%;
                height: 100%;
            }

            .fader {
                --thumb-height: calc(22 * var(--u));
                display: flex;
                flex-direction: column;
                align-items: center;
                gap: calc(8 * var(--u));
                width: calc(56 * var(--u));
            }

            .fader:first-child  { --accent: var(--dry-colour); }
            .fader:last-child   { --accent: var(--wet-colour); }

            .fader-label {
                font-size: calc(12 * var(--u));
                font-weight: 800;
                letter-spacing: 0.14em;
                text-transform: uppercase;
                color: var(--accent);
            }

            .fader-track {
                position: relative;
                flex: 1;
                width: calc(24 * var(--u));
                border-radius: calc(12 * var(--u));
                background: var(--panel);
                cursor: ns-resize;
                touch-action: none;
                outline: none;
            }

            .fader-track:focus-visible {
                box-shadow: 0 0 0 calc(2 * var(--u)) var(--accent);
            }

            .fader-fill {
                position: absolute;
                left: 0;
                right: 0;
                bottom: 0;
                height: calc(var(--position) * (100% - var(--thumb-height)) + var(--thumb-height) / 2);
                border-radius: calc(12 * var(--u));
                background: var(--accent);
                opacity: 0.35;
            }

            .fader-thumb {
                position: absolute;
                left: calc(-8 * var(--u));
                right: calc(-8 * var(--u));
                bottom: calc(var(--position) * (100% - var(--thumb-height)));
                height: var(--thumb-height);
                border-radius: calc(11 * var(--u));
                background: var(--accent);
                box-shadow: 0 calc(2 * var(--u)) calc(6 * var(--u)) rgba(0, 0, 0, 0.35);
                transition: transform 0.1s;
            }

            .fader-track:active .fader-thumb {
                transform: scale(1.08);
            }

            .fader-value {
                font-size: calc(12 * var(--u));
                font-weight: 700;
                font-variant-numeric: tabular-nums;
                white-space: nowrap;
            }
        </style>

        <div class="frame"><div class="main">
            <header>
                <h1>ECHO CANYON</h1>
                <span class="info">loading impulse response\u2026</span>
            </header>
            <div class="stage">
                <div class="canyon"><canvas></canvas></div>
            </div>
        </div></div>`;
    }
}


/* This is the function that a host (the command line patch player, or a Cmajor plugin
   loader, or our VScode extension, etc) will call in order to create a view for your patch.
*/
export default function createPatchView (patchConnection)
{
    const customElementName = "echo-canyon-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, EchoCanyonView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
