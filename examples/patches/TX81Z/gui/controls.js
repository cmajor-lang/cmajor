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
    The panel's controls: rotary knobs and faders for the parameters, the pitch-bend and
    modulation wheels, and the keyboard. Their look comes from the CSS in index.js.

    A parameter control talks to its owner through a `listener` object:
        onValueChange (param, value)     - the user has moved the control
        onGestureStart (param)           - a drag has begun...
        onGestureEnd (param)             - ...and finished
        onHover (param, isOver)          - the mouse has moved over or off the control
*/

import { constrainValue, toNormalised, fromNormalised, isStepped } from "./params.js";

const svgNS = "http://www.w3.org/2000/svg";

function createElement (tag, className, parent)
{
    const e = document.createElement (tag);

    if (className)
        e.className = className;

    parent?.appendChild (e);
    return e;
}

function createSVGElement (tag, attributes, parent)
{
    const e = document.createElementNS (svgNS, tag);

    for (const [name, value] of Object.entries (attributes))
        e.setAttribute (name, value);

    parent?.appendChild (e);
    return e;
}

/** Calls onStart (e), onMove (deltaY, e) and onEnd (pointerID) for a pointer drag that begins
    on `element`. deltaY is the vertical distance moved since the last call, in screen pixels.

    Once a drag has started, it's followed with listeners on the window rather than relying on
    pointer capture, which some web views don't honour: without it, a button released outside
    the element would never be seen, leaving the control stuck in its drag. A drag also ends if
    a move arrives with no button held (the button was released outside the window), or if the
    window loses focus.

    Normally only one drag at a time is allowed; with allowMultiplePointers, each pointer (e.g.
    each finger on a touch screen) gets its own.
*/
function addDragHandler (element, { onStart, onMove, onEnd, allowMultiplePointers = false })
{
    const activeDrags = new Map();  // pointerID -> function that ends that drag

    element.addEventListener ("pointerdown", (e) =>
    {
        if (e.button !== 0 || activeDrags.has (e.pointerId) || (activeDrags.size > 0 && ! allowMultiplePointers))
            return;

        e.preventDefault();

        const pointerID = e.pointerId;
        let lastY = e.clientY;

        const handleMove = (e) =>
        {
            if (e.pointerId !== pointerID)
                return;

            if ((e.buttons & 1) === 0)
                return endDrag();

            onMove?.(e.clientY - lastY, e);
            lastY = e.clientY;
        };

        const handleRelease = (e) =>
        {
            if (e.pointerId === pointerID)
                endDrag();
        };

        const endDrag = () =>
        {
            window.removeEventListener ("pointermove", handleMove, true);
            window.removeEventListener ("pointerup", handleRelease, true);
            window.removeEventListener ("pointercancel", handleRelease, true);
            window.removeEventListener ("blur", endDrag);
            activeDrags.delete (pointerID);

            if (element.hasPointerCapture?.(pointerID))
                element.releasePointerCapture (pointerID);

            onEnd?.(pointerID);
        };

        window.addEventListener ("pointermove", handleMove, true);
        window.addEventListener ("pointerup", handleRelease, true);
        window.addEventListener ("pointercancel", handleRelease, true);
        window.addEventListener ("blur", endDrag);
        activeDrags.set (pointerID, endDrag);

        // Capture still helps where it works, e.g. to keep getting moves outside the window
        try { element.setPointerCapture (pointerID); } catch {}

        onStart?.(e);
    });
}

//==============================================================================
/** A momentary push-button that looks pressed while it's held down, and calls onPress when
    it goes down, like the buttons on the hardware.
*/
export class PushButton
{
    constructor (element, { onPress } = {})
    {
        this.element = element;

        addDragHandler (element,
        {
            onStart: () =>
            {
                element.classList.add ("pressed");
                onPress?.();
            },
            onEnd: () => element.classList.remove ("pressed")
        });
    }
}

//==============================================================================
class ParameterControl
{
    constructor (param, listener, className)
    {
        this.param = param;
        this.listener = listener;
        this.element = createElement ("div", "control " + className);
        this.element.dataset.parameter = param.id;
        this.element.addEventListener ("pointerenter", () => listener.onHover?.(param, true));
        this.element.addEventListener ("pointerleave", () => listener.onHover?.(param, false));
    }

    /** Updates the control to show a value that came from the patch. */
    setValue (value)
    {
        this.normalised = toNormalised (this.param, value);
        this.update();
    }

    /** Applies a value that the user has asked for, and tells the listener if it changed. */
    changeValue (value)
    {
        value = constrainValue (this.param, value);

        if (value !== this.param.value)
        {
            this.param.value = value;
            this.listener.onValueChange?.(this.param, value);
        }
    }

    addLabel (parent)
    {
        createElement ("div", "control-label", parent).textContent = this.param.label;
    }

