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

import PianoKeyboard from "../cmaj_api/cmaj-piano-keyboard.js"
import ImageStripControl from "./helpers/cmaj-image-strip-control.js"
import * as presets from "./presets/presetBank.js"

export default function createPatchView (patchConnection)
{
    return new Pro54PatchView (patchConnection);
}

//==============================================================================
class Pro54Keyboard extends PianoKeyboard
{
    constructor()
    {
        super ({ naturalNoteWidth: 18.58,
                 accidentalWidth: 11,
                 accidentalPercentageHeight: 62,
                 naturalNoteBorder: "none",
                 accidentalNoteBorder: "none",
                 pressedNoteColour: "#00000044" });
    }

    getNoteColour (note)    { return "none"; }
    getNoteLabel (note)     { return ""; }
}

//==============================================================================
class Pro54Button extends HTMLElement
{
    constructor (imageURL)
    {
        super();

        this.imageURL = imageURL;
        this.isOn = false;

        this.classList.add ("control");
        this.classList.add ("button");
    }

    connectedCallback()
    {
        this.addEventListener ('mousedown', this.buttonPress);
        this.addEventListener ('touchstart', this.buttonPress);
    }

    setPatchConnection (patchConnection)
    {
        this.innerHTML = `<img draggable="false" src="${patchConnection.getResourceAddress (this.imageURL)}"></img>`;
        this.image = this.children[0];
        this.patchConnection = patchConnection;
        this.patchConnection.requestParameterValue?.(this.id);
        this.updateImage();
    }

    setCurrentValue (newValue)
    {
        this.isOn = newValue > 0.5;
        this.updateImage();
    }

    updateImage()
    {
        this.image.style.display = this.isOn ? "block" : "none";
    }

    buttonPress (event)
    {
        this.patchConnection.sendParameterGestureStart (this.id);
        this.patchConnection.sendEventOrValue (this.id, this.isOn ? 0 : 1);
        this.patchConnection.sendParameterGestureEnd (this.id);
        event.preventDefault();
    }
}

//==============================================================================
class Pro54ImageStrip extends ImageStripControl
{
    constructor (imageSettings)
    {
        super();
        this.imageSettings = imageSettings;
        this.classList.add ("control");
    }

    setPatchConnection (patchConnection)
    {
        this.setImage ({ imageURL: patchConnection.getResourceAddress (this.imageSettings.imageURL),
                          ...this.imageSettings });

        this.patchConnection = patchConnection;
        this.patchConnection.requestParameterValue?.(this.id);
    }

    onStartDrag()               { this.patchConnection?.sendParameterGestureStart?.(this.id); }
    onEndDrag()                 { this.patchConnection?.sendParameterGestureEnd?.(this.id); }
    onValueDragged (newValue)   { this.patchConnection.sendEventOrValue?.(this.id, newValue); }
}

//==============================================================================
class Pro54BlackKnob extends Pro54ImageStrip
{
    constructor()
    {
        super ({ imageURL: "./gui/assets/knob_black.png",
                 numImagesPerStrip : 128,
                 imageHeightPixels : 25,
                 sensitivity : 100 });

        this.classList.add ("knob");
    }
}

class Pro54MetalKnob extends Pro54ImageStrip
{
    constructor()
    {
        super ({ imageURL: "./gui/assets/knob_metal.png",
                 numImagesPerStrip : 128,
                 imageHeightPixels : 25,
                 sensitivity : 100 });

        this.classList.add ("knob");
    }
}

class Pro54WheelElement extends Pro54ImageStrip
{
    constructor()
    {
        super ({ imageURL: "./gui/assets/wheel.png",
                 numImagesPerStrip : 64,
                 imageHeightPixels : 71,
                 sensitivity : 100 });

        this.classList.add ("wheel");
    }

    handleExternalMIDI (message)
    {
        function isPitchBend (message) { return ((message >> 16) & 0xf0) == 0xe0; }
        function isController (message) { return ((message >> 16) & 0xf0) == 0xb0 && ((message >> 8) & 0xff) == 1; }

        if (this.id == "PitchBend")
        {
            if (isPitchBend (message))
                this.setCurrentValue ((message & 0xff) / 1.28);
        }
        else if (this.id == "ModWheel")
        {
            if (isController (message))
                this.setCurrentValue ((message & 0xff) / 1.28);
        }
    }

    onEndDrag()
    {
        if (this.id == "PitchBend")
        {
            // Reset the PB to 0.5 when drag ends
            this.onValueDragged (50);
        }
    }
}

