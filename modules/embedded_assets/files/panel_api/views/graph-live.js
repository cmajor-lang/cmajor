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

import { el } from "../core/dom.js";
import { ConnectionView } from "./connection-view.js";
import { createEndpointWidget, createWidgetOptions, hasLiveWidget, getEndpointDisplayName, getEndpointKind, getEndpointTypeDescription } from "../widgets/registry.js";
import { audioGranularity } from "../widgets/audio-meter.js";
import { describeParameter } from "../widgets/parameter-model.js";
import { gainToDB } from "../widgets/widget-base.js";
import { Popover } from "../shell/top-bar.js";
import { addSpectrumToggle, isSpectrumEndpoint } from "../widgets/spectrum-analyser.js";

/// The per-patch setting that remembers which endpoints' popovers show their spectrum
export const liveSpectrumSettingsKey = "liveSpectrumEndpoints";

/*
    The graph's live mode: the patch's own (top-level) endpoints become controls and meters
    next to their ports on the graph, so the graph is somewhere to play the patch from.

    - Each endpoint that has something to show or play gets a compact widget from the widget
      registry, in an overlay that <cmaj-patch-graph> draws inside the element carrying its
      pan/zoom transform (its slot="overlay"), so they move with the drawing. The graph leaves
      room for them (setBoundaryWidgets(): a gutter on each side of the root graph, with its
      ports spaced out to fit them), and says where each one goes (getBoundaryWidgetPlacements()).
    - Below liveWidgetMinZoom they'd be too small to use, so each one becomes an activity dot on
      its port instead.
    - Clicking one's name, or anywhere on it that isn't its control (or its dot), opens a popover
      with the full-size widget: an audio input's source, the scope, the keyboard, an event log.
      An audio endpoint's has a Spectrum toggle in its header, which shows the spectrum analyser
      under the meter (and widens the popover); that's remembered per patch and endpoint.
    - Hovering one highlights the signal path through its port, e.g. what a parameter drives.
    - The wires attached to the live ports show their activity (scaled by level), as does each
      dot. That comes from the endpoints themselves, so it's only measured at the boundary: it
      isn't spread further inwards, where it would be invented.

    It only uses the graph element's overlay API (see cmaj-graph.js), not its internals.

    It's a ConnectionView: its widgets are re-created when the connection or the endpoints
    change, and all of their data comes through its SubscriptionGroup, which pauses while the
    graph is hidden (setActive()) or live mode is off (setEnabled()). It asks for the same
    feeds as the I/O view's widgets (e.g. audio at audioGranularity), so the hub shares one
    server listener between them.
*/

/// Below this zoom the widgets would be too small to read, so each one becomes a dot on its port
export const liveWidgetMinZoom = 0.7;

/// The size of each kind of live widget, in layout units (which are CSS pixels at 100% zoom).
/// panel.css lays out the compact widgets to fit these heights.
export function getLiveWidgetSize (endpoint)
{
    const width = 160;
    const purpose = endpoint?.purpose ?? "";

    if (purpose === "audio in" || purpose === "audio out" || purpose === "midi in" || purpose === "midi out")
        return { width, height: 34 };

    if (purpose === "parameter")
    {
        const kind = describeParameter (endpoint).kind;
        return { width, height: kind === "options" ? 46 : (kind === "toggle" ? 42 : 40) };
    }

    return { width, height: 44 };    // an event: its last value in a chip
}

/// The parts of a widget that a click uses itself, rather than opening the popover
const controlSelector = "input, select, button, .cp-keyboard";

//==============================================================================
export class LiveGraphLayer  extends ConnectionView
{
    /// graph: the <cmaj-patch-graph> element
    constructor (ctx, graph)
    {
        super (ctx, { isEnabled: false });

        this.graph = graph;
        this.items = [];            // { endpoint, isInput, widget, element, dot, size, entry }
        this.showsDots = false;
        this.popoverItem = null;
        this.popoverWidget = null;
        this.widgetOptions = null;

        this.element = el ("div", { class: "cp-live-layer", slot: "overlay", hidden: true });
        graph.append (this.element);

        this.activity = new BoundaryActivity (ctx.scheduler, (levels) => this.showActivity (levels));

        this.popoverTitle = el ("h3", { class: "cp-section-title" });
        this.popoverBody = el ("div", { class: "cp-live-popover-body" });
        this.popover = new Popover (el ("div", null, this.popoverTitle, this.popoverBody), null, () => this.disposePopoverWidget());
        this.popover.element.classList.add ("cp-live-popover");
        this.popover.element.setAttribute ("aria-labelledby", "cp-live-popover-title");
        this.popoverTitle.id = "cp-live-popover-title";

        // (moving the popover with its widget is done once per frame, however often the view moves)
        this.positionPopover = () => { this.popover.position(); };

        this.disposer.add (() => { this.ctx.scheduler.cancel (this.positionPopover); this.popover.dispose(); this.element.remove(); });
        this.followConnection();
    }