    /** Hooks up the usual mouse behaviour: drag to move (shift for fine control),
        double-click to reset, and the scroll-wheel to nudge it.
    */
    addMouseHandling (dragTarget, getDragScale)
    {
        let dragPosition = 0;

        addDragHandler (dragTarget,
        {
            onStart: (e) =>
            {
                this.listener.onGestureStart?.(this.param);
                this.onDragStart?.(e);
                dragPosition = toNormalised (this.param, this.param.value);
            },
            onMove: (dy, e) =>
            {
                const scale = getDragScale() * (e.shiftKey ? 0.15 : 1);
                dragPosition = Math.min (1, Math.max (0, dragPosition - dy * scale));
                this.changeValue (fromNormalised (this.param, dragPosition));
            },
            onEnd: () => this.listener.onGestureEnd?.(this.param)
        });

        dragTarget.addEventListener ("dblclick", () =>
        {
            this.listener.onGestureStart?.(this.param);
            this.changeValue (this.param.init);
            this.listener.onGestureEnd?.(this.param);
        });

        dragTarget.addEventListener ("wheel", (e) =>
        {
            e.preventDefault();
            const direction = e.deltaY < 0 ? 1 : -1;
            const step = isStepped (this.param) ? this.param.step : (this.param.max - this.param.min) / 200;
            this.changeValue (this.param.value + direction * step * (e.shiftKey ? 0.1 : 1));
        }, { passive: false });
    }
}

//==============================================================================
/** A rotary knob, with a printed scale around it. */
export class Knob extends ParameterControl
{
    constructor (param, listener)
    {
        super (param, listener, "knob-control");

        const area = createElement ("div", "knob-area", this.element);
        const scale = createSVGElement ("svg", { class: "knob-scale", viewBox: "-30 -30 60 60" }, area);
        // The ridged skirt and the pointer turn with the knob, but the lighting on the skirt
        // and the highlight on the cap stay where they are, as the light source doesn't move
        const knob = createElement ("div", "knob", area);
        const grip = createElement ("div", "knob-grip", knob);
        createElement ("div", "knob-grip-lighting", knob);
        createElement ("div", "knob-cap", knob);
        const pointerRotor = createElement ("div", "knob-pointer-rotor", knob);
        createElement ("div", "knob-pointer", pointerRotor);
        this.rotatingParts = [grip, pointerRotor];
        this.addLabel (this.element);

        // Stepped knobs with only a few positions get a tick for each one
        const numSteps = isStepped (param) ? Math.round ((param.max - param.min) / param.step) : 0;
        const numTicks = (numSteps > 0 && numSteps <= 10) ? numSteps + 1 : 11;

        for (let i = 0; i < numTicks; ++i)
        {
            const angle = (-135 + 270 * i / (numTicks - 1)) * Math.PI / 180;
            const isEnd = i === 0 || i === numTicks - 1;
            const r1 = 21.5, r2 = isEnd ? 27 : 25;

            createSVGElement ("line", { x1: r1 * Math.sin (angle), y1: -r1 * Math.cos (angle),
                                        x2: r2 * Math.sin (angle), y2: -r2 * Math.cos (angle),
                                        class: "knob-tick" }, scale);
        }

        // A full turn of the knob takes 200 pixels of dragging
        this.addMouseHandling (area, () => 1 / 200);
        this.setValue (param.value);
    }

    update()
    {
        const transform = `rotate(${-135 + 270 * this.normalised}deg)`;

        for (const part of this.rotatingParts)
            part.style.transform = transform;
    }
}

//==============================================================================
/** A DX-style fader, with a printed 0-10 scale beside it. */
export class Slider extends ParameterControl
{
    constructor (param, listener, { height = 150, capHeight = 22 } = {})
    {
        super (param, listener, "slider-control");

        this.height = height;
        this.capHeight = capHeight;

        const area = createElement ("div", "slider-area", this.element);
        area.style.height = height + "px";
        this.area = area;

        const scale = createSVGElement ("svg", { class: "slider-scale", viewBox: `0 0 40 ${height}` }, area);
        createElement ("div", "slider-slot", area);
        this.cap = createElement ("div", "slider-cap", area);
        this.cap.style.height = capHeight + "px";
        this.addLabel (this.element);

        for (let i = 0; i <= 10; ++i)
        {
            const y = capHeight / 2 + (height - capHeight) * (1 - i / 10);
            const isMajor = (i % 5) === 0;
            const length = isMajor ? 7 : 4;

            createSVGElement ("line", { x1: 9 - length, y1: y, x2: 9, y2: y, class: "slider-tick" }, scale);
            createSVGElement ("line", { x1: 31, y1: y, x2: 31 + length, y2: y, class: "slider-tick" }, scale);
        }

        // Dragging moves the cap with the mouse; clicking the slot first jumps the cap to the mouse
        this.addMouseHandling (area, () => 1 / this.getTravelInScreenPixels());

        this.onDragStart = (e) =>
        {
            if (e.target !== this.cap)
            {
                const rect = area.getBoundingClientRect();
                const pixelScale = rect.height / height;
                const capCentre = (e.clientY - rect.top) / pixelScale;
                this.changeValue (fromNormalised (this.param, 1 - (capCentre - capHeight / 2) / (height - capHeight)));
            }
        };

        this.setValue (param.value);
    }

