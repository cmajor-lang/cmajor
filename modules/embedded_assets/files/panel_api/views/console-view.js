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

import { el, setPressed } from "../core/dom.js";
import { ConnectionView } from "./connection-view.js";
import { isLoggableEndpoint, getAllEndpoints } from "../widgets/registry.js";
import { formatEventValue } from "../widgets/event-monitor.js";

const maxEntries = 5000;
const maxRenderedRows = 1000;
const filterDelayMs = 150;

/// The panel's own messages (build results, errors...): always shown
const panelSource = "panel";

/*
    The Console tab in the bottom drawer: one log of everything arriving on the patch's event
    endpoints (outputs such as `console`, MIDI out, and the event/MIDI inputs), plus the panel's
    own messages. Each endpoint has a chip to show or hide it (remembered per patch); there's a
    text filter, pause and clear. Text sent to an endpoint called "console" is joined into lines,
    and an ESC character in it clears the log (as the old panel did).

    History survives rebuilds and status updates; it's kept in a ring of the last 5000 entries.
    It keeps listening while the drawer is hidden (it's a log), so it doesn't pause.
*/
export class ConsoleView  extends ConnectionView
{
    constructor (ctx)
    {
        super (ctx, { pausesWhenHidden: false });

        this.entries = new RingBuffer (maxEntries);
        this.hiddenSources = new Set();
        this.sources = new Map();       // name -> chip element
        this.partialLines = new Map();
        this.isPaused = false;
        this.filterText = "";
        this.filterTimer = null;
        this.pendingRows = [];

        this.list = el ("div", { class: "cp-console-list", role: "log", "aria-live": "off", tabindex: 0 });
        this.element = el ("div", { class: "cp-console" }, this.list);

        this.chips = el ("div", { class: "cp-chips", role: "group", "aria-label": "Sources" });
        this.showAllButton = el ("button", { class: "cp-chip", hidden: true, title: "Show every source", onclick: () => this.showAll() }, "Show all");
        this.filterInput = el ("input", { class: "cp-input cp-console-filter", type: "search", placeholder: "Filter  ( / )",
                                          "aria-label": "Filter the console", oninput: () => this.scheduleFilter() });
        this.pauseButton = el ("button", { class: "cp-button", "aria-pressed": "false", title: "Pause the log", onclick: () => this.setPaused (! this.isPaused) }, "Pause");
        this.toolbar = el ("div", { class: "cp-tab-tools" }, this.chips, this.showAllButton, this.filterInput, this.pauseButton,
                           el ("button", { class: "cp-button", title: "Clear the log", onclick: () => this.clear() }, "Clear"));

        this.flushRows = () => this.flush();
        this.disposer.add (() => { this.ctx.scheduler.cancel (this.flushRows); clearTimeout (this.filterTimer); });

        // (each patch remembers which sources it hides)
        this.disposer.add (ctx.store.subscribe (["patchSettings"], () =>
        {
            this.hiddenSources = new Set (ctx.settings.patch.get ("consoleHidden", []));
            this.hiddenSources.delete (panelSource);
            this.refreshChips();
            this.rerender();
        }, true));

        this.followConnection();
    }

    dispose()
    {
        super.dispose();
        this.element.remove();
    }

    focusFilter()
    {
        this.filterInput.focus();
        this.filterInput.select();
    }

    /// Shows only the given source, and the panel's own messages (e.g. from an endpoint widget's
    /// "Console" button). "Show all" undoes it.
    showOnly (source)
    {
        this.setHiddenSources ([...this.sources.keys()].filter (s => s !== source));
    }

    showAll()
    {
        this.setHiddenSources ([]);
    }

    setHiddenSources (sources)
    {
        this.hiddenSources = new Set (sources);
        this.hiddenSources.delete (panelSource);
        this.ctx.settings.patch.set ("consoleHidden", [...this.hiddenSources]);
        this.refreshChips();
        this.rerender();
    }

    //==============================================================================
    onAttach (connection, group)
    {
        // (a partial line from before belongs to a build that's gone)
        this.partialLines.clear();

        const endpoints = getAllEndpoints (this.ctx.store.state).filter (isLoggableEndpoint);
        const names = new Set (endpoints.map (e => e.endpointID));

        // the chips are for the endpoints that the patch has now (their history stays in the log)
        for (const [source, chip] of [...this.sources])
        {
            if (source !== panelSource && ! names.has (source))
            {
                chip.remove();
                this.sources.delete (source);
            }
        }

        for (const endpoint of endpoints)
        {
            this.addSource (endpoint.endpointID);
            group.subscribe (endpoint.endpointID, value => this.handleEndpointValue (endpoint, value));
        }

        this.refreshChips();
    }

    handleEndpointValue (endpoint, value)
    {
        if (endpoint.endpointID === "console" || (typeof value === "string" && endpoint.purpose === "console"))
        {
            this.addConsoleText (endpoint.endpointID, typeof value === "string" ? value : formatEventValue (value, endpoint));
            return;
        }

        this.add (endpoint.endpointID, formatEventValue (value, endpoint));
    }

