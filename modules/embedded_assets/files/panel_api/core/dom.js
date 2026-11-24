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

/*
    Small DOM helpers for the patch panel. The rule is that data (endpoint names, file paths,
    error text, anything from the server or the patch) never reaches innerHTML: the panel builds
    its elements with el ("div", { class: "x", onclick: fn }, child, "text", ...), where strings
    passed as children become text nodes.
*/

//==============================================================================
/** Creates an element.
    props:
      - class: a string, or an array (falsy items are skipped)
      - style: a string, or an object of properties (custom properties with "--" work too)
      - dataset: an object
      - on<event>: a listener, e.g. onclick, onpointerdown
      - anything else is set as a property if the element has one (value, checked, disabled,
        title, ...), otherwise as an attribute; null, undefined and false are skipped
    children: nodes, strings (as text), numbers, arrays, or null/undefined/false (skipped)
*/
export function el (tag, props, ...children)
{
    const element = tag.includes (":") ? document.createElementNS ("http://www.w3.org/2000/svg", tag.split (":")[1])
                                       : document.createElement (tag);

    if (props)
        setProps (element, props);

    append (element, ...children);
    return element;
}

export function setProps (element, props)
{
    for (const [key, value] of Object.entries (props))
    {
        if (value === undefined || value === null)
            continue;

        if (key === "class")
        {
            element.setAttribute ("class", Array.isArray (value) ? value.filter (Boolean).join (" ") : value);
        }
        else if (key === "style")
        {
            if (typeof value === "string")
                element.setAttribute ("style", value);
            else
                for (const [name, v] of Object.entries (value))
                    if (v !== undefined && v !== null)
                        name.startsWith ("--") ? element.style.setProperty (name, v) : (element.style[name] = v);
        }
        else if (key === "dataset")
        {
            Object.assign (element.dataset, value);
        }
        else if (key.startsWith ("on") && typeof value === "function")
        {
            element.addEventListener (key.substring (2), value);
        }
        else if (key in element && ! key.includes ("-") && ! (element instanceof SVGElement))
        {
            element[key] = value;
        }
        else if (value !== false)
        {
            element.setAttribute (key, value === true ? "" : value);
        }
    }

    return element;
}

export function append (parent, ...children)
{
    for (const child of children)
    {
        if (child === null || child === undefined || child === false)
            continue;

        if (Array.isArray (child))
            append (parent, ...child);
        else if (child instanceof Node)
            parent.appendChild (child);
        else
            parent.appendChild (document.createTextNode (String (child)));
    }

    return parent;
}

/// Replaces all of an element's children
export function replaceChildren (parent, ...children)
{
    parent.replaceChildren();
    return append (parent, ...children);
}

/// Adds an event listener and returns a function that removes it
export function listen (target, type, listener, options)
{
    target.addEventListener (type, listener, options);
    return () => target.removeEventListener (type, listener, options);
}

/// Shows a toggle button as on or off (its look, and aria-pressed)
export function setPressed (button, isOn)
{
    button.classList.toggle ("cp-on", !! isOn);
    button.setAttribute ("aria-pressed", isOn ? "true" : "false");
}

/** Follows a drag that starts with the pointerdown event e: the target captures the pointer,
    onMove (event) is called as it moves, and onEnd (event, wasCancelled) once when it's
    released, cancelled, or loses the capture. Returns a function that ends it early (without
    calling onEnd). */
export function trackPointer (e, target, onMove, onEnd)
{
    try { target.setPointerCapture (e.pointerId); }
    catch (error) {}   // (e.g. a synthetic event with no active pointer)

    const remove = () =>
    {
        target.removeEventListener ("pointermove", move);
        target.removeEventListener ("pointerup", up);
        target.removeEventListener ("pointercancel", cancel);
        target.removeEventListener ("lostpointercapture", cancel);
    };

    const finish = (ev, wasCancelled) =>
    {
        remove();

        if (target.hasPointerCapture?.(e.pointerId))
            target.releasePointerCapture (e.pointerId);

        onEnd?.(ev, wasCancelled);
    };

    const move = (ev) => { if (ev.pointerId === e.pointerId) onMove?.(ev); };
    const up = (ev) => { if (ev.pointerId === e.pointerId) finish (ev, false); };
    const cancel = (ev) => { if (ev.pointerId === e.pointerId) finish (ev, true); };

    target.addEventListener ("pointermove", move);
    target.addEventListener ("pointerup", up);
    target.addEventListener ("pointercancel", cancel);
    target.addEventListener ("lostpointercapture", cancel);
    return remove;
}

/// Collects clean-up functions, and calls them all (once) when dispose() is called
export class Disposer
{
    constructor()       { this.items = []; }

    add (...fns)
    {
        for (const fn of fns)
            if (typeof fn === "function")
                this.items.push (fn);

        return fns[0];
    }

    listen (target, type, listener, options)
    {
        return this.add (listen (target, type, listener, options));
    }

    dispose()
    {
        const items = this.items.reverse();
        this.items = [];

        for (const fn of items)
        {
            try { fn(); }
            catch (e) { console.error (e); }
        }
    }
}

/// Formats a number with a sensible number of digits for display
export function formatNumber (value, maxDecimals = 3)
{
    if (typeof value !== "number" || ! isFinite (value))
        return String (value);

    const magnitude = Math.abs (value);
    const decimals = magnitude >= 1000 ? 0 : magnitude >= 100 ? 1 : magnitude >= 10 ? 2 : maxDecimals;
    return Number (value.toFixed (decimals)).toString();
}