    getTravelInScreenPixels()
    {
        const rect = this.area.getBoundingClientRect();
        return Math.max (1, (rect.height / this.height) * (this.height - this.capHeight));
    }

    update()
    {
        this.cap.style.top = ((1 - this.normalised) * (this.height - this.capHeight)) + "px";
    }
}

//==============================================================================
/** A pitch-bend or modulation wheel. A pitch wheel's value runs from -1 to 1 and springs back
    to the centre when let go; a modulation wheel's runs from 0 to 1 and stays put.
*/
export class Wheel
{
    constructor ({ label, isPitchBend, onChange, height = 140, width = 34 })
    {
        this.isPitchBend = isPitchBend;
        this.onChange = onChange;
        this.height = height;
        this.width = width;
        this.value = 0;

        this.element = createElement ("div", "control wheel-control");
        const housing = createElement ("div", "wheel-housing", this.element);
        this.svg = createSVGElement ("svg", { class: "wheel", width, height, viewBox: `0 0 ${width} ${height}` }, housing);
        this.ridges = createSVGElement ("g", {}, this.svg);
        createElement ("div", "control-label", this.element).textContent = label;

        addDragHandler (housing,
        {
            onStart: () => cancelAnimationFrame (this.springAnimation),
            onMove: (dy) =>
            {
                const rect = housing.getBoundingClientRect();
                const range = isPitchBend ? 2 : 1;
                this.setValue (this.value - dy * range / (rect.height * 0.8), true);
            },
            onEnd: () => { if (isPitchBend) this.springBack(); }
        });

        this.paint();
    }

    setValue (newValue, notify)
    {
        newValue = Math.min (1, Math.max (this.isPitchBend ? -1 : 0, newValue));

        if (newValue !== this.value)
        {
            this.value = newValue;
            this.paint();

            if (notify)
                this.onChange?.(newValue);
        }
    }

    springBack()
    {
        const step = () =>
        {
            const next = Math.abs (this.value) < 0.02 ? 0 : this.value * 0.6;
            this.setValue (next, true);

            if (next !== 0)
                this.springAnimation = requestAnimationFrame (step);
        };

        this.springAnimation = requestAnimationFrame (step);
    }

    paint()
    {
        // Draws the ridges on the wheel's rim as it would look from the front: each one is a
        // line at a fixed angle around the wheel, projected onto the visible half
        const { width, height } = this;
        const radius = height / 2 - 1;
        const centreY = height / 2;
        const rotation = (this.isPitchBend ? this.value : (this.value - 0.5) * 2) * 0.9;
        const spacing = 0.13;

        this.ridges.replaceChildren();

        for (let i = -20; i <= 20; ++i)
        {
            const angle = i * spacing - rotation;

            if (Math.abs (angle) >= Math.PI / 2 - 0.05)
                continue;

            const y = centreY + radius * Math.sin (angle);
            const facing = Math.cos (angle);
            const isMarker = i === 0;

            createSVGElement ("line", { x1: 2, x2: width - 2, y1: y, y2: y,
                                        class: isMarker ? "wheel-marker" : "wheel-ridge",
                                        "stroke-width": (isMarker ? 3.2 : 1.4) * facing,
                                        opacity: 0.25 + 0.75 * facing }, this.ridges);
        }
    }
}

//==============================================================================
const blackKeyOffsets = { 1: -0.1, 3: 0.1, 6: -0.12, 8: 0, 10: 0.12 };

function isBlackKey (note)      { return (note % 12) in blackKeyOffsets; }