    get hasWidgets()        { return this.items.length > 0; }

    //==============================================================================
    onAttach (connection, group)
    {
        this.element.hidden = false;
        this.widgetOptions = createWidgetOptions (this.ctx, connection, group, {
            onShowInConsole: (endpointID) => { this.popover.close(); this.ctx.actions.showConsole (endpointID); },
        });

        const s = this.ctx.store.state;
        const isShown = (endpoint, isInput) => hasLiveWidget (endpoint, isInput) && ! endpoint.annotation?.hidden;

        for (const endpoint of s.inputs ?? [])
            if (isShown (endpoint, true))
                this.addItem (endpoint, true);

        for (const endpoint of s.outputs ?? [])
            if (isShown (endpoint, false))
                this.addItem (endpoint, false);

        this.applyLayout();
        this.place();
    }

    onDetach()
    {
        this.popover.close();

        for (const item of this.items)
            item.widget.dispose();

        this.items = [];
        this.widgetOptions = null;
        this.activity.clear();
        this.element.replaceChildren();
    }

    /// (live mode is off, or there's no patch)
    onNoConnection()
    {
        this.element.hidden = ! this.isEnabled;
        this.applyLayout();
        this.showActivity (null);
    }

    onLiveChanged (isLive)
    {
        if (! isLive)
            this.popover.close();
    }

    addItem (endpoint, isInput)
    {
        const widget = createEndpointWidget (endpoint, { ...this.widgetOptions, bridge: null, size: "compact" }, isInput);

        if (! widget)
            return;

        const name = getEndpointDisplayName (endpoint);
        const direction = isInput ? "in" : "out";
        const kind = `cp-kind-${getEndpointKind (endpoint)}`;

        const item = {
            endpoint, isInput, widget,
            size: getLiveWidgetSize (endpoint),
            entry: null,
            element: el ("div", { class: ["cp-live-item", `cp-live-${direction}`], dataset: { endpoint: endpoint.endpointID } }, widget.element),
            dot: el ("button", { class: ["cp-live-dot", `cp-live-${direction}`, kind], dataset: { endpoint: endpoint.endpointID },
                                 title: name, "aria-label": `${name}: show the controls`, "aria-haspopup": "dialog" })
        };

        // the name opens the full widget, and so does a click anywhere that isn't the control
        const nameElement = widget.nameElement;
        nameElement.tabIndex = 0;
        nameElement.setAttribute ("role", "button");
        nameElement.setAttribute ("aria-haspopup", "dialog");
        nameElement.setAttribute ("aria-expanded", "false");
        nameElement.title = `${nameElement.title}\nClick for more`;

        // (A parameter's whole control area is left to it: a double-click there resets the value,
        // and mustn't also open and close the popover. The second click of any double-click is
        // ignored too.)
        const isControlArea = (target) => !! target.closest?.(controlSelector)
                                            || (endpoint.purpose === "parameter" && widget.bodyElement.contains (target));

        widget.disposer.listen (item.element, "click", (e) =>
        {
            if (e.detail <= 1 && ! isControlArea (e.target))
                this.togglePopover (item);
        });

        widget.disposer.listen (nameElement, "keydown", (e) =>
        {
            if (e.key === "Enter" || e.key === " ")
            {
                e.preventDefault();
                this.togglePopover (item);
            }
        });

        widget.disposer.listen (item.dot, "click", () => this.togglePopover (item));

        // show the signal path through the port, e.g. what a parameter drives
        const highlight = () => { if (item.entry) this.graph.highlightBoundaryEndpoint (endpoint.endpointID, direction); };
        const unhighlight = () => this.graph.clearHighlight();

        for (const target of [item.element, item.dot])
        {
            widget.disposer.listen (target, "pointerenter", highlight);
            widget.disposer.listen (target, "pointerleave", unhighlight);
            widget.disposer.listen (target, "focusin", highlight);
            widget.disposer.listen (target, "focusout", unhighlight);
        }

        this.subscribeActivity (item);
        this.items.push (item);
        this.element.append (item.element, item.dot);
    }

