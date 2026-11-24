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

import PianoKeyboard from "../../cmaj_api/cmaj-piano-keyboard.js";
import * as midi from "../../cmaj_api/cmaj-midi-helpers.js";
import { el } from "../core/dom.js";
import { EndpointWidget } from "./widget-base.js";
import { MIDIControls } from "./midi-controls.js";

const keyboardTagName = "cmaj-panel-piano-keyboard";

/// The cmaj_api keyboard, re-drawn when its range attributes change (which the base class
/// observes, but only reads when it's created), so that the octave buttons can shift it
class PanelPianoKeyboard  extends PianoKeyboard
{
    attributeChangedCallback (_name, oldValue, newValue)
    {
        if (oldValue === newValue || ! this.notes)
            return;

        // (the held notes are released first, with the note numbers they were played with)
        this.allNotesOff();
        this.notes = [];
        this.refreshHTML();

        for (const child of this.root.children)
        {
            child.addEventListener ("touchstart", (event) => this.touchStart (event), { passive: false });
            child.addEventListener ("touchend",   (event) => this.touchEnd (event));
        }
    }
}

if (! customElements.get (keyboardTagName))
    customElements.define (keyboardTagName, PanelPianoKeyboard);

/*
    A MIDI input: an on-screen keyboard that plays into the endpoint (with the computer keyboard
    too, when it has focus), which lights up the notes arriving from anywhere else (the patch's
    GUI, a MIDI device), and shows the last message. Above it, the MIDIControls strip
    (midi-controls.js) sets its velocity, channel and octave, and has the bend, mod, sustain and
    panic controls.

    The compact one (in the graph's live mode) has no keyboard: it's a note-activity strip
    showing the notes that are playing, and its popover has the keyboard.
*/
export class MIDIInputWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "midi-in");

        if (this.isCompact)
        {
            this.strip = new NoteActivityStrip();
            this.bodyElement.append (this.strip.element);
            this.disposer.add (() => this.strip.dispose());
        }
        else
        {
            this.createKeyboard();
        }

        this.setValueText ("");

        this.subscribe (event =>
        {
            const message = event?.message;

            if (typeof message === "number")
            {
                this.keyboard?.handleExternalMIDI (message);
                this.strip?.handleMIDI (message);
                this.setValueText (describeMIDIMessage (message));
                this.flash();
            }
        });
    }

    createKeyboard()
    {
        const KeyboardClass = customElements.get (keyboardTagName);

        // (the keys keep their black and white in both themes; the lines and the pressed colour
        // come from the design tokens, which inherit into the keyboard's shadow root)
        const keyLine = "1px solid var(--cp-divider-strong)";
        const pressed = "var(--cp-event)";

        this.keyboard = new KeyboardClass ({ naturalNoteWidth: 16, accidentalWidth: 10, naturalNoteBorder: keyLine,
                                             accidentalNoteBorder: keyLine, pressedNoteColour: pressed });

        this.keyboard.setAttribute ("root-note", "36");
        this.keyboard.setAttribute ("note-count", "61");
        this.keyboard.classList.add ("cp-keyboard");
        this.keyboard.setAttribute ("aria-label", "Keyboard for " + this.endpoint.endpointID);

        // (the controls send the keyboard's notes, with their channel and velocity)
        this.controls = new MIDIControls ({ endpointID: this.endpoint.endpointID, patchConnection: this.patchConnection, keyboard: this.keyboard,
                                            settings: this.options.settings, store: this.options.store, scheduler: this.scheduler });

        this.bodyElement.append (this.controls.element, this.keyboard);
        this.disposer.add (() => { this.keyboard.allNotesOff?.(); this.controls.dispose(); });
    }
}

//==============================================================================
/// A MIDI output, in the graph's live mode: the note-activity strip and the last message.
/// (At full size, a MIDI output is an EventMonitorWidget, with its recent messages.)
export class MIDIActivityWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "midi-out");

        this.strip = new NoteActivityStrip();
        this.bodyElement.append (this.strip.element);
        this.disposer.add (() => this.strip.dispose());
        this.setValueText ("");

        this.subscribe (event =>
        {
            const message = event?.message;

            if (typeof message === "number")
            {
                this.strip.handleMIDI (message);
                this.setValueText (describeMIDIMessage (message));
                this.flash();
            }
        });
    }
}

//==============================================================================
/*
    A thin strip with a mark for each note that's playing, placed by pitch across the range of
    a piano (notes outside it sit at the ends). A released note fades out.
*/
export class NoteActivityStrip
{
    constructor ({ lowestNote = 21, highestNote = 108 } = {})
    {
        this.lowestNote = lowestNote;
        this.highestNote = highestNote;
        this.marks = new Map();     // note -> element
        this.element = el ("div", { class: "cp-note-strip", role: "img", "aria-label": "No notes playing" });
    }

    dispose()
    {
        this.allNotesOff();
    }

    handleMIDI (message)
    {
        if (midi.isNoteOn (message))
            this.noteOn (midi.getNoteNumber (message), midi.getVelocity (message) / 127);
        else if (midi.isNoteOff (message))
            this.noteOff (midi.getNoteNumber (message));
        else if (midi.isAllNotesOff (message) || midi.isAllSoundOff (message))
            this.allNotesOff();
    }

    noteOn (note, velocity = 1)
    {
        this.marks.get (note)?.remove();

        const range = this.highestNote - this.lowestNote + 1;
        const position = Math.max (0, Math.min (range - 1, note - this.lowestNote));
        const mark = el ("span", { class: "cp-note", style: `left: ${(position / range * 100).toFixed (3)}%; width: ${(100 / range).toFixed (3)}%;`
                                                            + ` --cp-note-velocity: ${(0.45 + 0.55 * velocity).toFixed (2)}` });

        this.marks.set (note, mark);
        this.element.append (mark);
        this.updateLabel();
    }

    noteOff (note)
    {
        const mark = this.marks.get (note);

        if (! mark)
            return;

        this.marks.delete (note);
        this.release (mark);
        this.updateLabel();
    }

    allNotesOff()
    {
        for (const mark of this.marks.values())
            mark.remove();

        this.marks.clear();
        this.updateLabel();
    }

    /// Fades the mark out, then removes it (straight away if there's no transition, e.g. under reduced motion)
    release (mark)
    {
        mark.classList.add ("cp-note-released");
        const remove = () => mark.remove();
        mark.addEventListener ("transitionend", remove, { once: true });
        setTimeout (remove, 1000);
    }

    updateLabel()
    {
        const notes = [...this.marks.keys()].sort ((a, b) => a - b).map (n => midi.getNoteNameWithOctaveNumber (n));
        this.element.setAttribute ("aria-label", notes.length > 0 ? "Playing " + notes.join (", ") : "No notes playing");
    }
}

/// A short description of a MIDI message, for a widget's value text
export function describeMIDIMessage (message)
{
    if (midi.isNoteOn (message) || midi.isNoteOff (message))
        return `${midi.isNoteOn (message) ? "on" : "off"} ${midi.getNoteNameWithOctaveNumber (midi.getNoteNumber (message))}`;

    return midi.getMIDIDescription (message);
}
