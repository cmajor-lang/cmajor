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

import { PatchGraphView } from "./cmaj-patch-graph/patch-graph-view.js";
import { getBoundaryActivityPaths } from "./cmaj-patch-graph/activity.js";
import { getPortID } from "./cmaj-patch-graph/layout.js";
import { PanelSettings } from "./core/persistence.js";

/*
    <cmaj-patch-graph> shows an interactive diagram of the session's patch, using the JSON
    from the "graph" code generation target. Clicking a name, type, port or connection calls
    its onOpenSource (location) property, if it's been given one (the panel forwards it to VS
    Code), or otherwise window.openSourceFile (location), if there is one.

    The server makes the graph once per build, and the session status's graphVersion says when
    it has changed, so refresh() only fetches it (with session.requestGraph()) when it's new.
    (With an older server that has no graphVersion, it falls back to requestGeneratedCode(),
    which costs the server a separate build, once per build log it sees.)

    The view is in one of three modes (viewMode):
      - "default": the text at a readable size, anchored at the graph's top-left (see
        PatchGraphView.showDefaultView()). A new graph starts like this, and it's re-applied
        when the element is resized.
      - "fitted": the whole graph (the Fit button, F, or a double-click on the background), also
        re-applied on resize.
      - "user": the user has panned or zoomed, and the view is left where they put it.

    When a new build of the same patch arrives, the collapsed graphs and the view are kept.
    They're also remembered per patch (by manifest file), in the settings that
    settingsForPatch (manifestFile) returns: by default a PanelSettings from core/persistence.js.

    For a host that puts its own controls next to the root graph's ports (the panel's live mode),
    there's an overlay API, which works by the endpoints' names:
      - setBoundaryWidgets ({ [name]: { width, height } } | null) makes room beside each of those
        ports, and getBoundaryWidgetPlacements() says where each one goes (in layout units)
        after each layout (onLayout is called then). Elements with slot="overlay" are drawn at
        those coordinates.
      - setBoundaryActivity (Map of name → level 0..1) lights the ports and the wires attached
        to them (see getBoundaryActivityPaths() in activity.js)
      - highlightBoundaryEndpoint (name, "in" | "out") shows the signal path through a port, and
        clearHighlight() removes it
      - hasGraph says whether a graph is shown
*/
export default class PatchGraph extends HTMLElement
{
    constructor()
    {
        super();

        this.isActive = false;
        this.autoRefresh = true;    // set to false to only fetch the graph when refresh() is called
        this.pendingTimer = null;
        this.saveTimer = null;
        this.requestNumber = 0;
        this.shownVersion = null;   // the graphVersion of the graph that's shown
        this.shownPatch = null;     // the manifest file of the patch it's from

        /// Called with a "file:line:column" location when one is clicked (by default,
        /// window.openSourceFile(), if there is one)
        this.onOpenSource = null;

        // (see setBoundaryActivity())
        this.boundaryActivity = { model: null, pathsForName: new Map(), pathLevels: new Map(), sentLevels: new Map() };

        /// Returns an object with get (name, default) and set (name, value), for the settings
        /// of the given patch, or null not to remember anything
        this.settingsForPatch = (manifestFile) => manifestFile ? new PanelSettings (manifestFile) : null;

        this.statusListener = () =>
        {
            if (this.autoRefresh)
                this.refresh();
        };

        this.root = this.attachShadow ({ mode: "open" });
        this.root.innerHTML = `<style>${this.getCSS()}</style>${this.getHTML()}`;
        this.holder = this.root.getElementById ("holder");
        this.messageElement = this.root.getElementById ("message");

        /// Called whenever the zoom or pan changes (e.g. for a host's zoom readout)
        this.onViewChange = null;

        /// Called with { positioned } (see cmaj-patch-graph/layout.js) after each layout
        this.onLayout = null;

        // Children of this element with slot="overlay" are drawn over the graph, inside the
        // element that carries its pan/zoom transform, so they can be placed at layout
        // coordinates (e.g. at the positioned.boundaryWidgets that the boundaryWidgets layout
        // option asks for: see setLayoutOptions()). They stay in the host's DOM, and its styles.
        this.overlaySlot = document.createElement ("slot");
        this.overlaySlot.name = "overlay";

        this.view = new PatchGraphView (this.holder, { onOpenSource: (source) => this.openSource (source),
                                                       onViewChange: (state) => { this.scheduleSave(); this.onViewChange?.(state); },
                                                       onLayout: (info) => this.onLayout?.(info),
                                                       overlay: this.overlaySlot,
                                                       defaultView: "readable",
                                                       fitTopMargin: 36 });    // (below the toolbar)

        this.root.getElementById ("zoomIn").onclick  = () => this.zoomBy (1.25);
        this.root.getElementById ("zoomOut").onclick = () => this.zoomBy (0.8);
        this.root.getElementById ("actualSize").onclick = () => this.showDefaultView();
        this.root.getElementById ("fit").onclick = () => this.fit();
        this.root.getElementById ("expandAll").onclick = () => this.setAllCollapsed (false);
        this.root.getElementById ("collapseAll").onclick = () => this.setAllCollapsed (true);

        this.resizeObserver = new ResizeObserver (() => this.applyViewModeIfVisible());

        this.resizeObserver.observe (this.holder);
    }

