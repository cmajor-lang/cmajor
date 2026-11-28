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
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

/*
    GUI for the DynamicEQ patch.

    The main display overlays the spectrum analyser (pre and/or post EQ) with the EQ curves:
    for each band it draws the static curve, the zone that the dynamics can move it through
    (static gain -> static gain + range), and the live curve as the dynamics act on it.

    Mouse controls on the display:
        drag a band handle          frequency and gain
        alt/option-drag a handle    dynamic range
        drag the range handle       dynamic range
        mouse wheel                 Q
        double-click a handle       turn the band (or low/high cut) on or off
        shift                       fine adjustment

    The filter responses are calculated exactly, using the same Simper SVF equations as
    calculateCoefficients() in dsp/Equaliser.cmajor, so keep the two in sync.
*/

const numBands = 6;
const bandColours = ["#ff6b6b", "#ffa94d", "#ffd43b", "#69db7c", "#4dabf7", "#b197fc"];
const bandTypeNames = ["Low Shelf", "Bell", "High Shelf"];

// The display's frequency range matches the spectrum sent by dsp/SpectrumAnalyser.cmajor
const minFrequency = 20;
const maxFrequency = 20000;

const eqDisplayRange = 20;      // the EQ curves are shown over +/- this many dB
const spectrumTop = 0;          // dBFS at the top of the display
const spectrumBottom = -90;     // dBFS at the bottom of the display

// In the analyser's "Music" mode, the spectrum is tilted by this many dB per octave
// around 1kHz. With 3dB/octave, a signal with equal energy in each octave (pink noise,
// and roughly the long-term spectrum of most music) looks flat, whereas in "Flat" mode
// white noise looks flat and pink noise falls by 3dB/octave.
const musicModeSlope = 3;
const slopePivotFrequency = 1000;

const sidechainMeterMin = -60;

//==============================================================================
function clamp (value, min, max)
{
    return Math.max (min, Math.min (max, value));
}

function formatFrequency (hz)
{
    if (hz >= 10000)  return (hz / 1000).toFixed (1) + " kHz";
    if (hz >= 1000)   return (hz / 1000).toFixed (2) + " kHz";
    if (hz >= 100)    return hz.toFixed (0) + " Hz";
    return hz.toFixed (1) + " Hz";
}

function formatDecibels (db)
{
    return (db > 0.05 ? "+" : "") + db.toFixed (1) + " dB";
}

function formatTime (ms)
{
    if (ms >= 1000)  return (ms / 1000).toFixed (2) + " s";
    if (ms >= 10)    return ms.toFixed (0) + " ms";
    return ms.toFixed (1) + " ms";
}

function getBandParameterID (bandIndex, suffix)
{
    return "band" + (bandIndex + 1) + suffix;
}

//==============================================================================
// Filter response calculations

/// Returns the effective SVF parameters for a bell or shelf, as in calculateCoefficients()
/// in Equaliser.cmajor. The filter's output is m0 * input + m1 * band + m2 * low.
function getBandCoefficients (type, frequency, q, gainDb, sampleRate)
{
    const g = Math.tan (Math.PI * clamp (frequency, 10, sampleRate * 0.48) / sampleRate);
    const k = 1 / q;
    const A = Math.pow (10, gainDb / 40);

    if (type === 0)  return { g: g / Math.sqrt (A), k, m0: 1, m1: k * (A - 1), m2: A * A - 1 };
    if (type === 2)  return { g: g * Math.sqrt (A), k, m0: A * A, m1: k * (1 - A) * A, m2: 1 - A * A };

    const bellK = k / A;
    return { g, k: bellK, m0: 1, m1: bellK * (A * A - 1), m2: 0 };
}

/// The magnitude in dB of an SVF at a frequency, given tanW = tan (pi * frequency / sampleRate).
/// The trapezoidal SVF is exactly the bilinear transform of its analogue prototype, so its
/// response is the analogue one evaluated at s = j * tanW / g.
function getSVFMagnitudeDb (c, tanW)
{
    const w = tanW / c.g;
    const denominatorReal = 1 - w * w;
    const denominatorImag = c.k * w;
    const numeratorReal = c.m0 * denominatorReal + c.m2;
    const numeratorImag = w * (c.m0 * c.k + c.m1);

    return 10 * Math.log10 ((numeratorReal * numeratorReal + numeratorImag * numeratorImag)
                              / (denominatorReal * denominatorReal + denominatorImag * denominatorImag));
}

/// The response of a band's sidechain detector filter (a unity-peak band-pass for bells,
/// Butterworth low/high-pass for shelves)
function getSidechainMagnitudeDb (type, frequency, q, sampleRate, tanW)
{
    const g = Math.tan (Math.PI * clamp (frequency, 10, sampleRate * 0.48) / sampleRate);
    const k = type === 1 ? 1 / q : Math.SQRT2;
    const w = tanW / g;
    const denominator = (1 - w * w) * (1 - w * w) + k * k * w * w;
    const numerator = type === 0 ? 1 : type === 2 ? w * w * w * w : k * k * w * w;

    return 10 * Math.log10 (numerator / denominator);
}

/// The response of the 12 or 24 dB/oct Butterworth low/high cut filters
function getCutMagnitudeDb (isHighPass, is24dB, frequency, sampleRate, tanW)
{
    const g = Math.tan (Math.PI * clamp (frequency, 10, sampleRate * 0.48) / sampleRate);
    const w = tanW / g;
    const w2 = w * w;

    const getStagePower = (k) =>
    {
        const denominator = (1 - w2) * (1 - w2) + k * k * w2;
        return (isHighPass ? w2 * w2 : 1) / denominator;
    };

    const power = is24dB ? getStagePower (0.76536686) * getStagePower (1.84775907)
                         : getStagePower (Math.SQRT2);

    return 10 * Math.log10 (power);
}

//==============================================================================
/// A rotary control for a parameter. The endpoint is supplied by a function so that the
/// band knobs can follow the currently selected band.
class Knob
{
    constructor (view, label, getEndpointID, options = {})
    {
        this.view = view;
        this.getEndpointID = getEndpointID;
        this.isLogarithmic = options.logarithmic ?? false;
        this.isBipolar = options.bipolar ?? false;
        this.format = options.format ?? ((v) => v.toFixed (2));

        this.element = document.createElement ("div");
        this.element.className = "knob";
        this.element.innerHTML = `
            <div class="knob-label">${label}</div>
            <svg viewBox="0 0 60 60" class="knob-dial">
                <path class="knob-track" d="${Knob.getArcPath (0, 1)}"></path>
                <path class="knob-arc"></path>
                <circle class="knob-cap" cx="30" cy="30" r="17"></circle>
                <line class="knob-pointer" x1="30" y1="30" x2="30" y2="16"></line>
            </svg>
            <div class="knob-value"></div>`;

        this.arc = this.element.querySelector (".knob-arc");
        this.pointer = this.element.querySelector (".knob-pointer");
        this.valueText = this.element.querySelector (".knob-value");

        const dial = this.element.querySelector (".knob-dial");
        dial.addEventListener ("pointerdown", (e) => this.startDrag (e));
        dial.addEventListener ("pointermove", (e) => this.drag (e));
        dial.addEventListener ("pointerup", (e) => this.endDrag (e));
        dial.addEventListener ("lostpointercapture", (e) => this.endDrag (e));
        dial.addEventListener ("dblclick", () => this.view.resetToDefault (this.getEndpointID()));
        dial.addEventListener ("wheel", (e) =>
        {
            e.preventDefault();
            const step = (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.002 : 0.01);
            this.view.nudgeValue (this.getEndpointID(), this.fromNormalised (this.toNormalised (this.getValue()) + step));
        }, { passive: false });
    }

    static getArcPath (startProportion, endProportion)
    {
        const startAngle = (-135 + 270 * startProportion) * Math.PI / 180;
        const endAngle   = (-135 + 270 * endProportion) * Math.PI / 180;
        const radius = 24;
        const x1 = 30 + radius * Math.sin (startAngle), y1 = 30 - radius * Math.cos (startAngle);
        const x2 = 30 + radius * Math.sin (endAngle),   y2 = 30 - radius * Math.cos (endAngle);
        const largeArc = Math.abs (endAngle - startAngle) > Math.PI ? 1 : 0;
        const sweep = endAngle > startAngle ? 1 : 0;

        return `M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} ${sweep} ${x2} ${y2}`;
    }

