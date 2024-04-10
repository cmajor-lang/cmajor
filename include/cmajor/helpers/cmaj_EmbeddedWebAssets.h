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

    struct File { std::string_view name, content; };

    static constexpr const char* cmajpatchconnection_js =
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { EventListenerList } from "./cmaj-event-listener-list.js"
import * as midi from "./cmaj-midi-helpers.js"
import PianoKeyboard from "./cmaj-piano-keyboard.js"
import GenericPatchView from "./cmaj-generic-patch-view.js"
import * as ParameterControls from "./cmaj-parameter-controls.js"

//==============================================================================
/** This class implements the API and much of the logic for communicating with
 *  an instance of a patch that is running.
 */
export class PatchConnection  extends EventListenerList
{
    constructor()
    {
        super();
    }

    /** Returns the current Cmajor version */
    getCmajorVersion()
    {
        const version = import ("./cmaj-version.js");
        return version.getCmajorVersion();
    })"
R"(

    //==============================================================================
    // Status-handling methods:

    /** Calling this will trigger an asynchronous callback to any status listeners with the
     *  patch's current state. Use addStatusListener() to attach a listener to receive it.
     */
    requestStatusUpdate()                             { this.sendMessageToServer ({ type: "req_status" }); }

    /** Attaches a listener function that will be called whenever the patch's status changes.
     *  The function will be called with a parameter object containing many properties describing the status,
     *  including whether the patch is loaded, any errors, endpoint descriptions, its manifest, etc.
     */
    addStatusListener (listener)                      { this.addEventListener    ("status", listener); }

    /** Removes a listener that was previously added with addStatusListener()
     */
    removeStatusListener (listener)                   { this.removeEventListener ("status", listener); }

    /** Causes the patch to be reset to its "just loaded" state. */
    resetToInitialState()                             { this.sendMessageToServer ({ type: "req_reset" }); }

    //==============================================================================
    // Methods for sending data to input endpoints:)"
R"(

    /** Sends a value to one of the patch's input endpoints.
     *
     *  This can be used to send a value to either an 'event' or 'value' type input endpoint.
     *  If the endpoint is a 'value' type, then the rampFrames parameter can optionally be used to specify
     *  the number of frames over which the current value should ramp to the new target one.
     *  The value parameter will be coerced to the type that is expected by the endpoint. So for
     *  examples, numbers will be converted to float or integer types, javascript objects and arrays
     *  will be converted into more complex types in as good a fashion is possible.
     */
    sendEventOrValue (endpointID, value, rampFrames, timeoutMillisecs)  { this.sendMessageToServer ({ type: "send_value", id: endpointID, value, rampFrames, timeout: timeoutMillisecs }); }

    /** Sends a short MIDI message value to a MIDI endpoint.
     *  The value must be a number encoded with `(byte0 << 16) | (byte1 << 8) | byte2`.
     */
    sendMIDIInputEvent (endpointID, shortMIDICode)    { this.sendEventOrValue (endpointID, { message: shortMIDICode }); }

    /** Tells the patch that a series of changes that constitute a gesture is about to take place
     *  for the given endpoint. Remember to call sendParameterGestureEnd() after they're done!
     */
    sendParameterGestureStart (endpointID)            { this.sendMessageToServer ({ type: "send_gesture_start", id: endpointID }); }

    /** Tells the patch that a gesture started by sendParameterGestureStart() has finished.
     */
    sendParameterGestureEnd (endpointID)              { this.sendMessageToServer ({ type: "send_gesture_end", id: endpointID }); }

    //==============================================================================
    // Stored state control methods:)"
R"(

    /** Requests a callback to any stored-state value listeners with the current value of a given key-value pair.
     *  To attach a listener to receive these events, use addStoredStateValueListener().
     *  @param {string} key
     */
    requestStoredStateValue (key)                     { this.sendMessageToServer ({ type: "req_state_value", key: key }); }

    /** Modifies a key-value pair in the patch's stored state.
     *  @param {string} key
     *  @param {Object} newValue
     */
    sendStoredStateValue (key, newValue)              { this.sendMessageToServer ({ type: "send_state_value", key: key, value: newValue }); }

    /** Attaches a listener function that will be called when any key-value pair in the stored state is changed.
     *  The listener function will receive a message parameter with properties 'key' and 'value'.
     */
    addStoredStateValueListener (listener)            { this.addEventListener    ("state_key_value", listener); }

    /** Removes a listener that was previously added with addStoredStateValueListener().
     */
    removeStoredStateValueListener (listener)         { this.removeEventListener ("state_key_value", listener); }

    /** Applies a complete stored state to the patch.
     *  To get the current complete state, use requestFullStoredState().
     */
    sendFullStoredState (fullState)                   { this.sendMessageToServer ({ type: "send_full_state", value: fullState }); }

    /** Asynchronously requests the full stored state of the patch.
     *  The listener function that is supplied will be called asynchronously with the state as its argument.
     */
    requestFullStoredState (callback)
    {
        const replyType = "fullstate_response_" + (Math.floor (Math.random() * 100000000)).toString();
        this.addSingleUseListener (replyType, callback);
        this.sendMessageToServer ({ type: "req_full_state", replyType: replyType });
    })"
R"(

    //==============================================================================
    // Listener methods:

    /** Attaches a listener function that will receive updates with the events or audio data
     *  that is being sent or received by an endpoint.
     *
     *  If the endpoint is an event or value, the callback will be given an argument which is
     *  the new value.
     *
     *  If the endpoint has the right shape to be treated as "audio" then the callback will receive
     *  a stream of updates of the min/max range of chunks of data that is flowing through it.
     *  There will be one callback per chunk of data, and the size of chunks is specified by
     *  the optional granularity parameter.
     *
     *  @param {string} endpointID
     *  @param {number} granularity - if defined, this specifies the number of frames per callback
     *  @param {boolean} sendFullAudioData - if false, the listener will receive an argument object containing
     *     two properties 'min' and 'max', which are each an array of values, one element per audio
     *     channel. This allows you to find the highest and lowest samples in that chunk for each channel.
     *     If sendFullAudioData is true, the listener's argument will have a property 'data' which is an
     *     array containing one array per channel of raw audio samples data.
     */
    addEndpointListener (endpointID, listener, granularity, sendFullAudioData)
    {
        listener.eventID = "event_" + endpointID + "_" + (Math.floor (Math.random() * 100000000)).toString();
        this.addEventListener (listener.eventID, listener);
        this.sendMessageToServer ({ type: "add_endpoint_listener", endpoint: endpointID, replyType:
                                    listener.eventID, granularity: granularity, fullAudioData: sendFullAudioData });
    })"
R"(

    /** Removes a listener that was previously added with addEndpointListener()
     *  @param {string} endpointID
    */
    removeEndpointListener (endpointID, listener)
    {
        this.removeEventListener (listener.eventID, listener);
        this.sendMessageToServer ({ type: "remove_endpoint_listener", endpoint: endpointID, replyType: listener.eventID });
    }

    /** This will trigger an asynchronous callback to any parameter listeners that are
     *  attached, providing them with its up-to-date current value for the given endpoint.
     *  Use addAllParameterListener() to attach a listener to receive the result.
     *  @param {string} endpointID
     */
    requestParameterValue (endpointID)                  { this.sendMessageToServer ({ type: "req_param_value", id: endpointID }); }

    /** Attaches a listener function which will be called whenever the value of a specific parameter changes.
     *  The listener function will be called with an argument which is the new value.
     *  @param {string} endpointID
     */
    addParameterListener (endpointID, listener)         { this.addEventListener ("param_value_" + endpointID.toString(), listener); }

    /** Removes a listener that was previously added with addParameterListener()
     *  @param {string} endpointID
    */
    removeParameterListener (endpointID, listener)      { this.removeEventListener ("param_value_" + endpointID.toString(), listener); }

    /** Attaches a listener function which will be called whenever the value of any parameter changes in the patch.
     *  The listener function will be called with an argument object with the fields 'endpointID' and 'value'.
     */
    addAllParameterListener (listener)                  { this.addEventListener ("param_value", listener); }

    /** Removes a listener that was previously added with addAllParameterListener()
     */
    removeAllParameterListener (listener)               { this.removeEventListener ("param_value", listener); })"
R"(

    /** This takes a relative path to an asset within the patch bundle, and converts it to a
     *  path relative to the root of the browser that is showing the view.
     *
     *  You need you use this in your view code to translate your asset URLs to a form that
     *  can be safely used in your view's HTML DOM (e.g. in its CSS). This is needed because the
     *  host's HTTP server (which is delivering your view pages) may have a different '/' root
     *  than the root of your patch (e.g. if a single server is serving multiple patch GUIs).
     *
     *  @param {string} path
     */
    getResourceAddress (path)                           { return path; }

    //==============================================================================
    /**
     *  This property contains various utility classes and functions from the Cmajor API,
     *  for use in your GUI or worker code.
     */
    utilities = {
        /** MIDI utility functions from cmaj-midi-helpers.js */
        midi,
        /** On-screen keyboard class from cmaj-piano-keyboard.js */
        PianoKeyboard,
        /** Basic parameter control GUI elements, from cmaj-parameter-controls.js */
        ParameterControls,
        /** The default view GUI, from cmaj-generic-patch-view.js */
        GenericPatchView
    };

    //==============================================================================
    // Private methods follow this point..

    /** @private */
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
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { PatchConnection } from "./cmaj-patch-connection.js";


