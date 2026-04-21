
/*
    Tuner-style GUI for the PitchDetector patch.

    Displays the detected note name, cents offset from the nearest semitone,
    and the raw frequency in Hz. A horizontal gauge shows the tuning offset
    visually, with colour coding: green when close to in-tune, amber/red
    when far off.
*/

// Note name lookup table — using flats for black keys (musical convention)
const noteNames = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];

/// Convert a frequency in Hz to the nearest MIDI note number.
function frequencyToNearestNote (freq)
{
    return Math.round (12 * Math.log2 (freq / 440) + 69);
}

/// Calculate how many cents the frequency is away from the nearest semitone.
/// Returns a value in the range -50 to +50.
function frequencyToCentsOffset (freq)
{
    const nearestNote = frequencyToNearestNote (freq);
    const nearestFreq = 440 * Math.pow (2, (nearestNote - 69) / 12);
    return 1200 * Math.log2 (freq / nearestFreq);
}

/// Get the display name for a MIDI note number, e.g. "A4", "C#5", "Eb3".
function noteNumberToName (noteNumber)
{
    const name = noteNames[noteNumber % 12];
    const octave = Math.floor (noteNumber / 12) - 1;
    return name + octave;
}

/// Returns true if a MIDI note number is a white (natural) key.
function isNaturalNote (note)
{
    return [0, 2, 4, 5, 7, 9, 11].includes (note % 12);
}

/// Convert a frequency to a fractional MIDI note number (not rounded).
/// e.g. A4 (440Hz) = 69.0, A4 slightly sharp = 69.1, etc.
function frequencyToFractionalNote (freq)
{
    return 12 * Math.log2 (freq / 440) + 69;
}

// --- Keyboard layout constants ---
// The keyboard spans C1 (MIDI 24) to C7 (MIDI 96).
const keyboardFirstNote = 24;
const keyboardLastNote  = 96;

/// Count the white keys in a MIDI note range (inclusive).
function countWhiteKeys (first, last)
{
    let count = 0;

    for (let n = first; n <= last; n++)
        if (isNaturalNote (n)) count++;

    return count;
}

const keyboardWhiteKeyCount = countWhiteKeys (keyboardFirstNote, keyboardLastNote);

/// Map a fractional MIDI note to a horizontal percentage (0–100) across the
/// keyboard. The layout is based on white-key positions: each white key
/// occupies one equal-width slot. For notes between white keys (i.e. sharps/
/// flats, or fractional pitches), we interpolate linearly between the centres
/// of the neighbouring white keys.
function noteToKeyboardPercent (fractionalNote)
{
    // Clamp to the keyboard range
    const clamped = Math.max (keyboardFirstNote, Math.min (keyboardLastNote, fractionalNote));

    // Find the integer note below and above
    const noteBelow = Math.floor (clamped);
    const fraction  = clamped - noteBelow;

    // Convert an integer MIDI note to the x-position of its centre, expressed
    // in white-key units. White keys are at positions 0.5, 1.5, 2.5, ...
    // Black keys sit at the boundary between their neighbours.
    function noteToCentre (note)
    {
        // Count white keys from the start up to (but not including) this note
        let whitesBefore = countWhiteKeys (keyboardFirstNote, note - 1);

        if (isNaturalNote (note))
            return whitesBefore + 0.5;  // centre of this white key

        // Black key — sits on the boundary between its neighbours
        return whitesBefore;
    }

    const posBelow = noteToCentre (noteBelow);
    const posAbove = (noteBelow < keyboardLastNote) ? noteToCentre (noteBelow + 1) : posBelow;
    const pos      = posBelow + fraction * (posAbove - posBelow);

    return (pos / keyboardWhiteKeyCount) * 100;
}

/// Build the piano keyboard HTML for the range firstNote..lastNote (inclusive).
/// Includes a floating marker element that can be positioned continuously.
function buildKeyboardHTML (firstNote, lastNote)
{
    const whiteKeyCount = countWhiteKeys (firstNote, lastNote);

    // Layout: position each key absolutely within a container whose width
    // is determined by the number of white keys.
    let naturals = "";
    let accidentals = "";
    let whiteIndex = 0;

    for (let n = firstNote; n <= lastNote; n++)
    {
        if (isNaturalNote (n))
        {
            // White key — position by its sequential index among white keys.
            // Width is expressed as a percentage of the container.
            const leftPercent  = (whiteIndex / whiteKeyCount) * 100;
            const widthPercent = (1 / whiteKeyCount) * 100;

            naturals += `<div class="kb-white"
                style="left:${leftPercent}%; width:${widthPercent}%"></div>`;
            whiteIndex++;
        }
        else
        {
            // Black key — centered on the boundary between its two neighbouring
            // white keys. whiteIndex already points past the preceding white key,
            // so the boundary sits at exactly whiteIndex / whiteKeyCount.
            const centerPercent = (whiteIndex / whiteKeyCount) * 100;
            const widthPercent  = (0.6 / whiteKeyCount) * 100;

            accidentals += `<div class="kb-black"
                style="left:${centerPercent - widthPercent / 2}%; width:${widthPercent}%"></div>`;
        }
    }

    // The marker is a thin vertical line that floats over the keyboard,
    // positioned via noteToKeyboardPercent() in updateDisplay().
    return `<div class="keyboard">
                ${naturals}${accidentals}
                <div class="kb-marker" id="kbMarker"></div>
            </div>`;
}