    getValue()          { return this.view.getValue (this.getEndpointID()); }
    getRange()          { return this.view.getRange (this.getEndpointID()); }

    toNormalised (value)
    {
        const { min, max } = this.getRange();

        if (this.isLogarithmic)
            return Math.log (clamp (value, min, max) / min) / Math.log (max / min);

        return (clamp (value, min, max) - min) / (max - min);
    }

    fromNormalised (proportion)
    {
        const { min, max } = this.getRange();
        proportion = clamp (proportion, 0, 1);

        if (this.isLogarithmic)
            return min * Math.pow (max / min, proportion);

        return min + proportion * (max - min);
    }

    startDrag (e)
    {
        e.preventDefault();
        e.currentTarget.setPointerCapture (e.pointerId);
        this.dragStartY = e.clientY;
        this.dragStartProportion = this.toNormalised (this.getValue());
        this.isDragging = true;
        this.view.beginGesture (this.getEndpointID());
    }

    drag (e)
    {
        if (! this.isDragging)
            return;

        const pixelsForFullRange = e.shiftKey ? 1000 : 200;
        const proportion = this.dragStartProportion + (this.dragStartY - e.clientY) / pixelsForFullRange;
        this.view.setValue (this.getEndpointID(), this.fromNormalised (proportion));
    }

    endDrag()
    {
        if (this.isDragging)
        {
            this.isDragging = false;
            this.view.endGesture (this.getEndpointID());
        }
    }

    refresh()
    {
        if (! this.view.hasEndpoint (this.getEndpointID()))
            return;

        const value = this.getValue();
        const proportion = this.toNormalised (value);
        const origin = this.isBipolar ? this.toNormalised (0) : 0;

        this.arc.setAttribute ("d", Knob.getArcPath (Math.min (origin, proportion), Math.max (origin, proportion)));
        this.pointer.setAttribute ("transform", `rotate(${-135 + 270 * proportion} 30 30)`);
        this.valueText.textContent = this.format (value);
    }
}

//==============================================================================
/// A row of buttons for choosing one of a parameter's text options
class OptionButtons
{
    constructor (view, getEndpointID, labels)
    {
        this.view = view;
        this.getEndpointID = getEndpointID;

        this.element = document.createElement ("div");
        this.element.className = "options";

        this.buttons = labels.map ((label, index) =>
        {
            const button = document.createElement ("button");
            button.textContent = label;
            button.addEventListener ("click", () => this.view.setValueAsGesture (this.getEndpointID(), index));
            this.element.appendChild (button);
            return button;
        });
    }

    refresh()
    {
        const selected = Math.round (this.view.getValue (this.getEndpointID()));
        this.buttons.forEach ((button, index) => button.classList.toggle ("selected", index === selected));
    }
}

/// A button for a boolean parameter
class ToggleButton
{
    constructor (view, getEndpointID, label, className = "")
    {
        this.view = view;
        this.getEndpointID = getEndpointID;

        this.element = document.createElement ("button");
        this.element.className = "toggle " + className;
        this.element.textContent = label;
        this.element.addEventListener ("click", () => this.view.toggleValue (this.getEndpointID()));
    }

    refresh()
    {
        this.element.classList.toggle ("on", !! this.view.getValue (this.getEndpointID()));
    }
}