//==============================================================================
/** A base class for parameter controls, which automatically connects to a
 *  PatchConnection to monitor a parameter and provides methods to modify it.
 */
export class ParameterControlBase  extends HTMLElement
{
    constructor()
    {
        super();

        // prevent any clicks from focusing on this element
        this.onmousedown = e => e.stopPropagation();
    })"
R"(

    /** Attaches the control to a given PatchConnection and endpoint.
     *
     * @param {PatchConnection} patchConnection - the connection to connect to, or pass
     *                                            undefined to disconnect the control.
     * @param {Object} endpointInfo - the endpoint details, as provided by a PatchConnection
     *                                in its status callback.
     */
    setEndpoint (patchConnection, endpointInfo)
    {
        this.detachListener();

        this.patchConnection = patchConnection;
        this.endpointInfo = endpointInfo;
        this.defaultValue = endpointInfo.annotation?.init || endpointInfo.defaultValue || 0;

        if (this.isConnected)
            this.attachListener();
    }

    /** Override this method in a child class, and it will be called when the parameter value changes,
     *  so you can update the GUI appropriately.
     */
    valueChanged (newValue) {}

    /** Your GUI can call this when it wants to change the parameter value. */
    setValue (value)     { this.patchConnection?.sendEventOrValue (this.endpointInfo.endpointID, value); }

    /** Call this before your GUI begins a modification gesture.
     *  You might for example call this if the user begins a mouse-drag operation.
     */
    beginGesture()       { this.patchConnection?.sendParameterGestureStart (this.endpointInfo.endpointID); }

    /** Call this after your GUI finishes a modification gesture */
    endGesture()         { this.patchConnection?.sendParameterGestureEnd (this.endpointInfo.endpointID); }

    /** This calls setValue(), but sandwiches it between some start/end gesture calls.
     *  You should use this to make sure a DAW correctly records automatiion for individual value changes
     *  that are not part of a gesture.
     */
    setValueAsGesture (value)
    {
        this.beginGesture();
        this.setValue (value);
        this.endGesture();
    })"
R"(

    /** Resets the parameter to its default value */
    resetToDefault()
    {
        if (this.defaultValue !== null)
            this.setValueAsGesture (this.defaultValue);
    }

    //==============================================================================
    /** @private */
    connectedCallback()
    {
        this.attachListener();
    }

    /** @protected */
    disconnectedCallback()
    {
        this.detachListener();
    }

    /** @private */
    detachListener()
    {
        if (this.listener)
        {
            this.patchConnection?.removeParameterListener?.(this.listener.endpointID, this.listener);
            this.listener = undefined;
        }
    }

    /** @private */
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
/** A simple rotary parameter knob control. */
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
        const min = endpointInfo?.annotation?.min || 0;
        const max = endpointInfo?.annotation?.max || 1;

        const createSvgElement = tag => window.document.createElementNS ("http://www.w3.org/2000/svg", tag);

        const svg = createSvgElement ("svg");
        svg.setAttribute ("viewBox", "0 0 100 100");)"
R"(

        const trackBackground = createSvgElement ("path");
        trackBackground.setAttribute ("d", "M20,76 A 40 40 0 1 1 80 76");
        trackBackground.classList.add ("knob-path");
        trackBackground.classList.add ("knob-track-background");

        const maxKnobRotation = 132;
        const isBipolar = min + max === 0;
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

        const remap = (source, sourceFrom, sourceTo, targetFrom, targetTo) =>
                        (targetFrom + (source - sourceFrom) * (targetTo - targetFrom) / (sourceTo - sourceFrom));

        const toValue = (knobRotation) => remap (knobRotation, -maxKnobRotation, maxKnobRotation, min, max);
        this.toRotation = (value) => remap (value, min, max, -maxKnobRotation, maxKnobRotation);

        this.rotation = this.toRotation (this.defaultValue);
        this.setRotation (this.rotation, true);

        const onMouseMove = (event) =>
        {
            event.preventDefault(); // avoid scrolling whilst dragging)"
R"(

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
            this.accumulatedRotation = nextRotation (this.accumulatedRotation, movementY * speedMultiplier);
            this.setValue (toValue (this.accumulatedRotation));
        };

        const onMouseUp = (event) =>
        {
            this.previousScreenY = undefined;
            this.accumulatedRotation = undefined;
            window.removeEventListener ("mousemove", onMouseMove);
            window.removeEventListener ("mouseup", onMouseUp);
            this.endGesture();
        };

        const onMouseDown = (event) =>
        {
            this.previousScreenY = event.screenY;
            this.accumulatedRotation = this.rotation;
            this.beginGesture();
            window.addEventListener ("mousemove", onMouseMove);
            window.addEventListener ("mouseup", onMouseUp);
            event.preventDefault();
        };

        const onTouchStart = (event) =>
        {
            this.previousClientY = event.changedTouches[0].clientY;
            this.accumulatedRotation = this.rotation;
            this.touchIdentifier = event.changedTouches[0].identifier;
            this.beginGesture();
            window.addEventListener ("touchmove", onTouchMove);
            window.addEventListener ("touchend", onTouchEnd);
            event.preventDefault();
        };)"
R"(

        const onTouchMove = (event) =>
        {
            for (const touch of event.changedTouches)
            {
                if (touch.identifier == this.touchIdentifier)
                {
                    const nextRotation = (rotation, delta) =>
                    {
                        const clamp = (v, min, max) => Math.min (Math.max (v, min), max);
                        return clamp (rotation - delta, -maxKnobRotation, maxKnobRotation);
                    };

                    const movementY = touch.clientY - this.previousClientY;
                    this.previousClientY = touch.clientY;

                    const speedMultiplier = event.shiftKey ? 0.25 : 1.5;
                    this.accumulatedRotation = nextRotation (this.accumulatedRotation, movementY * speedMultiplier);
                    this.setValue (toValue (this.accumulatedRotation));
                }
            }
        };

        const onTouchEnd = (event) =>
        {
            this.previousClientY = undefined;
            this.accumulatedRotation = undefined;
            window.removeEventListener ("touchmove", onTouchMove);
            window.removeEventListener ("touchend", onTouchEnd);
            this.endGesture();
        };

        this.addEventListener ("mousedown", onMouseDown);
        this.addEventListener ("dblclick", () => this.resetToDefault());
        this.addEventListener ('touchstart', onTouchStart);
    }

    /** Returns true if this type of control is suitable for the given endpoint info */
    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter";
    }

    /** @override */
    valueChanged (newValue)       { this.setRotation (this.toRotation (newValue), false); }

    /** Returns a string version of the given value */
    getDisplayValue (v)           { return toFloatDisplayValueWithUnit (v, this.endpointInfo); })"
R"(

    /** @private */
    setRotation (degrees, force)
    {
        if (force || this.rotation !== degrees)
        {
            this.rotation = degrees;
            this.trackValue.setAttribute ("stroke-dashoffset", this.getDashOffset (this.rotation));
            this.dial.style.transform = `translate(-50%,-50%) rotate(${degrees}deg)`;
        }
    }

    /** @private */
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
            display: inline-block;

            height: 1rem;
            width: 0.15rem;
            background-color: var(--knob-dial-tick-color);
        }`;
    }
})"
R"(

//==============================================================================
/** A boolean switch control */
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

    /** Returns true if this type of control is suitable for the given endpoint info */
    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter"
                && endpointInfo.annotation?.boolean;
    }

    /** @override */
    valueChanged (newValue)
    {
        const b = newValue > 0.5;
        this.currentValue = b;
        this.classList.remove (! b ? "switch-on" : "switch-off");
        this.classList.add (b ? "switch-on" : "switch-off");
    }

    /** Returns a string version of the given value */
    getDisplayValue (v)   { return `${v > 0.5 ? "On" : "Off"}`; }

    /** @private */
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
function toFloatDisplayValueWithUnit (v, endpointInfo)
{
    return `${v.toFixed (2)} ${endpointInfo.annotation?.unit ?? ""}`;
}

//==============================================================================
/** A control that allows an item to be selected from a drop-down list of options */
export class Options  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    })"
R"(

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        const toValue = (min, step, index) => min + (step * index);
        const toStepCount = count => count > 0 ? count - 1 : 1;

        const { min, max, options } = (() =>
        {
            if (Options.hasTextOptions (endpointInfo))
            {
                const optionList = endpointInfo.annotation.text.split ("|");
                const stepCount = toStepCount (optionList.length);
                let min = 0, max = stepCount, step = 1;

                if (endpointInfo.annotation.min != null && endpointInfo.annotation.max != null)
                {
                    min = endpointInfo.annotation.min;
                    max = endpointInfo.annotation.max;
                    step = (max - min) / stepCount;
                }

                const options = optionList.map ((text, index) => ({ value: toValue (min, step, index), text }));

                return { min, max, options };
            }

            if (Options.isExplicitlyDiscrete (endpointInfo))
            {
                const step = endpointInfo.annotation.step;

                const min = endpointInfo.annotation?.min || 0;
                const max = endpointInfo.annotation?.max || 1;

                const numDiscreteOptions = (((max - min) / step) | 0) + 1;

                const options = new Array (numDiscreteOptions);
                for (let i = 0; i < numDiscreteOptions; ++i)
                {
                    const value = toValue (min, step, i);
                    options[i] = { value, text: toFloatDisplayValueWithUnit (value, endpointInfo) };
                }

                return { min, max, options };
            }
        })();

        this.options = options;

        const stepCount = toStepCount (this.options.length);
        const normalise = value => (value - min) / (max - min);
        this.toIndex = value => Math.min (stepCount, normalise (value) * this.options.length) | 0;)"
R"(

        this.innerHTML = "";

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

    /** Returns true if this type of control is suitable for the given endpoint info */
    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter"
                && (this.hasTextOptions (endpointInfo) || this.isExplicitlyDiscrete (endpointInfo));
    }

    /** @override */
    valueChanged (newValue)
    {
        const index = this.toIndex (newValue);
        this.selectedIndex = index;
        this.select.selectedIndex = index;
    }

    /** Returns a string version of the given value */
    getDisplayValue (v)    { return this.options[this.toIndex(v)].text; }

    /** @private */
    static hasTextOptions (endpointInfo)
    {
        return endpointInfo.annotation?.text?.split?.("|").length > 1
    }

    /** @private */
    static isExplicitlyDiscrete (endpointInfo)
    {
        return endpointInfo.annotation?.discrete && endpointInfo.annotation?.step > 0;
    })"
R"(

    /** @private */
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
/** A control which wraps a child control, adding a label and value display box below it */
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

    /** @override */
    valueChanged (newValue)
    {
        this.valueText.innerText = this.childControl?.getDisplayValue (newValue);
    }

    /** @private */
    static getCSS()
    {
        return `
        .labelled-control {
            --labelled-control-font-color: var(--foreground);
            --labelled-control-font-size: 0.8rem;)"
