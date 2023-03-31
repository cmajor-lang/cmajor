//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include <array>
#include <string_view>

namespace cmaj
{

/// This contains the javascript and other asset files availble to patch views running in
/// a browser envirtonment.
///
/// It contains modules that provide the generic GUI view, and other helper classes that
/// can be used by custom views.
///
struct EmbeddedWebAssets
{
    static std::string_view findResource (std::string_view path)
    {
        for (auto& file : files)
            if (path == file.name)
                return file.content;

        return {};
    }

private:
    struct File { std::string_view name, content; };

    static constexpr const char* cmajpatchconnection_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"

import { EventListenerList } from "./cmaj-event-listener-list.js"

//==============================================================================
/// This class implements the API and much of the logic for communicating with
/// an instance of a patch that is running.
export class PatchConnection  extends EventListenerList
{
    constructor()
    {
        super();
    }

    //==============================================================================
    // Status-handling methods:

    /// Calling this will trigger an asynchronous callback to any status listeners with the
    /// patch's current state. Use addStatusListener() to attach a listener to receive it.
    requestStatusUpdate()                             { this.sendMessageToServer ({ type: "req_status" }); }

    /// Attaches a listener function that will be called whenever the patch's status changes.
    /// The function will be called with a parameter object containing many properties describing the status,
    /// including whether the patch is loaded, any errors, endpoint descriptions, its manifest, etc.
    addStatusListener (listener)                      { this.addEventListener    ("status", listener); }
    /// Removes a listener that was previously added with addStatusListener()
    removeStatusListener (listener)                   { this.removeEventListener ("status", listener); })"
R"(

    /// Causes the patch to be reset to its "just loaded" state.
    resetToInitialState()                             { this.sendMessageToServer ({ type: "req_reset" }); }

    //==============================================================================
    // Methods for sending data to input endpoints:

    /// Sends a value to one of the patch's input endpoints.
    /// This can be used to send a value to either an 'event' or 'value' type input endpoint.
    /// If the endpoint is a 'value' type, then the rampFrames parameter can optionally be used to specify
    /// the number of frames over which the current value should ramp to the new target one.
    /// The value parameter will be coerced to the type that is expected by the endpoint. So for
    /// examples, numbers will be converted to float or integer types, javascript objects and arrays
    /// will be converted into more complex types in as good a fashion is possible.
    sendEventOrValue (endpointID, value, rampFrames)  { this.sendMessageToServer ({ type: "send_value", id: endpointID, value: value, rampFrames: rampFrames }); }

    /// Sends a short MIDI message value to a MIDI endpoint.
    /// The value must be a number encoded with `(byte0 << 16) | (byte1 << 8) | byte2`.
    sendMIDIInputEvent (endpointID, shortMIDICode)    { this.sendEventOrValue (endpointID, { message: shortMIDICode }); }

    /// Tells the patch that a series of changes that constitute a gesture is about to take place
    /// for the given endpoint. Remember to call sendParameterGestureEnd() after they're done!
    sendParameterGestureStart (endpointID)            { this.sendMessageToServer ({ type: "send_gesture_start", id: endpointID }); }

    /// Tells the patch that a gesture started by sendParameterGestureStart() has finished.
    sendParameterGestureEnd (endpointID)              { this.sendMessageToServer ({ type: "send_gesture_end", id: endpointID }); })"
R"(

    //==============================================================================
    // Stored state control methods:

    /// Requests a callback to any stored-state value listeners with the current value of a given key-value pair.
    /// To attach a listener to receive these events, use addStoredStateValueListener().
    requestStoredStateValue (key)                     { this.sendMessageToServer ({ type: "req_state_value", key: key }); }
    /// Modifies a key-value pair in the patch's stored state.
    sendStoredStateValue (key, newValue)              { this.sendMessageToServer ({ type: "send_state_value", key : key, value: newValue }); }

    /// Attaches a listener function that will be called when any key-value pair in the stored state is changed.
    /// The listener function will receive a message parameter with properties 'key' and 'value'.
    addStoredStateValueListener (listener)            { this.addEventListener    ("state_key_value", listener); }
    /// Removes a listener that was previously added with addStoredStateValueListener().
    removeStoredStateValueListener (listener)         { this.removeEventListener ("state_key_value", listener); }

    /// Applies a complete stored state to the patch.
    /// To get the current complete state, use requestFullStoredState().
    sendFullStoredState (fullState)                   { this.sendMessageToServer ({ type: "send_full_state", value: fullState }); }

    /// Asynchronously requests the full stored state of the patch.
    /// The listener function that is supplied will be called asynchronously with the state as its argument.
    requestFullStoredState (callback)
    {
        const replyType = "fullstate_response_" + (Math.floor (Math.random() * 100000000)).toString();
        this.addSingleUseListener (replyType, callback);
        this.sendMessageToServer ({ type: "req_full_state", replyType: replyType });
    }

    //==============================================================================
    // Listener methods:)"
R"(

    /// Attaches a listener function which will be called whenever an event passes through a specific endpoint.
    /// This can be used to monitor both input and output endpoints.
    /// The listener function will be called with an argument which is the value of the event.
    addEndpointEventListener (endpointID, listener)
    {
        this.addEventListener ("event_" + endpointID, listener);
        this.sendMessageToServer ({ type: "set_endpoint_event_monitoring", endpoint: endpointID, active: true });
    }

    /// Removes a listener that was previously added with addEndpointEventListener()
    removeEndpointEventListener (endpointID, listener)
    {
        this.removeEventListener ("event_" + endpointID, listener);
        this.sendMessageToServer ({ type: "set_endpoint_event_monitoring", endpoint: endpointID, active: false });
    }

    /// This will trigger an asynchronous callback to any parameter listeners that are
    /// attached, providing them with its up-to-date current value for the given endpoint.
    /// Use addAllParameterListener() to attach a listener to receive the result.
    requestParameterValue (endpointID)                  { this.sendMessageToServer ({ type: "req_param_value", id: endpointID }); }

    /// Attaches a listener function which will be called whenever the value of a specific parameter changes.
    /// The listener function will be called with an argument which is the new value.
    addParameterListener (endpointID, listener)         { this.addEventListener ("param_value_" + endpointID.toString(), listener); }
    /// Removes a listener that was previously added with addParameterListener()
    removeParameterListener (endpointID, listener)      { this.removeEventListener ("param_value_" + endpointID.toString(), listener); })"
R"(

    /// Attaches a listener function which will be called whenever the value of any parameter changes in the patch.
    /// The listener function will be called with an argument object with the fields 'endpointID' and 'value'.
    addAllParameterListener (listener)                  { this.addEventListener ("param_value", listener); }
    /// Removes a listener that was previously added with addAllParameterListener()
    removeAllParameterListener (listener)               { this.removeEventListener ("param_value", listener); }

    /// This takes a relative path to an asset within the patch bundle, and converts it to a
    /// path relative to the root of the browser that is showing the view.
    /// You need you use this in your view code to translate your asset URLs to a form that
    /// can be safely used in your view's HTML DOM (e.g. in its CSS). This is needed because the
    /// host's HTTP server (which is delivering your view pages) may have a different '/' root
    /// than the root of your patch (e.g. if a single server is serving multiple patch GUIs).
    getResourceAddress (path)                           { return path; }


    //==============================================================================
    // Private methods follow this point..

    /// For internal use - delivers an incoming message object from the underlying API.
    deliverMessageFromServer (msg)
    {
        if (msg.type === "status")
            this.manifest = msg.message?.manifest;

        if (msg.type == "param_value")
            this.dispatchEvent ("param_value_" + msg.message.endpointID, msg.message.value);

        this.dispatchEvent (msg.type, msg.message);
    }
}
)";
    static constexpr const char* cmajparametercontrols_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


//==============================================================================
export class ParameterControlBase  extends HTMLElement
{
    constructor()
    {
        super();

        // prevent any clicks from focusing on this element
        this.onmousedown = e => e.stopPropagation();
    }

    /// This can be used to connect the parameter to a given connection and endpoint, or
    /// to disconnect it if called with undefined arguments
    setEndpoint (patchConnection, endpointInfo)
    {
        this.detachListener();

        this.patchConnection = patchConnection;
        this.endpointInfo = endpointInfo;
        this.defaultValue = endpointInfo.annotation?.init || endpointInfo.defaultValue || 0;

        if (this.isConnected)
            this.attachListener();
    }

    connectedCallback()
    {
        this.attachListener();
    }

    disconnectedCallback()
    {
        this.detachListener();
    }

    /// Implement this in a child class to be called when the value needs refreshing
    valueChanged (newValue) {}

    setValue (value)     { this.patchConnection?.sendEventOrValue (this.endpointInfo.endpointID, value); }
    beginGesture()       { this.patchConnection?.sendParameterGestureStart (this.endpointInfo.endpointID); }
    endGesture()         { this.patchConnection?.sendParameterGestureEnd (this.endpointInfo.endpointID); })"