class Pro54FilterElement extends Pro54ImageStrip
{
    constructor()
    {
        super ({ imageURL: "./gui/assets/filter.png",
                 numImagesPerStrip : 3,
                 imageHeightPixels : 28,
                 sensitivity : 100 });
    }

    onStartDrag()
    {
        if (this.currentValue < 0.3)
            this.setCurrentValue (0.5);
        else if (this.currentValue > 0.6)
            this.setCurrentValue (0);
        else
            this.setCurrentValue (1.0);

        this.patchConnection.sendParameterGestureStart (this.id);
        this.patchConnection.sendEventOrValue (this.id, this.currentValue);
        this.patchConnection.sendParameterGestureEnd (this.id);
    }

    onEndDrag() {}
    onValueDragged (newValue) {}
}

class Pro54VoicesElement extends Pro54ImageStrip
{
    constructor()
    {
        super ({ imageURL: "./gui/assets/voices.png",
                 numImagesPerStrip : 32,
                 imageHeightPixels : 14,
                 sensitivity : 100 });

        this.classList.add ("voices");
    }
}

//==============================================================================
class Pro54BlackButton extends Pro54Button
{
    constructor()
    {
        super ("./gui/assets/button_black.png");
    }
}

class Pro54GreyButton extends Pro54Button
{
    constructor()
    {
        super ("./gui/assets/button_grey.png");
    }
}

class Pro54OrangeButton extends Pro54Button
{
    constructor()
    {
        super ("./gui/assets/button_orange.png");
    }
}

class Pro54MIDIActivityLight extends Pro54Button
{
    constructor()
    {
        super ("./gui/assets/midi_blink.png");
        this.classList.add ("midiBlink");
    }

    handleExternalMIDI (message)
    {
        this.setCurrentValue (1);

        if (this.timeout)
            clearTimeout (this.timeout);

        this.timeout = setTimeout (() => this.setCurrentValue (0), 200);
    }
}

//==============================================================================
// Manage the program bank controls and update parameters when programs are selected

class Pro54ProgramName  extends HTMLElement
{
    constructor()
    {
        super();
        this.classList.add ("patchName");
        this.innerHTML = `<input class="patchNameText" type="value" placeholder="" style="position: absolute;" required maxlength="18"/>`
    }

    connectedCallback()
    {
        this.addEventListener ('change', this.inputValue);
    }

    setValue (name)
    {
        this.children[0].placeholder = name;
    }

    inputValue (e)
    {
    }
}

class Pro54ProgramDigitElement extends Pro54ImageStrip
{
    constructor()
    {
        super ({ imageURL: "./gui/assets/program7seg.png",
                 numImagesPerStrip : 8,
                 imageHeightPixels : 14,
                 sensitivity : 100 });

        this.classList.add ("programDigit");
    }
}

//==============================================================================
class Pro54ProgramBank extends HTMLElement
{
    constructor()
    {
        super();
        this.classList.add ("programBank");
    }