R"(

            position: relative;
            display: inline-block;
            margin: 0 0.4rem 0.4rem;
            vertical-align: top;
            text-align: left;
            padding: 0;
        }

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
window.customElements.define ("cmaj-labelled-control-holder", LabelledControlHolder);)"
R"(

//==============================================================================
/** Fetches all the CSS for the controls defined in this module */
export function getAllCSS()
{
    return `
        ${Options.getCSS()}
        ${Knob.getCSS()}
        ${Switch.getCSS()}
        ${LabelledControlHolder.getCSS()}`;
}

//==============================================================================
/** Creates a suitable control for the given endpoint.
 *
 *  @param {PatchConnection} patchConnection - the connection to connect to
 *  @param {Object} endpointInfo - the endpoint details, as provided by a PatchConnection
 *                                 in its status callback.
*/
export function createControl (patchConnection, endpointInfo)
{
    if (Switch.canBeUsedFor (endpointInfo))
        return new Switch (patchConnection, endpointInfo);

    if (Options.canBeUsedFor (endpointInfo))
        return new Options (patchConnection, endpointInfo);

    if (Knob.canBeUsedFor (endpointInfo))
        return new Knob (patchConnection, endpointInfo);

    return undefined;
}

//==============================================================================
/** Creates a suitable labelled control for the given endpoint.
 *
 *  @param {PatchConnection} patchConnection - the connection to connect to
 *  @param {Object} endpointInfo - the endpoint details, as provided by a PatchConnection
 *                                 in its status callback.
*/
export function createLabelledControl (patchConnection, endpointInfo)
{
    const control = createControl (patchConnection, endpointInfo);

    if (control)
        return new LabelledControlHolder (patchConnection, endpointInfo, control);

    return undefined;
})"
R"(

//==============================================================================
/** Takes a patch connection and its current status object, and tries to create
 *  a control for the given endpoint ID.
 *
 *  @param {PatchConnection} patchConnection - the connection to connect to
 *  @param {Object} status - the connection's current status
 *  @param {string} endpointID - the endpoint you'd like to control
 */
export function createLabelledControlForEndpointID (patchConnection, status, endpointID)
{
    for (const endpointInfo of status?.details?.inputs)
        if (endpointInfo.endpointID == endpointID)
            return createLabelledControl (patchConnection, endpointInfo);

    return undefined;
}
)";
    static constexpr const char* cmajmidihelpers_js =
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


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
                           | (2 << 8) | (2 << 10) | (3 << 12);)"
R"(

    const lastGroupLengths = (1 <<  0) | (2 <<  2) | (3 <<  4) | (2 <<  6)
                           | (1 <<  8) | (1 << 10) | (1 << 12) | (1 << 14)
                           | (1 << 16) | (1 << 18) | (1 << 20) | (1 << 22)
                           | (1 << 24) | (1 << 26) | (1 << 28) | (1 << 30);

    const firstByte = getByte0 (message);
    const group = (firstByte >> 4) & 7;

    return (group != 7 ? (mainGroupLengths >> (2 * group))
                       : (lastGroupLengths >> (2 * (firstByte & 15)))) & 3;
}

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
export function getControllerNumber (message)               { return getByte1 (message); })"
R"(
export function getControllerValue (message)                { return getByte2 (message); }
export function isControllerNumber (message, number)        { return getByte1 (message) == number && isController (message); }
export function isAllNotesOff (message)                     { return isControllerNumber (message, 123); }
export function isAllSoundOff (message)                     { return isControllerNumber (message, 120); }
export function isQuarterFrame (message)                    { return getByte0 (message) == 0xf1; }
export function isClock (message)                           { return getByte0 (message) == 0xf8; }
export function isStart (message)                           { return getByte0 (message) == 0xfa; }
export function isContinue (message)                        { return getByte0 (message) == 0xfb; }
export function isStop (message)                            { return getByte0 (message) == 0xfc; }
export function isActiveSense (message)                     { return getByte0 (message) == 0xfe; }
export function isMetaEvent (message)                       { return getByte0 (message) == 0xff; }
export function isSongPositionPointer (message)             { return getByte0 (message) == 0xf2; }
export function getSongPositionPointerValue (message)       { return get14BitValue (message); })"
R"(

export function getChromaticScaleIndex (note)               { return (note % 12) & 0xf; }
export function getOctaveNumber (note, octaveForMiddleC)    { return ((Math.floor (note / 12) + (octaveForMiddleC ? octaveForMiddleC : 3)) & 0xff) - 5; }
export function getNoteName (note)                          { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithSharps (note)                { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithFlats (note)                 { const names = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithOctaveNumber (note)          { return getNoteName (note) + getOctaveNumber (note); }
export function isNatural (note)                            { const nats = [true, false, true, false, true, true, false, true, false, true, false, true]; return nats[getChromaticScaleIndex (note)]; }
export function isAccidental (note)                         { return ! isNatural (note); }

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
    function getNote (m)   { const s = getNoteNameWithOctaveNumber (getNoteNumber (message)); return s.length < 4 ? s + " " : s; };)"
R"(

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
    if (isContinue (message))              return "Continue";
    if (isStop (message))                  return "Stop";
    if (isMetaEvent (message))             return "Meta-event: type " + getByte1 (message);
    if (isSongPositionPointer (message))   return "Song Position: " + getSongPositionPointerValue (message);

    return printHexMIDIData (message);
})"
R"TEXT(

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
            "Effect Control 1 (fine)",      "Effect Control 2 (fine)",        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,)TEXT"
R"TEXT(
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
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,)TEXT"
R"(
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
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


/** This event listener management class allows listeners to be attached and
 *  removed from named event types.
 */
export class EventListenerList
{
    constructor()
    {
        this.listenersPerType = {};
    }

    /** Adds a listener for a specifc event type.
     *  If the listener is already registered, this will simply add it again.
     *  Each call to addEventListener() must be paired with a removeventListener()
     *  call to remove it.
     *
     *  @param {string} type
     */
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
    })"
R"(

    /** Removes a listener that was previously added for the given event type.
     *  @param {string} type
     */
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

    /** Attaches a callback function that will be automatically unregistered
     *  the first time it is invoked.
     *
     *  @param {string} type
     */
    addSingleUseListener (type, listener)
    {
        const l = message =>
        {
            this.removeEventListener (type, l);
            listener?.(message);
        };

        this.addEventListener (type, l);
    }

    /** Synchronously dispatches an event object to all listeners
     *  that are registered for the given type.
     *
     *  @param {string} type
     */
    dispatchEvent (type, event)
    {
        const list = this.listenersPerType[type];

        if (list)
            for (const listener of list)
                listener?.(event);
    }

    /** Returns the number of listeners that are currently registered
     *  for the given type of event.
     *
     *  @param {string} type
     */
    getNumListenersForType (type)
    {
        const list = this.listenersPerType[type];
        return list ? list.length : 0;
    }
}
)";
    static constexpr const char* cmajserversession_js =
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { PatchConnection } from "./cmaj-patch-connection.js"
import { EventListenerList } from "./cmaj-event-listener-list.js"
)"
R"(

//==============================================================================
/*
 *  This class provides the API and manages the communication protocol between
 *  a javascript application and a Cmajor session running on some kind of server
 *  (which may be local or remote).
 *
 *  This is an abstract base class: some kind of transport layer will create a
 *  subclass of ServerSession which a client application can then use to control
 *  and interact with the server.
 */