R"(

    setValueAsGesture (value)
    {
        this.beginGesture();
        this.setValue (value);
        this.endGesture();
    }

    resetToDefault()
    {
        if (this.defaultValue !== null)
            this.setValueAsGesture (this.defaultValue);
    }

    //==============================================================================
    // private methods..
    detachListener()
    {
        if (this.listener)
        {
            this.patchConnection?.removeParameterListener?.(this.listener.endpointID, this.listener);
            this.listener = undefined;
        }
    }

    attachListener()
    {
        if (this.patchConnection && this.endpointInfo)
        {
            this.detachListener();

            this.listener = newValue => this.valueChanged (newValue);
            this.listener.endpointID = this.endpointInfo.endpointID;

            this.patchConnection.addParameterListener (this.endpointInfo.endpointID, this.listener);
            this.patchConnection.requestParameterValue (this.endpointInfo.endpointID);
        }
    }
}

//==============================================================================
export class Knob  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        this.innerHTML = "";
        this.className = "knob-container";
        this.min = this.endpointInfo.min || 0;
        this.max = this.endpointInfo.max || 1;

        const createSvgElement = tag => window.document.createElementNS ("http://www.w3.org/2000/svg", tag);

        const svg = createSvgElement ("svg");
        svg.setAttribute ("viewBox", "0 0 100 100");)"
R"(

        const trackBackground = createSvgElement ("path");
        trackBackground.setAttribute ("d", "M20,76 A 40 40 0 1 1 80 76");
        trackBackground.classList.add ("knob-path");
        trackBackground.classList.add ("knob-track-background");

        const maxKnobRotation = 132;
        const isBipolar = this.min + this.max === 0;
        const dashLength = isBipolar ? 251.5 : 184;
        const valueOffset = isBipolar ? 0 : 132;
        this.getDashOffset = val => dashLength - 184 / (maxKnobRotation * 2) * (val + valueOffset);

        this.trackValue = createSvgElement ("path");

        this.trackValue.setAttribute ("d", isBipolar ? "M50.01,10 A 40 40 0 1 1 50 10"
                                                     : "M20,76 A 40 40 0 1 1 80 76");
        this.trackValue.setAttribute ("stroke-dasharray", dashLength);
        this.trackValue.classList.add ("knob-path");
        this.trackValue.classList.add ("knob-track-value");

        this.dial = document.createElement ("div");
        this.dial.className = "knob-dial";

        const dialTick = document.createElement ("div");
        dialTick.className = "knob-dial-tick";
        this.dial.appendChild (dialTick);

        svg.appendChild (trackBackground);
        svg.appendChild (this.trackValue);

        this.appendChild (svg);
        this.appendChild (this.dial);

        const min = endpointInfo?.annotation?.min || 0;
        const max = endpointInfo?.annotation?.max || 1;

        const remap = (source, sourceFrom, sourceTo, targetFrom, targetTo) =>
                        (targetFrom + (source - sourceFrom) * (targetTo - targetFrom) / (sourceTo - sourceFrom));

        const toValue = (knobRotation) => remap (knobRotation, -maxKnobRotation, maxKnobRotation, min, max);
        this.toRotation = (value) => remap (value, min, max, -maxKnobRotation, maxKnobRotation);

        this.rotation = this.toRotation (this.defaultValue);
        this.setRotation (this.rotation, true);)"
R"(

        const onMouseMove = (event) =>
        {
            event.preventDefault(); // avoid scrolling whilst dragging

            const nextRotation = (rotation, delta) =>
            {
                const clamp = (v, min, max) => Math.min (Math.max (v, min), max);
                return clamp (rotation - delta, -maxKnobRotation, maxKnobRotation);
            };

            const workaroundBrowserIncorrectlyCalculatingMovementY = event.movementY === event.screenY;
            const movementY = workaroundBrowserIncorrectlyCalculatingMovementY ? event.screenY - this.previousScreenY
                                                                               : event.movementY;
            this.previousScreenY = event.screenY;

            const speedMultiplier = event.shiftKey ? 0.25 : 1.5;
            this.accumlatedRotation = nextRotation (this.accumlatedRotation, movementY * speedMultiplier);
            this.setValue (toValue (this.accumlatedRotation));
        };

        const onMouseUp = (event) =>
        {
            this.previousScreenY = undefined;
            this.accumlatedRotation = undefined;
            window.removeEventListener ("mousemove", onMouseMove);
            window.removeEventListener ("mouseup", onMouseUp);
            this.endGesture();
        };

        const onMouseDown = (event) =>
        {
            this.previousScreenY = event.screenY;
            this.accumlatedRotation = this.rotation;
            this.beginGesture();
            window.addEventListener ("mousemove", onMouseMove);
            window.addEventListener ("mouseup", onMouseUp);
            event.preventDefault();
        };

        this.addEventListener ("mousedown", onMouseDown);
        this.addEventListener ("mouseup", onMouseUp);
        this.addEventListener ("dblclick", () => this.resetToDefault());
    }

    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter";
    })"
R"(

    setRotation (degrees, force)
    {
        if (force || this.rotation !== degrees)
        {
            this.rotation = degrees;
            this.trackValue.setAttribute ("stroke-dashoffset", this.getDashOffset (this.rotation));
            this.dial.style.transform = `translate(-50%,-50%) rotate(${degrees}deg)`;
        }
    }

    valueChanged (newValue)       { this.setRotation (this.toRotation (newValue), false); }
    getDisplayValue (v)           { return `${v.toFixed (2)} ${this.endpointInfo.annotation?.unit ?? ""}`; }

    static getCSS()
    {
        return `
        .knob-container {
            --knob-track-background-color: var(--background);
            --knob-track-value-color: var(--foreground);

            --knob-dial-border-color: var(--foreground);
            --knob-dial-background-color: var(--background);
            --knob-dial-tick-color: var(--foreground);

            position: relative;
            display: inline-block;
            height: 5rem;
            width: 5rem;
            margin: 0;
            padding: 0;
        }

        .knob-path {
            fill: none;
            stroke-linecap: round;
            stroke-width: 0.15rem;
        }

        .knob-track-background {
            stroke: var(--knob-track-background-color);
        }

        .knob-track-value {
            stroke: var(--knob-track-value-color);
        }

        .knob-dial {
            position: absolute;
            text-align: center;
            height: 60%;
            width: 60%;
            top: 50%;
            left: 50%;
            border: 0.15rem solid var(--knob-dial-border-color);
            border-radius: 100%;
            box-sizing: border-box;
            transform: translate(-50%,-50%);
            background-color: var(--knob-dial-background-color);
        }

        .knob-dial-tick {
            position: absolute;
            display: inline-block;)"
R"(

            height: 1rem;
            width: 0.15rem;
            background-color: var(--knob-dial-tick-color);
        }`;
    }
}

//==============================================================================
export class Switch  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        const outer = document.createElement ("div");
        outer.classList = "switch-outline";

        const inner = document.createElement ("div");
        inner.classList = "switch-thumb";

        this.innerHTML = "";
        this.currentValue = this.defaultValue > 0.5;
        this.valueChanged (this.currentValue);
        this.classList.add ("switch-container");

        outer.appendChild (inner);
        this.appendChild (outer);
        this.addEventListener ("click", () => this.setValueAsGesture (this.currentValue ? 0 : 1.0));
    }

    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter"
                && endpointInfo.annotation?.boolean;
    }

    valueChanged (newValue)
    {
        const b = newValue > 0.5;
        this.currentValue = b;
        this.classList.remove (! b ? "switch-on" : "switch-off");
        this.classList.add (b ? "switch-on" : "switch-off");
    }

    getDisplayValue (v)   { return `${v > 0.5 ? "On" : "Off"}`; }

    static getCSS()
    {
        return `
        .switch-container {
            --switch-outline-color: var(--foreground);
            --switch-thumb-color: var(--foreground);
            --switch-on-background-color: var(--background);
            --switch-off-background-color: var(--background);)"
R"(

            position: relative;
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            width: 100%;
            margin: 0;
            padding: 0;
        }

        .switch-outline {
            position: relative;
            display: inline-block;
            height: 1.25rem;
            width: 2.5rem;
            border-radius: 10rem;
            box-shadow: 0 0 0 0.15rem var(--switch-outline-color);
            transition: background-color 0.1s cubic-bezier(0.5, 0, 0.2, 1);
        }

        .switch-thumb {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%,-50%);
            height: 1rem;
            width: 1rem;
            background-color: var(--switch-thumb-color);
            border-radius: 100%;
            transition: left 0.1s cubic-bezier(0.5, 0, 0.2, 1);
        }

        .switch-off .switch-thumb {
            left: 25%;
            background: none;
            border: var(--switch-thumb-color) solid 0.1rem;
            height: 0.8rem;
            width: 0.8rem;
        }
        .switch-on .switch-thumb {
            left: 75%;
        }

        .switch-off .switch-outline {
            background-color: var(--switch-on-background-color);
        }
        .switch-on .switch-outline {
            background-color: var(--switch-off-background-color);
        }`;
    }
}

//==============================================================================
export class Options  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        const optionList = endpointInfo.annotation.text.split ("|");
        const stepCount = optionList.length > 0 ? optionList.length - 1 : 1;
        let min = 0, max = stepCount, step = 1;)"
