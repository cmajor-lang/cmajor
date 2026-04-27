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

import * as midi from "./cmaj-midi-helpers.js"
import { PatchConnection } from "./cmaj-patch-connection.js"

/**
 *  An general-purpose on-screen piano keyboard component that allows clicks or
 *  key-presses to be used to play things.
 *
 *  To receive events, you can attach "note-down" and "note-up" event listeners via
 *  the standard HTMLElement/EventTarget event system, e.g.
 *
 *  myKeyboardElement.addEventListener("note-down", (note) => { ...handle note on... });
 *  myKeyboardElement.addEventListener("note-up",   (note) => { ...handle note off... });
 *
 *  The `note` object will contain a `note` property with the MIDI note number.
 *  (And obviously you can remove them with removeEventListener)
 *
 *  Or, if you're connecting the keyboard to a PatchConnection, you can use the helper
 *  method attachToPatchConnection() to create and attach some suitable listeners.
 *
 */
export default class PianoKeyboard extends HTMLElement
{
    /** @param {{ naturalNoteWidth?: number, accidentalWidth?: number, accidentalPercentageHeight?: number, naturalNoteBorder?: string, accidentalNoteBorder?: string, pressedNoteColour?: string }} [options] */
    constructor ({ naturalNoteWidth,
                   accidentalWidth,
                   accidentalPercentageHeight,
                   naturalNoteBorder,
                   accidentalNoteBorder,
                   pressedNoteColour } = {})
    {
        super();

        this.naturalWidth = naturalNoteWidth || 20;
        this.accidentalWidth = accidentalWidth || 12;
        this.accidentalPercentageHeight = accidentalPercentageHeight || 66;
        this.naturalBorder = naturalNoteBorder || "2px solid #333";
        this.accidentalBorder = accidentalNoteBorder || "2px solid #333";
        this.pressedColour = pressedNoteColour || "#8ad";

        this.root = this.attachShadow({ mode: "open" });

        this.root.addEventListener ("mousedown",   (event) => this.handleMouse (/** @type {MouseEvent} */ (event), true, false) );
        this.root.addEventListener ("mouseup",     (event) => this.handleMouse (/** @type {MouseEvent} */ (event), false, true) );
        this.root.addEventListener ("mousemove",   (event) => this.handleMouse (/** @type {MouseEvent} */ (event), false, false) );
        this.root.addEventListener ("mouseenter",  (event) => this.handleMouse (/** @type {MouseEvent} */ (event), false, false) );
        this.root.addEventListener ("mouseout",    (event) => this.handleMouse (/** @type {MouseEvent} */ (event), false, false) );

        this.addEventListener ("keydown",  (event) => this.handleKey (/** @type {KeyboardEvent} */ (event), true));
        this.addEventListener ("keyup",    (event) => this.handleKey (/** @type {KeyboardEvent} */ (event), false));
        this.addEventListener ("focusout", () => this.allNotesOff());

        this.currentDraggedNote = -1;
        this.currentExternalNotesOn = new Set();
        this.currentKeyboardNotes = new Set();
        this.currentPlayedNotes = new Set();
        this.currentDisplayedNotes = new Set();
        /** @type {{note: number, element: HTMLElement | null}[]} */
        this.notes = [];
        this.modifierKeys = 0;
        this.currentTouches = new Map();

        this.refreshHTML();

        for (let child of this.root.children)
        {
            child.addEventListener ("touchstart", (event) => this.touchStart (/** @type {TouchEvent} */ (event)), {passive:false} );
            child.addEventListener ("touchend",   (event) => this.touchEnd (/** @type {TouchEvent} */ (event)) );
        }
    }

    static get observedAttributes()
    {
        return ["root-note", "note-count", "key-map"];
    }

    get config()
    {
        return {
            rootNote: parseInt(this.getAttribute("root-note") || "36"),
            numNotes: parseInt(this.getAttribute("note-count") || "61"),
            keymap: this.getAttribute("key-map") || "KeyA KeyW KeyS KeyE KeyD KeyF KeyT KeyG KeyY KeyH KeyU KeyJ KeyK KeyO KeyL KeyP Semicolon",
        };
    }

    /** This attaches suitable listeners to make this keyboard control the given MIDI
     *  endpoint of a PatchConnection object. Use detachPatchConnection() to remove
     *  a connection later on.
     *
     *  @param {PatchConnection} patchConnection
     *  @param {string} midiInputEndpointID
     */
    attachToPatchConnection (patchConnection, midiInputEndpointID)
    {
        const velocity = 100;

        const callbacks = {
            noteDown: /** @type {EventListener} */ ((/** @type {CustomEvent<{note: number}>} */ e) => patchConnection.sendMIDIInputEvent (midiInputEndpointID, 0x900000 | (e.detail.note << 8) | velocity)),
            noteUp:   /** @type {EventListener} */ ((/** @type {CustomEvent<{note: number}>} */ e) => patchConnection.sendMIDIInputEvent (midiInputEndpointID, 0x800000 | (e.detail.note << 8) | velocity)),
            midiIn:   (/** @type {{message: number}} */ e) => this.handleExternalMIDI (e.message),
            midiInputEndpointID
        };

        if (! this.callbacks)
            this.callbacks = new Map();

        this.callbacks.set (patchConnection, callbacks);

        this.addEventListener ("note-down", callbacks.noteDown);
        this.addEventListener ("note-up",   callbacks.noteUp);
        patchConnection.addEndpointListener (midiInputEndpointID, callbacks.midiIn);
    }