/// Map a cents offset (-50 to +50) to a CSS colour.
/// Green near center, amber at ±25, red at ±50.
function centsToColour (cents)
{
    const absCents = Math.min (Math.abs (cents), 50);

    if (absCents < 5)   return "#4caf50";  // green — in tune
    if (absCents < 15)  return "#8bc34a";  // light green
    if (absCents < 25)  return "#ffc107";  // amber
    if (absCents < 40)  return "#ff9800";  // orange
    return "#f44336";                       // red — way off
}


// This is the web-component that we'll return for our patch's view.
// It uses the built-in ParameterControls library for the sensitivity knob,
// and custom DOM elements for the tuner display.
class TunerView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;
        this.Controls = this.patchConnection.utilities.ParameterControls;
        this.classList = "tuner-main";
        this.currentFrequency = 0;
        this.currentClarity = 0;
        this.innerHTML = this.getHTML();
    }

    connectedCallback()
    {
        // --- Wire up the sensitivity knob using the built-in parameter control ---
        this.statusListener = (status) =>
        {
            const knobHolder = this.querySelector ("#sensitivity-knob");
            knobHolder.innerHTML = "";

            const control = this.Controls.createLabelledControlForEndpointID (this.patchConnection, status, "sensitivity");
            knobHolder.appendChild (control);
        };

        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();

        // --- Listen for pitch detection events from the Cmajor patch ---
        this.frequencyListener = (value) =>
        {
            this.currentFrequency = value;
            this.updateDisplay();
        };

        this.clarityListener = (value) =>
        {
            this.currentClarity = value;
        };

        this.patchConnection.addEndpointListener ("detectedFrequency", this.frequencyListener);
        this.patchConnection.addEndpointListener ("detectedClarity", this.clarityListener);
    }

    disconnectedCallback()
    {
        this.patchConnection.removeStatusListener (this.statusListener);
        this.patchConnection.removeEndpointListener ("detectedFrequency", this.frequencyListener);
        this.patchConnection.removeEndpointListener ("detectedClarity", this.clarityListener);
    }

    updateDisplay()
    {
        const noteNameEl     = this.querySelector ("#noteName");
        const needleEl       = this.querySelector ("#needle");
        const centsReadoutEl = this.querySelector ("#centsReadout");
        const freqReadoutEl  = this.querySelector ("#freqReadout");

        const hasSignal = this.currentFrequency > 20 && this.currentClarity > 0;

        if (! hasSignal)
        {
            // No confident pitch — show dimmed "no signal" state.
            // Clear any inline colour left over from the last detected note,
            // so the .no-signal class colour takes effect.
            noteNameEl.textContent = "---";
            noteNameEl.className = "note-name no-signal";
            noteNameEl.style.color = "";
            centsReadoutEl.textContent = "--";
            centsReadoutEl.className = "cents-readout no-signal";
            centsReadoutEl.style.color = "";
            freqReadoutEl.textContent = "-- Hz";
            needleEl.style.left = "50%";
            needleEl.style.backgroundColor = "#444";
            this.updateKeyboardMarker (-1);
            return;
        }

        const noteNumber = frequencyToNearestNote (this.currentFrequency);
        const cents      = frequencyToCentsOffset (this.currentFrequency);
        const colour     = centsToColour (cents);

        // Note name (e.g. "A4", "C#5")
        noteNameEl.textContent = noteNumberToName (noteNumber);
        noteNameEl.className = "note-name";
        noteNameEl.style.color = colour;

        // Gauge needle position: map -50..+50 cents to 0%..100%
        const needlePercent = 50 + cents;
        needleEl.style.left = Math.max (0, Math.min (100, needlePercent)) + "%";
        needleEl.style.backgroundColor = colour;

        // Cents readout (e.g. "+12¢" or "-3¢")
        const centsRounded = Math.round (cents);
        const centsSign    = centsRounded > 0 ? "+" : "";
        centsReadoutEl.textContent = centsSign + centsRounded + "¢";
        centsReadoutEl.className = "cents-readout";
        centsReadoutEl.style.color = colour;

        // Frequency readout
        freqReadoutEl.textContent = this.currentFrequency.toFixed (1) + " Hz";

        // Position the floating marker on the keyboard using the exact
        // fractional pitch — not snapped to the nearest semitone.
        const fractionalNote = frequencyToFractionalNote (this.currentFrequency);
        this.updateKeyboardMarker (fractionalNote);
    }

    updateKeyboardMarker (fractionalNote)
    {
        const marker = this.querySelector ("#kbMarker");

        if (fractionalNote < 0)
        {
            marker.style.opacity = "0";
            return;
        }

        marker.style.opacity = "1";
        marker.style.left = noteToKeyboardPercent (fractionalNote) + "%";
    }

    getHTML()
    {
        return `
        <style>
            .tuner-main {
                display: block;
                background: #101010;
                height: 100%;
                width: 100%;
            }

            .tuner-display {
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
                color: #e0e0e0;
                display: flex;
                flex-direction: column;
                align-items: center;
                width: 100%;
                box-sizing: border-box;
                padding: 0.625rem 1.25rem;
                user-select: none;
            }

            .tuner-main * {
                --knob-dial-background-color: #2a2a3e;
                --knob-dial-border-color: #4caf50;
                --knob-dial-tick-color: #4caf50;
                --knob-track-background-color: #333;
                --knob-track-value-color: #4caf50;
            }

            .note-name {
                font-size: 3rem;
                font-weight: 700;
                letter-spacing: 0.125rem;
                line-height: 1;
                transition: color 0.15s ease;
                padding-top: 1rem;
            }

            .gauge-container {
                width: 100%;
                max-width: 20rem;
                margin: 0.75rem 0;
            }

            .gauge-labels {
                display: flex;
                justify-content: space-between;
                font-size: 0.6875rem;
                color: #666;
                margin-bottom: 0.25rem;
                padding: 0 0.125rem;
            }

            .gauge-track {
                position: relative;
                height: 1.5rem;
                background: #2a2a3e;
                border-radius: 0.75rem;
                overflow: hidden;
            }

            .gauge-center-line {
                position: absolute;
                left: 50%;
                top: 0;
                bottom: 0;
                width: 0.125rem;
                background: #555;
                transform: translateX(-50%);
            }

            .gauge-needle {
                position: absolute;
                top: 0.125rem;
                bottom: 0.125rem;
                width: 0.375rem;
                border-radius: 0.1875rem;
                background: #4caf50;
                transform: translateX(-50%);
                left: 50%;
                transition: left 0.12s ease-out, background-color 0.15s ease;
            }

            .cents-readout {
                font-size: 1.375rem;
                font-weight: 500;
                margin-top: 0.5rem;
                min-height: 1.875rem;
                transition: color 0.15s ease;
            }

            .frequency-readout {
                font-size: 0.875rem;
                color: #888;
                margin-top: 0.25rem;
                min-height: 1.25rem;
            }

            .no-signal {
                color: #444;
            }

            #sensitivity-knob {
                margin-top: 1rem;
            }

            .keyboard {
                position: relative;
                width: 100%;
                height: 2.5rem;
                margin-top: 1rem;
                border-radius: 0.25rem;
                overflow: hidden;
            }

            .kb-white {
                position: absolute;
                top: 0;
                height: 100%;
                background: #444;
                border-right: 0.0625rem solid #1a1a2e;
                box-sizing: border-box;
            }

            .kb-black {
                position: absolute;
                top: 0;
                height: 60%;
                background: #222;
                border-radius: 0 0 0.1rem 0.1rem;
                z-index: 1;
            }

            .kb-marker {
                position: absolute;
                top: 0;
                bottom: 0;
                width: 0.15rem;
                background: #4caf50;
                border-radius: 0.03rem;
                z-index: 2;
                transform: translateX(-50%);
                transition: left 0.1s ease-out, opacity 0.15s ease;
                opacity: 0;
                box-shadow: 0 0 0.375rem rgba(76, 175, 80, 0.6);
            }

            ${this.Controls.getAllCSS()}
        </style>

        <div class="tuner-display">
            <div class="note-name no-signal" id="noteName">---</div>

            <div class="gauge-container">
                <div class="gauge-labels">
                    <span>-50¢</span>
                    <span>0</span>
                    <span>+50¢</span>
                </div>
                <div class="gauge-track">
                    <div class="gauge-center-line"></div>
                    <div class="gauge-needle" id="needle"></div>
                </div>
            </div>

            <div class="cents-readout no-signal" id="centsReadout">--</div>
            <div class="frequency-readout" id="freqReadout">-- Hz</div>

            ${buildKeyboardHTML (24, 96)}

            <div id="sensitivity-knob"></div>
        </div>`;
    }
}


/* This is the function that a host (the command line patch player, or a Cmajor plugin
   loader, or our VScode extension, etc) will call in order to create a view for your patch.
*/
export default function createPatchView (patchConnection)
{
    const customElementName = "tuner-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, TunerView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