R"(

        if (endpointInfo.annotation.min != null && endpointInfo.annotation.max != null)
        {
            min = endpointInfo.annotation.min;
            max = endpointInfo.annotation.max;
            step = (max - min) / stepCount;
        }

        this.innerHTML = "";
        const normalise = value => (value - min) / (max - min);
        this.toIndex = value => Math.min (stepCount, normalise (value) * optionList.length) | 0;
        this.options = optionList.map ((text, index) => ({ value: min + (step * index), text }));

        this.select = document.createElement ("select");

        for (const option of this.options)
        {
            const optionElement = document.createElement ("option");
            optionElement.innerText = option.text;
            this.select.appendChild (optionElement);
        }

        this.selectedIndex = this.toIndex (this.defaultValue);

        this.select.addEventListener ("change", (e) =>
        {
            const newIndex = e.target.selectedIndex;

            // prevent local state change. the caller will update us when the backend actually applies the update
            e.target.selectedIndex = this.selectedIndex;

            this.setValueAsGesture (this.options[newIndex].value)
        });

        this.valueChanged (this.selectedIndex);

        this.className = "select-container";
        this.appendChild (this.select);

        const icon = document.createElement ("span");
        icon.className = "select-icon";
        this.appendChild (icon);
    }

    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter"
                && endpointInfo.annotation?.text?.split?.("|").length > 1;
    }

    valueChanged (newValue)
    {
        const index = this.toIndex (newValue);
        this.selectedIndex = index;
        this.select.selectedIndex = index;
    }

    getDisplayValue (v)    { return this.options[this.toIndex(v)].text; })"
R"(

    static getCSS()
    {
        return `
        .select-container {
            position: relative;
            display: block;
            font-size: 0.8rem;
            width: 100%;
            color: var(--foreground);
            border: 0.15rem solid var(--foreground);
            border-radius: 0.6rem;
            margin: 0;
            padding: 0;
        }

        select {
            background: none;
            appearance: none;
            -webkit-appearance: none;
            font-family: inherit;
            font-size: 0.8rem;

            overflow: hidden;
            text-overflow: ellipsis;

            padding: 0 1.5rem 0 0.6rem;

            outline: none;
            color: var(--foreground);
            height: 2rem;
            box-sizing: border-box;
            margin: 0;
            border: none;

            width: 100%;
        }

        select option {
            background: var(--background);
            color: var(--foreground);
        }

        .select-icon {
            position: absolute;
            right: 0.3rem;
            top: 0.5rem;
            pointer-events: none;
            background-color: var(--foreground);
            width: 1.4em;
            height: 1.4em;
            mask: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath d='M17,9.17a1,1,0,0,0-1.41,0L12,12.71,8.46,9.17a1,1,0,0,0-1.41,0,1,1,0,0,0,0,1.42l4.24,4.24a1,1,0,0,0,1.42,0L17,10.59A1,1,0,0,0,17,9.17Z'/%3E%3C/svg%3E");
            mask-repeat: no-repeat;
            -webkit-mask: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath d='M17,9.17a1,1,0,0,0-1.41,0L12,12.71,8.46,9.17a1,1,0,0,0-1.41,0,1,1,0,0,0,0,1.42l4.24,4.24a1,1,0,0,0,1.42,0L17,10.59A1,1,0,0,0,17,9.17Z'/%3E%3C/svg%3E");
            -webkit-mask-repeat: no-repeat;
        }`;
    }
})"
R"(

//==============================================================================
export class LabelledControlHolder  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo, childControl)
    {
        super();
        this.childControl = childControl;
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        this.innerHTML = "";
        this.className = "labelled-control";

        const centeredControl = document.createElement ("div");
        centeredControl.className = "labelled-control-centered-control";

        centeredControl.appendChild (this.childControl);

        const titleValueHoverContainer = document.createElement ("div");
        titleValueHoverContainer.className = "labelled-control-label-container";

        const nameText = document.createElement ("div");
        nameText.classList.add ("labelled-control-name");
        nameText.innerText = endpointInfo.annotation?.name || endpointInfo.name || endpointInfo.endpointID || "";

        this.valueText = document.createElement ("div");
        this.valueText.classList.add ("labelled-control-value");

        titleValueHoverContainer.appendChild (nameText);
        titleValueHoverContainer.appendChild (this.valueText);

        this.appendChild (centeredControl);
        this.appendChild (titleValueHoverContainer);
    }

    valueChanged (newValue)
    {
        this.valueText.innerText = this.childControl?.getDisplayValue (newValue);
    }

    static getCSS()
    {
        return `
        .labelled-control {
            --labelled-control-font-color: var(--foreground);
            --labelled-control-font-size: 0.8rem;

            position: relative;
            display: inline-block;
            margin: 0 0.4rem 0.4rem;
            vertical-align: top;
            text-align: left;
            padding: 0;
        })"
R"(

        .labelled-control-centered-control {
            position: relative;
            display: flex;
            align-items: center;
            justify-content: center;

            width: 5.5rem;
            height: 5rem;
        }

        .labelled-control-label-container {
            position: relative;
            display: block;
            max-width: 5.5rem;
            margin: -0.4rem auto 0.4rem;
            text-align: center;
            font-size: var(--labelled-control-font-size);
            color: var(--labelled-control-font-color);
            cursor: default;
        }

        .labelled-control-name {
            overflow: hidden;
            text-overflow: ellipsis;
        }

        .labelled-control-value {
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            overflow: hidden;
            text-overflow: ellipsis;
            opacity: 0;
        }

        .labelled-control:hover .labelled-control-name,
        .labelled-control:active .labelled-control-name {
            opacity: 0;
        }
        .labelled-control:hover .labelled-control-value,
        .labelled-control:active .labelled-control-value {
            opacity: 1;
        }`;
    }
}

window.customElements.define ("cmaj-knob-control", Knob);
window.customElements.define ("cmaj-switch-control", Switch);
window.customElements.define ("cmaj-options-control", Options);
window.customElements.define ("cmaj-labelled-control-holder", LabelledControlHolder);


//==============================================================================
export function createControl (patchConnection, endpointInfo)
{
    if (Switch.canBeUsedFor (endpointInfo))
        return new Switch (patchConnection, endpointInfo);

    if (Options.canBeUsedFor (endpointInfo))
        return new Options (patchConnection, endpointInfo);

    if (Knob.canBeUsedFor (endpointInfo))
        return new Knob (patchConnection, endpointInfo);

    return undefined;
})"
R"(

//==============================================================================
export function createLabelledControl (patchConnection, endpointInfo)
{
    const control = createControl (patchConnection, endpointInfo);

    if (control)
        return new LabelledControlHolder (patchConnection, endpointInfo, control);

    return undefined;
}
)";
    static constexpr const char* cmajmidihelpers_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


export function getByte0 (message)     { return (message >> 16) & 0xff; }
export function getByte1 (message)     { return (message >> 8) & 0xff; }
export function getByte2 (message)     { return message & 0xff; }

function isVoiceMessage (message, type)     { return ((message >> 16) & 0xf0) == type; }
function get14BitValue (message)            { return getByte1 (message) | (getByte2 (message) << 7); }

export function getChannel0to15 (message)   { return getByte0 (message) & 0x0f; }
export function getChannel1to16 (message)   { return getChannel0to15 (message) + 1; }

export function getMessageSize (message)
{
    const mainGroupLengths = (3 << 0) | (3 << 2) | (3 << 4) | (3 << 6)
                           | (2 << 8) | (2 << 10) | (3 << 12);

    const lastGroupLengths = (1 <<  0) | (2 <<  2) | (3 <<  4) | (2 <<  6)
                           | (1 <<  8) | (1 << 10) | (1 << 12) | (1 << 14)
                           | (1 << 16) | (1 << 18) | (1 << 20) | (1 << 22)
                           | (1 << 24) | (1 << 26) | (1 << 28) | (1 << 30);

    const firstByte = getByte0 (message);
    const group = (firstByte >> 4) & 7;

    return (group != 7 ? (mainGroupLengths >> (2 * group))
                       : (lastGroupLengths >> (2 * (firstByte & 15)))) & 3;
})"
R"(

export function isNoteOn  (message)                         { return isVoiceMessage (message, 0x90) && getVelocity (message) != 0; }
export function isNoteOff (message)                         { return isVoiceMessage (message, 0x80) || (isVoiceMessage (message, 0x90) && getVelocity (message) == 0); }

export function getNoteNumber (message)                     { return getByte1 (message); }
export function getVelocity (message)                       { return getByte2 (message); }