    setPatchConnection (patchConnection)
    {
        this.patchConnection = patchConnection;

        this.innerHTML = `
<pro54-orange-button id="ProgrammerRecord" min-value="0" max-value="1" label=""></pro54-orange-button>
<pro54-grey-button id="ProgrammerFile" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgrammerBank" min-value="0" max-value="1" label=""></pro54-grey-button>

<pro54-program-digit id="Digit100" min-value="1" max-value="8" label="Digit100"></pro54-program-digit>
<pro54-program-digit id="Digit10" min-value="1" max-value="8" label="Digit10"></pro54-program-digit>
<pro54-program-digit id="Digit1" min-value="1" max-value="8" label="Digit1"></pro54-program-digit>

<pro54-grey-button id="ProgramButton1" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton2" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton3" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton4" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton5" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton6" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton7" min-value="0" max-value="1" label=""></pro54-grey-button>
<pro54-grey-button id="ProgramButton8" min-value="0" max-value="1" label=""></pro54-grey-button>

<pro54-program-name id="PatchName"></pro54-program-name>

<select id="program-selector"></select>
`;

        for (let child of this.children)
            child.setPatchConnection?.(this.patchConnection);

        this.digit1        = this.querySelector ("#Digit1");
        this.digit10       = this.querySelector ("#Digit10");
        this.digit100      = this.querySelector ("#Digit100");
        this.button1       = this.querySelector ("#ProgramButton1");
        this.button2       = this.querySelector ("#ProgramButton2");
        this.button3       = this.querySelector ("#ProgramButton3");
        this.button4       = this.querySelector ("#ProgramButton4");
        this.button5       = this.querySelector ("#ProgramButton5");
        this.button6       = this.querySelector ("#ProgramButton6");
        this.button7       = this.querySelector ("#ProgramButton7");
        this.button8       = this.querySelector ("#ProgramButton8");
        this.recordButton  = this.querySelector ("#ProgrammerRecord");
        this.fileButton    = this.querySelector ("#ProgrammerFile");
        this.bankButton    = this.querySelector ("#ProgrammerBank");
        this.patchName     = this.querySelector ("#PatchName");
        this.patchSelect   = this.querySelector ("#program-selector");

        this.lastKnownProgramID = 0;
        this.fileButtonValue = 0;
        this.bankButtonValue = 0;
        this.recordButtonValue = 0;

        this.patchName.inputValue = e => {
            let newName = e.target.value;
            e.target.value = null;
            patchConnection.sendStoredStateValue ("setPatchName", newName);
            this.patchName.setValue (newName);
            this.patchSelect[presets.getIndexOfID (this.lastKnownProgramID)].text = this.lastKnownProgramID + " : " + newName;
        };

        this.recordButton.buttonPress = e => {
            this.recordButtonValue = (this.recordButtonValue == 0) ? 1 : 0;
            this.patchSelect.hidden = (this.recordButtonValue == 1);

            patchConnection.sendStoredStateValue ("recordEnabled", this.recordButtonValue);
            this.updateValues();
            e.preventDefault();
        };

        this.fileButton.buttonPress = e => {
            this.bankButtonValue = 0;
            this.fileButtonValue = (this.fileButtonValue == 0) ? 1 : 0;
            this.updateValues();
            e.preventDefault();
        };

        this.bankButton.buttonPress = e => {
            this.bankButtonValue = (this.bankButtonValue == 0) ? 1 : 0;
            this.fileButtonValue = 0;
            this.updateValues();
            e.preventDefault();
        };

        this.patchSelect.onchange = () =>
        {
            for (const opt of this.patchSelect.selectedOptions)
            {
                this.setNewProgramID (opt.value ^ 0);
                return;
            }
        };

        this.digit1.onValueDragged   = newValue => { this.setProgramDigit (0, newValue); }
        this.digit10.onValueDragged  = newValue => { this.setProgramDigit (1, newValue); }
        this.digit100.onValueDragged = newValue => { this.setProgramDigit (2, newValue); }

        this.button1.buttonPress = e => { this.programButtonPressed (1); e.preventDefault(); };
        this.button2.buttonPress = e => { this.programButtonPressed (2); e.preventDefault(); };
        this.button3.buttonPress = e => { this.programButtonPressed (3); e.preventDefault(); };
        this.button4.buttonPress = e => { this.programButtonPressed (4); e.preventDefault(); };
        this.button5.buttonPress = e => { this.programButtonPressed (5); e.preventDefault(); };
        this.button6.buttonPress = e => { this.programButtonPressed (6); e.preventDefault(); };
        this.button7.buttonPress = e => { this.programButtonPressed (7); e.preventDefault(); };
        this.button8.buttonPress = e => { this.programButtonPressed (8); e.preventDefault(); };

        this.stateValueChangeListener = (event) =>
        {
            if (event.key == "currentProgram")
                this.handleCurrentProgramChange (event.value ^ 0);

            if (event.key == "patchName")
                if (event.value)
                    this.patchName.setValue (event.value);

            if (event.key == "patchList")
                if (event.value)
                    this.initialisePatchList (event.value);
        }

        this.patchConnection.addStoredStateValueListener (this.stateValueChangeListener);

        this.updateValues();

        this.patchConnection.requestStoredStateValue ("currentProgram");
        this.patchConnection.requestStoredStateValue ("patchList");
        this.patchConnection.requestStoredStateValue ("patchName");
    }

    disconnectedCallback()
    {
        super.disconnectedCallback?.();
        this.patchConnection?.removeStoredStateValueListener (this.stateValueChangeListener);
    }

    setNewProgramID (newID)
    {
        this.patchConnection.sendStoredStateValue ("currentProgram", newID);
    }

    setProgramDigit (index, value)
    {
        this.setNewProgramID (presets.getIDWithNewDigit (this.lastKnownProgramID, index, value));
    }

    handleCurrentProgramChange (newProgramID)
    {
        this.lastKnownProgramID = newProgramID;
        this.updateValues();
    }

