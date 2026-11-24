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

import { el, Disposer } from "../core/dom.js";
import { frameScheduler } from "../core/frame-scheduler.js";

/*
    The base class for endpoint widgets. A widget shows one of the patch's top-level endpoints,
    at one of two sizes:
      - "full": a card in the I/O view
      - "compact": a small control for the graph's live mode (and anywhere space is tight)

    Widgets never talk to the server directly: they subscribe through a SubscriptionGroup from
    the EndpointDataHub (options.group), and send through options.patchConnection.

    options: { patchConnection, session, group, bridge, size, scheduler, onShowInConsole }
*/
export class EndpointWidget
{
    constructor (endpoint, options, kind)
    {
        this.endpoint = endpoint;
        this.options = options;
        this.size = options.size ?? "full";
        this.isCompact = this.size === "compact";
        this.patchConnection = options.patchConnection;
        this.group = options.group;
        this.scheduler = options.scheduler ?? frameScheduler;
        this.disposer = new Disposer();

        this.nameElement = el ("span", { class: "cp-ew-name", title: getEndpointTooltip (endpoint) }, getEndpointDisplayName (endpoint));

        if (endpoint.source && options.bridge)
        {
            this.nameElement.classList.add ("cp-link");
            this.nameElement.tabIndex = 0;
            this.nameElement.setAttribute ("role", "link");
            const open = () => options.bridge.openSourceFile (endpoint.source);
            this.nameElement.addEventListener ("click", open);
            this.nameElement.addEventListener ("keydown", e => { if (e.key === "Enter") open(); });
        }

        // (the small square in the endpoint kind's colour matches the graph's ports)
        this.kindMark = el ("span", { class: "cp-kind-mark", "aria-hidden": "true" });
        this.valueElement = el ("span", { class: "cp-ew-value" });
        this.headerElement = el ("div", { class: "cp-ew-header" },
                                 el ("span", { class: "cp-ew-label" }, this.kindMark, this.nameElement),
                                 this.valueElement);
        this.bodyElement = el ("div", { class: "cp-ew-body" });

        this.element = el ("div", { class: ["cp-ew", `cp-ew-${this.size}`, `cp-ew-${kind}`, `cp-kind-${getEndpointKind (endpoint)}`],
                                    dataset: { endpoint: endpoint.endpointID } },
                           this.headerElement, this.bodyElement);

        this.element.widget = this;
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    /// Subscribes through the group, and unsubscribes when the widget is disposed
    subscribe (listener, subscriptionOptions)
    {
        this.disposer.add (this.group.subscribe (this.endpoint.endpointID, listener, subscriptionOptions));
    }

    subscribeParameter (listener)
    {
        this.disposer.add (this.group.subscribeParameter (this.endpoint.endpointID, listener));
    }

    setValueText (text)
    {
        if (this.valueElement.textContent !== text)
            this.valueElement.textContent = text;
    }

    /// Briefly highlights the widget, to show that something arrived
    flash()
    {
        this.element.classList.remove ("cp-flash");
        void this.element.offsetWidth;
        this.element.classList.add ("cp-flash");
    }
}

//==============================================================================
export function getEndpointDisplayName (endpoint)
{
    const name = endpoint?.annotation?.name;
    return (typeof name === "string" && name !== "") ? name : (endpoint?.endpointID ?? "");
}

/// "stream" | "value" | "event", for colour coding (as in the graph)
export function getEndpointKind (endpoint)
{
    const type = endpoint?.endpointType;
    return (type === "stream" || type === "value" || type === "event") ? type : "event";
}

export function getEndpointTypeDescription (endpoint)
{
    const t = endpoint?.dataType;
    let typeName = t?.type ?? "";

    if (t?.type === "object" && t.class)
        typeName = t.class;
    else if (t?.type === "vector" || t?.type === "array")
        typeName = `${t.element?.type ?? "?"}[${t.size ?? ""}]`;

    return `${endpoint?.endpointType ?? ""} ${typeName}`.trim();
}

function getEndpointTooltip (endpoint)
{
    const lines = [`${endpoint.endpointID}: ${getEndpointTypeDescription (endpoint)}`];

    if (endpoint.purpose)
        lines.push (endpoint.purpose);

    if (endpoint.source)
        lines.push ("Click to open the source");

    return lines.join ("\n");
}

//==============================================================================
const canvasObservers = new WeakMap();
let sharedResizeObserver = null;

/// Keeps a canvas's backing store matched to its CSS size × devicePixelRatio, calling
/// onResize() after each change. Returns a function that stops observing.
export function observeCanvasSize (canvas, onResize)
{
    if (! sharedResizeObserver)
    {
        sharedResizeObserver = new ResizeObserver (entries =>
        {
            for (const entry of entries)
            {
                const c = entry.target;
                const dpr = globalThis.devicePixelRatio || 1;
                const w = Math.max (1, Math.round (entry.contentRect.width * dpr));
                const h = Math.max (1, Math.round (entry.contentRect.height * dpr));

                if (c.width !== w || c.height !== h)
                {
                    c.width = w;
                    c.height = h;
                }

                canvasObservers.get (c)?.();
            }
        });
    }

    canvasObservers.set (canvas, onResize);
    sharedResizeObserver.observe (canvas);

    return () =>
    {
        sharedResizeObserver.unobserve (canvas);
        canvasObservers.delete (canvas);
    };
}

/// Converts a linear gain to decibels, with a floor
export function gainToDB (gain, floor = -100)
{
    return gain > 0 ? Math.max (floor, 20 * Math.log10 (gain)) : floor;
}

export function formatDB (db)
{
    return db <= -99 ? "-∞ dB" : `${db.toFixed (1)} dB`;
}