export function isProgramChange (message)                   { return isVoiceMessage (message, 0xc0); }
export function getProgramChangeNumber (message)            { return getByte1 (message); }
export function isPitchWheel (message)                      { return isVoiceMessage (message, 0xe0); }
export function getPitchWheelValue (message)                { return get14BitValue (message); }
export function isAftertouch (message)                      { return isVoiceMessage (message, 0xa0); }
export function getAfterTouchValue (message)                { return getByte2 (message); }
export function isChannelPressure (message)                 { return isVoiceMessage (message, 0xd0); }
export function getChannelPressureValue (message)           { return getByte1 (message); }
export function isController (message)                      { return isVoiceMessage (message, 0xb0); }
export function getControllerNumber (message)               { return getByte1 (message); }
export function getControllerValue (message)                { return getByte2 (message); }
export function isControllerNumber (message, number)        { return getByte1 (message) == number && isController (message); }
export function isAllNotesOff (message)                     { return isControllerNumber (message, 123); }
export function isAllSoundOff (message)                     { return isControllerNumber (message, 120); }
export function isQuarterFrame (message)                    { return getByte0 (message) == 0xf1; })"
R"(
export function isClock (message)                           { return getByte0 (message) == 0xf8; }
export function isStart (message)                           { return getByte0 (message) == 0xfa; }
export function isContinue (message)                        { return getByte0 (message) == 0xfb; }
export function isStop (message)                            { return getByte0 (message) == 0xfc; }
export function isActiveSense (message)                     { return getByte0 (message) == 0xfe; }
export function isMetaEvent (message)                       { return getByte0 (message) == 0xff; }
export function isSongPositionPointer (message)             { return getByte0 (message) == 0xf2; }
export function getSongPositionPointerValue (message)       { return get14BitValue (message); }

export function getChromaticScaleIndex (note)               { return (note % 12) & 0xf; }
export function getOctaveNumber (note, octaveForMiddleC)    { return (Math.floor (note / 12) + ((octaveForMiddleC ? octaveForMiddleC : 3) - 5)) & 0xff; }
export function getNoteName (note)                          { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithSharps (note)                { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithFlats (note)                 { const names = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithOctaveNumber (note)          { return getNoteName (note) + getOctaveNumber (note); }
export function isNatural (note)                            { const nats = [true, false, true, false, true, true, false, true, false, true, false, true]; return nats[getChromaticScaleIndex (note)]; }
export function isAccidental (note)                         { return ! isNatural (note); })"
R"(

export function printHexMIDIData (message)
{
    const numBytes = getMessageSize (message);

    if (numBytes == 0)
        return "[empty]";

    let s = "";

    for (let i = 0; i < numBytes; ++i)
    {
        if (i != 0)  s += ' ';

        const byte = message >> (16 - 8 * i) & 0xff;
        s += "0123456789abcdef"[byte >> 4];
        s += "0123456789abcdef"[byte & 15];
    }

    return s;
}

export function getMIDIDescription (message)
{
    const channelText = " Channel " + getChannel1to16 (message);
    function getNote (m)   { const s = getNoteNameWithOctaveNumber (getNoteNumber (message)); return s.length < 4 ? s + " " : s; };

    if (isNoteOn (message))                return "Note-On:  "   + getNote (message) + channelText + "  Velocity " + getVelocity (message);
    if (isNoteOff (message))               return "Note-Off: "   + getNote (message) + channelText + "  Velocity " + getVelocity (message);
    if (isAftertouch (message))            return "Aftertouch: " + getNote (message) + channelText +  ": " + getAfterTouchValue (message);
    if (isPitchWheel (message))            return "Pitch wheel: " + getPitchWheelValue (message) + ' ' + channelText;
    if (isChannelPressure (message))       return "Channel pressure: " + getChannelPressureValue (message) + ' ' + channelText;
    if (isController (message))            return "Controller:" + channelText + ": " + getControllerName (getControllerNumber (message)) + " = " + getControllerValue (message);
    if (isProgramChange (message))         return "Program change: " + getProgramChangeNumber (message) + ' ' + channelText;
    if (isAllNotesOff (message))           return "All notes off:" + channelText;
    if (isAllSoundOff (message))           return "All sound off:" + channelText;
    if (isQuarterFrame (message))          return "Quarter-frame";
    if (isClock (message))                 return "Clock";
    if (isStart (message))                 return "Start";
    if (isContinue (message))              return "Continue";)"
R"TEXT(
    if (isStop (message))                  return "Stop";
    if (isMetaEvent (message))             return "Meta-event: type " + getByte1 (message);
    if (isSongPositionPointer (message))   return "Song Position: " + getSongPositionPointerValue (message);

    return printHexMIDIData (message);
}

export function getControllerName (controllerNumber)
{
    if (controllerNumber < 128)
    {
        const controllerNames = [
            "Bank Select",                  "Modulation Wheel (coarse)",      "Breath controller (coarse)",       undefined,
            "Foot Pedal (coarse)",          "Portamento Time (coarse)",       "Data Entry (coarse)",              "Volume (coarse)",
            "Balance (coarse)",             undefined,                        "Pan position (coarse)",            "Expression (coarse)",
            "Effect Control 1 (coarse)",    "Effect Control 2 (coarse)",      undefined,                          undefined,
            "General Purpose Slider 1",     "General Purpose Slider 2",       "General Purpose Slider 3",         "General Purpose Slider 4",
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            "Bank Select (fine)",           "Modulation Wheel (fine)",        "Breath controller (fine)",         undefined,
            "Foot Pedal (fine)",            "Portamento Time (fine)",         "Data Entry (fine)",                "Volume (fine)",
            "Balance (fine)",               undefined,                        "Pan position (fine)",              "Expression (fine)",
            "Effect Control 1 (fine)",      "Effect Control 2 (fine)",        undefined,                          undefined,)TEXT"
R"TEXT(
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            "Hold Pedal",                   "Portamento",                     "Sustenuto Pedal",                  "Soft Pedal",
            "Legato Pedal",                 "Hold 2 Pedal",                   "Sound Variation",                  "Sound Timbre",
            "Sound Release Time",           "Sound Attack Time",              "Sound Brightness",                 "Sound Control 6",
            "Sound Control 7",              "Sound Control 8",                "Sound Control 9",                  "Sound Control 10",
            "General Purpose Button 1",     "General Purpose Button 2",       "General Purpose Button 3",         "General Purpose Button 4",
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          "Reverb Level",
            "Tremolo Level",                "Chorus Level",                   "Celeste Level",                    "Phaser Level",
            "Data Button increment",        "Data Button decrement",          "Non-registered Parameter (fine)",  "Non-registered Parameter (coarse)",
            "Registered Parameter (fine)",  "Registered Parameter (coarse)",  undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,)TEXT"
R"(
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            "All Sound Off",                "All Controllers Off",            "Local Keyboard",                   "All Notes Off",
            "Omni Mode Off",                "Omni Mode On",                   "Mono Operation",                   "Poly Operation"
        ];

        const name = controllerNames[controllerNumber];

        if (name)
            return name;
    }

    return controllerNumber.toString();
}
)";
    static constexpr const char* cmajeventlistenerlist_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


/// This event listener management class allows listeners to be attached and
/// removed from named event types.
export class EventListenerList
{
    constructor()
    {
        this.listenersPerType = {};
    }

    /// Adds a listener for a specifc event type.
    /// If the listener is already registered, this will simply add it again.
    /// Each call to addEventListener() must be paired with a removeventListener()
    /// call to remove it.
    addEventListener (type, listener)
    {
        if (type && listener)
        {
            const list = this.listenersPerType[type];

            if (list)
                list.push (listener);
            else
                this.listenersPerType[type] = [listener];
        }
    }

    /// Removes a listener that was previously added to the given event.
    removeEventListener (type, listener)
    {
        if (type && listener)
        {
            const list = this.listenersPerType[type];

            if (list)
            {
                const i = list.indexOf (listener);

                if (i >= 0)
                    list.splice (i, 1);
            }
        }
    }

    /// Attaches a callback function that will be automatically unregistered
    /// the first time it is invoked.
    addSingleUseListener (type, listener)
    {
        const l = message =>
        {
            this.removeEventListener (type, l);
            listener?.(message);
        };

        this.addEventListener (type, l);
    })"
R"(

    /// Synchronously dispatches an event object to all listeners
    /// that are registered for the given type.
    dispatchEvent (type, event)
    {
        const list = this.listenersPerType[type];

        if (list)
            for (const listener of list)
                listener?.(event);
    }

    /// Returns the number of listeners that are currently registered
    /// for the given type of event.
    getNumListenersForType (type)
    {
        const list = this.listenersPerType[type];
        return list ? list.length : 0;
    }
}
)";
    static constexpr const char* cmajserversession_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"

import { PatchConnection } from "/cmaj_api/cmaj-patch-connection.js"
import { EventListenerList } from "/cmaj_api/cmaj-event-listener-list.js"


//==============================================================================
/// This class provides the API and manages the communication protocol between
/// a javascript application and a Cmajor session running on some kind of server
/// (which may be local or remote).
///
/// This is an abstract base class: some kind of transport layer will create a
/// subclass of ServerSession which a client application can then use to control
/// and interact with the server.
export class ServerSession   extends EventListenerList
{
    /// A server session must be given a unique string sessionID.
    constructor (sessionID)
    {
        super();

        this.sessionID = sessionID;
        this.activePatchConnections = new Set();
        this.status = { connected: false, loaded: false };
        this.lastServerMessageTime = Date.now();
        this.pingTimer = setInterval (() => this.pingServer(), 1311);
    }

