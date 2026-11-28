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
    The TX81Z's GUI, laid out like the keyboard versions of the 4-operator FM synths: a panel
    with the pitch-bend and modulation wheels above the left end of the keyboard, a 2x16
    character LCD and an algorithm chart, and a strip of controls for each operator.

    - Drag a knob or fader to change it (shift for fine control), double-click to reset it,
      or use the scroll-wheel. The LCD shows the parameter and its value while you do.
    - Hovering over a control shows its current value on the LCD.
    - Click an algorithm in the chart to select it.
    - Double-click the LCD to edit the voice name; Enter to keep it, Escape to cancel.
    - The PROGRAM buttons step through the 16 voices in the preset bank, wrapping round at
      either end.
    - Play the keyboard with the mouse (the lower down a key you click, the louder the note),
      or with the computer keyboard: A-W-S-E-D... plays from C, and Z and X change octave.

    The voice name and program number are kept in the patch's stored state, so hosts save
    them with the parameters.

    Voices are loaded by the patch's worker (worker/worker.js), which runs even when this GUI
    isn't open. It responds to MIDI program changes, so the program buttons just send one of
    those into the patch's MIDI input, as a MIDI controller would.
*/

import { parameters, parametersByID, operatorCount, getOperatorParameterID, updateFromStatus,
         toNormalised, formatValue, getWaveshape } from "./params.js";
import { DotMatrixLCD, isDisplayableCharacter } from "./lcd.js";
import { Knob, Slider, Wheel, Keyboard, PushButton } from "./controls.js";
import { AlgorithmChart, isCarrier } from "./algorithms.js";
import { wordmarkPath, wordmarkBounds } from "./cmajor-wordmark.js";
import { isValidProgramNumber, getProgramNumberAfterStep } from "./presets/presetBank.js";

// The panel's layout, from top to bottom: the controls end level with the bottom of the
// algorithm chart, and the keyboard sits just below them
const designWidth = 1430, sideMargin = 24;
const controlsTop = 84, controlsHeight = 406;

// The top parts of the master and voice sections have the same fixed height, so that the
// PROGRAM and ALGORITHM titles below them line up whatever size the text comes out
const upperBlockHeight = 152;
const keybedTop = controlsTop + controlsHeight + 16, keybedHeight = 244;
const designHeight = keybedTop + keybedHeight + 18;
const lcdColumns = 16;
// Voice names are up to 10 characters, as on the hardware
const voiceNameLength = 10;
const defaultVoiceName = "LatelyBass";
const lcdHoldTime = 1800;

// When a program change loads a new voice, its parameter changes arrive just afterwards, and
// shouldn't each flash up on the LCD
const programChangeQuietTime = 500;

// The patch's MIDI converter bends by up to 48 semitones, so the wheel only uses a
// part of its range, to give the TX81Z's default bend range of +/- 2 semitones
const midiBendRange = 48, wheelBendRange = 2;

const computerKeyboardNotes = "awsedftgyhujkolp;'";

// These aren't parameters, but the wheels show their positions on the LCD like one
const pitchBendDisplay  = { lcdTitle: "Pitch Bend", min: -1, max: 1,   bipolar: true, format: v => (v >= 0 ? "+" : "") + (v * wheelBendRange).toFixed (2) };
const modulationDisplay = { lcdTitle: "Modulation", min: 0,  max: 127, format: v => String (v) };

