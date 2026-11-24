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

import * as midi from "../../cmaj_api/cmaj-midi-helpers.js";
import { el } from "../core/dom.js";
import { EndpointWidget } from "./widget-base.js";

/*
    An event endpoint (an output, or an input that isn't a parameter or MIDI): shows the last
    value, flashes when one arrives, and (at full size) the last few values. "Console" opens the
    console filtered to this endpoint. The compact one shows the last value in a chip, which
    flashes.
*/
export class EventMonitorWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "event");

        this.count = 0;
        this.recent = [];

        if (this.isCompact)
        {
            this.chip = el ("span", { class: "cp-chip cp-ew-chip", "aria-live": "off" }, "–");
            this.bodyElement.append (this.chip);
        }
        else
        {
            this.setValueText ("–");
            this.listElement = el ("ol", { class: "cp-ew-recent", "aria-live": "off" });
            this.countElement = el ("span", { class: "cp-ew-count" }, "0 events");
            this.bodyElement.append (this.listElement,
                                     el ("div", { class: "cp-ew-footer" },
                                         this.countElement,
                                         options.onShowInConsole ? el ("button", { class: "cp-small-button", onclick: () => options.onShowInConsole (endpoint.endpointID) }, "Console")
                                                                 : null));
        }

        this.subscribe (value => this.handleValue (value));
    }

    handleValue (value)
    {
        const text = formatEventValue (value, this.endpoint);
        ++this.count;

        if (this.chip)
        {
            this.chip.textContent = text;
            this.chip.title = text;
        }
        else
        {
            this.setValueText (text);
        }

        this.flash();

        if (this.listElement)
        {
            this.recent.unshift (text);
            this.recent.length = Math.min (this.recent.length, 5);
            this.listElement.replaceChildren (...this.recent.map (t => el ("li", null, t)));
            this.countElement.textContent = `${this.count} event${this.count === 1 ? "" : "s"}`;
        }
    }
}

//==============================================================================
/// A value-type output can't be listened to, so this just says so
export class UnobservableWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "unobservable");
        this.setValueText ("");

        if (! this.isCompact)
            this.bodyElement.append (el ("p", { class: "cp-ew-note" }, "Value outputs can't be monitored"));
    }
}

//==============================================================================
/// A short description of an event value, for the widgets and the console
export function formatEventValue (value, endpoint)
{
    if (value && typeof value === "object" && typeof value.message === "number"
         && (endpoint?.purpose?.includes ("midi") || value._type === "Message" || Object.keys (value).length === 1))
        return midi.getMIDIDescription (value.message);

    if (typeof value === "number")
        return Number.isInteger (value) ? String (value) : Number (value.toPrecision (6)).toString();

    if (typeof value === "string")
        return value;

    try
    {
        if (value && typeof value === "object" && value._type)
        {
            const { _type, ...rest } = value;
            return `${_type} ${JSON.stringify (rest)}`;
        }

        return JSON.stringify (value);
    }
    catch (e)
    {
        return String (value);
    }
}