    /// Call `dispose()` when this session is no longer needed and should be released.
    dispose()
    {
        if (this.pingTimer)
        {
            clearInterval (this.pingTimer);
            this.pingTimer = undefined;
        }

        this.status = { connected: false, loaded: false };
    }

    //==============================================================================
    // Session status methods:)"
R"(

    /// Attaches a listener function which will be called when the session status changes.
    /// The function will be passed an argument object containing lots of properties describing the
    /// state, including any errors, loaded patch manifest, etc.
    addStatusListener (listener)                        { this.addEventListener    ("session_status", listener); }

    /// Removes a listener that was previously added by `addStatusListener()`
    removeStatusListener (listener)                     { this.removeEventListener ("session_status", listener); }

    /// Asks the server to asynchronously send a status update message with the latest status.
    requestSessionStatus()                              { this.sendMessageToServer ({ type: "req_session_status" }); }

    /// Returns the session's last known status object.
    getCurrentStatus()                                  { return this.status; }

    //==============================================================================
    // Patch loading:

    /// Asks the server to load the specified patch into our session.
    loadPatch (patchFileToLoad)
    {
        this.currentPatchLocation = patchFileToLoad;
        this.sendMessageToServer ({ type: "load_patch", file: patchFileToLoad });
    }

    /// Tells the server to asynchronously generate a list of patches that it knows about.
    /// The function provided will be called back with an array of manifest objects describing
    /// each of the patches.
    requestAvailablePatchList (callbackFunction)
    {
        const replyType = this.createReplyID ("patchlist_");
        this.addSingleUseListener (replyType, callbackFunction);
        this.sendMessageToServer ({ type: "req_patchlist",
                                    replyType: replyType });
    })"
R"(

    /// Creates and returns a new PatchConnection object which can be used to control the
    /// patch that this session has loaded.
    createPatchConnection()
    {
        class ServerPatchConnection  extends PatchConnection
        {
            constructor (session)
            {
                super();
                this.session = session;
                this.manifest = session.status?.manifest;
                this.session.activePatchConnections.add (this);
            }

            dispose()
            {
                this.session.activePatchConnections.delete (this);
                this.session = undefined;
            }

            sendMessageToServer (message)
            {
                this.session?.sendMessageToServer (message);
            }

            getResourceAddress (path)
            {
                if (! this.session?.status?.httpRootURL)
                    return undefined;

                return this.session.status.httpRootURL
                        + (path.startsWith ("/") ? path.substr (1) : path);
            })"
R"(

            /// Attaches a listener function that can monitor floating-point audio data on a suitable
            /// input or output endpoint.
            /// If an endpoint has the right shape to be treated as "audio" then this can be used to
            /// get a stream of updates of the min/max range of chunks of data that is flowing through it.
            /// There will be one callback per chunk of data, and the size of chunks can be modified by
            /// calling setAudioDataGranularity().
            /// The listener will receive an argument object containing two properties 'min' and 'max',
            /// which are each an array of values, one element per audio channel. This allows you to
            /// find the highest and lowest samples in that chunk for each channel.
            addEndpointAudioListener (endpointID, listener)
            {
                this.addEventListener ("audio_data_" + endpointID, listener);
                this.setAudioDataGranularity (endpointID, undefined);
            }

            /// Removes a listener that was previously added with addEndpointAudioListener()
            removeEndpointAudioListener (endpointID, listener)
            {
                this.removeEventListener ("audio_data_" + endpointID, listener);
                this.setAudioDataGranularity (endpointID, undefined);
            }

            /// This can be used to change the size of chunk being sent to an audio endpoint listener
            /// for a given endpoint. See addEndpointAudioListener() to attach the listeners.
            setAudioDataGranularity (endpointID, framesPerCallback)
            {
                if (! this.endpointAudioGranularities)
                    this.endpointAudioGranularities = {};

                if (framesPerCallback && framesPerCallback > 0 && framesPerCallback <= 96000)
                    this.endpointAudioGranularities[endpointID] = framesPerCallback;)"
R"(

                const currentGranularity = this.endpointAudioGranularities[endpointID] || 1024;
                const gran = this.getNumListenersForType ("audio_data_" + endpointID) > 0 ? currentGranularity : 0;
                this.sendMessageToServer ({ type: "set_endpoint_audio_monitoring", endpoint: endpointID, granularity: gran });
            }
        }

        return new ServerPatchConnection (this);
    }

    //==============================================================================
    // Audio input source handling:

    /// Sets a custom audio input source for a particular endpoint.
    /// If shouldMute is true, it will be muted. If fileDataToPlay is an array buffer that
    /// can be parsed as an audio file, then it will be sent across for the server to play
    /// as a loop.
    /// When a source is changed, a callback is sent to any audio input mode listeners (see
    /// `addAudioInputModeListener()`)
    setAudioInputSource (endpointID, shouldMute, fileDataToPlay)
    {
        const loopFile = "_audio_source_" + endpointID;

        if (fileDataToPlay)
        {
            this.registerFile (loopFile,
            {
               size: fileDataToPlay.byteLength,
               read: (start, length) => { return new Blob ([fileDataToPlay.slice (start, start + length)]); }
            });

            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        file: loopFile });
        }
        else
        {
            this.removeFile (loopFile);

            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        mute: !! shouldMute });
        }
    })"
R"(

    /// Attaches a listener function to be told when the input source for a particular
    /// endpoint is changed by a call to `setAudioInputSource()`.
    addAudioInputModeListener (endpointID, listener)    { this.addEventListener    ("audio_input_mode_" + endpointID, listener); }
    /// Removes a listener previously added with `addAudioInputModeListener()`
    removeAudioInputModeListener (endpointID, listener) { this.removeEventListener ("audio_input_mode_" + endpointID, listener); }

    /// Asks the server to send an update with the latest status to any audio mode listeners that
    /// are attached to the given endpoint.
    requestAudioInputMode (endpointID)                  { this.sendMessageToServer ({ type: "req_audio_input_mode", endpoint: endpointID }); }

    //==============================================================================
    // Audio device methods:

    /// Enables or disables audio playback.
    /// When playback state changes, a status update is sent to any status listeners.
    setAudioPlaybackActive (shouldBeActive)             { this.sendMessageToServer ({ type: "set_audio_playback_active", active: shouldBeActive }); }

    /// Asks the server to apply a new set of audio device properties.
    /// The properties object uses the same format as the object that is passed to the listeners
    /// (see `addAudioDevicePropertiesListener()`).
    setAudioDeviceProperties (newProperties)            { this.sendMessageToServer ({ type: "set_audio_device_props", properties: newProperties }); }

    /// Attaches a listener function which will be called when the audio device properties are
    /// changed. The listener function will be passed an argument object containing all the
    /// details about the device.
    /// Remove the listener when it's no longer needed with `removeAudioDevicePropertiesListener()`.
    addAudioDevicePropertiesListener (listener)         { this.addEventListener    ("audio_device_properties", listener); })"
R"(

    /// Removes a listener that was added with `addAudioDevicePropertiesListener()`
    removeAudioDevicePropertiesListener (listener)      { this.removeEventListener ("audio_device_properties", listener); }

    /// Causes an asynchronous callback to any audio device listeners that are registered.
    requestAudioDeviceProperties()                      { this.sendMessageToServer ({ type: "req_audio_device_props" }); }

    //==============================================================================
    /// Asks the server to asynchronously generate some code from the currently loaded patch.
    /// The `codeType` must be one of the strings that are listed in the status's `codeGenTargets`
    /// property.
    /// `extraOptions` is an object containing any target-specific properties, and may be undefined.
    /// The `callbackFunction` is a function that will be called with the result when it has
    /// been generated. The results object will contain properties for any code, errors and other
    /// metadata about the generated data.
    requestGeneratedCode (codeType, extraOptions, callbackFunction)
    {
        const replyType = this.createReplyID ("codegen_");
        this.addSingleUseListener (replyType, callbackFunction);
        this.sendMessageToServer ({ type: "req_codegen",
                                    codeType: codeType,
                                    options: extraOptions,
                                    replyType: replyType });
    }

    //==============================================================================
    // File change monitoring:)"
