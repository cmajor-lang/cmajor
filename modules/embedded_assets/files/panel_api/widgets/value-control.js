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

import { el, trackPointer } from "../core/dom.js";
import { EndpointWidget, getEndpointDisplayName } from "./widget-base.js";
import { describeParameter, sendParameterChange } from "./parameter-model.js";

/*
    A parameter: a slider, a toggle, or its options (a row of buttons if they're few and short,
    otherwise a drop-down list), depending on its annotation. It
    follows the parameter's value from the server (so moving it in the patch's own GUI moves
    this too), sends gesture start/end around drags, and resets to the default on double-click.

    While the slider is being dragged, the server's echoes don't move it (or its readout): they
    lag behind the pointer, so they'd make it jitter.

    Before it starts a gesture of its own (a drag, a click, a key, a reset), it calls
    options.onUserGesture (endpointID), so that anything else moving the parameter (the
    parameter fuzzer's automation) can stop, and end its own gesture first.
*/
export class ValueControlWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "parameter");

        this.model = describeParameter (endpoint);
        this.value = undefined;
        this.lastServerValue = undefined;
        this.isDragging = false;
        this.stopTrackingPointer = null;
        this.element.title = "Double-click to reset to " + this.model.format (this.model.init);

        const label = getEndpointDisplayName (endpoint);

        if (this.model.hasOptionButtons)
        {
            // a few short options: a button for each, the current one pressed
            this.optionButtons = this.model.options.map (o => el ("button", { class: "cp-seg-button", type: "button", "aria-pressed": "false", title: o.text,
                                                                         onclick: () => this.send (o.value, true) }, o.text));
            this.control = el ("div", { class: "cp-seg cp-option-buttons", role: "group", "aria-label": label }, this.optionButtons);
        }
        else if (this.model.kind === "options")
        {
            this.control = el ("select", { class: "cp-select", "aria-label": label,
                                           onchange: () => this.send (this.model.options[this.control.selectedIndex].value, true) },
                               this.model.options.map (o => el ("option", null, o.text)));
        }
        else if (this.model.kind === "toggle")
        {
            this.control = el ("button", { class: "cp-toggle", role: "switch", "aria-checked": "false", "aria-label": label,
                                           onclick: () => this.send (this.model.isOn (this.value) ? this.model.min : this.model.max, true) },
                               el ("span", { class: "cp-toggle-knob" }));
        }
        else
        {
            this.control = el ("input", { type: "range", class: "cp-slider", min: 0, max: 1, step: "any", "aria-label": label });
            this.disposer.listen (this.control, "pointerdown", (e) => this.beginDrag (e));
            this.disposer.listen (this.control, "input", () => this.send (this.model.fromNormalised (Number (this.control.value)), false));
            this.disposer.listen (this.control, "keydown", (e) => this.handleKey (e));
        }

        // (not on the option buttons: clicking one quickly twice mustn't undo the choice)
        this.disposer.listen (this.element, "dblclick", (e) =>
        {
            if (e.target !== this.nameElement && ! (this.optionButtons && this.control.contains (e.target)))
                this.send (this.model.init, true);
        });
        this.disposer.add (() => this.endDrag());
        this.bodyElement.append (this.control);

        this.showValue (this.model.init, false);
        this.subscribeParameter (value => this.showValue (value, true));
    }

    /// A drag on the slider is one gesture, from the press until the pointer is released (or
    /// the drag is cancelled, or the slider loses the pointer capture)
    beginDrag (e)
    {
        if (e.button !== 0 || this.isDragging)
            return;

        this.isDragging = true;
        this.options.onUserGesture?.(this.endpoint.endpointID);
        this.patchConnection.sendParameterGestureStart (this.endpoint.endpointID);
        this.stopTrackingPointer = trackPointer (e, this.control, null, () => this.endDrag());
    }

    endDrag()
    {
        this.stopTrackingPointer?.();
        this.stopTrackingPointer = null;

        if (this.isDragging)
        {
            this.isDragging = false;
            this.patchConnection.sendParameterGestureEnd (this.endpoint.endpointID);

            // (show where the server has it, now that its echoes are being followed again)
            if (this.lastServerValue !== undefined)
                this.showValue (this.lastServerValue, true);
        }
    }

    /// Sends a value: as a complete gesture of its own if asGesture is true, unless a drag's
    /// gesture is already open (e.g. a double-click while dragging), which it then joins
    send (value, asGesture)
    {
        value = this.model.snap (value);
        const id = this.endpoint.endpointID;

        if (asGesture && ! this.isDragging)
            this.options.onUserGesture?.(id);

        if (asGesture)
            sendParameterChange (this.patchConnection, id, value, this.isDragging);
        else
            this.patchConnection.sendEventOrValue (id, value);

        // show it straight away; the server's echo will confirm it
        this.showValue (value, false);
    }

    handleKey (e)
    {
        // (arrow keys move a range input by its own step, which is "any", so handle them here)
        const steps = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: 10, PageDown: -10 };

        if (steps[e.key])
        {
            e.preventDefault();
            const stepSize = this.model.step > 0 ? this.model.step : (this.model.max - this.model.min) / 100;
            this.send ((this.value ?? this.model.init) + steps[e.key] * stepSize * (e.shiftKey ? 0.1 : 1), true);
        }
        else if (e.key === "Home" || e.key === "End")
        {
            e.preventDefault();
            this.send (e.key === "Home" ? this.model.min : this.model.max, true);
        }
    }

    showValue (value, fromServer)
    {
        if (typeof value !== "number")
            value = Number (value);

        if (! isFinite (value))
            return;

        if (fromServer)
        {
            this.lastServerValue = value;
            this.options.onValueChanged?.(this.endpoint, value);

            // (don't fight the user's pointer with the server's echoes: see above)
            if (this.isDragging && this.model.kind === "slider")
                return;
        }

        this.value = value;
        const text = this.model.format (value);
        this.setValueText (text);

        if (this.model.hasOptionButtons)
        {
            const index = this.model.toIndex (value);

            this.optionButtons.forEach ((button, i) =>
            {
                button.classList.toggle ("cp-on", i === index);
                button.setAttribute ("aria-pressed", i === index ? "true" : "false");
            });
        }
        else if (this.model.kind === "options")
        {
            this.control.selectedIndex = this.model.toIndex (value);
        }
        else if (this.model.kind === "toggle")
        {
            const on = this.model.isOn (value);
            this.control.classList.toggle ("cp-on", on);
            this.control.setAttribute ("aria-checked", on ? "true" : "false");
        }
        else
        {
            this.control.value = String (this.model.toNormalised (value));
            this.control.setAttribute ("aria-valuetext", text);
            this.element.style.setProperty ("--cp-fill", (this.model.toNormalised (value) * 100).toFixed (2) + "%");
        }
    }
}