    updateValues()
    {
        const programID = this.lastKnownProgramID;
        const digits = presets.splitIntoDigits (programID);

        this.digit1.setCurrentValue (digits.d0);
        this.digit10.setCurrentValue (digits.d1);
        this.digit100.setCurrentValue (digits.d2);

        this.recordButton.setCurrentValue (this.recordButtonValue);
        this.fileButton.setCurrentValue (this.fileButtonValue);
        this.bankButton.setCurrentValue (this.bankButtonValue);

        this.patchSelect.selectedIndex = presets.getIndexOfID (programID);

        let currentSelectedButton = 0;

        if (this.fileButtonValue > 0)
            currentSelectedButton = digits.d2;
        else if (this.bankButtonValue > 0)
            currentSelectedButton = digits.d1;
        else
            currentSelectedButton = digits.d0;

        this.button1.setCurrentValue (currentSelectedButton == 1 ? 1 : 0);
        this.button2.setCurrentValue (currentSelectedButton == 2 ? 1 : 0);
        this.button3.setCurrentValue (currentSelectedButton == 3 ? 1 : 0);
        this.button4.setCurrentValue (currentSelectedButton == 4 ? 1 : 0);
        this.button5.setCurrentValue (currentSelectedButton == 5 ? 1 : 0);
        this.button6.setCurrentValue (currentSelectedButton == 6 ? 1 : 0);
        this.button7.setCurrentValue (currentSelectedButton == 7 ? 1 : 0);
        this.button8.setCurrentValue (currentSelectedButton == 8 ? 1 : 0);
    }

    programButtonPressed (v)
    {
        v = v % 10;

        if (this.fileButtonValue == 1)
            this.setProgramDigit (2, v);
        else if (this.bankButtonValue == 1)
            this.setProgramDigit (1, v);
        else
            this.setProgramDigit (0, v);
    }

    initialisePatchList (patchNames)
    {
        for (const patch of patchNames)
        {
            const opt = document.createElement ("option");
            opt.value = patch.id;
            opt.text = patch.id + ": " + patch.name;
            this.patchSelect.add (opt);
        }
    }
}