R"(

    /// Attaches a listener to be told when a file change is detected in the currently-loaded
    /// patch. The function will be called with an object that gives rough details about the
    /// type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
    /// won't provide any information about exactly which files are involved.
    addFileChangeListener (listener)                    { this.addEventListener    ("patch_source_changed", listener); }
    /// Removes a listener that was previously added with `addFileChangeListener()`.
    removeFileChangeListener (listener)                 { this.removeEventListener ("patch_source_changed", listener); }

    //==============================================================================
    // CPU level monitoring methods:

    /// Attaches a listener function which will be sent messages containing CPU info.
    /// To remove the listener, call `removeCPUListener()`. To change the rate of these
    /// messages, use `setCPULevelUpdateRate()`.
    addCPUListener (listener)                       { this.addEventListener    ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /// Removes a listener that was previously attached with `addCPUListener()`.
    removeCPUListener (listener)                    { this.removeEventListener ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /// Changes the frequency at which CPU level update messages are sent to listeners.
    setCPULevelUpdateRate (framesPerUpdate)         { this.cpuFramesPerUpdate = framesPerUpdate; this.updateCPULevelUpdateRate(); })"
R"(

    //==============================================================================
    /// Registers a virtual file with the server, under the given name.
    /// The contentProvider object must have a property called `size` which is a
    /// constant size in bytes for the file, and a method `read (offset, size)` which
    /// returns an array (or UInt8Array) of bytes for the data in a given chunk of the file.
    /// The server may repeatedly call this method at any time until `removeFile()` is
    /// called to deregister the file.
    registerFile (filename, contentProvider)
    {
        if (! this.files)
            this.files = new Map();

        this.files.set (filename, contentProvider);

        this.sendMessageToServer ({ type: "register_file",
                                    filename: filename,
                                    size: contentProvider.size });
    }

    /// Removes a file that was previously registered with `registerFile()`.
    removeFile (filename)
    {
        this.sendMessageToServer ({ type: "remove_file",
                                    filename: filename });
        this.files?.delete (filename);
    }

    //==============================================================================
    /// Sends a ping message to the server.
    /// You shouldn't need to call this - the ServerSession class takes care of sending
    /// a ping at regular intervals.
    pingServer()
    {
        this.sendMessageToServer ({ type: "ping" });

        if (Date.now() > this.lastServerMessageTime + 10000)
            this.setNewStatus ({
                connected: false,
                loaded: false,
                status: "Cannot connect to the Cmajor server"
            });
    }

    //==============================================================================
    // Private methods from this point...)"
R"(

    // An implementation subclass must call this when the session first connects
    handleSessionConnection()
    {
        if (! this.status.connected)
        {
            this.requestSessionStatus();
            this.requestAudioDeviceProperties();

            if (this.currentPatchLocation)
            {
                this.loadPatch (this.currentPatchLocation);
                this.currentPatchLocation = undefined;
            }
        }
    }

    // An implementation subclass must call this when a message arrives
    handleMessageFromServer (msg)
    {
        this.lastServerMessageTime = Date.now();
        const type = msg.type;
        const message = msg.message;

        switch (type)
        {
            case "cpu_info":
            case "audio_device_properties":
            case "patch_source_changed":
                this.dispatchEvent (type, message);
                break;

            case "session_status":
                message.connected = true;
                this.setNewStatus (message);
                break;

            case "req_file_read":
                this.handleFileReadRequest (message);
                break;

            case "ping":
                break;

            default:
                if (type.startsWith ("audio_input_mode_") || type.startsWith ("reply_"))
                {
                    this.dispatchEvent (type, message);
                    break;
                }

                for (const c of this.activePatchConnections)
                    c.deliverMessageFromServer (msg);

                break;
        }
    }

    setNewStatus (newStatus)
    {
        this.status = newStatus;
        this.dispatchEvent ("session_status", this.status);
        this.updateCPULevelUpdateRate();
    })"
R"(

    updateCPULevelUpdateRate()
    {
        const rate = this.getNumListenersForType ("cpu_info") > 0 ? (this.cpuFramesPerUpdate || 15000) : 0;
        this.sendMessageToServer ({ type: "set_cpu_info_rate",
                                    framesPerCallback: rate });
    }

    handleFileReadRequest (request)
    {
        const contentProvider = this.files?.get (request?.file);

        if (contentProvider && request.offset !== null && request.size != 0)
        {
            const data = contentProvider.read (request.offset, request.size);
            const reader = new FileReader();

            reader.onloadend = (e) =>
            {
                const base64 = e.target?.result?.split?.(",", 2)[1];

                if (base64)
                    this.sendMessageToServer ({ type: "file_content",
                                                file: request.file,
                                                data: base64,
                                                start: request.offset });
            };

            reader.readAsDataURL (data);
        }
    }

    createReplyID (stem)
    {
        return "reply_" + stem + this.createRandomID();
    }

    createRandomID()
    {
        return (Math.floor (Math.random() * 100000000)).toString();
    }
}
)";
    static constexpr const char* cmajgenericpatchview_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"

import * as Controls from "/cmaj_api/cmaj-parameter-controls.js"

//==============================================================================
class GenericPatchView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();

        this.patchConnection = patchConnection;

        this.statusListener = status =>
        {
            this.status = status;
            this.createControlElements();
        };

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();

        this.titleElement      = this.shadowRoot.getElementById ("patch-title");
        this.parametersElement = this.shadowRoot.getElementById ("patch-parameters");
    }

    connectedCallback()
    {
        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
    }

    disconnectedCallback()
    {
        this.patchConnection.removeStatusListener (this.statusListener);
    }

    //==============================================================================
    // private methods..
    createControlElements()
    {
        this.parametersElement.innerHTML = "";
        this.titleElement.innerText = this.status?.manifest?.name ?? "Cmajor";)"
R"(

        for (const endpointInfo of this.status?.details?.inputs)
        {
            if (! endpointInfo.annotation?.hidden)
            {
                const control = Controls.createLabelledControl (this.patchConnection, endpointInfo);

                if (control)
                    this.parametersElement.appendChild (control);
            }
        }
    }

    getHTML()
    {
        return `
            <style>
            * {
                box-sizing: border-box;
                user-select: none;
                -webkit-user-select: none;
                -moz-user-select: none;
                -ms-user-select: none;
                font-family: Avenir, 'Avenir Next LT Pro', Montserrat, Corbel, 'URW Gothic', source-sans-pro, sans-serif;
                font-size: 0.9rem;
            }

            :host {
                --header-height: 2.5rem;
                --foreground: #ffffff;
                --background: #1a1a1a;

                display: block;
                height: 100%;
                background-color: var(--background);
            }

            .main {
                background: var(--background);
                height: 100%;
            }

            .header {
                width: 100%;
                height: var(--header-height);
                border-bottom: 0.1rem solid var(--foreground);
                display: flex;
                justify-content: space-between;
                align-items: center;
            }

            #patch-title {
                color: var(--foreground);
                text-overflow: ellipsis;
                white-space: nowrap;
                overflow: hidden;
                cursor: default;
                font-size: 140%;
            })"
R"(

            .logo {
                flex: 1;
                height: 100%;
                background-color: var(--foreground);
                mask: url(cmaj_api/assets/sound-stacks-logo.svg);
                mask-repeat: no-repeat;
                -webkit-mask: url(cmaj_api/assets/sound-stacks-logo.svg);
                -webkit-mask-repeat: no-repeat;
                min-width: 6.25rem;
            }

            .header-filler {
                flex: 1;
            }

            #patch-parameters {
                height: calc(100% - var(--header-height));
                overflow: auto;
                padding: 1rem;
                text-align: center;
            }

            ${Controls.Options.getCSS()}
            ${Controls.Knob.getCSS()}
            ${Controls.Switch.getCSS()}
            ${Controls.LabelledControlHolder.getCSS()}

            </style>

            <div class="main">
              <div class="header">
                <span class="logo"></span>
                <h2 id="patch-title"></h2>
                <div class="header-filler"></div>
              </div>
              <div id="patch-parameters"></div>
            </div>`;
    }
}

window.customElements.define ("cmaj-generic-patch-view", GenericPatchView);

//==============================================================================
export default function createPatchView (patchConnection)
{
    return new GenericPatchView (patchConnection);
}
)";
    static constexpr const char* cmajpatchview_js =
        R"(//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


/// Returns a list of types of view that can be created for this patch
export function getAvailableViewTypes (patchConnection)
{
    if (! patchConnection)
        return [];

    if (patchConnection.manifest?.view?.src)
        return ["custom", "generic"];

    return ["generic"];
}