//==============================================================================
class TX81ZView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;
        this.controls = new Map();
        this.envelopeDisplays = [];
        this.carrierTags = [];
        this.voiceName = defaultVoiceName;
        this.currentProgram = 1;
        this.lastProgramChangeTime = -Infinity;
        this.keyboardOctave = 0;
        this.heldComputerKeys = new Map();
        this.activeGestures = 0;
        this.hoveredParameter = null;

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();
        this.panel = this.shadowRoot.querySelector (".panel");

        // Take the keyboard focus when clicked, so the computer keyboard can play notes
        this.addEventListener ("pointerdown", () => this.focus ({ preventScroll: true }));

        this.createControls();
    }

    connectedCallback()
    {
        // createPatchView() pins the view to the manifest's width and height, which hosts use as
        // its initial size. This view scales to fit any size, so it fills its container instead.
        this.style.width = "100%";
        this.style.height = "100%";

        this.resizeObserver = new ResizeObserver (() => this.updateScale());
        this.resizeObserver.observe (this);
        this.updateScale();

        this.tabIndex = 0;
        this.keyDownListener = (e) => this.handleKeyDown (e);
        this.keyUpListener = (e) => this.handleKeyUp (e);
        this.blurListener = () => this.releaseComputerKeys();
        this.addEventListener ("keydown", this.keyDownListener);
        this.addEventListener ("keyup", this.keyUpListener);
        this.addEventListener ("blur", this.blurListener);

        this.addPatchListeners();
        this.showIdleDisplay();
    }

    disconnectedCallback()
    {
        this.removePatchListeners();
        this.resizeObserver?.disconnect();
        this.removeEventListener ("keydown", this.keyDownListener);
        this.removeEventListener ("keyup", this.keyUpListener);
        this.removeEventListener ("blur", this.blurListener);
        this.releaseComputerKeys();
        clearTimeout (this.idleTimer);
        clearTimeout (this.midiLightTimer);
        this.lcd.dispose();
    }

    updateScale()
    {
        const width = this.clientWidth, height = this.clientHeight;

        if (width > 0 && height > 0)
        {
            const scale = Math.min (width / designWidth, height / designHeight);
            const x = (width - designWidth * scale) / 2;
            const y = (height - designHeight * scale) / 2;
            this.panel.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
        }
    }

    //==============================================================================
    // Talking to the patch

    addPatchListeners()
    {
        const pc = this.patchConnection;

        this.parameterListener = ({ endpointID, value }) =>
        {
            const param = parametersByID.get (endpointID);

            if (param && typeof value === "number")
            {
                // Only show a change on the LCD if it's a new value for a parameter we've already
                // heard about, so that the initial values don't all flash past when the GUI opens
                const isChange = param.hasValue && value !== param.value;
                param.value = value;
                param.hasValue = true;
                this.controls.get (endpointID)?.setValue (value);
                this.updateDependentDisplays (param);

                if (isChange && ! this.isLoadingProgram())
                    this.showParameter (param, value);
            }
        };

        this.statusListener = (status) =>
        {
            updateFromStatus (status);

            for (const param of parameters)
                this.controls.get (param.id)?.setValue (param.value);
        };

        this.stateListener = ({ key, value }) =>
        {
            if (key === "voiceName" && typeof value === "string" && ! this.nameEdit)
            {
                this.voiceName = value.substring (0, voiceNameLength);
                this.showIdleDisplay();
            }

            if (key === "currentProgram" && isValidProgramNumber (value))
            {
                this.currentProgram = value;
                this.showIdleDisplay();
            }
        };

        // Light up the keys for notes that arrive from elsewhere, e.g. a MIDI keyboard
        this.midiListener = ({ message }) =>
        {
            const status = (message >> 16) & 0xf0, note = (message >> 8) & 0x7f, velocity = message & 0x7f;

            if (status === 0x90 && velocity > 0)
                this.keyboard.setNoteHeld (note, "midi", true);
            else if (status === 0x80 || status === 0x90)
                this.keyboard.setNoteHeld (note, "midi", false);

            if (status === 0xc0)
            {
                // The worker is about to load the new voice, so go back to showing the
                // program, and keep its parameter changes off the LCD
                const programNumber = ((message >> 8) & 0x7f) + 1;

                if (isValidProgramNumber (programNumber))
                    this.currentProgram = programNumber;

                this.lastProgramChangeTime = performance.now();
                this.showIdleDisplay();
            }

            if (status === 0x80 || status === 0x90 || status === 0xc0)
                this.flashMIDILight();
        };

        pc.addAllParameterListener (this.parameterListener);
        pc.addStatusListener (this.statusListener);
        pc.addStoredStateValueListener (this.stateListener);
        pc.addEndpointListener ("midiIn", this.midiListener);

        pc.requestStatusUpdate();
        pc.requestStoredStateValue ("voiceName");
        pc.requestStoredStateValue ("currentProgram");

        for (const param of parameters)
            pc.requestParameterValue (param.id);
    }

    removePatchListeners()
    {
        const pc = this.patchConnection;

        if (! this.parameterListener)
            return;

        pc.removeAllParameterListener (this.parameterListener);
        pc.removeStatusListener (this.statusListener);
        pc.removeStoredStateValueListener (this.stateListener);
        pc.removeEndpointListener ("midiIn", this.midiListener);
        this.parameterListener = undefined;
    }

    sendMIDI (status, data1, data2)
    {
        this.patchConnection.sendMIDIInputEvent ("midiIn", (status << 16) | (data1 << 8) | data2);
        this.flashMIDILight();
    }

    sendPitchBend (wheelPosition)
    {
        const value = Math.round (8192 + wheelPosition * (wheelBendRange / midiBendRange) * 8192);
        const clamped = Math.min (16383, Math.max (0, value));
        this.sendMIDI (0xe0, clamped & 0x7f, clamped >> 7);
        this.showParameter (pitchBendDisplay, wheelPosition);
    }

    sendModulation (wheelPosition)
    {
        const value = Math.round (wheelPosition * 127);

        if (value !== this.lastModulationValue)
        {
            this.lastModulationValue = value;
            this.sendMIDI (0xb0, 1, value);
        }

        this.showParameter (modulationDisplay, value);
    }

    /** Asks the worker to load the program that's delta steps away from the current one. */
    stepProgram (delta)
    {
        // (this updates currentProgram straight away, so that pressing a button repeatedly
        // keeps stepping even if the worker hasn't caught up yet)
        this.currentProgram = getProgramNumberAfterStep (this.currentProgram, delta);
        this.lastProgramChangeTime = performance.now();
        this.sendMIDI (0xc0, this.currentProgram - 1, 0);
        this.showIdleDisplay();
    }

    isLoadingProgram()
    {
        return performance.now() - this.lastProgramChangeTime < programChangeQuietTime;
    }

    flashMIDILight()
    {
        this.midiLight.classList.add ("lit");
        clearTimeout (this.midiLightTimer);
        this.midiLightTimer = setTimeout (() => this.midiLight.classList.remove ("lit"), 120);
    }

    //==============================================================================
    // Building the panel

    createControls()
    {
        const root = this.shadowRoot;

        const controlListener =
        {
            onValueChange: (param, value) =>
            {
                this.patchConnection.sendEventOrValue (param.id, value);
                this.updateDependentDisplays (param);
                this.showParameter (param, value);
            },
            onGestureStart: (param) =>
            {
                ++this.activeGestures;
                this.patchConnection.sendParameterGestureStart (param.id);
                this.showParameter (param, param.value);
            },
            onGestureEnd: (param) =>
            {
                this.activeGestures = Math.max (0, this.activeGestures - 1);
                this.patchConnection.sendParameterGestureEnd (param.id);
                this.scheduleIdleDisplay();
            },
            onHover: (param, isOver) =>
            {
                this.hoveredParameter = isOver ? param : null;

                if (isOver)
                    this.showParameter (param, param.value);
                else
                    this.scheduleIdleDisplay();
            }
        };

        const addControl = (parent, ControlType, id, options) =>
        {
            const control = new ControlType (parametersByID.get (id), controlListener, options);
            this.controls.set (id, control);
            parent.appendChild (control.element);
            return control;
        };

        // The master section and the wheels
        addControl (root.querySelector ("#volume"), Slider, "volumeIn", { height: 99 });
        addControl (root.querySelector ("#transpose"), Knob, "pitchOffsetIn");
        this.midiLight = root.querySelector ("#midi-light");

        // The program buttons
        new PushButton (root.querySelector ("#program-previous"), { onPress: () => this.stepProgram (-1) });
        new PushButton (root.querySelector ("#program-next"),     { onPress: () => this.stepProgram (1) });

        const wheels = root.querySelector ("#wheels");
        wheels.appendChild (new Wheel ({ label: "PITCH", isPitchBend: true, width: 30, onChange: v => this.sendPitchBend (v) }).element);
        wheels.appendChild (new Wheel ({ label: "MOD", isPitchBend: false, width: 30, onChange: v => this.sendModulation (v) }).element);

        // The LCD
        this.lcd = new DotMatrixLCD (root.querySelector ("#lcd"), { columns: lcdColumns, rows: 2, dotPitch: 3.1 });
        const lcdWindow = root.querySelector ("#lcd-window");
        lcdWindow.addEventListener ("dblclick", () => this.startNameEdit());

        // The algorithm chart: this isn't a ParameterControl, so it gets its own listener
        const algorithmParam = parametersByID.get ("algorithmIn");
        this.algorithmChart = new AlgorithmChart ({ onSelect: (algorithm) =>
        {
            controlListener.onGestureStart (algorithmParam);
            algorithmParam.value = algorithm;
            controlListener.onValueChange (algorithmParam, algorithm);
            controlListener.onGestureEnd (algorithmParam);
        }});

        const chart = root.querySelector ("#algorithm-chart");
        chart.appendChild (this.algorithmChart.element);
        chart.addEventListener ("pointerenter", () => controlListener.onHover (algorithmParam, true));
        chart.addEventListener ("pointerleave", () => controlListener.onHover (algorithmParam, false));

        // The operators
        const operators = root.querySelector ("#operators");

        for (let op = 1; op <= operatorCount; ++op)
        {
            const id = (suffix) => getOperatorParameterID (op, suffix);

            const strip = document.createElement ("div");
            strip.className = "operator";
            strip.innerHTML = `
                <div class="operator-header"><span>OPERATOR ${op}</span><span class="carrier-tag"></span></div>
                <div class="knob-row oscillator-knobs"></div>
                <div class="knob-row sensitivity-knobs"><svg class="envelope-display" viewBox="0 0 96 46"><path></path></svg></div>
                <div class="slider-row">
                    <div class="slider-group level-group"><div class="group-title">OUTPUT</div><div class="sliders"></div></div>
                    <div class="slider-group envelope-group"><div class="group-title">ENVELOPE GENERATOR</div><div class="sliders"></div></div>
                </div>`;
            operators.appendChild (strip);

            const oscillatorKnobs = strip.querySelector (".oscillator-knobs");
            const sensitivityKnobs = strip.querySelector (".sensitivity-knobs");
            const levelSliders = strip.querySelector (".level-group .sliders");
            const envelopeSliders = strip.querySelector (".envelope-group .sliders");

            for (const suffix of ["WaveshapeIn", "FreqMultiplierIn", "FixedIn", "FeedbackIn"])
                addControl (oscillatorKnobs, Knob, id (suffix));

            const envelopeDisplay = sensitivityKnobs.querySelector (".envelope-display");

            for (const suffix of ["KVSIn", "RateScalingIn"])
                sensitivityKnobs.insertBefore (addControl (sensitivityKnobs, Knob, id (suffix)).element, envelopeDisplay);

            const sliderHeight = 174;
            addControl (levelSliders, Slider, id ("LevelIn"), { height: sliderHeight });

            for (const suffix of ["AttackRateIn", "DecayRate1In", "DecayLevelIn", "DecayRate2In", "ReleaseRateIn"])
                addControl (envelopeSliders, Slider, id (suffix), { height: sliderHeight });

            this.envelopeDisplays.push (envelopeDisplay.querySelector ("path"));
            this.carrierTags.push (strip.querySelector (".carrier-tag"));
        }

        // The keyboard
        this.keyboard = new Keyboard ({ lowestNote: 36, numKeys: 61, width: designWidth - 2 * sideMargin, height: 232,
                                        onNoteOn:  (note, velocity) => this.sendMIDI (0x90, note, velocity),
                                        onNoteOff: (note) => this.sendMIDI (0x80, note, 0) });
        root.querySelector ("#keybed").appendChild (this.keyboard.element);

        for (const param of parameters)
            this.updateDependentDisplays (param);
    }

    /** Updates the parts of the panel that show a parameter but aren't its control. */
    updateDependentDisplays (param)
    {
        if (param.id === "algorithmIn")
        {
            this.algorithmChart.setAlgorithm (param.value);

            this.carrierTags.forEach ((tag, i) =>
            {
                const carrier = isCarrier (param.value, i + 1);
                tag.textContent = carrier ? "CARRIER" : "MODULATOR";
                tag.classList.toggle ("carrier", carrier);
            });
        }

        if ((param.id === "volumeIn" || param.id === "algorithmIn") && ! this.isShowingParameter && ! this.nameEdit)
            this.showIdleDisplay();

        if (param.operator)
        {
            if (param.id.endsWith ("FeedbackIn"))
                this.algorithmChart.setFeedback (param.operator, param.value);

            this.updateEnvelopeDisplay (param.operator);
        }
    }

    /** Draws a sketch of an operator's envelope, using the same timings as TX81Z.cmajor, with
        time on a compressed scale so that both fast and slow envelopes are readable.
    */
    updateEnvelopeDisplay (op)
    {
        const path = this.envelopeDisplays[op - 1];

        if (! path)
            return;

        const get = (suffix) => parametersByID.get (getOperatorParameterID (op, suffix)).value;

        const floorDB = -60, width = 96, height = 46, top = 5, bottom = height - 4;
        const timeToX = (seconds) => Math.min (30, 9 * Math.log2 (1 + seconds * 3));
        const dBToY = (dB) => top + (bottom - top) * Math.min (1, Math.max (0, dB / floorDB));

        // An attack rate of 0 never gets anywhere
        const attackRate = get ("AttackRateIn");
        const attackTime = attackRate > 0 ? Math.pow (2, 3.5 - attackRate * 0.5) : Infinity;

        // A decay rate gives 24dB of attenuation in 2 ^ (5.5 - rate / 2) seconds, or none at rate 0
        const decaySlope = (rate) => rate > 0 ? 24 / Math.pow (2, 5.5 - rate * 0.5) : 0;

        const decayLevel = get ("DecayLevelIn");
        const decayLevelDB = decayLevel > 0 ? -3 * (15 - decayLevel) : floorDB;
        const decay1Slope = decaySlope (get ("DecayRate1In"));
        const decay1Time = decay1Slope > 0 ? -decayLevelDB / decay1Slope : Infinity;
        const decay2Slope = decaySlope (get ("DecayRate2In"));
        const releaseSlope = decaySlope (1 + get ("ReleaseRateIn") * 2);

        let x = 2;
        const points = [[x, bottom]];

        if (attackTime === Infinity)
        {
            points.push ([width - 2, bottom]);
        }
        else
        {
            x += timeToX (attackTime);
            points.push ([x, top]);

            // Decay 1 to its level (or hold at full level if it never decays)
            const sustainStartX = x + timeToX (decay1Time);
            const sustainLevel = decay1Time === Infinity ? 0 : decayLevelDB;
            x = Math.min (sustainStartX, width - 32);
            points.push ([x, dBToY (sustainLevel)]);

            // Decay 2, for as long as the key is held
            const holdTime = 0.6;
            const releaseStartDB = Math.max (floorDB, sustainLevel - decay2Slope * holdTime);
            x = width - 30;
            points.push ([x, dBToY (releaseStartDB)]);

            // Release
            const releaseTime = (releaseStartDB - floorDB) / releaseSlope;
            points.push ([x + timeToX (releaseTime), bottom]);
        }

        path.setAttribute ("d", "M" + points.map (p => p[0].toFixed (1) + " " + p[1].toFixed (1)).join (" L"));
    }

    //==============================================================================
    // The LCD

    showIdleDisplay()
    {
        this.isShowingParameter = false;

        if (this.nameEdit)
            return;

        const algorithm = parametersByID.get ("algorithmIn").value;
        const volume = parametersByID.get ("volumeIn").value;

        this.lcd.setText ("I" + String (this.currentProgram).padStart (2, "0") + "  " + this.voiceName,
                          ("ALG " + algorithm).padEnd (8) + ("VOL" + Math.round (volume) + "dB").padStart (8));
    }

    scheduleIdleDisplay()
    {
        clearTimeout (this.idleTimer);

        this.idleTimer = setTimeout (() =>
        {
            if (this.activeGestures > 0)
                return;

            if (this.hoveredParameter)
                this.showParameter (this.hoveredParameter, this.hoveredParameter.value, false);
            else
                this.showIdleDisplay();
        }, lcdHoldTime);
    }

    /** Puts a parameter's name on the top line, and its value and a bar graph (or for a
        waveform, a picture of it) on the bottom line.
    */
    showParameter (param, value, revertAfterwards = true)
    {
        if (this.nameEdit)
            return;

        this.isShowingParameter = true;
        const valueText = formatValue (param, value);
        let bottomLine;

        if (param.isWave)
        {
            this.defineWaveGlyphs (Math.round (value));
            bottomLine = valueText.padEnd (lcdColumns - 8) + "\x00\x01\x02\x03\x04\x05\x06\x07";
        }
        else
        {
            const valueWidth = 6;
            const barCells = lcdColumns - valueWidth - 1;
            bottomLine = this.createBarGraph (param, value, barCells) + " " + valueText.padStart (valueWidth);
        }

        this.lcd.setText (param.lcdTitle, bottomLine);

        if (revertAfterwards)
            this.scheduleIdleDisplay();
    }

    /** Returns a string of characters that draw a bar graph, defining the custom glyphs it needs. */
    createBarGraph (param, value, numCells)
    {
        const numColumns = numCells * 5;
        const proportion = toNormalised (param, value);
        const lit = new Array (numColumns).fill (false);

        if (param.bipolar)
        {
            const centre = Math.floor (numColumns / 2);
            const end = Math.round (proportion * (numColumns - 1));

            for (let i = Math.min (centre, end); i <= Math.max (centre, end); ++i)
                lit[i] = true;
        }
        else
        {
            const end = Math.round (proportion * numColumns);

            for (let i = 0; i < end; ++i)
                lit[i] = true;
        }

        // Each different 5-column pattern needs its own custom glyph
        const glyphForPattern = new Map();
        let text = "";

        for (let cell = 0; cell < numCells; ++cell)
        {
            let bits = 0;

            for (let i = 0; i < 5; ++i)
                bits = (bits << 1) | (lit[cell * 5 + i] ? 1 : 0);

            if (bits === 0)
            {
                text += "_";
                continue;
            }

            if (! glyphForPattern.has (bits))
            {
                const index = glyphForPattern.size;
                glyphForPattern.set (bits, index);
                this.lcd.setCustomGlyph (index, [0, bits, bits, bits, bits, bits, 0x1f, 0]);
            }

            text += String.fromCharCode (glyphForPattern.get (bits));
        }

        return text;
    }

    /** Draws one cycle of a waveform across the eight custom glyphs, which are 40x7 dots. */
    defineWaveGlyphs (waveshape)
    {
        const width = 40, height = 7;
        const rows = new Array (height).fill (0).map (() => new Array (width).fill (false));
        let previousRow = null;

        for (let x = 0; x < width; ++x)
        {
            const level = getWaveshape (waveshape, x / width);
            const row = Math.round ((1 - level) / 2 * (height - 1));

            // Join this column's dot to the previous one with a vertical line
            const from = previousRow ?? row;

            for (let y = Math.min (from, row); y <= Math.max (from, row); ++y)
                rows[y][x] = true;

            previousRow = row;
        }

        for (let cell = 0; cell < 8; ++cell)
        {
            const glyph = rows.map (row =>
            {
                let bits = 0;

                for (let i = 0; i < 5; ++i)
                    bits = (bits << 1) | (row[cell * 5 + i] ? 1 : 0);

                return bits;
            });

            this.lcd.setCustomGlyph (cell, [...glyph, 0]);
        }
    }

    //==============================================================================
    // Editing the voice name

    startNameEdit()
    {
        if (this.nameEdit)
            return;

        clearTimeout (this.idleTimer);
        this.nameEdit = { name: this.voiceName.padEnd (voiceNameLength), cursor: 0, original: this.voiceName };
        this.focus ({ preventScroll: true });
        this.updateNameEditDisplay();

        // clicking anywhere else keeps the new name
        this.nameEditClickListener = (e) =>
        {
            if (! e.composedPath().includes (this.shadowRoot.querySelector ("#lcd-window")))
                this.finishNameEdit (true);
        };

        this.shadowRoot.addEventListener ("pointerdown", this.nameEditClickListener);
    }

    updateNameEditDisplay()
    {
        this.lcd.setText ("Voice Name", this.nameEdit.name);
        this.lcd.setCursor ({ row: 1, column: this.nameEdit.cursor });
    }

    finishNameEdit (keepChanges)
    {
        if (! this.nameEdit)
            return;

        const name = keepChanges ? this.nameEdit.name.trimEnd() : this.nameEdit.original;
        this.nameEdit = undefined;
        this.shadowRoot.removeEventListener ("pointerdown", this.nameEditClickListener);
        this.lcd.setCursor (null);

        if (keepChanges && name !== this.voiceName)
            this.patchConnection.sendStoredStateValue ("voiceName", name);

        this.voiceName = name;
        this.showIdleDisplay();
    }

    /** The cursor can go one place past the last character, so that Backspace can delete it. */
    handleNameEditKey (e)
    {
        const edit = this.nameEdit;
        const replaceCharacter = (index, ch) => edit.name = edit.name.substring (0, index) + ch + edit.name.substring (index + 1);

        if (e.key === "Enter")              this.finishNameEdit (true);
        else if (e.key === "Escape")        this.finishNameEdit (false);
        else if (e.key === "ArrowLeft")     edit.cursor = Math.max (0, edit.cursor - 1);
        else if (e.key === "ArrowRight")    edit.cursor = Math.min (voiceNameLength, edit.cursor + 1);
        else if (e.key === "Backspace")
        {
            edit.cursor = Math.max (0, edit.cursor - 1);
            replaceCharacter (edit.cursor, " ");
        }
        else if (e.key === "Delete")
        {
            edit.name = (edit.name.substring (0, edit.cursor) + edit.name.substring (edit.cursor + 1)).padEnd (voiceNameLength);
        }
        else if (e.key.length === 1 && isDisplayableCharacter (e.key) && ! e.metaKey && ! e.ctrlKey)
        {
            // Like the hardware, typing overwrites the character under the cursor (or, when the
            // cursor is past the end, the last character)
            const index = Math.min (edit.cursor, voiceNameLength - 1);
            replaceCharacter (index, e.key);
            edit.cursor = index + 1;
        }
        else
        {
            return;
        }

        e.preventDefault();

        if (this.nameEdit)
            this.updateNameEditDisplay();
    }

    //==============================================================================
    // Playing from the computer keyboard

    handleKeyDown (e)
    {
        if (this.nameEdit)
            return this.handleNameEditKey (e);

        if (e.metaKey || e.ctrlKey || e.altKey || e.repeat)
            return;

        const key = e.key.toLowerCase();

        if (key === "z" || key === "x")
        {
            this.keyboardOctave = Math.min (3, Math.max (-3, this.keyboardOctave + (key === "z" ? -1 : 1)));
            this.lcd.setText ("Keyboard Octave", "C" + (4 + this.keyboardOctave));
            this.scheduleIdleDisplay();
            e.preventDefault();
            return;
        }

        const index = computerKeyboardNotes.indexOf (key);

        if (index >= 0 && ! this.heldComputerKeys.has (key))
        {
            const note = 60 + 12 * this.keyboardOctave + index;
            this.heldComputerKeys.set (key, note);
            this.keyboard.pressNote (note, 100, "computer");
            e.preventDefault();
        }
    }

    handleKeyUp (e)
    {
        const key = e.key.toLowerCase();
        const note = this.heldComputerKeys.get (key);

        if (note !== undefined)
        {
            this.heldComputerKeys.delete (key);
            this.keyboard.releaseNote (note, "computer");
        }
    }

    releaseComputerKeys()
    {
        for (const note of this.heldComputerKeys.values())
            this.keyboard.releaseNote (note, "computer");

        this.heldComputerKeys.clear();
    }

    //==============================================================================
    getHTML()
    {
        return `
<style>
    :host {
        display: block;
        position: relative;
        overflow: hidden;
        background: #141210;
        outline: none;
        user-select: none;
        -webkit-user-select: none;
        touch-action: none;
        cursor: default;

        --panel: #2d2925;
        --panel-dark: #1f1c1a;
        --print: #e6dcc3;
        --print-dim: #a99f88;
        --maroon: #8a2a35;
        --maroon-light: #b2414c;
        --teal: #2f7d74;
        --mustard: #d0a440;
        --label-font: "Helvetica Neue Condensed", "HelveticaNeue-CondensedBold", "Arial Narrow", "Roboto Condensed", "Helvetica Neue", Helvetica, Arial, sans-serif;
        --logo-font: "Helvetica Neue", Helvetica, Arial, sans-serif;
    }

    .panel {
        position: absolute;
        left: 0;
        top: 0;
        width: ${designWidth}px;
        height: ${designHeight}px;
        transform-origin: 0 0;
        border-radius: 10px;
        background: linear-gradient(180deg, #383330 0%, var(--panel) 6%, #27231f 100%);
        box-shadow: inset 0 1px 0 #4d4642, inset 0 -2px 0 #120f0d;
        font-family: var(--label-font);
        color: var(--print);
        overflow: hidden;
    }

    /* The name plate across the top */
    .header {
        position: absolute;
        left: ${sideMargin}px;
        right: ${sideMargin}px;
        top: 0;
        height: 64px;
        display: flex;
        align-items: center;
        gap: 22px;
    }

    .logo {
        font-family: var(--logo-font);
        font-size: 40px;
        font-weight: 800;
        font-style: italic;
        letter-spacing: 3px;
        color: var(--print);
        line-height: 1;
    }

    /* Lettering cut by thin horizontal lines, like the nameplates on 80s FM synths. The panel
       gets scaled, so the gaps need to be thick enough not to vanish or alias when it's small,
       and the soft edges keep them even when they land between pixels. */
    .striped {
        --stripe-solid: 4px;
        --stripe-gap: 2px;
        --stripe-feather: 1px;
        --stripes: repeating-linear-gradient(180deg,
                        #000 0,
                        #000 calc(var(--stripe-solid) - var(--stripe-feather) / 2),
                        transparent calc(var(--stripe-solid) + var(--stripe-feather) / 2),
                        transparent calc(var(--stripe-solid) + var(--stripe-gap) - var(--stripe-feather)),
                        #000 calc(var(--stripe-solid) + var(--stripe-gap)));
        -webkit-mask-image: var(--stripes);
                mask-image: var(--stripes);
    }

    .tagline {
        display: flex;
        flex-direction: column;
        gap: 3px;
        font-size: 11px;
        font-weight: bold;
        letter-spacing: 2.5px;
        color: var(--print-dim);
    }

    .tagline .main { color: var(--print); font-size: 12px; }

    .badges {
        margin-left: auto;
        display: flex;
        align-items: center;
        gap: 14px;
        font-size: 11px;
        font-weight: bold;
        letter-spacing: 2px;
        color: var(--print-dim);
    }

    .badge {
        padding: 3px 8px;
        border: 1px solid var(--print-dim);
        border-radius: 2px;
    }

    .maker {
        display: block;
        height: 34px;
        width: ${(34 * wordmarkBounds.width / wordmarkBounds.height).toFixed (1)}px;
        fill: var(--print);
    }

    .stripes {
        position: absolute;
        left: 0;
        right: 0;
        top: 64px;
        height: 7px;
        background: linear-gradient(180deg, var(--maroon) 0 3px, transparent 3px 4px, var(--teal) 4px 7px);
    }

    /* The control panel */
    .controls {
        position: absolute;
        left: ${sideMargin}px;
        right: ${sideMargin}px;
        top: ${controlsTop}px;
        height: ${controlsHeight}px;
        display: flex;
        gap: 18px;
    }

    section {
        display: flex;
        flex-direction: column;
    }

    .section-title {
        font-size: 11px;
        line-height: 13px;
        font-weight: bold;
        letter-spacing: 2px;
        padding: 3px 8px;
        margin-bottom: 10px;
        color: var(--print);
        background: var(--teal);
        border-radius: 2px;
    }

    .master { width: 100px; }

    .master-controls {
        display: flex;
        justify-content: space-between;
    }

    .master-right {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: space-between;
        gap: 8px;
        padding-top: 8px;
    }

    .midi-indicator {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 5px;
    }

    #midi-light {
        width: 9px;
        height: 9px;
        border-radius: 50%;
        background: #4a1410;
        box-shadow: inset 0 1px 2px #000;
    }

    #midi-light.lit {
        background: #ff3b24;
        box-shadow: 0 0 6px 2px rgba(255, 70, 40, 0.6), inset 0 -1px 2px #a0180c;
    }

    /* The program section takes up the space above the wheels, with its buttons centred in it */
    .upper-block {
        flex: none;
        height: ${upperBlockHeight}px;
        display: flex;
        flex-direction: column;
    }

    #program {
        flex: 1;
        display: flex;
        flex-direction: column;
    }

    #program .section-title { margin-bottom: 0; }

    .program-buttons {
        flex: 1;
        align-items: center;
        display: flex;
        justify-content: center;
    }

    /* Like the hardware, each pair of buttons sits in a black recess in the panel */
    .program-bezel {
        display: flex;
        gap: 3px;
        padding: 3px;
        border-radius: 2px;
        background: #0b0b0b;
        box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.9), 0 1px 0 rgba(255, 255, 255, 0.07);
    }

    /* Flat keys with nearly square corners, shaped like the hardware's. They sink into
       the recess when pressed. */
    .program-button {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 40px;
        height: 34px;
        padding: 0;
        border: none;
        border-radius: 1.5px;
        outline: none;
        cursor: pointer;
        background: linear-gradient(180deg, #4b4641 0%, #34302c 60%, #2a2724 100%);
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.14), inset 0 -2px 0 rgba(0, 0, 0, 0.35),
                    inset 1px 0 0 rgba(255, 255, 255, 0.05), inset -1px 0 0 rgba(0, 0, 0, 0.3);
    }

    /* The hardware's arrows are outlines, with a stepped back edge */
    .program-button svg {
        width: 26px;
        height: 26px;
        fill: none;
        stroke: var(--print);
        stroke-width: 1.1px;
        stroke-linejoin: miter;
    }

    .program-button.pressed {
        transform: translateY(1px);
        background: linear-gradient(180deg, #3b3733 0%, #2d2a26 60%, #252220 100%);
        box-shadow: inset 0 2px 3px rgba(0, 0, 0, 0.5);
    }

    #wheels {
        margin-top: auto;
        display: flex;
        justify-content: space-around;
        padding: 12px 6px 0;
        border-top: 1px solid #4a433d;
    }

    .centre { width: 344px; }

    /* The LCD sits behind a smoked window in a black bezel */
    #lcd-window {
        align-self: center;
        padding: 9px;
        border-radius: 4px;
        background: linear-gradient(180deg, #0c0b0a, #1d1a18);
        box-shadow: inset 0 2px 4px #000, 0 1px 0 #4a433d;
        cursor: text;
    }

    #lcd {
        display: block;
        border-radius: 1px;
        box-shadow: inset 0 0 8px rgba(0, 0, 0, 0.5);
    }

    .lcd-caption {
        align-self: center;
        margin-top: 6px;
        font-size: 9px;
        letter-spacing: 2px;
        color: var(--print-dim);
    }

    #algorithm-chart { align-self: center; }
    .algorithm-chart { display: block; }

    .algorithm { cursor: pointer; }
    .algorithm-background { fill: transparent; stroke: #4a433d; stroke-width: 1; }
    .algorithm:hover .algorithm-background { fill: rgba(230, 220, 195, 0.05); }
    .algorithm.selected .algorithm-background { fill: rgba(208, 164, 64, 0.16); stroke: var(--mustard); }
    .algorithm-number { fill: var(--print-dim); font-size: 13px; font-weight: bold; }
    .algorithm.selected .algorithm-number { fill: var(--mustard); }
    .algorithm-wire { fill: none; stroke: var(--print-dim); stroke-width: 1.4; }
    .algorithm.selected .algorithm-wire { stroke: var(--print); }
    .algorithm-feedback { stroke-dasharray: 2 1.5; }
    .algorithm-op { fill: var(--panel-dark); stroke: var(--print-dim); stroke-width: 1.4; }
    .algorithm-op.carrier { fill: var(--maroon); }
    .algorithm.selected .algorithm-op { stroke: var(--print); }
    .algorithm-op-number { fill: var(--print); font-size: 10px; font-weight: bold; text-anchor: middle; }

    /* An operator strip */
    #operators {
        flex: 1;
        flex-direction: row;
        gap: 10px;
        min-width: 0;
    }

    .operator {
        flex: 1;
        display: flex;
        flex-direction: column;
        padding: 0 4px 6px;
        border-radius: 4px;
        background: rgba(0, 0, 0, 0.12);
        box-shadow: inset 0 0 0 1px #3d3733;
    }

    .operator-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin: 0 -4px 8px;
        padding: 4px 8px;
        border-radius: 4px 4px 0 0;
        background: var(--maroon);
        font-size: 12px;
        font-weight: bold;
        letter-spacing: 2px;
    }

    .carrier-tag {
        font-size: 9px;
        letter-spacing: 1.5px;
        color: #e8b9ba;
    }

    .carrier-tag.carrier { color: var(--print); }

    .knob-row {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin-bottom: 6px;
    }

    .envelope-display {
        width: 96px;
        height: 46px;
        border-radius: 3px;
        background: #121614;
        box-shadow: inset 0 1px 3px #000, 0 1px 0 #4a433d;
    }

    .envelope-display path {
        fill: none;
        stroke: #a9cf3c;
        stroke-width: 1.5;
        stroke-linejoin: round;
    }

    .slider-row {
        display: flex;
        justify-content: space-between;
        margin-top: auto;
    }

    .slider-group { display: flex; flex-direction: column; }
    .slider-group .sliders { display: flex; }

    .group-title {
        font-size: 9px;
        font-weight: bold;
        letter-spacing: 1.5px;
        text-align: center;
        padding-bottom: 3px;
        margin-bottom: 4px;
        border-bottom: 2px solid var(--teal);
    }

    .level-group .group-title { border-bottom-color: var(--maroon-light); }

    /* Controls */
    .control {
        display: flex;
        flex-direction: column;
        align-items: center;
    }

    .control-label {
        margin-top: 2px;
        font-size: 10px;
        line-height: 12px;
        font-weight: bold;
        letter-spacing: 1px;
        white-space: nowrap;
        color: var(--print);
    }

    .knob-control { width: 50px; }

    .knob-area {
        position: relative;
        width: 50px;
        height: 50px;
        cursor: ns-resize;
    }

    .knob-scale {
        position: absolute;
        inset: 0;
        width: 100%;
        height: 100%;
    }

    .knob-tick { stroke: var(--print-dim); stroke-width: 1.3; }

    .knob {
        position: absolute;
        left: 9px;
        top: 9px;
        width: 32px;
        height: 32px;
        border-radius: 50%;
        box-shadow: 0 3px 4px rgba(0, 0, 0, 0.7), 0 0 0 1px #0c0b0a;
    }

    .knob-grip, .knob-grip-lighting, .knob-cap, .knob-pointer-rotor {
        position: absolute;
        border-radius: 50%;
    }

    /* The knurled skirt, which turns with the knob... */
    .knob-grip {
        inset: 0;
        background: repeating-conic-gradient(from 0deg, #2a2725 0deg 6deg, #171513 6deg 12deg);
    }

    /* ...and the light falling on it from above, which doesn't */
    .knob-grip-lighting {
        inset: 0;
        background: linear-gradient(180deg, rgba(255, 255, 255, 0.10) 0%, transparent 45%, rgba(0, 0, 0, 0.30) 100%);
    }

    .knob-cap {
        inset: 3px;
        background: radial-gradient(circle at 40% 30%, #4b4642 0%, #24211f 55%, #161412 100%);
        box-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.12);
    }

    .knob-pointer-rotor { inset: 3px; }

    .knob-pointer {
        position: absolute;
        left: calc(50% - 1px);
        top: 2px;
        width: 2px;
        height: 9px;
        border-radius: 1px;
        background: var(--print);
    }

    .slider-control { width: 34px; }

    .slider-area {
        position: relative;
        width: 34px;
        cursor: ns-resize;
    }

    .slider-scale {
        position: absolute;
        left: -3px;
        top: 0;
        width: 40px;
        height: 100%;
    }

    .slider-tick { stroke: var(--print-dim); stroke-width: 1; }

    .slider-slot {
        position: absolute;
        left: 14px;
        top: 4px;
        bottom: 4px;
        width: 6px;
        border-radius: 3px;
        background: #0a0908;
        box-shadow: inset 0 1px 2px #000, 0 1px 0 #4a433d;
    }

    .slider-cap {
        position: absolute;
        left: 5px;
        width: 24px;
        border-radius: 2px;
        background: linear-gradient(180deg, #45403c 0%, #1e1c1a 45%, #121110 55%, #2c2926 100%);
        box-shadow: 0 3px 4px rgba(0, 0, 0, 0.7), inset 0 1px 0 rgba(255, 255, 255, 0.18);
    }

    .slider-cap::after {
        content: "";
        position: absolute;
        left: 2px;
        right: 2px;
        top: calc(50% - 1px);
        height: 2px;
        background: var(--print);
    }

    .wheel-control { gap: 4px; }

    .wheel-housing {
        padding: 5px 4px;
        border-radius: 4px;
        background: #0b0a09;
        box-shadow: inset 0 2px 5px #000, 0 1px 0 #4a433d;
        cursor: ns-resize;
    }

    .wheel {
        display: block;
        border-radius: 3px;
        background: linear-gradient(180deg, #080707 0%, #2e2b28 30%, #3b3734 50%, #2e2b28 70%, #080707 100%);
    }

    .wheel-ridge { stroke: #0b0a09; }
    .wheel-marker { stroke: var(--print); }

    /* The keyboard */
    #keybed {
        position: absolute;
        left: ${sideMargin}px;
        right: ${sideMargin}px;
        top: ${keybedTop}px;
        height: ${keybedHeight}px;
        box-sizing: border-box;
        padding-top: 6px;
        background: linear-gradient(180deg, #0b0a09, #1a1715);
        box-shadow: inset 0 3px 6px #000;
        border-radius: 3px;
    }

    .keyboard {
        position: relative;
        margin: 0 auto;
        cursor: pointer;
    }

    .key {
        position: absolute;
        top: 0;
        box-sizing: border-box;
    }

    .white-key {
        border-radius: 0 0 4px 4px;
        border: 1px solid #8d887d;
        border-top: none;
        background: linear-gradient(180deg, #d8d3c6 0%, #f2eee4 12%, #f7f4ec 88%, #dcd7cb 100%);
        box-shadow: inset 0 -5px 0 #cfc9ba;
    }

    .white-key.down {
        background: linear-gradient(180deg, #c9c3b5 0%, #e5e0d4 15%, #ebe7dd 100%);
        box-shadow: inset 0 -2px 0 #c2bcad, inset 2px 0 3px rgba(0, 0, 0, 0.15);
    }

    .black-key {
        z-index: 1;
        border-radius: 0 0 3px 3px;
        background: linear-gradient(90deg, #0a0909 0%, #262321 18%, #1b1917 80%, #050505 100%);
        box-shadow: 0 3px 3px rgba(0, 0, 0, 0.55), inset 0 -8px 0 #2f2b28, inset 0 -9px 0 #0c0b0a;
    }

    .black-key.down {
        box-shadow: 0 1px 1px rgba(0, 0, 0, 0.55), inset 0 -3px 0 #2f2b28, inset 0 -4px 0 #0c0b0a;
        background: linear-gradient(90deg, #080707 0%, #1c1a18 18%, #151312 80%, #040404 100%);
    }
</style>

<div class="panel">
    <div class="header">
        <div class="logo striped">TX81Z</div>
        <div class="tagline">
            <span class="main">FM DIGITAL TONE GENERATOR</span>
            <span>4 OPERATOR &middot; 8 ALGORITHMS &middot; 8 WAVEFORMS</span>
        </div>
        <div class="badges">
            <span class="badge">8 VOICE POLY</span>
            <svg class="maker" viewBox="${wordmarkBounds.x} ${wordmarkBounds.y} ${wordmarkBounds.width} ${wordmarkBounds.height}"
                 role="img" aria-label="Cmajor"><path d="${wordmarkPath}"></path></svg>
        </div>
    </div>
    <div class="stripes"></div>

    <div class="controls">
        <section class="master">
            <div class="upper-block">
                <div class="section-title">MASTER</div>
                <div class="master-controls">
                    <div id="volume"></div>
                    <div class="master-right">
                        <div id="transpose"></div>
                        <div class="midi-indicator"><div id="midi-light"></div><div class="control-label">MIDI</div></div>
                    </div>
                </div>
            </div>
            <div id="program">
                <div class="section-title">PROGRAM</div>
                <div class="program-buttons">
                    <div class="program-bezel">
                        <button type="button" tabindex="-1" class="program-button" id="program-previous" aria-label="Previous program"><svg viewBox="0 0 20 20"><polygon points="17,5.6 15.5,5.6 15.5,3.2 3,10 15.5,16.8 15.5,14.4 17,14.4"></polygon></svg></button>
                        <button type="button" tabindex="-1" class="program-button" id="program-next" aria-label="Next program"><svg viewBox="0 0 20 20"><polygon points="3,5.6 4.5,5.6 4.5,3.2 17,10 4.5,16.8 4.5,14.4 3,14.4"></polygon></svg></button>
                    </div>
                </div>
            </div>
            <div id="wheels"></div>
        </section>

        <section class="centre">
            <div class="upper-block">
                <div class="section-title">VOICE</div>
                <div id="lcd-window"><canvas id="lcd"></canvas></div>
                <div class="lcd-caption">DOUBLE-CLICK DISPLAY TO NAME VOICE</div>
            </div>
            <div class="section-title">ALGORITHM</div>
            <div id="algorithm-chart"></div>
        </section>

        <section id="operators"></section>
    </div>

    <div id="keybed" title="Click lower down a key to play louder. The computer keys A W S E D F T G Y H U J K play notes, and Z / X change octave."></div>
</div>`;
    }
}

//==============================================================================
export default function createPatchView (patchConnection)
{
    const customElementName = "cmaj-tx81z-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, TX81ZView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