    /** This removes the connection to a PatchConnection object that was previously attached
     *  with attachToPatchConnection().
     *
     *  @param {PatchConnection} patchConnection
     */
    detachPatchConnection (patchConnection)
    {
        const callbacks = this.callbacks?.get (patchConnection);

        if (callbacks)
        {
            this.removeEventListener ("note-down", callbacks.noteDown);
            this.removeEventListener ("note-up",   callbacks.noteUp);
            patchConnection.removeEndpointListener (callbacks.midiInputEndpointID, callbacks.midiIn);
        }

        this.callbacks?.delete (patchConnection);
    }

    //==============================================================================
    /** Can be overridden to return the color to use for a note index
     *  @param {number} _note */
    getNoteColour (_note)   { return undefined; }

    /** Can be overridden to return the text label to draw on a note index
     *  @param {number} note */
    getNoteLabel (note)     { return midi.getChromaticScaleIndex (note) === 0 ? midi.getNoteNameWithOctaveNumber (note) : ""; }

    /** Clients should call this to deliver a MIDI message, which the keyboard will use to
     *  highlight the notes that are currently playing.
     *  @param {number} message
     */
    handleExternalMIDI (message)
    {
        if (midi.isNoteOn (message))
        {
            const note = midi.getNoteNumber (message);
            this.currentExternalNotesOn.add (note);
            this.refreshActiveNoteElements();
        }
        else if (midi.isNoteOff (message))
        {
            const note = midi.getNoteNumber (message);
            this.currentExternalNotesOn.delete (note);
            this.refreshActiveNoteElements();
        }
    }

    /** This method will be called when the user plays a note. The default behaviour is
     *  to dispath an event, but you could override this if you needed to.
     *  @param {number} note
    */
    sendNoteOn (note)   { this.dispatchEvent (new CustomEvent('note-down', { detail: { note: note }})); }

    /** This method will be called when the user releases a note. The default behaviour is
     *  to dispath an event, but you could override this if you needed to.
     *  @param {number} note
    */
    sendNoteOff (note)  { this.dispatchEvent (new CustomEvent('note-up',   { detail: { note: note } })); }

    /** Clients can call this to force all the notes to turn off, e.g. in a "panic". */
    allNotesOff()
    {
        this.setDraggedNote (-1);
        this.modifierKeys = 0;

        for (let note of this.currentKeyboardNotes.values())
            this.removeKeyboardNote (note);

        this.currentExternalNotesOn.clear();
        this.refreshActiveNoteElements();
    }

    /** @param {number} newNote */
    setDraggedNote (newNote)
    {
        if (newNote !== this.currentDraggedNote)
        {
            if (this.currentDraggedNote >= 0)
                this.sendNoteOff (this.currentDraggedNote);

            this.currentDraggedNote = newNote;

            if (this.currentDraggedNote >= 0)
                this.sendNoteOn (this.currentDraggedNote);

            this.refreshActiveNoteElements();
        }
    }

    /** @param {number} note */
    addKeyboardNote (note)
    {
        if (! this.currentKeyboardNotes.has (note))
        {
            this.sendNoteOn (note);
            this.currentKeyboardNotes.add (note);
            this.refreshActiveNoteElements();
        }
    }

    /** @param {number} note */
    removeKeyboardNote (note)
    {
        if (this.currentKeyboardNotes.has (note))
        {
            this.sendNoteOff (note);
            this.currentKeyboardNotes.delete (note);
            this.refreshActiveNoteElements();
        }
    }

    /** @param {number} note */
    isNoteActive (note)
    {
        return note === this.currentDraggedNote
            || this.currentExternalNotesOn.has (note)
            || this.currentKeyboardNotes.has (note);
    }

    //==============================================================================
    /** @private @param {TouchEvent} event */
    touchEnd (event)
    {
        for (const touch of event.changedTouches)
        {
            const note = this.currentTouches.get (touch.identifier);
            this.currentTouches.delete (touch.identifier);
            if (note !== undefined)
                this.removeKeyboardNote (note);
        }

        event.preventDefault();
    }

    /** @private @param {TouchEvent} event */
    touchStart (event)
    {
        for (const touch of event.changedTouches)
        {
            const note = parseInt ((/** @type {HTMLElement} */ (touch.target)).id.substring (4));
            this.currentTouches.set (touch.identifier, note);
            this.addKeyboardNote (note);
        }

        event.preventDefault();
    }