/// Creates and returns a HTMLElement view which can be shown to control this patch.
///
/// If no preferredType argument is supplied, this will return either a custom patch-specific
/// view (if the manifest specifies one), or a generic view if not. The preferredType argument
/// can be used to choose one of the types of view returned by getAvailableViewTypes().
export async function createPatchView (patchConnection, preferredType)
{
    if (patchConnection?.manifest)
    {
        let view = patchConnection.manifest.view;

        if (view && preferredType === "generic")
            if (view.src)
                view = undefined;

        const viewModuleURL = view?.src ? view.src : "/cmaj_api/cmaj-generic-patch-view.js";
        const viewModule = await import (patchConnection.getResourceAddress (viewModuleURL));
        const patchView = await viewModule?.default (patchConnection);

        if (patchView)
        {
            patchView.style.display = "block";

            if (view?.width > 10)
                patchView.style.width = view.width + "px";
            else
                patchView.style.width = undefined;)"
R"(

            if (view?.height > 10)
                patchView.style.height = view.height + "px";
            else
                patchView.style.height = undefined;

            return patchView;
        }
    }

    return undefined;
}
)";
    static constexpr const char* assets_cmajorlogo_svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="150 140 1620 670">
  <g>
    <path
      d="M944.511,462.372V587.049H896.558V469.165c0-27.572-13.189-44.757-35.966-44.757-23.577,0-39.958,19.183-39.958,46.755V587.049H773.078V469.165c0-27.572-13.185-44.757-35.962-44.757-22.378,0-39.162,19.581-39.162,46.755V587.049H650.4v-201.4h47.551v28.77c8.39-19.581,28.771-32.766,54.346-32.766,27.572,0,46.353,11.589,56.343,35.166,11.589-23.577,33.57-35.166,65.934-35.166C918.937,381.652,944.511,412.42,944.511,462.372Zm193.422-76.724h47.953v201.4h-47.953V557.876c-6.794,19.581-31.167,33.567-64.335,33.567q-42.558,0-71.928-29.969c-19.183-20.381-28.771-45.155-28.771-75.128s9.588-54.743,28.771-74.726c19.581-20.377,43.556-30.366,71.928-30.366,33.168,0,57.541,13.985,64.335,33.566Zm3.6,100.7c0-17.579-5.993-32.368-17.981-43.953-11.589-11.59-26.374-17.583-43.559-17.583s-31.167,5.993-42.756,17.583c-11.187,11.585-16.783,26.374-16.783,43.953s5.6,32.369,16.783,43.958c11.589,11.589,25.575,17.583,42.756,17.583s31.97-5.994,43.559-17.583C1135.537,518.715,1141.53,503.929,1141.53,486.346Zm84.135,113.49c0,21.177-7.594,29.571-25.575,29.571-2.8,0-7.192-.4-13.185-.8v42.357c4.393.8,11.187,1.2,19.979,1.2,44.355,0,66.734-22.776,66.734-67.932V385.648h-47.953Zm23.978-294.108c-15.987,0-28.774,12.385-28.774,28.372s12.787,28.369,28.774,28.369a28.371,28.371,0,0,0,0-56.741Zm239.674,104.694c21.177,20.381,31.966,45.956,31.966,75.924s-10.789,55.547-31.966,75.928-47.154,30.769-77.926,30.769-56.741-10.392-77.922-30.769-31.966-45.955-31.966-75.928,10.789-55.543,31.966-75.924,47.154-30.768,77.922-30.768S1468.136,390.041,1489.317,410.422Zm-15.585,75.924c0-17.981-5.994-32.766-17.985-44.753-11.988-12.39-26.773-18.383-44.356-18.383-17.981,0-32.766,5.993-44.754,18.383-11.589,11.987-17.583,26.772-17.583,44.753s5.994,32.77,17.583,45.156c11.988,11.987,26.773,17.985,44.754,17.985q26.374,0,44.356-17.985C1467.738,519.116,1473.732,504.331,1473.732,486.346Zm184.122-104.694c-28.373,0-50.349,12.787-59.941,33.964V385.648h-47.551v201.4h47.551v-105.9c0-33.169,21.177-53.948,54.345-5)"
R"(3.948a102.566,102.566,0,0,1,19.979,2V382.85A74.364,74.364,0,0,0,1657.854,381.652ZM580.777,569.25l33.909,30.087c-40.644,47.027-92.892,70.829-156.173,70.829-58.637,0-108.567-19.737-149.788-59.8C268.082,570.31,247.763,519.8,247.763,460s20.319-109.726,60.962-149.786c41.221-40.059,91.151-60.38,149.788-60.38,62.119,0,113.789,22.643,154.432,68.507l-33.864,30.134c-16.261-19.069-35.272-32.933-56.978-41.783V486.346H496.536V621.1Q546.954,610.231,580.777,569.25Zm-237.74,9.1A150.247,150.247,0,0,0,396.5,614.04V486.346H370.929V319.387a159.623,159.623,0,0,0-27.892,22.829Q297.187,389.16,297.186,460C297.186,507.229,312.47,547.06,343.037,578.354Zm115.476,46.66a187.178,187.178,0,0,0,27.28-1.94V486.346H474.548V295.666c-5.236-.426-10.567-.677-16.035-.677a177.387,177.387,0,0,0-40.029,4.4V486.346H407.239v131.4A175.161,175.161,0,0,0,458.513,625.014Z"
      fill="#fff" />
  </g>
</svg>
)";
    static constexpr const char* assets_soundstackslogo_svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 560">
  <g>
    <path d="M532.3,205.9c4.4,5.9,6.6,13.4,6.6,22.7c0,9.2-2.3,17.3-6.9,24.2c-4.6,6.9-11.2,12.3-19.7,16.2c-8.5,3.9-18.6,5.8-30.1,5.8
        c-11.6,0-21.9-2.1-30.8-6.2c-8.9-4.1-15.9-10.2-21-18.1c-5.1-8-7.6-17.7-7.6-29.1v-5.5h23.5v5.5c0,10.9,3.3,19,9.8,24.4
        c6.5,5.4,15.2,8.1,26.1,8.1c11,0,19.3-2.3,24.9-6.9c5.6-4.6,8.4-10.6,8.4-17.9c0-4.8-1.3-8.8-4-11.9c-2.6-3.1-6.4-5.5-11.2-7.4
        c-4.8-1.8-10.6-3.6-17.4-5.2l-8.1-2c-9.8-2.3-18.4-5.2-25.6-8.7c-7.3-3.4-12.9-8-16.8-13.8s-5.9-13.1-5.9-22
        c0-9.1,2.2-16.9,6.7-23.3c4.5-6.5,10.7-11.4,18.6-15s17.2-5.3,27.9-5.3c10.7,0,20.3,1.8,28.7,5.5c8.4,3.7,15.1,9.1,19.9,16.3
        c4.8,7.2,7.3,16.2,7.3,27.1v7.9h-23.5v-7.9c0-6.6-1.4-12-4.1-16.1c-2.7-4.1-6.5-7.1-11.3-9c-4.8-1.9-10.5-2.9-16.9-2.9
        c-9.4,0-16.7,1.9-21.9,5.8c-5.2,3.9-7.8,9.3-7.8,16.2c0,4.7,1.1,8.5,3.4,11.6c2.3,3,5.6,5.5,10,7.4c4.4,1.9,9.9,3.6,16.5,5.1l8.1,2
        c10,2.2,18.8,5,26.4,8.5C521.9,195.4,527.9,200.1,532.3,205.9z M637.7,268.1c-8.5,4.5-18.2,6.7-29,6.7c-10.9,0-20.5-2.2-28.9-6.7
        c-8.4-4.5-15.1-10.9-19.9-19.2c-4.8-8.4-7.3-18.3-7.3-29.7v-3.3c0-11.4,2.4-21.3,7.3-29.6c4.8-8.3,11.5-14.7,19.9-19.2
        c8.4-4.5,18.1-6.8,28.9-6.8c10.9,0,20.5,2.3,29,6.8c8.5,4.5,15.2,11,20,19.2c4.8,8.3,7.3,18.2,7.3,29.6v3.3
        c0,11.4-2.4,21.3-7.3,29.7C652.9,257.2,646.2,263.6,637.7,268.1z M642.3,216.5c0-11.3-3.1-20.1-9.2-26.5
        c-6.2-6.4-14.3-9.6-24.4-9.6c-9.8,0-17.9,3.2-24.1,9.6c-6.2,6.4-9.4,15.2-9.4,26.5v2c0,11.3,3.1,20.1,9.4,26.5
        c6.2,6.4,14.3,9.6,24.1,9.6c10,0,18.1-3.2,24.3-9.6c6.2-6.4,9.4-15.2,9.4-26.5V216.5z M699.5,268.2c6.5,3.7,13.8,5.5,22,5.5
        c10.3,0,18-1.9,23.3-5.8s8.9-8,11-12.4h3.5v16.3h22.2V163.2h-22.7V218c0,11.7-2.8,20.7-8.5,27c-5.6,6.2-13.1,9.4-22.3,9.4
        c-8.4,0-14.9-2.2-19.6-6.7c-4.7-4.5-7-11.4-7-20.8v-63.6h-22.7v65.1c0,9.4,1.8,17.5,5.5,24.2C688,259.3,693,264.5,699.5,268.2z)"