export class ServerSession   extends EventListenerList
{
    /** A server session must be given a unique sessionID.
     * @param {string} sessionID - this must be a unique string which is safe for
     *                             use as an identifier or filename
    */
    constructor (sessionID)
    {
        super();

        this.sessionID = sessionID;
        this.activePatchConnections = new Set();
        this.status = { connected: false, loaded: false };
        this.lastServerMessageTime = Date.now();
        this.checkForServerTimer = setInterval (() => this.checkServerStillExists(), 2000);
    }

    /** Call `dispose()` when this session is no longer needed and should be released. */
    dispose()
    {
        if (this.checkForServerTimer)
        {
            clearInterval (this.checkForServerTimer);
            this.checkForServerTimer = undefined;
        }

        this.status = { connected: false, loaded: false };
    }

    //==============================================================================
    // Session status methods:

    /** Attaches a listener function which will be called when the session status changes.
     *  The listener will be called with an argument object containing lots of properties
     *  describing the state, including any errors, loaded patch manifest, etc.
     */
    addStatusListener (listener)                        { this.addEventListener    ("session_status", listener); })"
R"(

    /** Removes a listener that was previously added by `addStatusListener()`
     */
    removeStatusListener (listener)                     { this.removeEventListener ("session_status", listener); }

    /** Asks the server to asynchronously send a status update message with the latest status.
     */
    requestSessionStatus()                              { this.sendMessageToServer ({ type: "req_session_status" }); }

    /** Returns the session's last known status object. */
    getCurrentStatus()                                  { return this.status; }

    //==============================================================================
    // Patch loading:

    /** Asks the server to load the specified patch into our session.
     */
    loadPatch (patchFileToLoad)
    {
        this.currentPatchLocation = patchFileToLoad;
        this.sendMessageToServer ({ type: "load_patch", file: patchFileToLoad });
    }

    /** Tells the server to asynchronously generate a list of patches that it has access to.
     *  The function provided will be called back with an array of manifest objects describing
     *  each of the patches.
     */
    requestAvailablePatchList (callbackFunction)
    {
        const replyType = this.createReplyID ("patchlist_");
        this.addSingleUseListener (replyType, callbackFunction);
        this.sendMessageToServer ({ type: "req_patchlist",
                                    replyType: replyType });
    }

    /** Creates and returns a new PatchConnection object which can be used to control the
     *  patch that this session has loaded.
     */
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
            })"
R"(

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
            }
        }

        return new ServerPatchConnection (this);
    }

    //==============================================================================
    // Audio input source handling:

    /**
     *  Sets a custom audio input source for a particular endpoint.
     *
     *  When a source is changed, a callback is sent to any audio input mode listeners (see
     *  `addAudioInputModeListener()`)
     *
     *  @param {Object} endpointID
     *  @param {boolean} shouldMute - if true, the endpoint will be muted
     *  @param {Uint8Array | Array} fileDataToPlay - if this is some kind of array containing
     *  binary data that can be parsed as an audio file, then it will be sent across for the
     *  server to play as a looped input sample.
     */
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
            this.removeFile (loopFile);)"
R"(

            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        mute: !! shouldMute });
        }
    }

    /** Attaches a listener function to be told when the input source for a particular
     *  endpoint is changed by a call to `setAudioInputSource()`.
     */
    addAudioInputModeListener (endpointID, listener)    { this.addEventListener    ("audio_input_mode_" + endpointID, listener); }

    /** Removes a listener previously added with `addAudioInputModeListener()` */
    removeAudioInputModeListener (endpointID, listener) { this.removeEventListener ("audio_input_mode_" + endpointID, listener); }

    /** Asks the server to send an update with the latest status to any audio mode listeners that
     *  are attached to the given endpoint.
     *  @param {string} endpointID
     */
    requestAudioInputMode (endpointID)                  { this.sendMessageToServer ({ type: "req_audio_input_mode", endpoint: endpointID }); }

    //==============================================================================
    // Audio device methods:

    /** Enables or disables audio playback.
     *  When playback state changes, a status update is sent to any status listeners.
     * @param {boolean} shouldBeActive
     */
    setAudioPlaybackActive (shouldBeActive)             { this.sendMessageToServer ({ type: "set_audio_playback_active", active: shouldBeActive }); }

    /** Asks the server to apply a new set of audio device properties.
     *  The properties object uses the same format as the object that is passed to the listeners
     *  (see `addAudioDevicePropertiesListener()`).
     */
    setAudioDeviceProperties (newProperties)            { this.sendMessageToServer ({ type: "set_audio_device_props", properties: newProperties }); })"
R"(

    /** Attaches a listener function which will be called when the audio device properties are
     *  changed.
     *
     *  You can remove the listener when it's no longer needed with `removeAudioDevicePropertiesListener()`.
     *
     *  @param listener - this callback will receive an argument object containing all the
     *                    details about the device.
     */
    addAudioDevicePropertiesListener (listener)         { this.addEventListener    ("audio_device_properties", listener); }

    /** Removes a listener that was added with `addAudioDevicePropertiesListener()` */
    removeAudioDevicePropertiesListener (listener)      { this.removeEventListener ("audio_device_properties", listener); }

    /** Causes an asynchronous callback to any audio device listeners that are registered. */
    requestAudioDeviceProperties()                      { this.sendMessageToServer ({ type: "req_audio_device_props" }); }

    //==============================================================================
    /** Asks the server to asynchronously generate some code from the currently loaded patch.
     *
     *  @param {string} codeType - this must be one of the strings that are listed in the
     *                             status's `codeGenTargets` property. For example, "cpp"
     *                             would request a C++ version of the patch.
     *  @param {Object} [extraOptions] - this optionally provides target-specific properties.
     *  @param callbackFunction - this function will be called with the result when it has
     *                            been generated. Its argument will be an object containing the
     *                            code, errors and other metadata about the patch.
     */
    requestGeneratedCode (codeType, extraOptions, callbackFunction)
    {
        const replyType = this.createReplyID ("codegen_");
        this.addSingleUseListener (replyType, callbackFunction);
        this.sendMessageToServer ({ type: "req_codegen",)"
R"(
                                    codeType: codeType,
                                    options: extraOptions,
                                    replyType: replyType });
    }

    //==============================================================================
    // File change monitoring:

    /** Attaches a listener to be told when a file change is detected in the currently-loaded
     *  patch. The function will be called with an object that gives rough details about the
     *  type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
     *  won't provide any information about exactly which files are involved.
     */
    addFileChangeListener (listener)                    { this.addEventListener    ("patch_source_changed", listener); }

    /** Removes a listener that was previously added with `addFileChangeListener()`.
     */
    removeFileChangeListener (listener)                 { this.removeEventListener ("patch_source_changed", listener); }

    //==============================================================================
    // CPU level monitoring methods:

    /** Attaches a listener function which will be sent messages containing CPU info.
     *  To remove the listener, call `removeCPUListener()`. To change the rate of these
     *  messages, use `setCPULevelUpdateRate()`.
     */
    addCPUListener (listener)                       { this.addEventListener    ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /** Removes a listener that was previously attached with `addCPUListener()`. */
    removeCPUListener (listener)                    { this.removeEventListener ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /** Changes the frequency at which CPU level update messages are sent to listeners. */
    setCPULevelUpdateRate (framesPerUpdate)         { this.cpuFramesPerUpdate = framesPerUpdate; this.updateCPULevelUpdateRate(); })"
R"(

    /** Attaches a listener to be told when a file change is detected in the currently-loaded
     *  patch. The function will be called with an object that gives rough details about the
     *  type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
     *  won't provide any information about exactly which files are involved.
     */
    addInfiniteLoopListener (listener)              { this.addEventListener    ("infinite_loop_detected", listener); }

    /** Removes a listener that was previously added with `addFileChangeListener()`. */
    removeInfiniteLoopListener (listener)           { this.removeEventListener ("infinite_loop_detected", listener); }

    //==============================================================================
    /** Registers a virtual file with the server, under the given name.
     *
     *  @param {string} filename - the full path name of the file
     *  @param {Object} contentProvider - this object must have a property called `size` which is a
     *            constant size in bytes for the file, and a method `read (offset, size)` which
     *            returns an array (or UInt8Array) of bytes for the data in a given chunk of the file.
     *            The server may repeatedly call this method at any time until `removeFile()` is
     *            called to deregister the file.
     */
    registerFile (filename, contentProvider)
    {
        if (! this.files)
            this.files = new Map();

        this.files.set (filename, contentProvider);

        this.sendMessageToServer ({ type: "register_file",
                                    filename: filename,
                                    size: contentProvider.size });
    }

    /** Removes a file that was previously registered with `registerFile()`. */
    removeFile (filename)
    {
        this.sendMessageToServer ({ type: "remove_file",
                                    filename: filename });
        this.files?.delete (filename);
    })"
R"(

    //==============================================================================
    // Private methods from this point...

    /** An implementation subclass must call this when the session first connects
     *  @private
     */
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

    /** An implementation subclass must call this when a message arrives
     *  @private
     */
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
            case "infinite_loop_detected":
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
                this.sendMessageToServer ({ type: "ping" });
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
    })"