    dispose()
    {
        this.saveNow();
        this.setSession (undefined);
        this.resizeObserver.disconnect();
        this.view.dispose();
    }

    connectedCallback()
    {
        this.isActive = true;

        if (this.autoRefresh)
            this.refresh();
    }

    disconnectedCallback()
    {
        this.isActive = false;
        this.cancelRefresh();
        this.saveNow();
    }

    setSession (session)
    {
        this.cancelRefresh();
        this.session?.removeStatusListener?.(this.statusListener);
        this.session = session;
        this.session?.addStatusListener?.(this.statusListener);

        if (this.autoRefresh)
            this.refresh();
    }

    /// Fetches the graph soon, if it's changed. Returns false if it can't (it has no session, or
    /// isn't in the page), in which case nothing happens.
    refresh()
    {
        if (! this.session || ! this.isActive)
            return false;

        // (a fetch that's already pending reads the status when it runs)
        if (! this.pendingTimer)
        {
            this.pendingTimer = setTimeout (() =>
            {
                this.pendingTimer = undefined;
                this.requestGraph();
            }, 100);
        }

        return true;
    }

    cancelRefresh()
    {
        if (this.pendingTimer)
        {
            clearTimeout (this.pendingTimer);
            this.pendingTimer = undefined;
        }
    }

    async requestGraph()
    {
        const requestNumber = ++this.requestNumber;
        const status = this.session?.status ?? {};
        const patch = status.manifestFile ?? "";

        try
        {
            let model, version;

            if (typeof status.graphVersion === "string")
            {
                if (this.isShowing (status.graphVersion, patch))
                    return;

                const reply = await this.session.requestGraph();
                model = reply?.graph;
                version = reply?.version;

                // A reply that isn't for the patch and build that the status describes now is
                // out of date (the patch changed or rebuilt meanwhile): the newer status will
                // bring another refresh. Showing it would also save its collapsed graphs and zoom
                // under the wrong patch.
                const current = this.session?.status ?? {};

                if (model && ((reply.manifestFile !== undefined && reply.manifestFile !== (current.manifestFile ?? ""))
                               || version !== current.graphVersion))
                    return;

                // (it failed to build since the status was sent: the next status will say so)
                if (! model && this.view.model && this.shownPatch === patch)
                    return;
            }
            else if (status.loaded)
            {
                // an older server, which makes the graph with a separate build each time it's asked
                version = "log:" + (status.log ?? "");

                if (this.isShowing (version, patch))
                    return;

                const reply = await this.session.requestGeneratedCode ("graph", {});
                model = (typeof reply?.code === "string" && reply.code.length > 0) ? JSON.parse (reply.code) : undefined;
            }
            else if (status.error || status.status)
            {
                // it's rebuilding, or the build failed: keep showing the last good graph, but
                // only if it's this patch's (a status may not say which patch it's loading)
                if (this.view.model && patch && this.shownPatch !== patch)
                    this.clearGraph();

                if (! this.view.model)
                    this.showMessage (status.error ? "The patch hasn't built successfully yet" : "Loading…");

                return;
            }

            // a newer request has been made since this one
            if (requestNumber !== this.requestNumber)
                return;

            if (! model)
            {
                this.clearGraph();
                this.showMessage ("No patch loaded");
                return;
            }

            await this.showModel (model, version, patch);
        }
        catch (e)
        {
            this.showMessage ("Couldn't show the graph: " + (e?.message ?? e));
        }
    }

    /// Removes the graph that's shown (saving its state first)
    clearGraph()
    {
        this.saveNow();
        this.view.clear();
        this.shownVersion = null;
        this.shownPatch = null;
    }

    isShowing (version, patch)
    {
        return this.view.model != null && version === this.shownVersion && patch === this.shownPatch;
    }

