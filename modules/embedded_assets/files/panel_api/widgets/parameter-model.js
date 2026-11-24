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

import { formatNumber } from "../core/dom.js";

/*
    Describes how to show and edit a parameter, from its endpoint annotation (min, max, step,
    init, unit, text, boolean, discrete), following the same rules as the generic patch view
    (cmaj_api/cmaj-parameter-controls.js):

      - text "a|b|c" → options, spread evenly over min..max (or 0..n-1)
      - discrete with a step → options, one per step
      - boolean → a toggle
      - otherwise a continuous slider

    A few short options (see isShortOptionList()) are shown as a row of buttons rather than a
    drop-down list: hasOptionButtons.
*/
export function describeParameter (endpoint)
{
    const a = endpoint?.annotation ?? {};
    const isNumber = (x) => typeof x === "number" && isFinite (x);

    const textOptions = typeof a.text === "string" ? a.text.split ("|") : [];
    let kind = "slider", options = null;
    let min = isNumber (a.min) ? a.min : 0;
    let max = isNumber (a.max) ? a.max : 1;

    if (textOptions.length > 1)
    {
        kind = "options";

        if (! (isNumber (a.min) && isNumber (a.max)))
        {
            min = 0;
            max = textOptions.length - 1;
        }

        const step = (max - min) / (textOptions.length - 1);
        options = textOptions.map ((text, i) => ({ value: min + step * i, text }));
    }
    else if (a.discrete && isNumber (a.step) && a.step > 0)
    {
        const count = Math.min (1000, Math.floor ((max - min) / a.step) + 1);

        if (count <= 32)
        {
            kind = "options";
            options = Array.from ({ length: count }, (_, i) => ({ value: min + a.step * i, text: formatValueWithUnit (min + a.step * i, a.unit) }));
        }
    }
    else if (a.boolean || endpoint?.dataType?.type === "bool")
    {
        kind = "toggle";
        min = 0;
        max = 1;
    }

    if (max <= min)
        max = min + 1;

    const step = isNumber (a.step) && a.step > 0 ? a.step : (endpoint?.dataType?.type?.startsWith ("int") ? 1 : 0);
    const init = isNumber (a.init) ? a.init : (isNumber (endpoint?.defaultValue) ? endpoint.defaultValue : min);

    const toIndex = (value) => options ? Math.max (0, Math.min (options.length - 1, Math.round ((value - min) / (max - min) * (options.length - 1)))) : 0;

    const isOn = (value) => value >= 0.5 * (min + max);

    return {
        kind, min, max, step, init, options,
        hasOptionButtons: kind === "options" && isShortOptionList (options),
        unit: a.unit ?? "",
        isHidden: !! a.hidden,

        toNormalised:   (value) => Math.max (0, Math.min (1, (value - min) / (max - min))),
        fromNormalised: (n) => snap (min + Math.max (0, Math.min (1, n)) * (max - min), min, max, step),
        snap:           (value) => snap (value, min, max, step),
        toIndex,

        /// For a toggle: whether a value counts as on (at or above the middle of its range)
        isOn,

        format (value)
        {
            if (typeof value !== "number")
                return value === undefined ? "–" : String (value);

            if (kind === "options")
                return options[toIndex (value)]?.text ?? "";

            if (kind === "toggle")
                return isOn (value) ? "On" : "Off";

            return formatValueWithUnit (value, a.unit);
        }
    };
}

/// The most options, and the most characters in all of their texts together, that are shown as
/// a row of buttons: as many as fit across a compact (graph live mode) widget
export const maxOptionButtons = 4;
export const maxOptionButtonsTextLength = 14;

/// True if a parameter's options are few and short enough (e.g. "Off|On", "Sine|Saw|Square")
/// to show as a row of buttons, one per option
export function isShortOptionList (options)
{
    return Array.isArray (options) && options.length > 1 && options.length <= maxOptionButtons
             && options.reduce ((total, o) => total + String (o.text).trim().length, 0) <= maxOptionButtonsTextLength;
}

/// Rounds to a multiple of the step (of the step itself, not relative to min) and then clamps
/// to the range, as the server does (PatchParameterProperties::snapAndConstrainValue() in
/// cmaj_PatchHelpers.h), so that what's shown is what the patch gets
function snap (value, min, max, step)
{
    if (step > 0)
        value = Math.round (value / step) * step;

    return Math.max (min, Math.min (max, value));
}

//==============================================================================
/// Sends a parameter value as one complete gesture (e.g. a click on a toggle, or a reset).
/// Inside a gesture that's already open (e.g. a drag), it just sends the value.
export function sendParameterChange (patchConnection, endpointID, value, isInsideGesture = false)
{
    if (! isInsideGesture)
        patchConnection.sendParameterGestureStart (endpointID);

    patchConnection.sendEventOrValue (endpointID, value);

    if (! isInsideGesture)
        patchConnection.sendParameterGestureEnd (endpointID);
}

/// Sets a parameter back to its default value
export function resetParameter (patchConnection, endpoint)
{
    sendParameterChange (patchConnection, endpoint.endpointID, describeParameter (endpoint).init);
}

export function formatValueWithUnit (value, unit)
{
    const text = formatNumber (value, 2);
    return unit ? `${text} ${unit}` : text;
}