//==============================================================================
class DynamicEQView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;

        this.endpoints = new Map();     // endpointID -> endpoint info from the status
        this.values = new Map();        // endpointID -> current parameter value
        this.gestureTimers = new Map(); // endpoints with a gesture in progress from the mouse wheel

        this.selectedBand = 2;
        this.analyserMode = 3;          // 0 = off, 1 = pre, 2 = post, 3 = both
        this.analyserSlope = 0;         // 0 = flat, 1 = music (see musicModeSlope)
        this.sampleRate = 48000;

        this.dynamicGain = new Float32Array (numBands);
        this.displayedDynamicGain = new Float32Array (numBands);
        this.sidechainLevel = new Float32Array (numBands).fill (-100);
        this.displayedSidechainLevel = new Float32Array (numBands).fill (-100);

        this.preSpectrum = null;
        this.postSpectrum = null;
        this.displayedPreSpectrum = null;
        this.displayedPostSpectrum = null;

        this.handles = [];
        this.hoveredHandle = null;
        this.mousePosition = null;
        this.dragState = null;

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();
        this.createControls();

        this.canvas = this.shadowRoot.getElementById ("display");
        this.context = this.canvas.getContext ("2d");
        this.meterCanvas = this.shadowRoot.getElementById ("sidechain-meter");
        this.meterContext = this.meterCanvas.getContext ("2d");

        this.setupDisplayMouseHandling();
        this.setupMeterMouseHandling();
    }

    //==============================================================================
    connectedCallback()
    {
        this.statusListener = (status) =>
        {
            this.endpoints.clear();

            for (const endpoint of status?.details?.inputs ?? [])
            {
                if (endpoint.purpose === "parameter" || endpoint.annotation?.min !== undefined || endpoint.annotation?.boolean)
                {
                    this.endpoints.set (endpoint.endpointID, endpoint);

                    if (! this.values.has (endpoint.endpointID))
                        this.values.set (endpoint.endpointID, Number (endpoint.annotation?.init ?? 0));

                    this.patchConnection.requestParameterValue (endpoint.endpointID);
                }
            }

            this.refreshControls();
        };

        this.parameterListener = ({ endpointID, value }) =>
        {
            this.values.set (endpointID, Number (value));
            this.refreshControls();
        };

        this.bandStateListener = (state) =>
        {
            this.sampleRate = state.sampleRate || this.sampleRate;

            for (let i = 0; i < numBands; ++i)
            {
                this.dynamicGain[i] = state.dynamicGain[i];
                this.sidechainLevel[i] = state.sidechainLevel[i];
            }
        };

        this.spectrumListener = (spectrum) =>
        {
            this.preSpectrum = spectrum.pre;
            this.postSpectrum = spectrum.post;
        };

        this.storedStateListener = ({ key, value }) => this.setAnalyserOption (key, value);

        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.addAllParameterListener (this.parameterListener);
        this.patchConnection.addEndpointListener ("bandState", this.bandStateListener);
        this.patchConnection.addEndpointListener ("spectrum", this.spectrumListener);
        this.patchConnection.addStoredStateValueListener (this.storedStateListener);
        this.patchConnection.requestStatusUpdate();
        this.patchConnection.requestStoredStateValue ("analyserMode");
        this.patchConnection.requestStoredStateValue ("analyserSlope");

        this.resizeCanvases();
        this.lastFrameTime = performance.now();

        const animate = (time) =>
        {
            this.updateDisplay (time);
            this.animationFrame = window.requestAnimationFrame (animate);
        };

        this.animationFrame = window.requestAnimationFrame (animate);
    }

    disconnectedCallback()
    {
        window.cancelAnimationFrame (this.animationFrame);
        this.patchConnection.removeStatusListener (this.statusListener);
        this.patchConnection.removeAllParameterListener (this.parameterListener);
        this.patchConnection.removeEndpointListener ("bandState", this.bandStateListener);
        this.patchConnection.removeEndpointListener ("spectrum", this.spectrumListener);
        this.patchConnection.removeStoredStateValueListener (this.storedStateListener);

        for (const endpointID of this.gestureTimers.keys())
            this.endGesture (endpointID);
    }

    //==============================================================================
    // Parameter access

    hasEndpoint (endpointID)        { return this.endpoints.has (endpointID); }

    getValue (endpointID)
    {
        return this.values.get (endpointID) ?? 0;
    }

    getRange (endpointID)
    {
        const annotation = this.endpoints.get (endpointID)?.annotation ?? {};
        return { min: annotation.min ?? 0, max: annotation.max ?? 1 };
    }

    setValue (endpointID, value)
    {
        const endpoint = this.endpoints.get (endpointID);

        if (! endpoint)
            return;

        const annotation = endpoint.annotation ?? {};

        if (annotation.boolean)
            value = value ? 1 : 0;
        else if (annotation.text)
            value = Math.round (value);

        if (annotation.min !== undefined && annotation.max !== undefined)
            value = clamp (value, annotation.min, annotation.max);

        this.values.set (endpointID, value);
        this.patchConnection.sendEventOrValue (endpointID, annotation.boolean ? value !== 0 : value);
        this.refreshControls();
    }

    beginGesture (endpointID)       { this.patchConnection.sendParameterGestureStart (endpointID); }
    endGesture (endpointID)
    {
        clearTimeout (this.gestureTimers.get (endpointID));
        this.gestureTimers.delete (endpointID);
        this.patchConnection.sendParameterGestureEnd (endpointID);
    }

    setValueAsGesture (endpointID, value)
    {
        this.beginGesture (endpointID);
        this.setValue (endpointID, value);
        this.endGesture (endpointID);
    }

    toggleValue (endpointID)
    {
        this.setValueAsGesture (endpointID, this.getValue (endpointID) ? 0 : 1);
    }

    resetToDefault (endpointID)
    {
        const init = this.endpoints.get (endpointID)?.annotation?.init;

        if (init !== undefined)
            this.setValueAsGesture (endpointID, Number (init));
    }

    /// For mouse-wheel changes, which don't have a clear start and end: the gesture
    /// ends once the wheel has been still for a moment
    nudgeValue (endpointID, value)
    {
        if (this.gestureTimers.has (endpointID))
            clearTimeout (this.gestureTimers.get (endpointID));
        else
            this.beginGesture (endpointID);

        this.setValue (endpointID, value);
        this.gestureTimers.set (endpointID, setTimeout (() => this.endGesture (endpointID), 400));
    }

    getBand (index)
    {
        const get = (suffix) => this.getValue (getBandParameterID (index, suffix));

        return {
            index,
            enabled:    !! get ("Enable"),
            type:       Math.round (get ("Type")),
            frequency:  get ("Frequency"),
            gain:       get ("Gain"),
            q:          get ("Q"),
            threshold:  get ("Threshold"),
            range:      get ("Range"),
            channel:    Math.round (get ("Channel")),
            sidechain:  Math.round (get ("Sidechain")),
            solo:       !! get ("Solo"),
            colour:     bandColours[index]
        };
    }

    //==============================================================================
    // Controls panel

    createControls()
    {
        const root = this.shadowRoot;
        const bandParam = (suffix) => () => getBandParameterID (this.selectedBand, suffix);

        // Band selector tabs
        const tabs = root.getElementById ("band-tabs");
        this.bandTabs = [];

        for (let i = 0; i < numBands; ++i)
        {
            const tab = document.createElement ("div");
            tab.className = "band-tab";
            tab.style.setProperty ("--band-colour", bandColours[i]);
            tab.innerHTML = `
                <div class="led" title="Band on/off"></div>
                <div class="band-number">${i + 1}</div>
                <div class="band-summary"></div>
                <div class="gain-meter"><div class="gain-meter-bar"></div></div>`;

            tab.addEventListener ("click", () => this.selectBand (i));
            tab.querySelector (".led").addEventListener ("click", (e) =>
            {
                e.stopPropagation();
                this.toggleValue (getBandParameterID (i, "Enable"));
            });

            tabs.appendChild (tab);
            this.bandTabs.push ({
                element: tab,
                summary: tab.querySelector (".band-summary"),
                meterBar: tab.querySelector (".gain-meter-bar")
            });
        }

        // Selected band's options
        this.bandControls = [];
        const addBandControl = (control, parent) => { root.getElementById (parent).appendChild (control.element); this.bandControls.push (control); };

        addBandControl (new OptionButtons (this, bandParam ("Type"),      ["Low Shelf", "Bell", "High Shelf"]), "type-options");
        addBandControl (new OptionButtons (this, bandParam ("Channel"),   ["Stereo", "Mid", "Side"]), "channel-options");
        addBandControl (new OptionButtons (this, bandParam ("Sidechain"), ["Split", "Wide"]), "sidechain-options");
        addBandControl (new ToggleButton (this, bandParam ("Enable"), "On"), "band-buttons");
        addBandControl (new ToggleButton (this, bandParam ("Solo"), "Solo", "solo"), "band-buttons");

        addBandControl (new Knob (this, "Freq",    bandParam ("Frequency"), { logarithmic: true, format: formatFrequency }), "eq-knobs");
        addBandControl (new Knob (this, "Gain",    bandParam ("Gain"),      { bipolar: true, format: formatDecibels }), "eq-knobs");
        addBandControl (new Knob (this, "Q",       bandParam ("Q"),         { logarithmic: true, format: (v) => v.toFixed (2) }), "eq-knobs");

        addBandControl (new Knob (this, "Thresh",  bandParam ("Threshold"), { format: (v) => v.toFixed (1) + " dB" }), "dynamics-knobs");
        addBandControl (new Knob (this, "Range",   bandParam ("Range"),     { bipolar: true, format: formatDecibels }), "dynamics-knobs");
        addBandControl (new Knob (this, "Ratio",   bandParam ("Ratio"),     { logarithmic: true, format: (v) => v.toFixed (1) + ":1" }), "dynamics-knobs");
        addBandControl (new Knob (this, "Attack",  bandParam ("Attack"),    { logarithmic: true, format: formatTime }), "dynamics-knobs");
        addBandControl (new Knob (this, "Release", bandParam ("Release"),   { logarithmic: true, format: formatTime }), "dynamics-knobs");

        // Global controls
        this.globalControls = [];
        const addGlobalControl = (control, parent) => { root.getElementById (parent).appendChild (control.element); this.globalControls.push (control); };

        addGlobalControl (new ToggleButton (this, () => "lowCutEnable", "Low Cut"), "low-cut");
        addGlobalControl (new Knob (this, "Freq", () => "lowCutFrequency", { logarithmic: true, format: formatFrequency }), "low-cut");
        addGlobalControl (new OptionButtons (this, () => "lowCutSlope", ["12", "24"]), "low-cut");

        addGlobalControl (new ToggleButton (this, () => "highCutEnable", "High Cut"), "high-cut");
        addGlobalControl (new Knob (this, "Freq", () => "highCutFrequency", { logarithmic: true, format: formatFrequency }), "high-cut");
        addGlobalControl (new OptionButtons (this, () => "highCutSlope", ["12", "24"]), "high-cut");

        addGlobalControl (new Knob (this, "Output", () => "outputGain", { bipolar: true, format: formatDecibels }), "output");

        // Analyser options: these are GUI settings rather than parameters, so they're kept
        // in the patch's stored state, which restores them when the GUI is reopened
        for (const [elementID, key] of [["analyser-options", "analyserMode"], ["slope-options", "analyserSlope"]])
        {
            root.querySelectorAll (`#${elementID} button`).forEach ((button, index) =>
            {
                button.addEventListener ("click", () =>
                {
                    this.setAnalyserOption (key, index);
                    this.patchConnection.sendStoredStateValue (key, index);
                });
            });
        }

        this.refreshAnalyserOptions();
    }

    setAnalyserOption (key, value)
    {
        const numOptions = key === "analyserMode" ? 4 : 2;

        if (Number.isInteger (value) && value >= 0 && value < numOptions)
        {
            this[key] = value;
            this.refreshAnalyserOptions();
        }
    }

    refreshAnalyserOptions()
    {
        const select = (elementID, selected) =>
            this.shadowRoot.querySelectorAll (`#${elementID} button`)
                .forEach ((button, index) => button.classList.toggle ("selected", index === selected));

        select ("analyser-options", this.analyserMode);
        select ("slope-options", this.analyserSlope);
    }

    selectBand (index)
    {
        this.selectedBand = index;
        this.refreshControls();
    }

    refreshControls()
    {
        const colour = bandColours[this.selectedBand];
        this.shadowRoot.getElementById ("band-detail").style.setProperty ("--accent", colour);
        this.shadowRoot.getElementById ("band-title").textContent = "Band " + (this.selectedBand + 1);

        for (const control of this.bandControls)
            control.refresh();

        for (const control of this.globalControls)
            control.refresh();

        for (let i = 0; i < numBands; ++i)
        {
            const band = this.getBand (i);
            const tab = this.bandTabs[i];
            tab.element.classList.toggle ("selected", i === this.selectedBand);
            tab.element.classList.toggle ("enabled", band.enabled);
            tab.element.classList.toggle ("soloed", band.solo);
            tab.summary.textContent = formatFrequency (band.frequency);
        }
    }

    //==============================================================================
    // Display geometry

    resizeCanvases()
    {
        const scale = window.devicePixelRatio || 1;

        for (const canvas of [this.canvas, this.meterCanvas])
        {
            canvas.width  = Math.round (canvas.clientWidth  * scale);
            canvas.height = Math.round (canvas.clientHeight * scale);
            canvas.getContext ("2d").setTransform (scale, 0, 0, scale, 0, 0);
        }

        this.width = this.canvas.clientWidth;
        this.height = this.canvas.clientHeight;
        this.columnSampleRate = 0;
    }

    /// The display's curves are calculated at every other pixel column
    updateColumns()
    {
        if (this.columnSampleRate === this.sampleRate && this.columnWidth === this.width)
            return;

        this.columnSampleRate = this.sampleRate;
        this.columnWidth = this.width;
        this.numColumns = Math.ceil (this.width / 2) + 1;
        this.columnX = new Float32Array (this.numColumns);
        this.columnTanW = new Float64Array (this.numColumns);

        for (let i = 0; i < this.numColumns; ++i)
        {
            const x = Math.min (i * 2, this.width);
            this.columnX[i] = x;
            this.columnTanW[i] = Math.tan (Math.PI * Math.min (this.xToFrequency (x), this.sampleRate * 0.499) / this.sampleRate);
        }
    }

    frequencyToX (frequency)    { return this.width * Math.log (frequency / minFrequency) / Math.log (maxFrequency / minFrequency); }
    xToFrequency (x)            { return minFrequency * Math.pow (maxFrequency / minFrequency, x / this.width); }

    getEQTop()                  { return 14; }
    getEQBottom()               { return this.height - 22; }
    decibelsToY (db)            { const mid = (this.getEQTop() + this.getEQBottom()) / 2; return mid - (db / eqDisplayRange) * (this.getEQBottom() - this.getEQTop()) / 2; }
    yToDecibels (y)             { const mid = (this.getEQTop() + this.getEQBottom()) / 2; return (mid - y) / ((this.getEQBottom() - this.getEQTop()) / 2) * eqDisplayRange; }
    spectrumToY (db)            { return this.height * (spectrumTop - db) / (spectrumTop - spectrumBottom); }

    //==============================================================================
    // Drawing

    updateDisplay (time)
    {
        const elapsedSeconds = Math.min (0.1, (time - this.lastFrameTime) / 1000);
        this.lastFrameTime = time;

        if (this.canvas.clientWidth !== this.width || this.canvas.clientHeight !== this.height)
            this.resizeCanvases();

        this.updateColumns();
        this.smoothLiveValues (elapsedSeconds);

        // Nothing to draw until the patch has told us about its parameters
        if (! this.hasEndpoint ("band1Frequency"))
            return;

        const bands = [];

        for (let i = 0; i < numBands; ++i)
            bands.push (this.getBand (i));

        this.drawDisplay (bands);
        this.drawSidechainMeter (bands[this.selectedBand]);

        for (let i = 0; i < numBands; ++i)
            this.updateTabMeter (i);
    }

    smoothLiveValues (elapsedSeconds)
    {
        const follow = 1 - Math.exp (-elapsedSeconds / 0.03);

        for (let i = 0; i < numBands; ++i)
        {
            this.displayedDynamicGain[i] += (this.dynamicGain[i] - this.displayedDynamicGain[i]) * follow;
            this.displayedSidechainLevel[i] += (this.sidechainLevel[i] - this.displayedSidechainLevel[i]) * follow;
        }

        // Spectrum: fast attack, slower release
        const smoothSpectrum = (target, displayed) =>
        {
            if (! target)
                return displayed;

            if (! displayed || displayed.length !== target.length)
                return Float32Array.from (target);

            const attack = 1 - Math.exp (-elapsedSeconds / 0.01);
            const release = 1 - Math.exp (-elapsedSeconds / 0.25);

            for (let i = 0; i < target.length; ++i)
                displayed[i] += (target[i] - displayed[i]) * (target[i] > displayed[i] ? attack : release);

            return displayed;
        };

        this.displayedPreSpectrum  = smoothSpectrum (this.preSpectrum, this.displayedPreSpectrum);
        this.displayedPostSpectrum = smoothSpectrum (this.postSpectrum, this.displayedPostSpectrum);
    }

    drawDisplay (bands)
    {
        const ctx = this.context;
        const w = this.width, h = this.height;

        const background = ctx.createLinearGradient (0, 0, 0, h);
        background.addColorStop (0, "#191c22");
        background.addColorStop (1, "#101216");
        ctx.fillStyle = background;
        ctx.fillRect (0, 0, w, h);

        this.drawGrid();
        this.drawSpectrum();

        // Calculate all the curves
        const n = this.numColumns;
        const curves = bands.map ((band) =>
        {
            if (! band.enabled)
                return null;

            const live = band.gain + this.displayedDynamicGain[band.index];
            const staticCoeffs = getBandCoefficients (band.type, band.frequency, band.q, band.gain, this.sampleRate);
            const rangeCoeffs  = getBandCoefficients (band.type, band.frequency, band.q, band.gain + band.range, this.sampleRate);
            const liveCoeffs   = getBandCoefficients (band.type, band.frequency, band.q, live, this.sampleRate);

            const result = { staticCurve: new Float32Array (n), rangeCurve: new Float32Array (n), liveCurve: new Float32Array (n) };

            for (let i = 0; i < n; ++i)
            {
                const tanW = this.columnTanW[i];
                result.staticCurve[i] = getSVFMagnitudeDb (staticCoeffs, tanW);
                result.rangeCurve[i]  = getSVFMagnitudeDb (rangeCoeffs, tanW);
                result.liveCurve[i]   = getSVFMagnitudeDb (liveCoeffs, tanW);
            }

            return result;
        });

        const total = new Float32Array (n);

        for (const curve of curves)
            if (curve)
                for (let i = 0; i < n; ++i)
                    total[i] += curve.liveCurve[i];

        for (const [prefix, isHighPass] of [["lowCut", true], ["highCut", false]])
        {
            if (this.getValue (prefix + "Enable"))
            {
                const frequency = this.getValue (prefix + "Frequency");
                const is24dB = this.getValue (prefix + "Slope") > 0;

                for (let i = 0; i < n; ++i)
                    total[i] += getCutMagnitudeDb (isHighPass, is24dB, frequency, this.sampleRate, this.columnTanW[i]);
            }
        }

        // Band fills: the static curve's area, and the zone the dynamics can move it through
        const zeroY = this.decibelsToY (0);

        bands.forEach ((band, index) =>
        {
            const curve = curves[index];

            if (! curve)
                return;

            const isSelected = index === this.selectedBand;

            ctx.globalAlpha = isSelected ? 0.16 : 0.08;
            this.fillBetween (curve.staticCurve, null, band.colour, zeroY);

            if (Math.abs (band.range) > 0.01)
            {
                ctx.globalAlpha = isSelected ? 0.28 : 0.15;
                this.fillBetween (curve.staticCurve, curve.rangeCurve, band.colour);
            }

            ctx.globalAlpha = 1;
        });

        // The selected band's sidechain filter
        const selected = bands[this.selectedBand];

        if (selected.enabled && selected.sidechain === 0)
        {
            const sidechainCurve = new Float32Array (n);

            for (let i = 0; i < n; ++i)
                sidechainCurve[i] = getSidechainMagnitudeDb (selected.type, selected.frequency, selected.q, this.sampleRate, this.columnTanW[i]);

            ctx.setLineDash ([4, 4]);
            ctx.globalAlpha = 0.5;
            this.strokeCurve (sidechainCurve, selected.colour, 1);
            ctx.setLineDash ([]);
            ctx.globalAlpha = 1;
        }

        // Band lines: the static curve thin and dim, the live curve brighter
        bands.forEach ((band, index) =>
        {
            const curve = curves[index];

            if (! curve)
                return;

            const isSelected = index === this.selectedBand;
            ctx.globalAlpha = isSelected ? 0.6 : 0.35;
            this.strokeCurve (curve.staticCurve, band.colour, 1);

            if (Math.abs (band.range) > 0.01)
            {
                ctx.globalAlpha = isSelected ? 0.5 : 0.25;
                ctx.setLineDash ([2, 3]);
                this.strokeCurve (curve.rangeCurve, band.colour, 1);
                ctx.setLineDash ([]);
            }

            ctx.globalAlpha = 1;
            this.strokeCurve (curve.liveCurve, band.colour, isSelected ? 2 : 1.25);
        });

        // The combined response
        ctx.shadowColor = "rgba(255, 255, 255, 0.35)";
        ctx.shadowBlur = 6;
        this.strokeCurve (total, "#f4f6fa", 2.25);
        ctx.shadowBlur = 0;

        this.drawHandles (bands);
        this.drawReadout();
    }

    drawGrid()
    {
        const ctx = this.context;
        const w = this.width, h = this.height;

        ctx.lineWidth = 1;
        ctx.font = "10px -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";

        for (const decade of [10, 100, 1000, 10000])
        {
            for (let multiple = 1; multiple < 10; ++multiple)
            {
                const frequency = decade * multiple;

                if (frequency < minFrequency || frequency > maxFrequency)
                    continue;

                const x = Math.round (this.frequencyToX (frequency)) + 0.5;
                const isMajor = multiple === 1 || multiple === 2 || multiple === 5;
                ctx.strokeStyle = isMajor ? "rgba(255, 255, 255, 0.075)" : "rgba(255, 255, 255, 0.03)";
                ctx.beginPath();
                ctx.moveTo (x, 0);
                ctx.lineTo (x, h);
                ctx.stroke();

                if (isMajor)
                {
                    ctx.fillStyle = "rgba(200, 205, 215, 0.45)";
                    ctx.textAlign = "center";
                    ctx.fillText (frequency >= 1000 ? (frequency / 1000) + "k" : String (frequency), clamp (x, 10, w - 10), h - 7);
                }
            }
        }

        for (let db = -18; db <= 18; db += 6)
        {
            const y = Math.round (this.decibelsToY (db)) + 0.5;
            ctx.strokeStyle = db === 0 ? "rgba(255, 255, 255, 0.16)" : "rgba(255, 255, 255, 0.06)";
            ctx.beginPath();
            ctx.moveTo (0, y);
            ctx.lineTo (w, y);
            ctx.stroke();

            ctx.fillStyle = "rgba(200, 205, 215, 0.45)";
            ctx.textAlign = "left";
            ctx.fillText ((db > 0 ? "+" : "") + db, 5, y - 3);
        }

        // Spectrum scale on the right
        ctx.textAlign = "right";
        ctx.fillStyle = "rgba(140, 160, 190, 0.45)";

        for (let db = -80; db <= -10; db += 20)
            ctx.fillText (String (db), w - 5, this.spectrumToY (db) + 3);

        // With a slope applied, that scale is only exact at the pivot frequency
        if (this.analyserSlope === 1)
            ctx.fillText (`+${musicModeSlope} dB/oct @ 1k`, w - 5, this.spectrumToY (0) + 13);
    }

    drawSpectrum()
    {
        const ctx = this.context;
        const showPre  = (this.analyserMode & 1) !== 0 && this.displayedPreSpectrum;
        const showPost = (this.analyserMode & 2) !== 0 && this.displayedPostSpectrum;

        const slope = this.analyserSlope === 1 ? musicModeSlope : 0;

        const tracePath = (levels) =>
        {
            const numPoints = levels.length;
            ctx.beginPath();

            for (let i = 0; i < numPoints; ++i)
            {
                const x = this.width * i / (numPoints - 1);
                const tilt = slope * Math.log2 (this.xToFrequency (x) / slopePivotFrequency);
                const y = clamp (this.spectrumToY (levels[i] + tilt), 0, this.height);

                if (i === 0)
                    ctx.moveTo (x, y);
                else
                    ctx.lineTo (x, y);
            }
        };

        if (showPre)
        {
            tracePath (this.displayedPreSpectrum);
            ctx.lineTo (this.width, this.height);
            ctx.lineTo (0, this.height);
            ctx.closePath();

            const gradient = ctx.createLinearGradient (0, 0, 0, this.height);
            gradient.addColorStop (0, "rgba(110, 140, 190, 0.55)");
            gradient.addColorStop (1, "rgba(60, 80, 120, 0.12)");
            ctx.fillStyle = gradient;
            ctx.fill();
        }

        if (showPost)
        {
            tracePath (this.displayedPostSpectrum);
            ctx.strokeStyle = "rgba(205, 220, 245, 0.55)";
            ctx.lineWidth = 1;
            ctx.stroke();
        }
    }

    traceCurve (curve)
    {
        const ctx = this.context;

        for (let i = 0; i < this.numColumns; ++i)
        {
            const y = clamp (this.decibelsToY (curve[i]), -10, this.height + 10);

            if (i === 0)
                ctx.moveTo (this.columnX[i], y);
            else
                ctx.lineTo (this.columnX[i], y);
        }
    }

    strokeCurve (curve, colour, lineWidth)
    {
        const ctx = this.context;
        ctx.beginPath();
        this.traceCurve (curve);
        ctx.strokeStyle = colour;
        ctx.lineWidth = lineWidth;
        ctx.stroke();
    }

    /// Fills the area between two curves, or between a curve and a horizontal line
    fillBetween (curve1, curve2, colour, baselineY)
    {
        const ctx = this.context;
        ctx.beginPath();
        this.traceCurve (curve1);

        if (curve2)
        {
            for (let i = this.numColumns - 1; i >= 0; --i)
                ctx.lineTo (this.columnX[i], clamp (this.decibelsToY (curve2[i]), -10, this.height + 10));
        }
        else
        {
            ctx.lineTo (this.width, baselineY);
            ctx.lineTo (0, baselineY);
        }

        ctx.closePath();
        ctx.fillStyle = colour;
        ctx.fill();
    }

    drawHandles (bands)
    {
        const ctx = this.context;
        this.handles = [];

        // Low and high cut handles sit at their -3dB points
        for (const [prefix, label] of [["lowCut", "LC"], ["highCut", "HC"]])
        {
            const enabled = !! this.getValue (prefix + "Enable");
            const x = this.frequencyToX (this.getValue (prefix + "Frequency"));
            const y = this.decibelsToY (-3);
            const isHovered = this.hoveredHandle?.kind === "cut" && this.hoveredHandle.prefix === prefix;

            ctx.globalAlpha = enabled ? 1 : 0.4;
            ctx.fillStyle = "#2a2f38";
            ctx.strokeStyle = isHovered ? "#ffffff" : "#c8ced8";
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.rect (x - 9, y - 7, 18, 14);
            ctx.fill();
            ctx.stroke();
            ctx.fillStyle = "#e8ecf2";
            ctx.font = "bold 8px -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";
            ctx.textAlign = "center";
            ctx.fillText (label, x, y + 3);
            ctx.globalAlpha = 1;

            this.handles.push ({ kind: "cut", prefix, x, y, radius: 10 });
        }

        // Draw the selected band last so that it's on top
        const order = bands.map ((b) => b.index).filter ((i) => i !== this.selectedBand).concat ([this.selectedBand]);

        for (const index of order)
        {
            const band = bands[index];
            const x = this.frequencyToX (band.frequency);
            const y = this.decibelsToY (band.gain);
            const isSelected = index === this.selectedBand;
            const isHovered = this.hoveredHandle?.band === index;

            if (band.enabled && Math.abs (band.range) > 0.01)
            {
                // A line to the range handle, with a dot showing where the dynamics currently are
                const rangeY = this.decibelsToY (band.gain + band.range);
                const liveY = this.decibelsToY (band.gain + this.displayedDynamicGain[index]);

                ctx.strokeStyle = band.colour;
                ctx.globalAlpha = 0.6;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo (x, y);
                ctx.lineTo (x, rangeY);
                ctx.stroke();
                ctx.globalAlpha = 1;

                ctx.fillStyle = band.colour;
                ctx.beginPath();
                ctx.moveTo (x, rangeY - 6);
                ctx.lineTo (x + 6, rangeY);
                ctx.lineTo (x, rangeY + 6);
                ctx.lineTo (x - 6, rangeY);
                ctx.closePath();
                ctx.fill();

                ctx.fillStyle = "#ffffff";
                ctx.beginPath();
                ctx.arc (x, liveY, 2.5, 0, Math.PI * 2);
                ctx.fill();

                this.handles.push ({ kind: "range", band: index, x, y: rangeY, radius: 8 });
            }

            const radius = isSelected ? 10 : 9;

            ctx.beginPath();
            ctx.arc (x, y, radius, 0, Math.PI * 2);

            if (band.enabled)
            {
                ctx.fillStyle = band.colour;
                ctx.fill();
            }
            else
            {
                ctx.fillStyle = "rgba(30, 33, 40, 0.9)";
                ctx.fill();
                ctx.strokeStyle = band.colour;
                ctx.lineWidth = 1.5;
                ctx.stroke();
            }

            if (isSelected || isHovered)
            {
                ctx.strokeStyle = "#ffffff";
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.arc (x, y, radius + 2.5, 0, Math.PI * 2);
                ctx.stroke();
            }

            if (band.solo)
            {
                ctx.strokeStyle = "#ffe066";
                ctx.setLineDash ([3, 2]);
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                ctx.arc (x, y, radius + 6, 0, Math.PI * 2);
                ctx.stroke();
                ctx.setLineDash ([]);
            }

            ctx.fillStyle = band.enabled ? "#15171b" : band.colour;
            ctx.font = "bold 11px -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";
            ctx.textAlign = "center";
            ctx.fillText (String (index + 1), x, y + 4);

            // M or S badge for mid/side bands
            if (band.channel !== 0)
            {
                ctx.fillStyle = "#e8ecf2";
                ctx.font = "bold 8px -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";
                ctx.fillText (band.channel === 1 ? "M" : "S", x + radius + 5, y - radius + 2);
            }

            this.handles.push ({ kind: "band", band: index, x, y, radius: radius + 3 });
        }
    }

    drawReadout()
    {
        const ctx = this.context;
        ctx.font = "11px -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";

        let text = null;

        if (this.hoveredHandle?.kind === "band" || this.hoveredHandle?.kind === "range")
        {
            const band = this.getBand (this.hoveredHandle.band);
            text = `Band ${band.index + 1} · ${bandTypeNames[band.type]} · ${formatFrequency (band.frequency)} · `
                 + `${formatDecibels (band.gain)} · Q ${band.q.toFixed (2)} · range ${formatDecibels (band.range)}`;
        }
        else if (this.hoveredHandle?.kind === "cut")
        {
            const prefix = this.hoveredHandle.prefix;
            text = `${prefix === "lowCut" ? "Low" : "High"} cut · ${formatFrequency (this.getValue (prefix + "Frequency"))} · `
                 + `${this.getValue (prefix + "Slope") > 0 ? 24 : 12} dB/oct${this.getValue (prefix + "Enable") ? "" : " (off)"}`;
        }
        else if (this.mousePosition)
        {
            text = `${formatFrequency (this.xToFrequency (this.mousePosition.x))} · ${formatDecibels (this.yToDecibels (this.mousePosition.y))}`;
        }

        if (text)
        {
            const width = ctx.measureText (text).width + 16;
            ctx.fillStyle = "rgba(10, 12, 15, 0.75)";
            ctx.fillRect (this.width - width - 30, 6, width, 20);
            ctx.fillStyle = "#e3e7ee";
            ctx.textAlign = "left";
            ctx.fillText (text, this.width - width - 22, 20);
        }

        ctx.fillStyle = "rgba(200, 205, 215, 0.3)";
        ctx.textAlign = "left";
        ctx.fillText ("drag: freq/gain · alt-drag: range · wheel: Q · double-click: on/off · shift: fine", 30, this.height - 7 - 14);
    }

    //==============================================================================
    // Display mouse handling

    findHandle (x, y)
    {
        // later handles are drawn on top, so search backwards
        for (let i = this.handles.length - 1; i >= 0; --i)
        {
            const handle = this.handles[i];

            if (Math.hypot (handle.x - x, handle.y - y) <= handle.radius)
                return handle;
        }

        return null;
    }

    getMousePosition (e)
    {
        const bounds = this.canvas.getBoundingClientRect();
        return { x: e.clientX - bounds.left, y: e.clientY - bounds.top };
    }

    setupDisplayMouseHandling()
    {
        const canvas = this.canvas;

        canvas.addEventListener ("pointerdown", (e) =>
        {
            const position = this.getMousePosition (e);
            const handle = this.findHandle (position.x, position.y);

            if (! handle)
                return;

            e.preventDefault();
            canvas.setPointerCapture (e.pointerId);

            let endpointIDs;

            if (handle.kind === "cut")
            {
                endpointIDs = [handle.prefix + "Frequency"];
            }
            else
            {
                this.selectBand (handle.band);

                if (handle.kind === "range" || e.altKey)
                    endpointIDs = [getBandParameterID (handle.band, "Range")];
                else
                    endpointIDs = [getBandParameterID (handle.band, "Frequency"), getBandParameterID (handle.band, "Gain")];
            }

            const isRangeDrag = handle.kind === "range" || (handle.kind === "band" && e.altKey);

            this.dragState = {
                handle,
                isRangeDrag,
                endpointIDs,
                wasFine: e.shiftKey,
                startMouse: position,
                startX: handle.kind === "cut" ? handle.x : this.frequencyToX (this.getValue (getBandParameterID (handle.band, "Frequency"))),
                startY: isRangeDrag ? this.decibelsToY (this.getBand (handle.band).gain + this.getBand (handle.band).range) : handle.y
            };

            for (const endpointID of endpointIDs)
                this.beginGesture (endpointID);
        });

        canvas.addEventListener ("pointermove", (e) =>
        {
            const position = this.getMousePosition (e);
            this.mousePosition = position;

            if (! this.dragState)
            {
                this.hoveredHandle = this.findHandle (position.x, position.y);
                canvas.style.cursor = this.hoveredHandle ? (this.hoveredHandle.kind === "range" ? "ns-resize" : "grab") : "crosshair";
                return;
            }

            const drag = this.dragState;
            const scale = e.shiftKey ? 0.15 : 1;
            const x = drag.startX + (position.x - drag.startMouse.x) * scale;
            const y = drag.startY + (position.y - drag.startMouse.y) * scale;

            // Restart the reference point when shift is pressed or released, so the handle doesn't jump
            if (drag.wasFine !== e.shiftKey)
            {
                drag.wasFine = e.shiftKey;
                drag.startMouse = position;
                drag.startX = drag.lastX ?? drag.startX;
                drag.startY = drag.lastY ?? drag.startY;
                return;
            }

            drag.lastX = x;
            drag.lastY = y;

            if (drag.handle.kind === "cut")
            {
                this.setValue (drag.endpointIDs[0], this.xToFrequency (x));
            }
            else if (drag.isRangeDrag)
            {
                const band = this.getBand (drag.handle.band);
                this.setValue (drag.endpointIDs[0], this.yToDecibels (y) - band.gain);
            }
            else
            {
                this.setValue (drag.endpointIDs[0], this.xToFrequency (x));
                this.setValue (drag.endpointIDs[1], this.yToDecibels (y));
            }
        });

        const endDrag = () =>
        {
            if (this.dragState)
            {
                for (const endpointID of this.dragState.endpointIDs)
                    this.endGesture (endpointID);

                this.dragState = null;
            }
        };

        canvas.addEventListener ("pointerup", endDrag);
        canvas.addEventListener ("lostpointercapture", endDrag);

        canvas.addEventListener ("pointerleave", () =>
        {
            if (! this.dragState)
            {
                this.mousePosition = null;
                this.hoveredHandle = null;
            }
        });

        canvas.addEventListener ("dblclick", (e) =>
        {
            const position = this.getMousePosition (e);
            const handle = this.findHandle (position.x, position.y);

            if (handle?.kind === "cut")
                this.toggleValue (handle.prefix + "Enable");
            else if (handle?.kind === "band")
                this.toggleValue (getBandParameterID (handle.band, "Enable"));
        });

        canvas.addEventListener ("wheel", (e) =>
        {
            e.preventDefault();
            const position = this.getMousePosition (e);
            const handle = this.findHandle (position.x, position.y);
            const bandIndex = (handle && handle.kind !== "cut") ? handle.band : this.selectedBand;
            const endpointID = getBandParameterID (bandIndex, "Q");
            const amount = e.shiftKey ? 0.0005 : 0.002;

            this.nudgeValue (endpointID, this.getValue (endpointID) * Math.exp (-e.deltaY * amount));
        }, { passive: false });
    }

    //==============================================================================
    // Sidechain meter: the selected band's detector level, with a draggable threshold,
    // and the gain change that the dynamics are applying

    getMeterGeometry()
    {
        const height = this.meterCanvas.clientHeight;
        return { top: 6, bottom: height - 14, width: this.meterCanvas.clientWidth };
    }

    levelToMeterY (db)
    {
        const { top, bottom } = this.getMeterGeometry();
        return bottom - (clamp (db, sidechainMeterMin, 0) - sidechainMeterMin) / -sidechainMeterMin * (bottom - top);
    }

    meterYToLevel (y)
    {
        const { top, bottom } = this.getMeterGeometry();
        return sidechainMeterMin + (bottom - y) / (bottom - top) * -sidechainMeterMin;
    }

    drawSidechainMeter (band)
    {
        const ctx = this.meterContext;
        const { top, bottom, width } = this.getMeterGeometry();
        const height = this.meterCanvas.clientHeight;

        ctx.clearRect (0, 0, width, height);

        // Level meter
        const meterX = 4, meterWidth = 14;
        ctx.fillStyle = "#0c0e11";
        ctx.fillRect (meterX, top, meterWidth, bottom - top);

        const levelY = this.levelToMeterY (this.displayedSidechainLevel[band.index]);
        const gradient = ctx.createLinearGradient (0, bottom, 0, top);
        gradient.addColorStop (0, "#2b8a3e");
        gradient.addColorStop (0.7, "#94d82d");
        gradient.addColorStop (1, "#ffd43b");
        ctx.fillStyle = gradient;
        ctx.fillRect (meterX, levelY, meterWidth, bottom - levelY);

        // Threshold marker
        const thresholdY = this.levelToMeterY (band.threshold);
        ctx.fillStyle = band.colour;
        ctx.fillRect (meterX - 3, thresholdY - 1, meterWidth + 6, 2);
        ctx.beginPath();
        ctx.moveTo (meterX + meterWidth + 3, thresholdY);
        ctx.lineTo (meterX + meterWidth + 9, thresholdY - 5);
        ctx.lineTo (meterX + meterWidth + 9, thresholdY + 5);
        ctx.closePath();
        ctx.fill();

        // Gain change meter, centred on 0dB
        const grX = 34, grWidth = 10;
        const centreY = (top + bottom) / 2;
        const gainToY = (db) => centreY - clamp (db, -18, 18) / 18 * (bottom - top) / 2;
        const gainChange = this.displayedDynamicGain[band.index];

        ctx.fillStyle = "#0c0e11";
        ctx.fillRect (grX, top, grWidth, bottom - top);
        ctx.fillStyle = gainChange < 0 ? "#ff922b" : "#51cf66";
        const grY = gainToY (gainChange);
        ctx.fillRect (grX, Math.min (grY, centreY), grWidth, Math.abs (grY - centreY));

        // Range limit
        ctx.fillStyle = "rgba(255, 255, 255, 0.35)";
        ctx.fillRect (grX - 2, gainToY (band.range) - 0.5, grWidth + 4, 1);
        ctx.fillStyle = "rgba(255, 255, 255, 0.2)";
        ctx.fillRect (grX, centreY - 0.5, grWidth, 1);

        ctx.font = "9px -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif";
        ctx.textAlign = "left";
        ctx.fillStyle = "rgba(200, 205, 215, 0.6)";
        ctx.fillText ("SC", meterX + 1, height - 3);
        ctx.fillText ("GR", grX - 1, height - 3);

        const sidechainLevel = this.displayedSidechainLevel[band.index];
        this.shadowRoot.getElementById ("meter-readout-sidechain").textContent = `SC ${sidechainLevel <= -99 ? "-inf" : sidechainLevel.toFixed (1)} dB`;
        this.shadowRoot.getElementById ("meter-readout-gain").textContent = `GR ${formatDecibels (gainChange)}`;
    }

    setupMeterMouseHandling()
    {
        const canvas = this.meterCanvas;
        let isDragging = false;

        const setThreshold = (e) =>
        {
            const bounds = canvas.getBoundingClientRect();
            const level = this.meterYToLevel (e.clientY - bounds.top);
            this.setValue (getBandParameterID (this.selectedBand, "Threshold"), level);
        };

        canvas.addEventListener ("pointerdown", (e) =>
        {
            e.preventDefault();
            canvas.setPointerCapture (e.pointerId);
            isDragging = true;
            this.dragThresholdID = getBandParameterID (this.selectedBand, "Threshold");
            this.beginGesture (this.dragThresholdID);
            setThreshold (e);
        });

        canvas.addEventListener ("pointermove", (e) => { if (isDragging) setThreshold (e); });

        const endDrag = () =>
        {
            if (isDragging)
            {
                isDragging = false;
                this.endGesture (this.dragThresholdID);
            }
        };

        canvas.addEventListener ("pointerup", endDrag);
        canvas.addEventListener ("lostpointercapture", endDrag);
    }

    updateTabMeter (index)
    {
        // Gain change, shown growing left (cut) or right (boost) from the centre
        const gainChange = this.displayedDynamicGain[index];
        const proportion = clamp (Math.abs (gainChange) / 18, 0, 1) * 50;
        const bar = this.bandTabs[index].meterBar;

        bar.style.left = (gainChange < 0 ? 50 - proportion : 50) + "%";
        bar.style.width = proportion + "%";
        bar.style.background = gainChange < 0 ? "#ff922b" : "#51cf66";
    }

    //==============================================================================
    getHTML()
    {
        return `
        <style>
            :host {
                display: block;
                width: 1000px;
                height: 640px;
                background: #14161a;
                color: #cfd3da;
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
                font-size: 11px;
                user-select: none;
                -webkit-user-select: none;
                overflow: hidden;
            }

            * { box-sizing: border-box; }

            button {
                font-family: inherit;
                font-size: 10px;
                color: #aeb4bf;
                background: #1f2228;
                border: 1px solid #30353e;
                border-radius: 3px;
                padding: 3px 6px;
                cursor: pointer;
            }

            button:hover { color: #e6e9ee; border-color: #464c57; }

            .header {
                height: 40px;
                display: flex;
                align-items: center;
                gap: 14px;
                padding: 0 14px;
                background: linear-gradient(#1d2026, #16181d);
                border-bottom: 1px solid #262a31;
            }

            /* The logo image is black on transparent, so it's used as a mask over a
               background colour, which lets it be drawn in any colour */
            .logo {
                width: 61px;
                height: 18px;
                flex-shrink: 0;
                background-color: #cfd3da;
                -webkit-mask: var(--logo-url) center / contain no-repeat;
                mask: var(--logo-url) center / contain no-repeat;
            }

            .header-divider {
                width: 1px;
                height: 18px;
                background: #3a3f48;
            }

            .title {
                font-size: 13px;
                font-weight: 700;
                letter-spacing: 0.18em;
                color: #eceef2;
            }

            .title span { color: #ffa94d; }

            .subtitle { color: #6c7380; font-size: 11px; }

            .spacer { flex: 1; }

            .header-label {
                color: #7d8490;
                font-size: 10px;
                letter-spacing: 0.08em;
                text-transform: uppercase;
            }

            .options { display: flex; }

            .options button {
                border-radius: 0;
                margin-left: -1px;
                flex: 1;
                white-space: nowrap;
            }

            .options button:first-child { border-radius: 3px 0 0 3px; margin-left: 0; }
            .options button:last-child  { border-radius: 0 3px 3px 0; }

            .options button.selected {
                background: #3a404b;
                color: #ffffff;
                border-color: #555c69;
                position: relative;
            }

            #display {
                display: block;
                width: 1000px;
                height: 390px;
                cursor: crosshair;
                touch-action: none;
            }

            .panel {
                height: 210px;
                display: flex;
                flex-direction: column;
                padding: 10px 12px;
                gap: 10px;
                border-top: 1px solid #262a31;
                background: #16181c;
            }

            .panel-body {
                flex: 1;
                display: flex;
                gap: 12px;
            }

            #band-tabs { display: flex; gap: 6px; }

            .band-tab {
                flex: 1;
                height: 30px;
                display: flex;
                align-items: center;
                gap: 6px;
                padding: 0 8px;
                border-radius: 4px;
                background: #1c1f25;
                border: 1px solid #2a2e36;
                cursor: pointer;
            }

            .band-tab:hover { border-color: #3c424d; }

            .band-tab.selected {
                background: #23272e;
                border-color: var(--band-colour);
            }

            .band-tab.soloed { box-shadow: inset 0 0 0 1px #ffe066; }

            .band-tab .led {
                width: 9px;
                height: 9px;
                border-radius: 50%;
                background: #3a3e46;
                border: 1px solid var(--band-colour);
                flex-shrink: 0;
            }

            .band-tab.enabled .led {
                background: var(--band-colour);
                box-shadow: 0 0 5px var(--band-colour);
            }

            .band-number { color: var(--band-colour); font-weight: 700; font-size: 12px; }
            .band-tab:not(.enabled) .band-number { opacity: 0.5; }

            .band-summary { color: #8a919d; font-size: 10px; width: 52px; white-space: nowrap; }

            .gain-meter {
                flex: 1;
                height: 4px;
                background: #0e1013;
                border-radius: 2px;
                position: relative;
                overflow: hidden;
            }

            .gain-meter-bar { position: absolute; top: 0; bottom: 0; }

            #band-detail {
                --accent: #ffd43b;
                flex: 1;
                display: flex;
                gap: 14px;
            }

            .band-options {
                width: 170px;
                display: flex;
                flex-direction: column;
                gap: 5px;
            }

            .band-header {
                display: flex;
                align-items: center;
                gap: 8px;
            }

            #band-title {
                flex: 1;
                color: var(--accent);
                font-weight: 700;
                font-size: 12px;
                letter-spacing: 0.05em;
            }

            .option-label {
                color: #6c7380;
                font-size: 9px;
                text-transform: uppercase;
                letter-spacing: 0.08em;
                margin-top: 2px;
            }

            #band-buttons { display: flex; gap: 4px; }
            #band-buttons button { width: 42px; }

            .toggle.on {
                background: #3a404b;
                color: #ffffff;
                border-color: var(--accent, #8ce99a);
            }

            .toggle.solo.on {
                background: #5c4d12;
                border-color: #ffe066;
                color: #ffe066;
            }

            .section {
                display: flex;
                flex-direction: column;
                gap: 4px;
            }

            .section-title {
                color: #6c7380;
                font-size: 9px;
                text-transform: uppercase;
                letter-spacing: 0.1em;
                border-bottom: 1px solid #262a31;
                padding-bottom: 3px;
            }

            .knob-row { display: flex; gap: 2px; }

            .knob {
                width: 58px;
                display: flex;
                flex-direction: column;
                align-items: center;
            }

            .knob-label { color: #8a919d; font-size: 10px; margin-bottom: 2px; }

            .knob-dial {
                width: 48px;
                height: 48px;
                cursor: ns-resize;
                touch-action: none;
            }

            .knob-track { fill: none; stroke: #2b3038; stroke-width: 5; stroke-linecap: round; }
            .knob-arc   { fill: none; stroke: var(--accent, #8ce99a); stroke-width: 5; stroke-linecap: round; }
            .knob-cap   { fill: #262a31; stroke: #3a3f48; stroke-width: 1; }
            .knob-pointer { stroke: #e8ebf0; stroke-width: 2.5; stroke-linecap: round; }

            .knob-value {
                color: #dfe3ea;
                font-size: 10px;
                margin-top: 1px;
                white-space: nowrap;
                font-variant-numeric: tabular-nums;
            }

            .meter-section { width: 70px; }

            #sidechain-meter {
                width: 60px;
                height: 104px;
                cursor: ns-resize;
                touch-action: none;
            }

            #meter-readout {
                color: #8a919d;
                font-size: 9px;
                line-height: 11px;
                white-space: nowrap;
                overflow: hidden;
                font-variant-numeric: tabular-nums;
            }

            .global-panel {
                width: 202px;
                border-left: 1px solid #262a31;
                padding-left: 12px;
                display: flex;
                flex-direction: column;
                gap: 4px;
                --accent: #dee2e6;
            }

            /* A grid, so that the three knobs share a row (the output section has no
               toggle above its knob), and the slope buttons sit at the bottom of the panel */
            .global-row {
                flex: 1;
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                grid-template-rows: auto auto 1fr;
                gap: 4px 6px;
                justify-items: center;
            }

            .cut-section { display: contents; }

            .cut-section > :nth-child(1) { grid-row: 1; }
            .cut-section > :nth-child(2) { grid-row: 2; }
            .cut-section > :nth-child(3) { grid-row: 3; }

            #low-cut > *  { grid-column: 1; }
            #high-cut > * { grid-column: 2; }
            #output > *   { grid-column: 3; grid-row: 2; }

            .cut-section .toggle { width: 100%; }
            .cut-section .options { width: 100%; align-self: end; }
        </style>

        <div class="header">
            <div class="logo" role="img" aria-label="Cmajor"
                 style="--logo-url: url('${this.patchConnection.getResourceAddress ("ui/cmajor-logo.png")}')"></div>
            <div class="header-divider"></div>
            <div class="title">DYNAMIC <span>EQ</span></div>
            <div class="subtitle">6-band floating dynamic equaliser</div>
            <div class="spacer"></div>
            <div class="header-label">Analyser</div>
            <div class="options" id="analyser-options">
                <button>Off</button><button>Pre</button><button>Post</button><button>Pre + Post</button>
            </div>
            <div class="options" id="slope-options">
                <button title="No slope: white noise and impulses look flat">Flat</button>
                <button title="Tilts the spectrum by +3dB/octave around 1kHz, so that pink noise (and the average spectrum of most music) looks flat">Music</button>
            </div>
        </div>

        <canvas id="display"></canvas>

        <div class="panel">
            <div id="band-tabs"></div>

            <div class="panel-body">
                <div id="band-detail">
                    <div class="band-options">
                        <div class="band-header">
                            <div id="band-title">Band 3</div>
                            <div id="band-buttons"></div>
                        </div>
                        <div class="option-label">Type</div>
                        <div id="type-options"></div>
                        <div class="option-label">Channel</div>
                        <div id="channel-options"></div>
                        <div class="option-label">Sidechain</div>
                        <div id="sidechain-options"></div>
                    </div>

                    <div class="section">
                        <div class="section-title">EQ</div>
                        <div class="knob-row" id="eq-knobs"></div>
                    </div>

                    <div class="section">
                        <div class="section-title">Dynamics</div>
                        <div class="knob-row" id="dynamics-knobs"></div>
                    </div>

                    <div class="section meter-section">
                        <div class="section-title">Detector</div>
                        <canvas id="sidechain-meter" title="Drag to set the threshold"></canvas>
                        <div id="meter-readout">
                            <div id="meter-readout-sidechain"></div>
                            <div id="meter-readout-gain"></div>
                        </div>
                    </div>
                </div>

                <div class="global-panel">
                    <div class="section-title">Filters &amp; Output</div>
                    <div class="global-row">
                        <div class="cut-section" id="low-cut"></div>
                        <div class="cut-section" id="high-cut"></div>
                        <div class="cut-section" id="output"></div>
                    </div>
                </div>
            </div>
        </div>`;
    }
}

//==============================================================================
export default function createPatchView (patchConnection)
{
    const customElementName = "dynamic-eq-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, DynamicEQView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