    /// A new build of the same patch keeps the current collapsed graphs and zoom/pan; another
    /// patch gets the ones that were saved for it
    async showModel (model, version, patch)
    {
        const isSamePatch = this.view.model != null && patch === this.shownPatch;
        let collapsedPaths, transform, viewMode;

        if (isSamePatch)
        {
            collapsedPaths = this.view.getCollapsedPaths();
            viewMode = this.viewMode;
            transform = viewMode === "user" ? { ...this.view.transform } : undefined;
        }
        else
        {
            this.saveNow();
            const saved = this.loadSavedState (patch);
            collapsedPaths = saved?.collapsed;
            transform = saved?.transform ?? undefined;
            viewMode = saved?.fitted ? "fitted" : "default";
        }

        this.shownVersion = version;
        this.shownPatch = patch;
        this.showMessage (null);
        this.updateFitTopMargin();

        await this.view.setModel (model, { collapsedPaths, transform, viewMode });
    }

    //==============================================================================
    // The collapsed graphs and the view are saved as "graphView" in the patch's settings:
    // { collapsed: [paths], transform: { x, y, k } | null, fitted: bool }. The transform is only
    // saved in "user" mode; null means the default view, or the fitted one if fitted is true.

    loadSavedState (patch)
    {
        const saved = this.getSettings (patch)?.get ("graphView", null);

        if (! saved || typeof saved !== "object")
            return null;

        return { collapsed: Array.isArray (saved.collapsed) ? saved.collapsed.filter (p => typeof p === "string") : undefined,
                 transform: saved.transform ?? null,
                 fitted: saved.fitted === true };
    }

    getSettings (patch)
    {
        try
        {
            return this.settingsForPatch?.(patch) ?? null;
        }
        catch (e)
        {
            return null;
        }
    }

    scheduleSave()
    {
        if (this.view?.model && ! this.saveTimer)
            this.saveTimer = setTimeout (() => this.saveNow(), 300);
    }

    saveNow()
    {
        if (this.saveTimer)
        {
            clearTimeout (this.saveTimer);
            this.saveTimer = null;
        }

        if (! this.view?.model || ! this.shownPatch)
            return;

        const { x, y, k } = this.view.transform;

        this.getSettings (this.shownPatch)?.set ("graphView", {
            collapsed: this.view.getCollapsedPaths(),
            transform: this.viewMode === "user" ? { x, y, k } : null,
            fitted: this.viewMode === "fitted"
        });
    }

    //==============================================================================
    /// "default", "fitted" or "user" (see above)
    get viewMode()                  { return this.view.viewMode; }

    /// Fits the whole graph, and keeps it fitted when the element is resized
    fit()                           { this.setViewMode ("fitted"); }

    /// Goes back to the default view: readable text, anchored at the top-left
    showDefaultView()               { this.setViewMode ("default"); }

    setViewMode (mode)
    {
        this.updateFitTopMargin();
        this.view.setViewMode (mode);
        this.scheduleSave();
    }

    /// Collapses (or expands) every nested graph, and shows the result in the default view
    async setAllCollapsed (shouldCollapse)
    {
        this.updateFitTopMargin();
        await this.view.setAllCollapsed (shouldCollapse);
    }

    /// Zooms around the centre (which counts as the user moving the view)
    zoomBy (factor)                 { this.view.zoomBy (factor); }

    /// Changes some of the layout options (see cmaj-patch-graph/layout.js), merging them into
    /// the current ones (a null value removes one), and lays out the graph that's shown again
    /// (see PatchGraphView.setLayoutOptions())
    async setLayoutOptions (changes)
    {
        this.updateFitTopMargin();
        await this.view.setLayoutOptions (changes);
    }

    /// The current zoom, where 1 means the text is at its designed size
    getZoom()                       { return this.view.transform.k; }

    /// Re-applies the default or fitted view (e.g. after a resize); a view the user has moved is
    /// left alone, and so is one that's hidden (e.g. in a hidden VS Code tab: it has no size)
    applyViewModeIfVisible()
    {
        this.updateFitTopMargin();
        this.view.applyViewMode();
    }

    // (leave room at the top for the toolbar, if it's shown)
    updateFitTopMargin()
    {
        this.view.fitTopMargin = this.hasAttribute ("no-toolbar") ? 20 : 36;
    }

    //==============================================================================
    // The overlay API (see above)

    get hasGraph()                  { return this.view.model != null; }

    /// Makes room beside the root graph's ports for some controls: { [endpointName]: { width,
    /// height } }, or null for none. getBoundaryWidgetPlacements() then says where they go.
    async setBoundaryWidgets (sizes)
    {
        const hasAny = sizes != null && Object.keys (sizes).length > 0;
        const current = this.view.layoutOptions.boundaryWidgets ?? null;

        if (JSON.stringify (hasAny ? sizes : null) !== JSON.stringify (current))
            await this.setLayoutOptions ({ boundaryWidgets: hasAny ? sizes : null });
    }