//==============================================================================
class Pro54PatchView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();

        this.patchConnection = patchConnection;

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();

        this.patchConnection.requestStatusUpdate();

        const container = this.shadowRoot.getElementById ("main");

        for (let child of container.children)
            child.setPatchConnection?.(this.patchConnection);

        this.patchConnection.addAllParameterListener (event =>
        {
            const element = this.shadowRoot.getElementById (event.endpointID);
            element.setCurrentValue?.(event.value);
        });

        this.patchConnection.addEndpointListener ("midiIn", message =>
        {
            this.keyboardElement?.handleExternalMIDI (message.message);
            this.modWheelElement?.handleExternalMIDI (message.message);
            this.pitchBendElement?.handleExternalMIDI (message.message);
            this.midiBlink?.handleExternalMIDI (message.message);
        });

        this.shadowRoot.addEventListener ("contextmenu", (event) => event.preventDefault());
        this.shadowRoot.addEventListener ('touchstart',  (event) => event.preventDefault());

        this.keyboardElement  = this.shadowRoot.getElementById ("Keyboard");
        this.modWheelElement  = this.shadowRoot.getElementById ("ModWheel");
        this.pitchBendElement = this.shadowRoot.getElementById ("PitchBend");
        this.midiBlink        = this.shadowRoot.getElementById ("MidiBlink");
        this.programBank      = this.shadowRoot.getElementById ("ProgramBank");

        this.keyboardElement.addEventListener("note-down", (note) => this.sendNoteOnOffToPatch (note.detail.note, true));
        this.keyboardElement.addEventListener("note-up",   (note) => this.sendNoteOnOffToPatch (note.detail.note, false));

        this.hasOnscreenKeyboard = true; // this property tells hosts that they don't need to provide a keyboard
    }

    getScaleFactorLimits()
    {
        return { minScale: 0.50,
                 maxScale: 1.25 };
    }

    sendNoteOnOffToPatch (note, isOn)
    {
        const controlByte = isOn ? 0x900000 : 0x800000;
        const velocity = 100;

        if (this.patchConnection)
            this.patchConnection.sendMIDIInputEvent ("midiIn", controlByte | (note << 8) | velocity);
    }

    getHTML()
    {
      const newLocal = `
<style>
* {
  box-sizing: border-box;
  user-select: none;
  -webkit-user-select: none;
  -moz-user-select: none;
  -ms-user-select: none;
  margin: 0;
  padding: 0;
}

:host {
  background-color: black;
  position: relative;
}

#main {
  position: relative;
  background-image: url(${this.patchConnection.getResourceAddress ("./gui/assets/background.png")});
  width: 762px;
  height: 358px;
  transform: scale(1.5);
  transform-origin: 0% 0%;
}

.control {
  display: block;
  position: absolute;
  overflow: hidden;
}

.knob {
  width: 25px;
  height: 25px;
}

.button {
  width: 13px;
  height: 19px;
}

.voices {
    width: 20px;
    height: 14px;
}

.wheel {
    width: 10px;
    height: 71px;
}

.midiBlink {
    width: 9px;
    height: 9px;
}

.programDigit {
    width: 10px;
    height: 14px;
}

.programBank {
    width: 100px;
    height: 50px;
}

.programmer {
    width: 100px;
    height: 50px;
}

.patchName {
    position: absolute;
    width: 126px;
    height: 13px;
}

.patchNameText {
    background-color: #330000;
    font-size: 11px;
    font-family: monospace;
    color: #ff0000;
    width: 126px;
    height: 13px;
    border: 0px;
}

::placeholder
{
    color: #dd0000;
}

#PolyModFilterEnv         { left: 23px;   top: 23px; }
#PolyModOscB              { left: 61px;   top: 23px; }
#PolyModFreqA             { left: 95px;   top: 26px; }
#PolyModPWA               { left: 115px;  top: 26px; }
#PolyModFilt              { left: 135px;  top: 26px; }
#OscAFreq                 { left: 168px;  top: 23px; }
#OscASaw                  { left: 202px;  top: 26px; }
#OscAPulse                { left: 222px;  top: 26px; }
#OscAPW                   { left: 244px;  top: 23px; }
#OscASync                 { left: 281px;  top: 26px; }
#MixerOscALevel           { left: 313px;  top: 23px; }
#MixerOscBLevel           { left: 349px;  top: 23px; }
#MixerNoiseLevel          { left: 385px;  top: 23px; }
#ExternalInputLevel       { left: 385px;  top: 69px; }
#FilterCutoff             { left: 432px;  top: 23px; }
#FilterResonance          { left: 468px;  top: 23px; }
#FilterEnvAmt             { left: 504px;  top: 23px; }
#FilterKeyboardTracking   { left: 540px;  top: 23px; }
#FilterAttack             { left: 432px;  top: 69px; }
#FilterDecay              { left: 468px;  top: 69px; }
#FilterSustain            { left: 504px;  top: 69px; }
#FilterRelease            { left: 540px;  top: 69px; }
#DelayTime                { left: 607px;  top: 23px; }
#DelaySpread              { left: 643px;  top: 23px; }
#DelayDepth               { left: 679px;  top: 23px; }
#DelayRate                { left: 715px;  top: 23px; }
#DelayFeedback            { left: 607px;  top: 69px; }
#DelayHiCut               { left: 643px;  top: 69px; }
#DelayLoCut               { left: 679px;  top: 69px; }
#DelayINV                 { left: 721px;  top: 72px; }
#DelayON                  { left: 613px;  top: 118px; }
#DelayWet                 { left: 643px;  top: 115px; }
#DelaySync                { left: 685px;  top: 118px; }
#DelayMidi                { left: 721px;  top: 118px; }
#LfoMidiSync              { left: 41px;   top: 72px; }
#LfoFrequency             { left: 61px;   top: 69px; }
#LfoShapeSaw              { left: 95px;   top: 72px; }
#LfoShapeTri              { left: 115px;  top: 72px; }
#LfoShapePulse            { left: 135px;  top: 72px; }
#OscBFreq                 { left: 168px;  top: 69px; }
#OscBFreqFine             { left: 206px;  top: 69px; }
#OscBShapeSaw             { left: 240px;  top: 72px; }
#OscBShapeTri             { left: 260px;  top: 72px; }
#OscBShapePulse           { left: 280px;  top: 72px; }
#OscBPWAmount             { left: 302px;  top: 69px; }
#OscBSubOsc               { left: 336px;  top: 72px; }
#OscBKKeyboardTracking    { left: 356px;  top: 72px; }
#WheelModulationLfoNoise  { left: 23px;   top: 115px; }
#WheelModulationFreqOscA  { left: 55px;   top: 118px; }
#WheelModulationFreqOscB  { left: 75px;   top: 118px; }
#WheelModulationPWA       { left: 95px;   top: 118px; }
#WheelModulationPWB       { left: 115px;  top: 118px; }
#WheelModulationFilter    { left: 135px;  top: 118px; }
#Glide                    { left: 168px;  top: 115px; }
#Unison                   { left: 212px;  top: 118px; }
#AmplifierAttack          { left: 432px;  top: 115px; }
#AmplifierDecay           { left: 467px;  top: 115px; }
#AmplifierSustain         { left: 504px;  top: 115px; }
#AmplifierRelease         { left: 540px;  top: 115px; }
#Release                  { left: 386px;  top: 118px; }
#Velocity                 { left: 346px;  top: 118px; }
#Repeat                   { left: 21px;   top: 72px; }
#Drone                    { left: 574px;  top: 118px; }
#FilterHPF                { left: 574px;  top: 26px; }
#FilterInvertEnv          { left: 574px;  top: 72px; }
#Analog                   { left: 302px;  top: 115px; }
#MasterTune               { left: 607px;  top: 161px; }
#Volume                   { left: 679px;  top: 161px; }
#ModWheel                 { left: 55px;   top: 258px; }
#PitchBend                { left: 27px;   top: 258px; }
#FilterVersion            { left: 24px;   top: 162px; width: 50px; height: 28px; }
#ActiveVoices             { left: 253px;  top: 120px; }
#TestTone                 { left: 649px;  top: 164px; }
#MidiBlink                { left: 723px;  top: 168px; }
#Digit1                   { left: 266px;  top: 166px; }
#Digit10                  { left: 256px;  top: 166px; }
#Digit100                 { left: 246px;  top: 166px; }
#ProgramButton1           { left: 286px;  top: 164px; }
#ProgramButton2           { left: 306px;  top: 164px; }
#ProgramButton3           { left: 326px;  top: 164px; }
#ProgramButton4           { left: 346px;  top: 164px; }
#ProgramButton5           { left: 366px;  top: 164px; }
#ProgramButton6           { left: 386px;  top: 164px; }
#ProgramButton7           { left: 406px;  top: 164px; }
#ProgramButton8           { left: 426px;  top: 164px; }
#ProgrammerRecord         { left: 174px;  top: 164px; }
#ProgrammerFile           { left: 202px;  top: 164px; }
#ProgrammerBank           { left: 222px;  top: 164px; }
#PatchName                { left: 454px;  top: 166px; }

#Keyboard {
    position: absolute;
    left: 82px;
    top: 249px;
    height: 102px;
}

#program-selector {
    position: absolute;
    left: 454px; top: 164px; width: 128px; height: 17px;
    background: none;
    border: none;
    outline: none;
    opacity: 0;
}

</style>

<div id="main">
    <pro54-black-knob     id="PolyModFilterEnv"         min-value="0"  max-value="100"   label="PolyMod Source Filt Env"></pro54-black-knob>
    <pro54-black-knob     id="PolyModOscB"              min-value="0"  max-value="100"   label="PolyMod Source Osc B"></pro54-black-knob>
    <pro54-black-button   id="PolyModFreqA"             min-value="0"  max-value="1"     label="PolyMod Dest Freq A"></pro54-black-button>
    <pro54-black-button   id="PolyModPWA"               min-value="0"  max-value="1"     label="PolyMod Dest PWidth A"></pro54-black-button>
    <pro54-black-button   id="PolyModFilt"              min-value="0"  max-value="1"     label="PolyMod Dest Filter"></pro54-black-button>
    <pro54-black-knob     id="OscAFreq"                 min-value="0"  max-value="100"   label="Oscillator A Frequency"></pro54-black-knob>
    <pro54-black-button   id="OscASaw"                  min-value="0"  max-value="1"     label="Oscillator A Sawtooth"></pro54-black-button>
    <pro54-black-button   id="OscAPulse"                min-value="0"  max-value="1"     label="Oscillator A Pulse"></pro54-black-button>
    <pro54-black-knob     id="OscAPW"                   min-value="0"  max-value="100"   label="Oscillator A PulseWidth"></pro54-black-knob>
    <pro54-black-button   id="OscASync"                 min-value="0"  max-value="1"     label="Oscillator A Sync"></pro54-black-button>
    <pro54-black-knob     id="MixerOscALevel"           min-value="0"  max-value="100"   label="Mixer Oscillator A"></pro54-black-knob>
    <pro54-black-knob     id="MixerOscBLevel"           min-value="0"  max-value="100"   label="Mixer Oscillator B"></pro54-black-knob>
    <pro54-black-knob     id="MixerNoiseLevel"          min-value="0"  max-value="100"   label="Mixer Noise"></pro54-black-knob>
    <pro54-black-knob     id="ExternalInputLevel"       min-value="0"  max-value="100"   label="Mixer External Input"></pro54-black-knob>
    <pro54-black-knob     id="FilterCutoff"             min-value="0"  max-value="100"   label="Filter Cutoff"></pro54-black-knob>
    <pro54-black-knob     id="FilterResonance"          min-value="0"  max-value="100"   label="Filter Resonance"></pro54-black-knob>
    <pro54-black-knob     id="FilterEnvAmt"             min-value="0"  max-value="100"   label="Filter Envelope Amount"></pro54-black-knob>
    <pro54-black-knob     id="FilterKeyboardTracking"   min-value="0"  max-value="100"   label="Filter Keyboard Follow"></pro54-black-knob>
    <pro54-black-knob     id="FilterAttack"             min-value="0"  max-value="100"   label="Filter Attack"></pro54-black-knob>
    <pro54-black-knob     id="FilterDecay"              min-value="0"  max-value="100"   label="Filter Decay"></pro54-black-knob>
    <pro54-black-knob     id="FilterSustain"            min-value="0"  max-value="100"   label="Filter Sustain"></pro54-black-knob>
    <pro54-black-knob     id="FilterRelease"            min-value="0"  max-value="100"   label="Filter Release"></pro54-black-knob>
    <pro54-black-knob     id="DelayTime"                min-value="0"  max-value="100"   label="Delay Effect Time"></pro54-black-knob>
    <pro54-black-knob     id="DelaySpread"              min-value="0"  max-value="100"   label="Delay Effect Spread"></pro54-black-knob>
    <pro54-black-knob     id="DelayDepth"               min-value="0"  max-value="100"   label="Delay Effect Depth"></pro54-black-knob>
    <pro54-black-knob     id="DelayRate"                min-value="0"  max-value="100"   label="Delay Effect Rate"></pro54-black-knob>
    <pro54-black-knob     id="DelayFeedback"            min-value="0"  max-value="100"   label="Delay Effect Feedback"></pro54-black-knob>
    <pro54-black-knob     id="DelayHiCut"               min-value="0"  max-value="100"   label="Delay Effect Low Cut"></pro54-black-knob>
    <pro54-black-knob     id="DelayLoCut"               min-value="0"  max-value="100"   label="Delay Effect High Cut"></pro54-black-knob>
    <pro54-black-button   id="DelayINV"                 min-value="0"  max-value="1"     label="Delay Effect Invert"></pro54-black-button>
    <pro54-black-button   id="DelayON"                  min-value="0"  max-value="1"     label="Delay Effect On"></pro54-black-button>
    <pro54-black-knob     id="DelayWet"                 min-value="0"  max-value="100"   label="Delay Effect Wet"></pro54-black-knob>
    <pro54-black-button   id="DelaySync"                min-value="0"  max-value="1"     label="Delay Effect Sync"></pro54-black-button>
    <pro54-black-button   id="DelayMidi"                min-value="0"  max-value="1"     label="Delay Effect MIDI Sync"></pro54-black-button>
    <pro54-black-button   id="LfoMidiSync"              min-value="0"  max-value="1"     label="LFO MIDI Sync"></pro54-black-button>
    <pro54-black-knob     id="LfoFrequency"             min-value="0"  max-value="100"   label="LFO Frequency"></pro54-black-knob>
    <pro54-black-button   id="LfoShapeSaw"              min-value="0"  max-value="1"     label="LFO Sawtooth"></pro54-black-button>
    <pro54-black-button   id="LfoShapeTri"              min-value="0"  max-value="1"     label="LFO Triangle"></pro54-black-button>
    <pro54-black-button   id="LfoShapePulse"            min-value="0"  max-value="1"     label="LFO Pulse"></pro54-black-button>
    <pro54-black-knob     id="OscBFreq"                 min-value="0"  max-value="100"   label="Oscillator B Frequency"></pro54-black-knob>
    <pro54-black-knob     id="OscBFreqFine"             min-value="0"  max-value="100"   label="Oscillator B Freq Fine"></pro54-black-knob>
    <pro54-black-button   id="OscBShapeSaw"             min-value="0"  max-value="1"     label="Oscillator B Sawtooth"></pro54-black-button>
    <pro54-black-button   id="OscBShapeTri"             min-value="0"  max-value="1"     label="Oscillator B Triangle"></pro54-black-button>
    <pro54-black-button   id="OscBShapePulse"           min-value="0"  max-value="1"     label="Oscillator B Pulse"></pro54-black-button>
    <pro54-black-knob     id="OscBPWAmount"             min-value="0"  max-value="100"   label="Oscillator B PulseWidth"></pro54-black-knob>
    <pro54-black-button   id="OscBSubOsc"               min-value="0"  max-value="1"     label="Oscillator B Low Freq"></pro54-black-button>
    <pro54-black-button   id="OscBKKeyboardTracking"    min-value="0"  max-value="1"     label="Oscillator B Key Follow"></pro54-black-button>
    <pro54-black-knob     id="WheelModulationLfoNoise"  min-value="0"  max-value="100"   label="WheelMod LFO-Noise Mix"></pro54-black-knob>
    <pro54-black-button   id="WheelModulationFreqOscA"  min-value="0"  max-value="1"     label="WheelMod Dest Freq A"></pro54-black-button>
    <pro54-black-button   id="WheelModulationFreqOscB"  min-value="0"  max-value="1"     label="WheelMod Dest Freq B"></pro54-black-button>
    <pro54-black-button   id="WheelModulationPWA"       min-value="0"  max-value="1"     label="WheelMod Dest PWidth A"></pro54-black-button>
    <pro54-black-button   id="WheelModulationPWB"       min-value="0"  max-value="1"     label="WheelMod Dest PWidth B"></pro54-black-button>
    <pro54-black-button   id="WheelModulationFilter"    min-value="0"  max-value="1"     label="WheelMod Dest Filter"></pro54-black-button>
    <pro54-black-knob     id="Glide"                    min-value="0"  max-value="100"   label="Glide Time"></pro54-black-knob>
    <pro54-black-button   id="Unison"                   min-value="0"  max-value="1"     label="Unisono Mode"></pro54-black-button>
    <pro54-black-knob     id="AmplifierAttack"          min-value="0"  max-value="100"   label="Amplifier Attack"></pro54-black-knob>
    <pro54-black-knob     id="AmplifierDecay"           min-value="0"  max-value="100"   label="Amplifier Decay"></pro54-black-knob>
    <pro54-black-knob     id="AmplifierSustain"         min-value="0"  max-value="100"   label="Amplifier Sustain"></pro54-black-knob>
    <pro54-black-knob     id="AmplifierRelease"         min-value="0"  max-value="100"   label="Amplifier Release"></pro54-black-knob>
    <pro54-black-button   id="Release"                  min-value="0"  max-value="1"     label="Release on/off"></pro54-black-button>
    <pro54-black-button   id="Velocity"                 min-value="0"  max-value="1"     label="Velocity on/off"></pro54-black-button>
    <pro54-black-button   id="Repeat"                   min-value="0"  max-value="1"     label="LFO Envelope Trigger"></pro54-black-button>
    <pro54-black-button   id="Drone"                    min-value="0"  max-value="1"     label="Amplifier Hold"></pro54-black-button>
    <pro54-black-button   id="FilterHPF"                min-value="0"  max-value="100"   label="Filter HPF-Mode"></pro54-black-button>
    <pro54-black-button   id="FilterInvertEnv"          min-value="0"  max-value="1"     label="Filter Envelope Invert"></pro54-black-button>
    <pro54-black-knob     id="Analog"                   min-value="0"  max-value="100"   label="Analog"></pro54-black-knob>
    <pro54-metal-knob     id="MasterTune"               min-value="0"  max-value="100"   label="Master Tune"></pro54-metal-knob>
    <pro54-metal-knob     id="Volume"                   min-value="0"  max-value="100"   label="Master Volume"></pro54-metal-knob>
    <pro54-wheel          id="ModWheel"                 min-value="0"  max-value="100"   label="Modulation Wheel"></pro54-wheel>
    <pro54-wheel          id="PitchBend"                min-value="0"  max-value="100"   label="Pitch Bend"></pro54-wheel>
    <pro54-filter         id="FilterVersion"            min-value="0"  max-value="1"     label="Filter Version"></pro54-filter>
    <pro54-voices         id="ActiveVoices"             min-value="1"  max-value="32"    label="Active Voices"></pro54-voices>
    <pro54-grey-button    id="TestTone"                 min-value="0"  max-value="1"     label="Test Tone"></pro54-grey-button>

    <pro54-program-bank id="ProgramBank"></pro54-program-bank>
    <pro54-midi-light id="MidiBlink" min-value="0" max-value="1" label="Midi Blink"></pro54-midi-light>
    <pro54-keyboard id="Keyboard"></pro54-keyboard>

</div>
`;
        return newLocal;
    }
}

window.customElements.define ("pro54-black-knob", Pro54BlackKnob);
window.customElements.define ("pro54-metal-knob", Pro54MetalKnob);
window.customElements.define ("pro54-black-button", Pro54BlackButton);
window.customElements.define ("pro54-grey-button", Pro54GreyButton);
window.customElements.define ("pro54-orange-button", Pro54OrangeButton);
window.customElements.define ("pro54-voices", Pro54VoicesElement);
window.customElements.define ("pro54-wheel", Pro54WheelElement);
window.customElements.define ("pro54-filter", Pro54FilterElement);
window.customElements.define ("pro54-midi-light", Pro54MIDIActivityLight);
window.customElements.define ("pro54-program-digit", Pro54ProgramDigitElement);
window.customElements.define ("pro54-program-bank", Pro54ProgramBank);
window.customElements.define ("pro54-program-name", Pro54ProgramName);
window.customElements.define ("pro54-keyboard", Pro54Keyboard);
window.customElements.define ("pro54-patch-view", Pro54PatchView);