R"(

    /** @private */
    checkServerStillExists()
    {
        if (Date.now() > this.lastServerMessageTime + 10000)
            this.setNewStatus ({
                connected: false,
                loaded: false,
                status: "Cannot connect to the Cmajor server"
            });
    }

    /** @private */
    setNewStatus (newStatus)
    {
        this.status = newStatus;
        this.dispatchEvent ("session_status", this.status);
        this.updateCPULevelUpdateRate();
    }

    /** @private */
    updateCPULevelUpdateRate()
    {
        const rate = this.getNumListenersForType ("cpu_info") > 0 ? (this.cpuFramesPerUpdate || 15000) : 0;
        this.sendMessageToServer ({ type: "set_cpu_info_rate",
                                    framesPerCallback: rate });
    }

    /** @private */
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

    /** @private */
    createReplyID (stem)
    {
        return "reply_" + stem + this.createRandomID();
    }

    /** @private */
    createRandomID()
    {
        return (Math.floor (Math.random() * 100000000)).toString();
    }
}
)";
    static constexpr const char* cmajpianokeyboard_js =
        R"(//
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
    constructor ({ naturalNoteWidth,
                   accidentalWidth,
                   accidentalPercentageHeight,
                   naturalNoteBorder,
                   accidentalNoteBorder,
                   pressedNoteColour } = {})
    {
        super();)"
R"(

        this.naturalWidth = naturalNoteWidth || 20;
        this.accidentalWidth = accidentalWidth || 12;
        this.accidentalPercentageHeight = accidentalPercentageHeight || 66;
        this.naturalBorder = naturalNoteBorder || "2px solid #333";
        this.accidentalBorder = accidentalNoteBorder || "2px solid #333";
        this.pressedColour = pressedNoteColour || "#8ad";

        this.root = this.attachShadow({ mode: "open" });

        this.root.addEventListener ("mousedown",   (event) => this.handleMouse (event, true, false) );
        this.root.addEventListener ("mouseup",     (event) => this.handleMouse (event, false, true) );
        this.root.addEventListener ("mousemove",   (event) => this.handleMouse (event, false, false) );
        this.root.addEventListener ("mouseenter",  (event) => this.handleMouse (event, false, false) );
        this.root.addEventListener ("mouseout",    (event) => this.handleMouse (event, false, false) );

        this.addEventListener ("keydown",  (event) => this.handleKey (event, true));
        this.addEventListener ("keyup",    (event) => this.handleKey (event, false));
        this.addEventListener ("focusout", (event) => this.allNotesOff());

        this.currentDraggedNote = -1;
        this.currentExternalNotesOn = new Set();
        this.currentKeyboardNotes = new Set();
        this.currentPlayedNotes = new Set();
        this.currentDisplayedNotes = new Set();
        this.notes = [];
        this.modifierKeys = 0;
        this.currentTouches = new Map();

        this.refreshHTML();

        for (let child of this.root.children)
        {
            child.addEventListener ("touchstart", (event) => this.touchStart (event) );
            child.addEventListener ("touchend",   (event) => this.touchEnd (event) );
        }
    }

    static get observedAttributes()
    {
        return ["root-note", "note-count", "key-map"];
    })"
R"(

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
            noteDown: e => patchConnection.sendMIDIInputEvent (midiInputEndpointID, 0x900000 | (e.detail.note << 8) | velocity),
            noteUp:   e => patchConnection.sendMIDIInputEvent (midiInputEndpointID, 0x800000 | (e.detail.note << 8) | velocity),
            midiIn:   e => this.handleExternalMIDI (e.message),
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
        const callbacks = this.callbacks.get (patchConnection);)"
R"(

        if (callbacks)
        {
            this.removeEventListener ("note-down", callbacks.noteDown);
            this.removeEventListener ("note-up",   callbacks.noteUp);
            patchConnection.removeEndpointListener (callbacks.midiInputEndpointID, callbacks.midiIn);
        }

        this.callbacks[patchConnection] = undefined;
    }

    //==============================================================================
    /** Can be overridden to return the color to use for a note index */
    getNoteColour (note)    { return undefined; }

    /** Can be overridden to return the text label to draw on a note index */
    getNoteLabel (note)     { return midi.getChromaticScaleIndex (note) == 0 ? midi.getNoteNameWithOctaveNumber (note) : ""; }

    /** Clients should call this to deliver a MIDI message, which the keyboard will use to
     *  highlight the notes that are currently playing.
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
    */
    sendNoteOn (note)   { this.dispatchEvent (new CustomEvent('note-down', { detail: { note: note }})); }

    /** This method will be called when the user releases a note. The default behaviour is
     *  to dispath an event, but you could override this if you needed to.
    */
    sendNoteOff (note)  { this.dispatchEvent (new CustomEvent('note-up',   { detail: { note: note } })); })"
R"(

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

    setDraggedNote (newNote)
    {
        if (newNote != this.currentDraggedNote)
        {
            if (this.currentDraggedNote >= 0)
                this.sendNoteOff (this.currentDraggedNote);

            this.currentDraggedNote = newNote;

            if (this.currentDraggedNote >= 0)
                this.sendNoteOn (this.currentDraggedNote);

            this.refreshActiveNoteElements();
        }
    }

    addKeyboardNote (note)
    {
        if (! this.currentKeyboardNotes.has (note))
        {
            this.sendNoteOn (note);
            this.currentKeyboardNotes.add (note);
            this.refreshActiveNoteElements();
        }
    }

    removeKeyboardNote (note)
    {
        if (this.currentKeyboardNotes.has (note))
        {
            this.sendNoteOff (note);
            this.currentKeyboardNotes.delete (note);
            this.refreshActiveNoteElements();
        }
    }

    isNoteActive (note)
    {
        return note == this.currentDraggedNote
            || this.currentExternalNotesOn.has (note)
            || this.currentKeyboardNotes.has (note);
    }

    //==============================================================================
    /** @private */
    touchEnd (event)
    {
        for (const touch of event.changedTouches)
        {
            const note = this.currentTouches.get (touch.identifier);
            this.currentTouches.delete (touch.identifier);
            this.removeKeyboardNote (note);
        }

        event.preventDefault();
    })"
R"(

    /** @private */
    touchStart (event)
    {
        for (const touch of event.changedTouches)
        {
            const note = touch.target.id.substring (4);
            this.currentTouches.set (touch.identifier, note);
            this.addKeyboardNote (note);
        }

        event.preventDefault();
    }

    /** @private */
    handleMouse (event, isDown, isUp)
    {
        if (isDown)
            this.isDragging = true;

        if (this.isDragging)
        {
            let newActiveNote = -1;

            if (event.buttons != 0 && event.type != "mouseout")
            {
                const note = event.target.id.substring (4);

                if (note !== undefined)
                    newActiveNote = parseInt (note);
            }

            this.setDraggedNote (newActiveNote);

            if (! isDown)
                event.preventDefault();
        }

        if (isUp)
            this.isDragging = false;
    }

    /** @private */
    handleKey (event, isDown)
    {
        if (event.key == "Meta" || event.key == "Alt" || event.key == "Control" || event.key == "Shift")
        {
            this.modifierKeys += isDown ? 1 : -1;
            return;
        }

        if (this.modifierKeys != 0)
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
            const elem = this.shadowRoot.getElementById (`note${i.toString()}`);
            this.notes.push ({ note: i, element: elem });
        })"
R"(

        this.style.maxWidth = window.getComputedStyle (this).scrollWidth;
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

    /** @private */
    getAccidentalOffset (note)
    {
        let index = midi.getChromaticScaleIndex (note);

        let negativeOffset = -this.accidentalWidth / 16;
        let positiveOffset = 3 * this.accidentalWidth / 16;

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

            if (midi.isNatural (note + 1) || i == config.numNotes - 1)
                x += this.naturalWidth;
        }

        this.style.maxWidth = (x + 1) + "px";

        return `<div tabindex="0" class="note-holder" style="width: ${x + 1}px;">
                ${naturals}
                ${accidentals}
                </div>`;
    })"
R"(

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
)";
    static constexpr const char* cmajgenericpatchview_js =
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import * as Controls from "./cmaj-parameter-controls.js"

//==============================================================================
/** A simple, generic view which can control any type of patch */
class GenericPatchView extends HTMLElement
{
    /** Creates a view for a patch.
     *  @param {PatchConnection} patchConnection - the connection to the target patch
     */
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
        this.shadowRoot.innerHTML = this.getHTML();)"
R"(

        this.titleElement      = this.shadowRoot.getElementById ("patch-title");
        this.parametersElement = this.shadowRoot.getElementById ("patch-parameters");
    }

    /** This is picked up by some of our wrapper code to know whether it makes
     *  sense to put a title bar/logo above the GUI.
     */
    hasOwnTitleBar()
    {
        return true;
    }

    //==============================================================================
    /** @private */
    connectedCallback()
    {
        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
    }

    /** @private */
    disconnectedCallback()
    {
        this.patchConnection.removeStatusListener (this.statusListener);
    }

    /** @private */
    createControlElements()
    {
        this.parametersElement.innerHTML = "";
        this.titleElement.innerText = this.status?.manifest?.name ?? "Cmajor";

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

    /** @private */
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
                --background: #1a1a1a;)"
R"(

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
            }

            .logo {
                flex: 1;
                height: 80%;
                margin-left: 0.3rem;
                margin-right: 0.3rem;
                background-color: var(--foreground);
                mask: url(cmaj_api/assets/cmajor-logo.svg);
                mask-repeat: no-repeat;
                -webkit-mask: url(cmaj_api/assets/cmajor-logo.svg);
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

            ${Controls.getAllCSS()}

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

window.customElements.define ("cmaj-generic-patch-view", GenericPatchView);)"
R"(