    /// Where the layout put each boundary widget: [{ name, direction ("in" | "out"), x, y, width,
    /// height, portX, portY, portWidth, portHeight }] in layout units (see layout.js)
    getBoundaryWidgetPlacements()
    {
        return this.view.positioned?.boundaryWidgets ?? [];
    }

    /// Shows the activity of the root graph's own endpoints: levels is a Map of endpoint name
    /// → level (0 to 1). Each one lights the endpoint's port and the wires attached to it
    /// (see getBoundaryActivityPaths()). It's cheap to call on every frame: only the changes
    /// that would be visible reach the drawing.
    setBoundaryActivity (levels)
    {
        const a = this.boundaryActivity;
        const model = this.view.model;

        // (a new graph has no activity shown, and may have different paths)
        if (a.model !== model)
        {
            a.model = model;
            a.pathsForName = getBoundaryActivityPaths (model);
            a.sentLevels.clear();
        }

        if (! model)
            return;

        a.pathLevels.clear();

        for (const [name, paths] of a.pathsForName)
        {
            const level = levels?.get (name) ?? 0;

            for (const path of paths)
                a.pathLevels.set (path, Math.max (a.pathLevels.get (path) ?? 0, level));
        }

        let updates = null;

        for (const [path, level] of a.pathLevels)
        {
            const rounded = Math.round (level * 50) / 50;

            if (rounded !== (a.sentLevels.get (path) ?? 0))
            {
                a.sentLevels.set (path, rounded);
                (updates ??= []).push ([path, rounded]);
            }
        }

        if (updates)
            this.view.setActivity (updates);
    }

    /// Every path that's lit by setBoundaryActivity(), and its level (for tests)
    getBoundaryActivityLevels()
    {
        return new Map ([...this.boundaryActivity.sentLevels].filter (([, level]) => level > 0));
    }

    /// Highlights the signal path through one of the root graph's own ports
    highlightBoundaryEndpoint (name, direction)
    {
        const rootID = this.view.model?.root?.id;

        if (rootID !== undefined)
            this.view.highlightPorts ([getPortID (rootID, direction === "out" ? "out" : "in", name)]);
    }

    clearHighlight()                { this.view.clearHighlight(); }

    openSource (location)
    {
        if (this.onOpenSource)
            this.onOpenSource (location);
        else
            window.openSourceFile?.(location);
    }

    showMessage (text)
    {
        this.messageElement.textContent = text ?? "";
        this.messageElement.style.display = text ? "block" : "none";
    }

    getHTML()
    {
        return `
            <div id="holder"></div>
            <div id="toolbar">
              <button id="zoomIn" title="Zoom in (or pinch, or ctrl/cmd + scroll)">+</button>
              <button id="zoomOut" title="Zoom out (or pinch, or ctrl/cmd + scroll)">−</button>
              <button id="actualSize" title="Show the text at its full size">100%</button>
              <button id="fit" title="Fit the whole graph to the panel (or double-click the background)">Fit</button>
              <button id="expandAll">Expand all</button>
              <button id="collapseAll">Collapse nested</button>
            </div>
            <div id="message"></div>`;
    }

    getCSS()
    {
        return `
            /* The colours are the --cmaj-graph-* custom properties (see render-svg.js for the
               list), which inherit into this shadow root: the panel's design tokens
               (theme/tokens.css) set them. Without them, render-svg.js has its own defaults. */
            :host {
                display: block;
                position: relative;
                height: max(24rem, 65vh);
                min-height: 10rem;
                resize: vertical;
                overflow: hidden;
            }

            #holder {
                position: absolute;
                inset: 0;
                background: var(--cmaj-graph-background, #2a3331);
            }

            #toolbar {
                position: absolute;
                top: 0.4rem;
                right: 0.6rem;
                display: flex;
                gap: 0.3rem;
            }

            #toolbar button {
                font: inherit;
                font-size: 0.75rem;
                color: var(--cmaj-graph-text, #e4ebe9);
                background: var(--cmaj-graph-node-fill, #1e2523);
                border: 1px solid var(--cmaj-graph-stroke, #6f827c);
                border-radius: 3px;
                padding: 0.15rem 0.5rem;
                cursor: pointer;
                opacity: 0.85;
            }

            #toolbar button:hover {
                opacity: 1;
            }

            /* (for a host that shows its own controls) */
            :host([no-toolbar]) #toolbar {
                display: none;
            }

            #message {
                display: none;
                position: absolute;
                top: 0.6rem;
                left: 0.8rem;
                color: var(--cmaj-graph-text, #e4ebe9);
                opacity: 0.7;
            }
            `;
    }
}

// (this module may be loaded more than once, e.g. from different URLs)
if (! customElements.get ("cmaj-patch-graph"))
    customElements.define ("cmaj-patch-graph", PatchGraph);