    /// Joins text into lines; an ESC character clears the log
    addConsoleText (source, text)
    {
        const esc = text.lastIndexOf ("\u001b");

        if (esc >= 0)
        {
            this.clear();
            text = text.substring (esc + 1);
        }

        const lines = ((this.partialLines.get (source) ?? "") + text).split ("\n");
        this.partialLines.set (source, lines.pop());

        for (const line of lines)
            this.add (source, line);
    }

    /// Adds an entry. level is "info" | "warning" | "error".
    add (source, text, level = "info")
    {
        if (this.isPaused && level === "info")
            return;

        if (source !== panelSource)
            this.addSource (source);

        const entry = { time: new Date(), source, text: String (text), level };
        this.entries.push (entry);

        if (this.isVisible (entry))
        {
            // (frames don't run while the page is hidden, so this can grow until it's shown)
            this.pendingRows.push (entry);

            if (this.pendingRows.length > 2 * maxRenderedRows)
                this.pendingRows.splice (0, this.pendingRows.length - maxRenderedRows);

            this.ctx.scheduler.schedule (this.flushRows);
        }
    }

    addSource (source)
    {
        if (this.sources.has (source))
            return;

        const chip = el ("button", { class: "cp-chip", dataset: { source }, "aria-pressed": "true",
                                     onclick: () =>
                                     {
                                         const hidden = new Set (this.hiddenSources);
                                         hidden.has (source) ? hidden.delete (source) : hidden.add (source);
                                         this.setHiddenSources (hidden);
                                     } }, source);

        this.sources.set (source, chip);
        this.chips.append (chip);
        this.refreshChips();
    }

    refreshChips()
    {
        for (const [source, chip] of this.sources)
            setPressed (chip, ! this.hiddenSources.has (source));

        this.showAllButton.hidden = ! [...this.sources.keys()].some (s => this.hiddenSources.has (s));
    }

    isVisible (entry)
    {
        return ! this.hiddenSources.has (entry.source)
                && (! this.filterText || entry.text.toLowerCase().includes (this.filterText) || entry.source.toLowerCase().includes (this.filterText));
    }

    scheduleFilter()
    {
        clearTimeout (this.filterTimer);

        this.filterTimer = setTimeout (() =>
        {
            this.filterTimer = null;
            this.filterText = this.filterInput.value.toLowerCase();
            this.rerender();
        }, filterDelayMs);
    }

    setPaused (shouldPause)
    {
        this.isPaused = shouldPause;
        setPressed (this.pauseButton, shouldPause);
        this.pauseButton.textContent = shouldPause ? "Resume" : "Pause";
    }

    clear()
    {
        this.entries.clear();
        this.pendingRows = [];
        this.partialLines.clear();
        this.list.replaceChildren();
    }

    rerender()
    {
        this.list.replaceChildren();
        this.pendingRows = this.entries.toArray().filter (e => this.isVisible (e)).slice (-maxRenderedRows);
        this.flush();
    }

    flush()
    {
        if (this.pendingRows.length === 0)
            return;

        const isAtBottom = this.list.scrollTop + this.list.clientHeight >= this.list.scrollHeight - 4;
        const rows = this.pendingRows.slice (-maxRenderedRows);
        this.pendingRows = [];

        const fragment = document.createDocumentFragment();

        for (const entry of rows)
            fragment.append (createRow (entry));

        this.list.append (fragment);

        while (this.list.childElementCount > maxRenderedRows)
            this.list.firstElementChild.remove();

        if (isAtBottom)
            this.list.scrollTop = this.list.scrollHeight;
    }
}

//==============================================================================
/// A fixed-size ring of the most recent items
export class RingBuffer
{
    constructor (capacity)
    {
        this.capacity = capacity;
        this.items = new Array (capacity);
        this.start = 0;
        this.size = 0;
    }

    push (item)
    {
        this.items[(this.start + this.size) % this.capacity] = item;

        if (this.size < this.capacity)
            ++this.size;
        else
            this.start = (this.start + 1) % this.capacity;
    }

    clear()
    {
        this.items = new Array (this.capacity);
        this.start = 0;
        this.size = 0;
    }

    /// The items, oldest first
    toArray()
    {
        const result = new Array (this.size);

        for (let i = 0; i < this.size; ++i)
            result[i] = this.items[(this.start + i) % this.capacity];

        return result;
    }
}

function createRow (entry)
{
    const t = entry.time;
    const time = `${String (t.getHours()).padStart (2, "0")}:${String (t.getMinutes()).padStart (2, "0")}:${String (t.getSeconds()).padStart (2, "0")}.${String (t.getMilliseconds()).padStart (3, "0")}`;

    return el ("div", { class: ["cp-log-row", entry.level !== "info" && `cp-log-${entry.level}`] },
               el ("span", { class: "cp-log-time" }, time),
               el ("span", { class: "cp-log-source" }, entry.source),
               el ("span", { class: "cp-log-text" }, entry.text));
}