/** A piano keyboard. The velocity of a note depends on how far down the key you click. */
export class Keyboard
{
    constructor ({ lowestNote = 36, numKeys = 61, width, height, onNoteOn, onNoteOff })
    {
        this.lowestNote = lowestNote;
        this.highestNote = lowestNote + numKeys - 1;
        this.width = width;
        this.height = height;
        this.onNoteOn = onNoteOn;
        this.onNoteOff = onNoteOff;

        this.keys = new Map();
        this.notesHeld = new Map();  // note -> set of sources that are holding it down
        this.pointerNotes = new Map();

        this.element = createElement ("div", "keyboard");
        this.element.style.width = width + "px";
        this.element.style.height = height + "px";

        let numWhiteKeys = 0;

        for (let note = this.lowestNote; note <= this.highestNote; ++note)
            if (! isBlackKey (note))
                ++numWhiteKeys;

        this.whiteKeyWidth = width / numWhiteKeys;
        this.blackKeyWidth = this.whiteKeyWidth * 0.58;
        this.blackKeyHeight = height * 0.62;

        const blackKeys = [];
        this.whiteKeyNotes = [];
        let whiteIndex = 0;

        for (let note = this.lowestNote; note <= this.highestNote; ++note)
        {
            const key = createElement ("div", "");

            if (isBlackKey (note))
            {
                const centre = (whiteIndex + blackKeyOffsets[note % 12]) * this.whiteKeyWidth;
                key.className = "key black-key";
                key.style.left = (centre - this.blackKeyWidth / 2) + "px";
                key.style.width = this.blackKeyWidth + "px";
                key.style.height = this.blackKeyHeight + "px";
                key.dataset.left = centre - this.blackKeyWidth / 2;
                blackKeys.push (key);
            }
            else
            {
                key.className = "key white-key";
                key.style.left = (whiteIndex * this.whiteKeyWidth) + "px";
                key.style.width = this.whiteKeyWidth + "px";
                key.style.height = height + "px";
                this.element.appendChild (key);
                this.whiteKeyNotes.push (note);
                ++whiteIndex;
            }

            this.keys.set (note, key);
        }

        // black keys go on top of the white ones
        for (const key of blackKeys)
            this.element.appendChild (key);

        this.addPointerHandling();
    }

    /** Finds the note and velocity for a point in the keyboard's own (unscaled) coordinates. */
    getNoteAt (x, y)
    {
        if (y < 0 || y > this.height || x < 0 || x > this.width)
            return null;

        const toVelocity = (proportion) => Math.round (30 + 97 * Math.min (1, Math.max (0, proportion)));

        if (y < this.blackKeyHeight)
        {
            for (const [note, key] of this.keys)
            {
                const left = parseFloat (key.dataset.left);

                if (isBlackKey (note) && x >= left && x < left + this.blackKeyWidth)
                    return { note, velocity: toVelocity (y / this.blackKeyHeight) };
            }
        }

        const note = this.whiteKeyNotes[Math.min (this.whiteKeyNotes.length - 1, Math.floor (x / this.whiteKeyWidth))];
        return { note, velocity: toVelocity (y / this.height) };
    }

    addPointerHandling()
    {
        const getLocalPosition = (e) =>
        {
            const rect = this.element.getBoundingClientRect();
            return { x: (e.clientX - rect.left) * this.width / rect.width,
                     y: (e.clientY - rect.top) * this.height / rect.height };
        };

        const playNoteAt = (e) =>
        {
            const { x, y } = getLocalPosition (e);
            const hit = this.getNoteAt (x, y);
            const previous = this.pointerNotes.get (e.pointerId);

            if (previous === hit?.note)
                return;

            if (previous !== undefined)
            {
                this.pointerNotes.delete (e.pointerId);
                this.releaseNote (previous, "pointer" + e.pointerId);
            }

            if (hit)
            {
                this.pointerNotes.set (e.pointerId, hit.note);
                this.pressNote (hit.note, hit.velocity, "pointer" + e.pointerId);
            }
        };

        const stopNote = (pointerID) =>
        {
            const note = this.pointerNotes.get (pointerID);

            if (note !== undefined)
            {
                this.pointerNotes.delete (pointerID);
                this.releaseNote (note, "pointer" + pointerID);
            }
        };

        // Each finger or mouse plays its own note, which follows it as it glides across the keys
        addDragHandler (this.element,
        {
            allowMultiplePointers: true,
            onStart: (e) => playNoteAt (e),
            onMove: (_, e) => playNoteAt (e),
            onEnd: (pointerID) => stopNote (pointerID)
        });
    }

    /** Plays a note from one of the GUI's own sources (a pointer or the computer keyboard). */
    pressNote (note, velocity, source)
    {
        if (note < 0 || note > 127)
            return;

        this.setNoteHeld (note, source, true);
        this.onNoteOn?.(note, velocity);
    }

    releaseNote (note, source)
    {
        if (note < 0 || note > 127)
            return;

        this.setNoteHeld (note, source, false);
        this.onNoteOff?.(note);
    }

    /** Shows or hides a key as being held down without sending anything, e.g. for MIDI
        that's coming from elsewhere.
    */
    setNoteHeld (note, source, isHeld)
    {
        let sources = this.notesHeld.get (note);

        if (! sources)
            this.notesHeld.set (note, sources = new Set());

        if (isHeld)
            sources.add (source);
        else
            sources.delete (source);

        this.keys.get (note)?.classList.toggle ("down", sources.size > 0);
    }

    clearNotesFromSource (source)
    {
        for (const note of this.notesHeld.keys())
            this.setNoteHeld (note, source, false);
    }
}