    /// Feeds the endpoint's activity (its level, or a pulse for each event or parameter change)
    /// to the wires and the dot
    subscribeActivity (item)
    {
        const id = item.endpoint.endpointID;
        const purpose = item.endpoint.purpose ?? "";

        if (purpose === "audio in" || purpose === "audio out")
        {
            item.widget.disposer.add (this.group.subscribe (id, (data) => this.activity.setLevel (id, getAudioActivity (data)),
                                                            { granularity: audioGranularity }));
        }
        else if (purpose === "parameter")
        {
            let lastValue;

            item.widget.disposer.add (this.group.subscribeParameter (id, (value) =>
            {
                if (lastValue !== undefined && value !== lastValue)
                    this.activity.setLevel (id, 1);

                lastValue = value;
            }));
        }
        else
        {
            item.widget.disposer.add (this.group.subscribe (id, () => this.activity.setLevel (id, 1)));
        }
    }

    /// The graph leaves room next to each port for its widget
    applyLayout()
    {
        const sizes = this.isEnabled && this.items.length > 0
                        ? Object.fromEntries (this.items.map (item => [item.endpoint.endpointID, item.size])) : null;

        this.graph.setBoundaryWidgets (sizes);
    }

    //==============================================================================
    /// Puts each widget where the graph made room for it (called after every layout)
    place()
    {
        const entries = new Map (this.graph.getBoundaryWidgetPlacements().map (w => [`${w.direction}:${w.name}`, w]));

        for (const item of this.items)
        {
            const entry = entries.get (`${item.isInput ? "in" : "out"}:${item.endpoint.endpointID}`) ?? null;
            item.entry = entry;
            item.element.hidden = ! entry;
            item.dot.hidden = ! entry;

            if (entry)
            {
                Object.assign (item.element.style, { left: entry.x + "px", top: entry.y + "px", width: entry.width + "px", height: entry.height + "px" });

                // (a zero-sized anchor at the outer edge of the port: the CSS draws the dot beside it)
                const dotX = item.isInput ? entry.portX : entry.portX + entry.portWidth;
                Object.assign (item.dot.style, { left: dotX + "px", top: (entry.portY + entry.portHeight / 2) + "px" });
            }
        }

        this.schedulePopoverPosition();
    }

    /// Switches between widgets and dots, and keeps the dots the same size on screen
    updateZoom (zoom)
    {
        this.element.style.setProperty ("--cp-live-k", String (zoom));

        const showDots = zoom < liveWidgetMinZoom;

        if (showDots !== this.showsDots)
        {
            this.showsDots = showDots;
            this.element.classList.toggle ("cp-live-dots", showDots);

            // (what it was attached to has just disappeared)
            this.popover.close();
        }

        this.schedulePopoverPosition();
    }

    schedulePopoverPosition()
    {
        if (this.popover.isOpen)
            this.ctx.scheduler.schedule (this.positionPopover);
    }

    /// levels: a Map of endpoint name → level (or null for none)
    showActivity (levels)
    {
        this.graph.setBoundaryActivity (levels ?? emptyLevels);

        for (const item of this.items)
        {
            const level = levels?.get (item.endpoint.endpointID) ?? 0;
            const text = level > 0 ? level.toFixed (2) : "0";

            if (item.dot.style.getPropertyValue ("--activity") !== text)
                item.dot.style.setProperty ("--activity", text);
        }
    }