//==============================================================================
/** Creates a generic view element which can be used to control any patch.
 *  @param {PatchConnection} patchConnection - the connection to the target patch
 */
export default function createPatchView (patchConnection)
{
    return new GenericPatchView (patchConnection);
}
)";
    static constexpr const char* cmajpatchview_js =
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { PatchConnection } from "./cmaj-patch-connection.js"

//==============================================================================
/** Returns a list of types of view that can be created for this patch.
 */
export function getAvailableViewTypes (patchConnection)
{
    if (! patchConnection)
        return [];

    if (patchConnection.manifest?.view?.src)
        return ["custom", "generic"];

    return ["generic"];
})"
R"(

//==============================================================================
/** Creates and returns a HTMLElement view which can be shown to control this patch.
 *
 *  If no preferredType argument is supplied, this will return either a custom patch-specific
 *  view (if the manifest specifies one), or a generic view if not. The preferredType argument
 *  can be used to choose one of the types of view returned by getAvailableViewTypes().
 *
 *  @param {PatchConnection} patchConnection - the connection to use
 *  @param {string} preferredType - the name of the type of view to open, e.g. "generic"
 *                                  or the name of one of the views in the manifest
 *  @returns {HTMLElement} a HTMLElement that can be displayed as the patch GUI
 */
export async function createPatchView (patchConnection, preferredType)
{
    if (patchConnection?.manifest)
    {
        let view = patchConnection.manifest.view;

        if (view && preferredType === "generic")
            if (view.src)
                view = undefined;

        const viewModuleURL = view?.src ? patchConnection.getResourceAddress (view.src) : "./cmaj-generic-patch-view.js";
        const viewModule = await import (viewModuleURL);
        const patchView = await viewModule?.default (patchConnection);

        if (patchView)
        {
            patchView.style.display = "block";

            if (view?.width > 10)
                patchView.style.width = view.width + "px";
            else
                patchView.style.width = undefined;

            if (view?.height > 10)
                patchView.style.height = view.height + "px";
            else
                patchView.style.height = undefined;

            return patchView;
        }
    }

    return undefined;
})"
R"(

//==============================================================================
/** If a patch view declares itself to be scalable, this will attempt to scale it to fit
 *  into a given parent element.
 *
 *  @param {HTMLElement} view - the patch view
 *  @param {HTMLElement} parentToScale - the patch view's direct parent element, to which
 *                                       the scale factor will be applied
 *  @param {HTMLElement} parentContainerToFitTo - an outer parent of the view, whose bounds
 *                                                the view will be made to fit
 */
export function scalePatchViewToFit (view, parentToScale, parentContainerToFitTo)
{
    function getClientSize (view)
    {
        const clientStyle = getComputedStyle (view);

        return {
            width:  view.clientHeight - parseFloat (clientStyle.paddingTop)  - parseFloat (clientStyle.paddingBottom),
            height: view.clientWidth  - parseFloat (clientStyle.paddingLeft) - parseFloat (clientStyle.paddingRight)
        };
    }

    const scaleLimits = view.getScaleFactorLimits?.();

    if (scaleLimits && (scaleLimits.minScale || scaleLimits.maxScale) && parentContainerToFitTo)
    {
        const minScale = scaleLimits.minScale || 0.25;
        const maxScale = scaleLimits.maxScale || 5.0;

        const targetSize = getClientSize (parentContainerToFitTo);
        const clientSize = getClientSize (view);

        const scaleW = targetSize.width / clientSize.width;
        const scaleH = targetSize.height / clientSize.height;

        const scale = Math.min (maxScale, Math.max (minScale, Math.min (scaleW, scaleH)));

        parentToScale.style.transform = `scale(${scale})`;
    }
    else
    {
        parentToScale.style.transform = "none";
    }
})"
R"(

//==============================================================================
class PatchViewHolder extends HTMLElement
{
    constructor (view)
    {
        super();
        this.view = view;
        this.style = `display: block; position: relative; width: 100%; height: 100%; overflow: visible; transform-origin: 0% 0%;`;
    }

    connectedCallback()
    {
        this.appendChild (this.view);
        this.resizeObserver = new ResizeObserver (() => scalePatchViewToFit (this.view, this, this.parentElement));
        this.resizeObserver.observe (this.parentElement);
        scalePatchViewToFit (this.view, this, this.parentElement);
    }

    disconnectedCallback()
    {
        this.resizeObserver = undefined;
        this.innerHTML = "";
    }
}

window.customElements.define ("cmaj-patch-view-holder", PatchViewHolder);

//==============================================================================
/** Creates and returns a HTMLElement view which can be shown to control this patch.
 *
 *  Unlike createPatchView(), this will return a holder element that handles scaling
 *  and resizing, and which follows changes to the size of the parent that you
 *  append it to.
 *
 *  If no preferredType argument is supplied, this will return either a custom patch-specific
 *  view (if the manifest specifies one), or a generic view if not. The preferredType argument
 *  can be used to choose one of the types of view returned by getAvailableViewTypes().
 *
 *  @param {PatchConnection} patchConnection - the connection to use
 *  @param {string} preferredType - the name of the type of view to open, e.g. "generic"
 *                                  or the name of one of the views in the manifest
 *  @returns {HTMLElement} a HTMLElement that can be displayed as the patch GUI
 */
export async function createPatchViewHolder (patchConnection, preferredType)
{
    const view = await createPatchView (patchConnection, preferredType);

    if (view)
        return new PatchViewHolder (view);
}
)";
    static constexpr const char* cmajaudioworklethelper_js =
        R"(//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { PatchConnection } from "./cmaj-patch-connection.js"

//==============================================================================
// N.B. code will be serialised to a string, so all `registerWorkletProcessor`s
// dependencies must be self contained and not capture things in the outer scope
async function serialiseWorkletProcessorFactoryToDataURI (CmajorClass, workletName, hostDescription)
{
    const serialisedInvocation = `(${registerWorkletProcessor.toString()}) ("${workletName}", ${CmajorClass.toString()}, "${hostDescription}");`

    let reader = new FileReader();
    reader.readAsDataURL (new Blob ([serialisedInvocation], { type: "text/javascript" }));

    return await new Promise (res => { reader.onloadend = () => res (reader.result); });
})"
R"(

function registerWorkletProcessor (workletName, CmajorClass, hostDescription)
{
    function makeConsumeOutputEvents ({ wrapper, eventOutputs, dispatchOutputEvent })
    {
        const outputEventHandlers = eventOutputs.map (({ endpointID }) =>
        {
            const readCount = wrapper[`getOutputEventCount_${endpointID}`]?.bind (wrapper);
            const reset = wrapper[`resetOutputEventCount_${endpointID}`]?.bind (wrapper);
            const readEventAtIndex = wrapper[`getOutputEvent_${endpointID}`]?.bind (wrapper);

            return () =>
            {
                const count = readCount();

                for (let i = 0; i < count; ++i)
                    dispatchOutputEvent (endpointID, readEventAtIndex (i));

                reset();
            };
        });

        return () => outputEventHandlers.forEach ((consume) => consume() );
    }

    function setInitialParameterValues (parametersMap)
    {
        for (const { initialise } of Object.values (parametersMap))
            initialise();
    }

    function makeEndpointMap (wrapper, endpoints, initialValueOverrides)
    {
        const toKey = ({ endpointType, endpointID }) =>
        {
            switch (endpointType)
            {
                case "event": return `sendInputEvent_${endpointID}`;
                case "value": return `setInputValue_${endpointID}`;
            }

            throw "Unhandled endpoint type";
        };

        const lookup = {};

        for (const { endpointID, endpointType, annotation, purpose } of endpoints)
        {
            const key = toKey ({ endpointType, endpointID });
            const wrapperUpdate = wrapper[key]?.bind (wrapper);

            const snapAndConstrainValue = (value) =>
            {
                if (annotation.step != null)
                    value = Math.round (value / annotation.step) * annotation.step;)"
R"(

                if (annotation.min != null && annotation.max != null)
                    value = Math.min (Math.max (value, annotation.min), annotation.max);

                return value;
            };

            const update = (value, rampFrames) =>
            {
                // N.B. value clamping and rampFrames from annotations not currently applied
                const entry = lookup[endpointID];
                entry.cachedValue = value;
                wrapperUpdate (value, rampFrames);
            };

            if (update)
            {
                const initialValue = initialValueOverrides[endpointID] ?? annotation?.init;

                lookup[endpointID] = {
                    snapAndConstrainValue,
                    update,
                    initialise: initialValue != null ? () => update (initialValue) : () => {},
                    purpose,
                    cachedValue: undefined,
                };
            }
        }

        return lookup;
    }

    function makeStreamEndpointHandler ({ wrapper, toEndpoints, wrapperMethodNamePrefix })
    {
        const endpoints = toEndpoints (wrapper);
        if (endpoints.length === 0)
            return () => {};

        let handlers = [];
        let targetChannels = [];
        let channelCount = 0;

        for (const endpoint of endpoints)
        {
            const handleFrames = wrapper[`${wrapperMethodNamePrefix}_${endpoint.endpointID}`]?.bind (wrapper);

            if (! handleFrames)
                return () => {};

            handlers.push (handleFrames);
            targetChannels.push (channelCount);
            channelCount += endpoint.numAudioChannels;
        }

        return (channels, blockSize) =>
        {
            for (let i = 0; i < handlers.length; i++)
                handlers[i] (channels, blockSize, targetChannels[i]);
        }
    })"
