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

import "../cmaj-graph.js";
import { el, Disposer, setPressed } from "../core/dom.js";
import { LiveGraphLayer } from "./graph-live.js";

/*
    The Graph tab: hosts <cmaj-patch-graph>. The server makes the graph once per build, and
    the status's graphVersion changes when it does, so it's fetched while the tab is visible,
    whenever that changes (or, from an older server, after each build). The graph's collapsed
    nodes and zoom/pan are kept across rebuilds, and remembered per patch.

    It has two modes, chosen in the tab bar and remembered per patch (as "graphMode"):
      - "structure": just the graph
      - "live" (the default): the patch's endpoints as controls and meters next to their ports,
        with activity on the wires attached to them (see graph-live.js)
*/
export class GraphView
{
    constructor (ctx)
    {
        this.ctx = ctx;
        this.disposer = new Disposer();
        this.isActive = false;
        this.needsRefresh = true;
        this.mode = null;

        this.graph = el ("cmaj-patch-graph", { class: "cp-graph", "no-toolbar": true });
        this.graph.autoRefresh = false;
        this.graph.onOpenSource = (location) => ctx.bridge.openSourceFile (location);
        this.frameRequest = null;

        // (the loaded patch's settings must be the panel's own object: see forPatch())
        this.graph.settingsForPatch = (manifestFile) => ctx.settings.forPatch (manifestFile);

        this.live = new LiveGraphLayer (ctx, this.graph);
        this.disposer.add (() => this.live.dispose());

        this.legend = el ("div", { class: "cp-live-legend", hidden: true,
                                   title: "The wires show the activity of the patch's own inputs and outputs. What happens inside isn't measured." },
                          el ("span", { class: "cp-live-legend-mark", "aria-hidden": "true" }), "Boundary signals only");

        this.element = el ("div", { class: "cp-graph-view" }, this.graph, this.legend);
        this.graph.setSession (ctx.session);

        this.graph.onLayout = () =>
        {
            this.live.place();
            this.updateLegend();
        };

        // (the graph's own toolbar is hidden, and its controls live in the tab bar instead)
        this.zoomText = el ("button", { class: "cp-seg-button cp-zoom-text", title: "Show the text at its full size (0)", "aria-label": "Zoom: reset to 100%",
                                        onclick: () => this.showDefaultView() }, "100%");
        this.graph.onViewChange = () => this.updateZoomText();

        const makeModeButton = (mode, label, title) =>
            el ("button", { class: "cp-seg-button cp-mode-button", dataset: { mode }, title, "aria-pressed": "false", onclick: () => this.setMode (mode) }, label);

        this.modeButtons = [makeModeButton ("structure", "Structure", "Show the graph's structure"),
                            makeModeButton ("live", "Live", "Play the patch from the graph: its inputs, outputs and parameters as controls and meters next to their ports")];

        this.toolbar = el ("div", { class: "cp-tab-tools" },
                           el ("div", { class: "cp-seg cp-mode-seg", role: "group", "aria-label": "Graph mode" }, this.modeButtons),
                           el ("div", { class: "cp-seg", role: "group", "aria-label": "Zoom" },
                               el ("button", { class: "cp-seg-button", title: "Zoom out (or pinch, or ctrl/cmd + scroll)", "aria-label": "Zoom out",
                                               onclick: () => this.graph.zoomBy (0.8) }, "−"),
                               this.zoomText,
                               el ("button", { class: "cp-seg-button", title: "Zoom in (or pinch, or ctrl/cmd + scroll)", "aria-label": "Zoom in",
                                               onclick: () => this.graph.zoomBy (1.25) }, "+")),
                           el ("button", { class: "cp-button", title: "Fit the whole graph into the view (F, or double-click the background)", onclick: () => this.fit() },
                               "Fit", el ("kbd", null, "F")),
                           el ("button", { class: "cp-button cp-optional", title: "Expand every nested graph", onclick: () => this.graph.setAllCollapsed (false) }, "Expand all"),
                           el ("button", { class: "cp-button cp-optional", title: "Collapse the nested graphs", onclick: () => this.graph.setAllCollapsed (true) }, "Collapse"));

        // (the graph itself skips the fetch if what it's showing is still current)
        this.disposer.add (ctx.store.subscribe (["graphVersion", "loaded", "lastBuildTime", "connection", "manifestFile"], () =>
        {
            this.needsRefresh = true;
            this.refreshIfVisible();
            this.updateLegend();
        }));

        // each patch remembers its mode (and a new patch gets the last one chosen)
        this.disposer.add (ctx.store.subscribe (["patchSettings"], () => this.setMode (ctx.settings.patchOrGlobal.get ("graphMode", "live"), false), true));

        this.disposer.add (() =>
        {
            if (this.frameRequest !== null)
                cancelAnimationFrame (this.frameRequest);

            this.graph.dispose?.();
        });
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    setActive (isActive)
    {
        this.isActive = isActive;
        this.refreshIfVisible();

        // (the live layer also pauses while the page is hidden, e.g. a VS Code tab in the background)
        this.live.setActive (isActive);

        // (it may have been laid out while hidden, with no size to fit to; a view that the user
        // has moved is left alone)
        if (isActive && this.frameRequest === null)
        {
            this.frameRequest = requestAnimationFrame (() =>
            {
                this.frameRequest = null;
                this.graph.applyViewModeIfVisible?.();
            });
        }
    }

    /// "structure" or "live"
    setMode (mode, save = true)
    {
        mode = mode === "structure" ? "structure" : "live";

        if (save)
            this.ctx.settings.patchOrGlobal.set ("graphMode", mode);

        this.mode = mode;

        for (const b of this.modeButtons)
            setPressed (b, b.dataset.mode === mode);

        this.live.setEnabled (mode === "live");
        this.updateLegend();
    }

    updateLegend()
    {
        this.legend.hidden = ! (this.mode === "live" && this.live.hasWidgets && this.graph.hasGraph);
    }

    refreshIfVisible()
    {
        // (if the graph can't fetch yet, e.g. it isn't in the page, the next attempt will)
        if (this.isActive && this.needsRefresh && this.graph.refresh())
            this.needsRefresh = false;
    }

    fit()
    {
        this.graph.fit?.();
    }

    showDefaultView()
    {
        this.graph.showDefaultView?.();
    }

    updateZoomText()
    {
        const zoom = this.graph.getZoom();
        const text = Math.round (zoom * 100) + "%";

        if (this.zoomText.textContent !== text)
            this.zoomText.textContent = text;

        this.live.updateZoom (zoom);
    }
}