    //==============================================================================
    /// Opens (or closes) an item's popover. It belongs to the item's name (which gets
    /// aria-expanded, and the focus back on Escape), but hangs off the whole widget; in dot
    /// mode, where the widget isn't drawn, it's the dot's.
    togglePopover (item)
    {
        if (this.popover.isOpen && this.popoverItem === item)
        {
            this.popover.close();
            return;
        }

        this.popover.close();
        this.popoverItem = item;

        const inDotMode = item.element.getClientRects().length === 0 && item.dot.getClientRects().length > 0;
        const anchor = inDotMode ? item.dot : item.widget.nameElement;

        // (the widget's own header has the name, so the title says what it is)
        const endpoint = item.endpoint;
        this.popoverTitle.replaceChildren (getEndpointRole (endpoint, item.isInput), el ("span", { class: "cp-count" }, getEndpointTypeDescription (endpoint)));
        this.popoverWidget = createEndpointWidget (endpoint, { ...this.widgetOptions, size: "full" }, item.isInput);
        this.popoverBody.replaceChildren (this.popoverWidget?.element ?? "");
        this.popover.element.classList.toggle ("cp-live-popover-wide", endpoint.purpose === "midi in");
        this.popover.element.classList.remove ("cp-live-popover-spectrum");

        if (this.popoverWidget && isSpectrumEndpoint (endpoint))
            addSpectrumToggle (this.popoverWidget, liveSpectrumSettingsKey, { onChange: (isShown) =>
            {
                this.popover.element.classList.toggle ("cp-live-popover-spectrum", isShown);
                this.schedulePopoverPosition();
            }});

        this.popover.open (anchor, { align: item.isInput ? "start" : "end",
                                     positionFrom: inDotMode ? null : item.element,
                                     focus: ":is(.cp-seg-button.cp-on, .cp-slider, .cp-select, .cp-toggle, .cp-keyboard, .cp-small-button):not(.cp-spectrum *)" });
    }

    disposePopoverWidget()
    {
        this.popoverWidget?.dispose();
        this.popoverWidget = null;
        this.popoverItem = null;
    }
}

const emptyLevels = new Map();

//==============================================================================
/// What an endpoint is for, in a few words, e.g. "Audio input" or "Parameter"
export function getEndpointRole (endpoint, isInput)
{
    const roles = { "audio in": "Audio input", "audio out": "Audio output", "midi in": "MIDI input", "midi out": "MIDI output", "parameter": "Parameter" };
    return roles[endpoint?.purpose] ?? (isInput ? "Input" : "Output");
}

/// An audio level message ({ min: [..], max: [..] }) as an activity level, 0 to 1 (-60 dB to 0 dB)
export function getAudioActivity (data)
{
    let peak = 0;

    for (const list of [data?.min, data?.max])
        if (list)
            for (let i = 0; i < list.length; ++i)
                peak = Math.max (peak, Math.abs (list[i]));

    return Math.max (0, Math.min (1, (gainToDB (peak) + 60) / 60));
}

/// How quickly the activity fades: it halves in this many milliseconds
export const activityHalfLifeMs = 90;

/// Below this, an endpoint counts as idle
const idleActivity = 0.02;

/*
    The activity of the live endpoints: a level (0 to 1) per endpoint name, which jumps up when
    data arrives (setLevel()) and decays exponentially (halving every activityHalfLifeMs,
    whatever the frame rate), so that a steady signal holds steady, and an event is a pulse
    that fades. onFrame (levels) is called with the Map of levels on each frame while any are
    above idle (and once more when they've all gone), and the scheduler goes idle after that.
*/
export class BoundaryActivity
{
    constructor (scheduler, onFrame)
    {
        this.scheduler = scheduler;
        this.onFrame = onFrame;
        this.levels = new Map();            // endpoint name -> level
        this.tick = (_time, dt) => this.update (dt);
    }

    /// Raises an endpoint's level (it decays from there)
    setLevel (name, level)
    {
        if (level > (this.levels.get (name) ?? 0))
            this.levels.set (name, Math.min (1, level));

        this.scheduler.schedule (this.tick);
    }

    getLevel (name)         { return this.levels.get (name) ?? 0; }

    /// Makes everything idle at once
    clear()
    {
        this.scheduler.cancel (this.tick);
        this.levels.clear();
        this.onFrame?.(this.levels);
    }

    /// Decays the levels by dt milliseconds' worth. Returns true while any are still above idle.
    update (dt = 1000 / 60)
    {
        const factor = Math.pow (0.5, dt / activityHalfLifeMs);

        for (const [name, level] of this.levels)
        {
            if (level * factor < idleActivity)
                this.levels.delete (name);
            else
                this.levels.set (name, level * factor);
        }

        this.onFrame?.(this.levels);
        return this.levels.size > 0;
    }
}