R"(

    function makeInputStreamEndpointHandler (wrapper)
    {
        return makeStreamEndpointHandler ({
            wrapper,
            toEndpoints: wrapper => wrapper.getInputEndpoints().filter (({ purpose }) => purpose === "audio in"),
            wrapperMethodNamePrefix: "setInputStreamFrames",
        });
    }

    function makeOutputStreamEndpointHandler (wrapper)
    {
        return makeStreamEndpointHandler ({
            wrapper,
            toEndpoints: wrapper => wrapper.getOutputEndpoints().filter (({ purpose }) => purpose === "audio out"),
            wrapperMethodNamePrefix: "getOutputFrames",
        });
    }

    class WorkletProcessor extends AudioWorkletProcessor
    {
        static get parameterDescriptors()
        {
            return [];
        }

        constructor ({ processorOptions, ...options })
        {
            super (options);

            this.processImpl = undefined;
            this.consumeOutputEvents = undefined;

            const { sessionID = Date.now() & 0x7fffffff, initialValueOverrides = {} } = processorOptions;

            const wrapper = new CmajorClass();

            wrapper.initialise (sessionID, sampleRate)
                .then (() => this.initialisePatch (wrapper, initialValueOverrides))
                .catch (error => { throw new Error (error)});
        }

        process (inputs, outputs)
        {
            const input = inputs[0];
            const output = outputs[0];

            this.processImpl?.(input, output);
            this.consumeOutputEvents?.();

            return true;
        }

        sendPatchMessage (payload)
        {
            this.port.postMessage ({ type: "patch", payload });
        }

        sendParameterValueChanged (endpointID, value)
        {
            this.sendPatchMessage ({
                type: "param_value",
                message: { endpointID, value }
            });
        })"
R"(

        initialisePatch (wrapper, initialValueOverrides)
        {
            try
            {
                const inputParameters = wrapper.getInputEndpoints().filter (({ purpose }) => purpose === "parameter");
                const parametersMap = makeEndpointMap (wrapper, inputParameters, initialValueOverrides);

                setInitialParameterValues (parametersMap);

                const toParameterValuesWithKey = (endpointKey, parametersMap) =>
                {
                    const toValue = ([endpoint, { cachedValue }]) => ({ [endpointKey]: endpoint, value: cachedValue });
                    return Object.entries (parametersMap).map (toValue);
                };

                const initialValues = toParameterValuesWithKey ("endpointID", parametersMap);
                const initialState = wrapper.getState();

                const resetState = () =>
                {
                    wrapper.restoreState (initialState);

                    // N.B. update cache used for `req_param_value` messages (we don't currently read from the wasm heap)
                    setInitialParameterValues (parametersMap);
                };

                const isNonAudioOrParameterEndpoint = ({ purpose }) => ! ["audio in", "parameter"].includes (purpose);
                const otherInputs = wrapper.getInputEndpoints().filter (isNonAudioOrParameterEndpoint);
                const otherInputEndpointsMap = makeEndpointMap (wrapper, otherInputs, initialValueOverrides);

                const isEvent = ({ endpointType }) => endpointType === "event";
                const eventInputs = wrapper.getInputEndpoints().filter (isEvent);
                const eventOutputs = wrapper.getOutputEndpoints().filter (isEvent);

                const makeEndpointListenerMap = (eventEndpoints) =>
                {
                    const listeners = {};

                    for (const { endpointID } of eventEndpoints)
                        listeners[endpointID] = [];)"
R"(

                    return listeners;
                };

                const inputEventListeners = makeEndpointListenerMap (eventInputs);
                const outputEventListeners = makeEndpointListenerMap (eventOutputs);

                this.consumeOutputEvents = makeConsumeOutputEvents ({
                    eventOutputs,
                    wrapper,
                    dispatchOutputEvent: (endpointID, event) =>
                    {
                        for (const { replyType } of outputEventListeners[endpointID] ?? [])
                        {
                            this.sendPatchMessage ({
                                type: replyType,
                                message: event.event, // N.B. chucking away frame and typeIndex info for now
                            });
                        }
                    },
                });

                const blockSize = 128;
                const prepareInputFrames = makeInputStreamEndpointHandler (wrapper);
                const processOutputFrames = makeOutputStreamEndpointHandler (wrapper);

                this.processImpl = (input, output) =>
                {
                    prepareInputFrames (input, blockSize);
                    wrapper.advance (blockSize);
                    processOutputFrames (output, blockSize);
                };

                // N.B. the message port makes things straightforward, but it allocates (when sending + receiving).
                // so, we aren't doing ourselves any favours. we probably ought to marshal raw bytes over to the gui in
                // a pre-allocated lock-free message queue (using `SharedArrayBuffer` + `Atomic`s) and transform the raw
                // messages there.
                this.port.addEventListener ("message", e =>
                {
                    if (e.data.type !== "patch")
                        return;

                    const msg = e.data.payload;)"
R"(

                    switch (msg.type)
                    {
                        case "req_status":
                        {
                            this.sendPatchMessage ({
                                type: "status",
                                message: {
                                    details: {
                                        inputs: wrapper.getInputEndpoints(),
                                        outputs: wrapper.getOutputEndpoints(),
                                    },
                                    sampleRate,
                                    host: hostDescription ? hostDescription : "WebAudio"
                                },
                            });
                            break;
                        }

                        case "req_reset":
                        {
                            resetState();
                            initialValues.forEach (v => this.sendParameterValueChanged (v.endpointID, v.value));
                            break;
                        }

                        case "req_param_value":
                        {
                            // N.B. keep a local cache here so that we can send the values back when requested.
                            // we could instead have accessors into the wasm heap.
                            const endpointID = msg.id;
                            const parameter = parametersMap[endpointID];
                            if (! parameter)
                                return;

                            const value = parameter.cachedValue;
                            this.sendParameterValueChanged (endpointID, value);
                            break;
                        }

                        case "send_value":
                        {
                            const endpointID = msg.id;
                            const parameter = parametersMap[endpointID];)"
R"(

                            if (parameter)
                            {
                                const newValue = parameter.snapAndConstrainValue (msg.value);
                                parameter.update (newValue, msg.rampFrames);

                                this.sendParameterValueChanged (endpointID, newValue);
                                return;
                            }

                            const inputEndpoint = otherInputEndpointsMap[endpointID];

                            if (inputEndpoint)
                            {
                                inputEndpoint.update (msg.value);

                                for (const { replyType } of inputEventListeners[endpointID] ?? [])
                                {
                                    this.sendPatchMessage ({
                                        type: replyType,
                                        message: inputEndpoint.cachedValue,
                                    });
                                }
                            }
                            break;
                        }

                        case "send_gesture_start": break;
                        case "send_gesture_end": break;

                        case "req_full_state":
                            this.sendPatchMessage ({
                                type: msg?.replyType,
                                message: {
                                    parameters: toParameterValuesWithKey ("name", parametersMap),
                                },
                            });
                            break;

                        case "send_full_state":
                        {
                            const { parameters = [] } = e.data.payload?.value || [];

                            for (const [endpointID, parameter] of Object.entries (parametersMap))
                            {
                                const namedNextValue = parameters.find (({ name }) => name === endpointID);)"
R"(

                                if (namedNextValue)
                                    parameter.update (namedNextValue.value);
                                else
                                    parameter.initialise();

                                this.sendParameterValueChanged (endpointID, parameter.cachedValue);
                            }
                            break;
                        }

                        case "add_endpoint_listener":
                        {
                            const insertIfValidEndpoint = (lookup, msg) =>
                            {
                                const endpointID = msg?.endpoint;
                                const listeners = lookup[endpointID]

                                if (! listeners)
                                    return false;

                                return listeners.push ({ replyType: msg?.replyType }) > 0;
                            };

                            if (! insertIfValidEndpoint (inputEventListeners, msg))
                                insertIfValidEndpoint (outputEventListeners, msg)

                            break;
                        }

                        case "remove_endpoint_listener":
                        {
                            const removeIfValidReplyType = (lookup, msg) =>
                            {
                                const endpointID = msg?.endpoint;
                                const listeners = lookup[endpointID];

                                if (! listeners)
                                    return false;

                                const index = listeners.indexOf (msg?.replyType);

                                if (index === -1)
                                    return false;

                                return listeners.splice (index, 1).length === 1;
                            };)"
R"(

                            if (! removeIfValidReplyType (inputEventListeners, msg))
                                removeIfValidReplyType (outputEventListeners, msg)

                            break;
                        }

                        default:
                            break;
                    }
                });

                this.port.postMessage ({ type: "initialised" });
                this.port.start();
            }
            catch (e)
            {
                this.port.postMessage (e.toString());
            }
        }
    }

    registerProcessor (workletName, WorkletProcessor);
}