    /** @private @param {MouseEvent} event @param {boolean} isDown @param {boolean} isUp */
    handleMouse (event, isDown, isUp)
    {
        if (isDown)
            this.isDragging = true;

        if (this.isDragging)
        {
            let newActiveNote = -1;

            if (event.buttons !== 0 && event.type !== "mouseout")
            {
                const noteStr = (/** @type {HTMLElement | null} */ (event.target))?.id.substring (4);

                if (noteStr !== undefined)
                    newActiveNote = parseInt (noteStr);
            }

            this.setDraggedNote (newActiveNote);

            if (! isDown)
                event.preventDefault();
        }

        if (isUp)
            this.isDragging = false;
    }

    /** @private @param {KeyboardEvent} event @param {boolean} isDown */
    handleKey (event, isDown)
    {
        if (event.key === "Meta" || event.key === "Alt" || event.key === "Control" || event.key === "Shift")
        {
            this.modifierKeys += isDown ? 1 : -1;
            return;
        }

        if (this.modifierKeys !== 0)
            return;

        const config = this.config;
        const index = config.keymap.split (" ").indexOf (event.code);

        if (index >= 0)
        {
            const note = Math.floor ((config.rootNote + (config.numNotes / 4) + 11) / 12) * 12 + index;

            if (isDown)
                this.addKeyboardNote (note);
            else
                this.removeKeyboardNote (note);

            event.preventDefault();
        }
    }

    /** @private */
    refreshHTML()
    {
        this.root.innerHTML = `<style>${this.getCSS()}</style>${this.getNoteElements()}`;

        for (let i = 0; i < 128; ++i)
        {
            const elem = this.root.getElementById (`note${i.toString()}`);
            this.notes.push ({ note: i, element: elem });
        }

        this.style.maxWidth = String (this.scrollWidth);
    }

    /** @private */
    refreshActiveNoteElements()
    {
        for (let note of this.notes)
        {
            if (note.element)
            {
                if (this.isNoteActive (note.note))
                    note.element.classList.add ("active");
                else
                    note.element.classList.remove ("active");
            }
        }
    }

    /** @private @param {number} note */
    getAccidentalOffset (note)
    {
        const index = midi.getChromaticScaleIndex (note);

        const negativeOffset = -this.accidentalWidth / 16;
        const positiveOffset = 3 * this.accidentalWidth / 16;

        const accOffset = this.naturalWidth - (this.accidentalWidth / 2);
        const offsets = [ 0, negativeOffset, 0, positiveOffset, 0, 0, negativeOffset, 0, 0, 0, positiveOffset, 0 ];

        return accOffset + offsets[index];
    }

    /** @private */
    getNoteElements()
    {
        const config = this.config;
        let naturals = "", accidentals = "";
        let x = 0;

        for (let i = 0; i < config.numNotes; ++i)
        {
            const note = config.rootNote + i;
            const name = this.getNoteLabel (note);

            if (midi.isNatural (note))
            {
                naturals += `<div class="natural-note note" id="note${note}" style=" left: ${x + 1}px"><p>${name}</p></div>`;
            }
            else
            {
                let accidentalOffset = this.getAccidentalOffset (note);
                accidentals += `<div class="accidental-note note" id="note${note}" style="left: ${x + accidentalOffset}px"></div>`;
            }

            if (midi.isNatural (note + 1) || i === config.numNotes - 1)
                x += this.naturalWidth;
        }

        this.style.maxWidth = (x + 1) + "px";

        return `<div tabindex="0" class="note-holder" style="width: ${x + 1}px;">
                ${naturals}
                ${accidentals}
                </div>`;
    }

    /** @private */
    getCSS()
    {
        let extraColours = "";
        const config = this.config;

        for (let i = 0; i < config.numNotes; ++i)
        {
            const note = config.rootNote + i;
            const colourOverride = this.getNoteColour (note);

            if (colourOverride)
                extraColours += `#note${note}:not(.active) { background: ${colourOverride}; }`;
        }

        return `
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
                display: block;
                overflow: auto;
                position: relative;
            }

            .natural-note {
                position: absolute;
                border: ${this.naturalBorder};
                background: #fff;
                width: ${this.naturalWidth}px;
                height: 100%;

                display: flex;
                align-items: end;
                justify-content: center;
            }

            p {
                pointer-events: none;
                text-align: center;
                font-size: 0.7rem;
                color: grey;
            }

            .accidental-note {
                position: absolute;
                top: 0;
                border: ${this.accidentalBorder};
                background: #333;
                width: ${this.accidentalWidth}px;
                height: ${this.accidentalPercentageHeight}%;
            }

            .note-holder {
                position: relative;
                height: 100%;
            }

            .active {
                background: ${this.pressedColour};
            }

            ${extraColours}
            `
    }
}