R"(
         M822.3,216.9c0-11.7,2.8-20.7,8.5-26.8c5.6-6.2,13.2-9.2,22.5-9.2c8.2,0,14.7,2.2,19.4,6.7c4.7,4.5,7,11.4,7,20.8v63.4h22.7v-65.1
        c0-9.4-1.8-17.4-5.5-24.1c-3.7-6.7-8.7-11.8-15.1-15.5c-6.4-3.7-13.7-5.5-21.9-5.5c-10.4,0-18.3,1.9-23.5,5.7
        c-5.3,3.8-8.9,7.9-11,12.3h-3.5v-16.3h-22.2v108.5h22.7V216.9z M923.3,249.2c-4.6-8.3-6.9-18.3-6.9-30v-3.3
        c0-11.6,2.3-21.6,6.8-29.9c4.5-8.4,10.6-14.7,18.3-19.1c7.6-4.4,16-6.6,25.1-6.6c7,0,12.9,0.9,17.7,2.6c4.8,1.8,8.7,4,11.8,6.7
        c3.1,2.7,5.4,5.5,7,8.5h3.5v-60.3h22.7v154H1007v-15.4h-3.5c-2.8,4.7-7,8.9-12.6,12.8c-5.6,3.8-13.8,5.7-24.3,5.7
        c-8.9,0-17.2-2.2-24.9-6.6C934.1,263.8,928,257.4,923.3,249.2z M939.3,218.5c0,11.7,3.2,20.8,9.6,27.1c6.4,6.3,14.4,9.5,24.1,9.5
        c9.8,0,17.9-3.2,24.3-9.5c6.4-6.3,9.6-15.3,9.6-27.1v-2c0-11.6-3.2-20.5-9.5-26.8c-6.3-6.3-14.4-9.5-24.4-9.5
        c-9.7,0-17.7,3.2-24.1,9.5c-6.4,6.3-9.6,15.3-9.6,26.8V218.5z M514.3,366.9c-7.6-3.4-16.4-6.3-26.4-8.5l-8.1-2
        c-6.6-1.5-12.1-3.2-16.5-5.1c-4.4-1.9-7.7-4.4-10-7.4c-2.3-3-3.4-6.9-3.4-11.6c0-6.9,2.6-12.3,7.8-16.2c5.2-3.9,12.5-5.8,21.9-5.8
        c6.5,0,12.1,1,16.9,2.9c4.8,1.9,8.6,4.9,11.3,9c2.7,4.1,4.1,9.5,4.1,16.1v7.9h23.5v-7.9c0-10.9-2.4-19.9-7.3-27.1
        c-4.8-7.2-11.5-12.6-19.9-16.3c-8.4-3.7-18-5.5-28.7-5.5c-10.7,0-20,1.8-27.9,5.3s-14.1,8.5-18.6,15c-4.5,6.5-6.7,14.2-6.7,23.3
        c0,8.9,2,16.3,5.9,22c4,5.7,9.6,10.3,16.8,13.8c7.3,3.4,15.8,6.3,25.6,8.7l8.1,2c6.7,1.6,12.5,3.3,17.4,5.2
        c4.8,1.8,8.6,4.3,11.2,7.4c2.6,3.1,4,7,4,11.9c0,7.3-2.8,13.3-8.4,17.9c-5.6,4.6-13.9,6.9-24.9,6.9c-10.9,0-19.5-2.7-26.1-8.1
        c-6.5-5.4-9.8-13.6-9.8-24.4v-5.5h-23.5v5.5c0,11.4,2.5,21.2,7.6,29.1c5.1,8,12.1,14,21,18.2c8.9,4.1,19.2,6.2,30.8,6.2
        c11.6,0,21.6-1.9,30.1-5.8c8.5-3.9,15.1-9.3,19.7-16.2c4.6-6.9,6.9-15,6.9-24.2s-2.2-16.8-6.6-22.7
        C527.9,375.1,521.9,370.4,514.3,366.9z M597.4,302.4h-22.7v35.9h-29.9v19.1h29.9v67.5c0,6.6,1.9,11.9,5.8,15.8)"
R"(
        c3.9,4,9.1,5.9,15.7,5.9h29.9v-19.1h-22.7c-4.1,0-6.2-2.2-6.2-6.6v-63.6h32.3v-19.1h-32.3V302.4z M757.4,427.8h9.2v18.9h-19.1
        c-5.1,0-9.3-1.5-12.5-4.4c-3.2-2.9-4.8-6.9-4.8-11.9v-1.5h-3.3c-2.8,5.7-7.1,10.6-13,14.7c-5.9,4.1-14.3,6.2-25.3,6.2
        c-9.1,0-17.5-2.2-25.1-6.6c-7.6-4.4-13.7-10.7-18.3-19c-4.5-8.3-6.8-18.3-6.8-30v-3.3c0-11.7,2.3-21.7,6.9-30
        c4.6-8.3,10.7-14.6,18.4-19c7.6-4.4,15.9-6.6,24.9-6.6c10.6,0,18.7,1.9,24.3,5.7c5.6,3.8,9.9,8.1,12.6,13h3.5v-15.6h22.2v82.9
        C751.2,425.6,753.2,427.8,757.4,427.8z M728.8,391.5c0-11.6-3.2-20.5-9.6-26.8c-6.4-6.3-14.5-9.5-24.3-9.5
        c-9.7,0-17.7,3.2-24.1,9.5c-6.4,6.3-9.6,15.3-9.6,26.8v2c0,11.7,3.2,20.8,9.6,27.1c6.4,6.3,14.4,9.5,24.1,9.5
        c10,0,18.1-3.2,24.4-9.5c6.3-6.3,9.5-15.3,9.5-27.1V391.5z M848.4,422.2c-5,4.9-12.2,7.4-21.8,7.4c-6.3,0-12-1.4-17-4.2
        c-5.1-2.8-9.1-6.9-12-12.3c-2.9-5.4-4.4-12-4.4-19.6v-2c0-7.6,1.5-14.1,4.4-19.5c2.9-5.4,6.9-9.5,12-12.3c5.1-2.9,10.7-4.3,17-4.3
        c6.5,0,11.8,1.2,16.1,3.5c4.3,2.3,7.6,5.5,9.9,9.5c2.3,4,3.9,8.4,4.6,13.2l22-4.6c-1.3-7.6-4.2-14.6-8.7-20.9
        c-4.5-6.3-10.4-11.4-17.7-15.2c-7.3-3.8-16.2-5.7-26.6-5.7c-10.4,0-19.8,2.2-28.3,6.7c-8.4,4.5-15.1,10.9-20,19.1
        c-4.9,8.3-7.4,18.3-7.4,30v2.9c0,11.7,2.5,21.8,7.4,30.1c4.9,8.4,11.6,14.7,20,19.1c8.4,4.4,17.9,6.6,28.3,6.6
        c10.4,0,19.3-1.9,26.6-5.6c7.3-3.7,13.2-8.8,17.7-15.1c4.5-6.3,7.6-13.2,9.3-20.7l-22-5.1C856.6,411,853.4,417.3,848.4,422.2z
         M991.9,338.2h-30.1l-42.7,42.2h-3.5v-87.8h-22.7v154h22.7v-45.1h3.5l44.7,45.1h29.9l-56.5-55.9L991.9,338.2z M1081.5,397.9
        c-3.7-4.4-8.8-7.8-15.3-10.1c-6.5-2.3-13.6-4.2-21.3-5.5l-7.7-1.3c-5.9-1-10.5-2.6-14-4.6c-3.4-2.1-5.2-5.3-5.2-9.7
        c0-4.1,1.8-7.3,5.3-9.6c3.5-2.3,8.4-3.4,14.5-3.4c6.3,0,11.6,1.4,15.8,4.1c4.3,2.7,7,7.4,8.4,14l21.1-5.9
        c-2.3-9.4-7.4-16.8-15.3-22.3c-7.8-5.5-17.9-8.2-30-8.2c-12.6,0-22.7,2.8-30.4,8.5c-7.6,5.6-11.4,13.6-11.4,23.9)"
R"(
        c0,6.9,1.8,12.5,5.3,16.9c3.5,4.4,8.3,7.8,14.3,10.3c6,2.5,12.7,4.4,20,5.7l7.5,1.3c7.2,1.3,12.6,3,16.3,5.1
        c3.7,2.1,5.5,5.3,5.5,9.7c0,4.4-1.9,8-5.8,10.8c-3.9,2.8-9.4,4.2-16.6,4.2c-4.8,0-9.3-0.7-13.5-2.2c-4.2-1.5-7.7-4-10.5-7.5
        c-2.8-3.5-4.8-8.3-5.9-14.3l-21.1,5.1c2.1,12.5,7.6,21.8,16.7,27.9c9.1,6.2,20.5,9.2,34.3,9.2c13.6,0,24.5-3,32.6-9
        c8.1-6,12.1-14.4,12.1-25.3C1087.1,408.1,1085.3,402.3,1081.5,397.9z M112.4,151v37.9l82.2,39.7l151.2-73v-37.9l-151.2,73
        L112.4,151z M112.4,369.1V407l82.2,39.7l151.2-73v-37.9l-151.2,73L112.4,369.1z M112.4,226.7v37.9l151.2,73l82.2-39.7v-37.9
        l-82.2,39.7L112.4,226.7z"/>
  </g>
</svg>
)";


    static constexpr std::array files =
    {
        File { "cmaj-patch-connection.js", std::string_view (cmajpatchconnection_js, 9387) },
        File { "cmaj-parameter-controls.js", std::string_view (cmajparametercontrols_js, 21622) },
        File { "cmaj-midi-helpers.js", std::string_view (cmajmidihelpers_js, 12587) },
        File { "cmaj-event-listener-list.js", std::string_view (cmajeventlistenerlist_js, 2585) },
        File { "cmaj-server-session.js", std::string_view (cmajserversession_js, 18834) },
        File { "cmaj-generic-patch-view.js", std::string_view (cmajgenericpatchview_js, 4997) },
        File { "cmaj-patch-view.js", std::string_view (cmajpatchview_js, 2217) },
        File { "assets/cmajor-logo.svg", std::string_view (assets_cmajorlogo_svg, 2913) },
        File { "assets/sound-stacks-logo.svg", std::string_view (assets_soundstackslogo_svg, 6471) }
    };

};

} // namespace cmaj