//==============================================================================
async function connectToAudioIn (audioContext, node)
{
    try
    {
        const input = await navigator.mediaDevices.getUserMedia ({
            audio: {
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl:  false,
        }});

        if (! input)
            throw new Error();

        const source = audioContext.createMediaStreamSource (input);

        if (! source)
            throw new Error();

        source.connect (node);
    }
    catch (e)
    {
        console.warn (`Could not open audio input`);
    }
}

async function connectToMIDI (connection, midiEndpointID)
{
    try
    {
        if (! navigator.requestMIDIAccess)
            throw new Error ("Web MIDI API not supported.");

        const midiAccess = await navigator.requestMIDIAccess ({ sysex: true, software: true });

        for (const input of midiAccess.inputs.values())
        {
            input.onmidimessage = ({ data }) =>
                connection.sendMIDIInputEvent (midiEndpointID, data[2] | (data[1] << 8) | (data[0] << 16));
        }
    }
    catch (e)
    {
        console.warn (`Could not open MIDI devices: ${e}`);
    }
}
)"
R"(

//==============================================================================
/**  This class provides a PatchConnection that controls a Cmajor audio worklet
 *   node.
 */
export class AudioWorkletPatchConnection extends PatchConnection
{
    constructor (manifest)
    {
        super();

        this.manifest = manifest;
        this.cachedState = {};
    }

    //==============================================================================
    /**  Initialises this connection to load and control the given Cmajor class.
     *
     *   @param {Object} parameters - the parameters to use
     *   @param {Object} parameters.CmajorClass - the generated Cmajor class
     *   @param {AudioContext} parameters.audioContext - a web audio AudioContext object
     *   @param {string} parameters.workletName - the name to give the new worklet that is created
     *   @param {string} parameters.hostDescription - a description of the host that is using the patch
     *   @param {number} [parameters.sessionID] - an integer to use for the session ID, or undefined to use a default
     *   @param {Object} [parameters.initialValueOverrides] - optional initial values for parameter endpoints
     *   @param {string} [parameters.rootResourcePath] - optionally, a root to use when resolving resource paths
     */
    async initialise ({ CmajorClass,
                        audioContext,
                        workletName,
                        hostDescription,
                        sessionID,
                        initialValueOverrides,
                        rootResourcePath })
    {
        this.audioContext = audioContext;

        if (rootResourcePath)
        {
            this.rootResourcePath = rootResourcePath.toString();

            if (! this.rootResourcePath.endsWith ("/"))
                this.rootResourcePath += "/";
        }
        else
        {
            this.rootResourcePath = window.location.href;)"
R"(

            if (! this.rootResourcePath.endsWith ("/"))
                this.rootResourcePath += "/../";
        }

        const dataURI = await serialiseWorkletProcessorFactoryToDataURI (CmajorClass, workletName, hostDescription);
        await audioContext.audioWorklet.addModule (dataURI);

        this.inputEndpoints = CmajorClass.prototype.getInputEndpoints();
        this.outputEndpoints = CmajorClass.prototype.getOutputEndpoints();

        const audioInputEndpoints  = this.inputEndpoints.filter (({ purpose }) => purpose === "audio in");
        const audioOutputEndpoints = this.outputEndpoints.filter (({ purpose }) => purpose === "audio out");

        let inputChannelCount = 0;
        let outputChannelCount = 0;

        audioInputEndpoints.forEach  ((endpoint) => { inputChannelCount = inputChannelCount + endpoint.numAudioChannels; });
        audioOutputEndpoints.forEach ((endpoint) => { outputChannelCount = outputChannelCount + endpoint.numAudioChannels; });

        const hasInput = inputChannelCount > 0;
        const hasOutput = outputChannelCount > 0;

        const node = new AudioWorkletNode (audioContext, workletName, {
            numberOfInputs: +hasInput,
            numberOfOutputs: +hasOutput,
            channelCountMode: "explicit",
            channelCount: hasInput ? inputChannelCount : undefined,
            outputChannelCount: hasOutput ? [outputChannelCount] : [],

            processorOptions:
            {
                sessionID,
                initialValueOverrides
            }
        });

        const waitUntilWorkletInitialised = async () =>
        {
            return new Promise ((resolve) =>
            {
                const filterForInitialised = (e) =>
                {
                    if (e.data.type === "initialised")
                    {
                        node.port.removeEventListener ("message", filterForInitialised);
                        resolve();
                    }
                };)"
R"TEXT(

                node.port.addEventListener ("message", filterForInitialised);
            });
        };

        node.port.start();

        await waitUntilWorkletInitialised();

        this.audioNode = node;

        node.port.addEventListener ("message", e =>
        {
            if (e.data.type === "patch")
            {
                const msg = e.data.payload;

                if (msg?.type === "status")
                    msg.message = { manifest: this.manifest, ...msg.message };

                this.deliverMessageFromServer (msg)
            }
        });

        await this.startPatchWorker();
    }

    //==============================================================================
    /**  Attempts to connect this connection to the default audio and MIDI channels.
     *   This must only be called once initialise() has completed successfully.
     *
     *   @param {AudioContext} audioContext - a web audio AudioContext object
     */
    async connectDefaultAudioAndMIDI (audioContext)
    {
        if (! this.audioNode)
            throw new Error ("AudioWorkletPatchConnection.initialise() must have been successfully completed before calling connectDefaultAudioAndMIDI()");

        const getInputWithPurpose = (purpose) =>
        {
            for (const i of this.inputEndpoints)
                if (i.purpose === purpose)
                    return i.endpointID;
        }

        const midiEndpointID = getInputWithPurpose ("midi in");

        if (midiEndpointID)
            connectToMIDI (this, midiEndpointID);

        if (getInputWithPurpose ("audio in"))
            connectToAudioIn (audioContext, this.audioNode);

        this.audioNode.connect (audioContext.destination);
    }

    //==============================================================================
    sendMessageToServer (msg)
    {
        this.audioNode.port.postMessage ({ type: "patch", payload: msg });
    })TEXT"
R"(

    requestStoredStateValue (key)
    {
        this.dispatchEvent ("state_key_value", { key, value: this.cachedState[key] });
    }

    sendStoredStateValue (key, newValue)
    {
        const changed = this.cachedState[key] != newValue;

        if (changed)
        {
            const shouldRemove = newValue == null;
            if (shouldRemove)
            {
                delete this.cachedState[key];
                return;
            }

            this.cachedState[key] = newValue;
            // N.B. notifying the client only when updating matches behaviour of the patch player
            this.dispatchEvent ("state_key_value", { key, value: newValue });
        }
    }

    sendFullStoredState (fullState)
    {
        const currentStateCleared = (() =>
        {
            const out = {};
            Object.keys (this.cachedState).forEach (k => out[k] = undefined);
            return out;
        })();

        const incomingStateValues = fullState.values ?? {};
        const nextStateValues = { ...currentStateCleared, ...incomingStateValues };

        Object.entries (nextStateValues).forEach (([key, value]) => this.sendStoredStateValue (key, value));

        // N.B. worklet will handle the `parameters` part
        super.sendFullStoredState (fullState);
    }

    requestFullStoredState (callback)
    {
        // N.B. the worklet only handles the `parameters` part, so we patch the key-value state in here
        super.requestFullStoredState (msg => callback ({ values: { ...this.cachedState }, ...msg }));
    }

    getResourceAddress (path)
    {
        return this.rootResourcePath + path;
    }

    async readResource (path)
    {
        return fetch (path);
    }

    async readResourceAsAudioData (path)
    {
        const response = await this.readResource (path);
        const buffer = await this.audioContext.decodeAudioData (await response.arrayBuffer());

        let frames = [];

        for (let i = 0; i < buffer.length; ++i)
            frames.push ([]);)"
R"(

        for (let chan = 0; chan < buffer.numberOfChannels; ++chan)
        {
            const src = buffer.getChannelData (chan);

            for (let i = 0; i < buffer.length; ++i)
                frames[i].push (src[i]);
        }

        return { frames, sampleRate: buffer.sampleRate };
    }

    //==============================================================================
    /** @private */
    async startPatchWorker()
    {
        if (this.manifest.worker?.length > 0)
        {
            const module = await import (this.getResourceAddress (this.manifest.worker));
            module.default (this);
        }
    }
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


    static constexpr std::array files =
    {
        File { "cmaj-patch-connection.js", std::string_view (cmajpatchconnection_js, 12712) },
        File { "cmaj-parameter-controls.js", std::string_view (cmajparametercontrols_js, 29343) },
        File { "cmaj-midi-helpers.js", std::string_view (cmajmidihelpers_js, 13253) },
        File { "cmaj-event-listener-list.js", std::string_view (cmajeventlistenerlist_js, 3474) },
        File { "cmaj-server-session.js", std::string_view (cmajserversession_js, 18844) },
        File { "cmaj-piano-keyboard.js", std::string_view (cmajpianokeyboard_js, 15540) },
        File { "cmaj-generic-patch-view.js", std::string_view (cmajgenericpatchview_js, 6186) },
        File { "cmaj-patch-view.js", std::string_view (cmajpatchview_js, 7221) },
        File { "cmaj-audio-worklet-helper.js", std::string_view (cmajaudioworklethelper_js, 27974) },
        File { "assets/cmajor-logo.svg", std::string_view (assets_cmajorlogo_svg, 2913) }
    };

};

} // namespace cmaj
