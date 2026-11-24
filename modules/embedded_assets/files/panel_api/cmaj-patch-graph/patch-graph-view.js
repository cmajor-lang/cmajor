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

import { getGraphNodeIDsBelowDepth, createLayeredLayoutEngine } from "./layout.js";
import { renderSVGElement } from "./render-svg.js";
import { ActivityOverlay, getActivityPaths } from "./activity.js";

/*
    An interactive view of a patch graph inside a container element:
      - drag or scroll to pan, pinch or ctrl/cmd + scroll to zoom (or call zoomBy()),
        double-click the background to fit
      - click a graph's ▾/▸ control to collapse or expand it (which re-runs the layout, keeping
        that graph where it was on screen)
      - hover a port, connection or node to highlight the signal path through it
      - click a name, port, type or connection to open its source location
      - live activity can be shown on nodes, ports and connections (see activity.js),
        either pushed in with setActivity(), or from an activitySource

    const view = new PatchGraphView (containerElement, { onOpenSource, activitySource });
    await view.setModel (jsonFromTheGraphTarget);

    layoutEngine defaults to createLayeredLayoutEngine() from layout.js.

    onOpenSource (location, event) is given the "file:line:column" string that
    openSourceFile() expects.
*/
export class PatchGraphView
{
    constructor (container, options = {})
    {
        this.container = container;
        this.layoutEngine = options.layoutEngine ?? createLayeredLayoutEngine();
        this.layoutOptions = options.layoutOptions ?? {};
        this.onOpenSource = options.onOpenSource ?? ((source) => globalThis.openSourceFile?.(source));
        this.onLayout = options.onLayout ?? (() => {});
        this.initialCollapseDepth = options.initialCollapseDepth;
        this.fitTopMargin = options.fitTopMargin ?? 20;     // e.g. to leave room for a toolbar
        this.activity = new ActivityOverlay();
        this.activitySource = options.activitySource ?? null;
        this.unsubscribeActivity = null;

        this.model = null;
        this.positioned = null;
        this.svg = null;
        this.collapsed = new Set();
        this.transform = { x: 0, y: 0, k: 1 };
        this.layoutGeneration = 0;

        this.container.style.overflow = "hidden";
        this.container.style.touchAction = "none";

        this.listeners = [];
        this.listen (container, "wheel",        (e) => this.handleWheel (e), { passive: false });
        this.listen (container, "pointerdown",  (e) => this.handlePointerDown (e));
        this.listen (container, "pointermove",  (e) => this.handlePointerMove (e));
        this.listen (container, "pointerup",    (e) => this.handlePointerUp (e));
        this.listen (container, "pointerover",  (e) => this.handlePointerOver (e));
        this.listen (container, "pointerleave", () => this.clearHighlight());
        this.listen (container, "dblclick",     (e) => { if (! this.getInteractiveTarget (e.target)) this.fit(); });
    }

    dispose()
    {
        this.unsubscribeFromActivitySource();

        for (const [target, type, fn, opts] of this.listeners)
            target.removeEventListener (type, fn, opts);

        this.listeners = [];
        this.container.replaceChildren();
    }

    /// Removes the current graph, leaving the view empty
    clear()
    {
        this.unsubscribeFromActivitySource();
        this.activity.clearActivity();
        ++this.layoutGeneration;
        this.model = null;
        this.positioned = null;
        this.svg = null;
        this.viewport = null;
        this.container.replaceChildren();
    }

    //==============================================================================
    async setModel (model)
    {
        this.unsubscribeFromActivitySource();
        this.activity.clearActivity();
        this.model = model;
        this.collapsed = new Set (this.initialCollapseDepth !== undefined
                                    ? getGraphNodeIDsBelowDepth (model, this.initialCollapseDepth) : []);
        await this.relayout();
        this.fit();
        this.subscribeToActivitySource();
    }

    //==============================================================================
    /// Sets the levels (0 to 1) for some node, endpoint or connection paths. updates can be a
    /// Map, an array of [path, level] pairs, or an object of path → level. Levels persist
    /// until changed, and survive collapsing, expanding and re-layout.
    setActivity (updates)
    {
        this.activity.setActivity (updates);
    }

    clearActivity()
    {
        this.activity.clearActivity();
    }

    /// Replaces the activity source (or removes it, if null)
    setActivitySource (source)
    {
        this.unsubscribeFromActivitySource();
        this.activity.clearActivity();
        this.activitySource = source ?? null;
        this.subscribeToActivitySource();
    }

    subscribeToActivitySource()
    {
        if (this.activitySource && this.model && ! this.unsubscribeActivity)
            this.unsubscribeActivity = this.activitySource.subscribe (getActivityPaths (this.model),
                                                                      (updates) => this.setActivity (updates)) ?? null;
    }

    unsubscribeFromActivitySource()
    {
        this.unsubscribeActivity?.();
        this.unsubscribeActivity = null;
    }

    async setLayoutOptions (layoutOptions)
    {
        this.layoutOptions = layoutOptions;
        await this.relayout();
    }

    /// Collapses or expands a graph, keeping its top-left corner (where its toggle is) at the
    /// same place on screen
    async toggleCollapsed (nodeID)
    {
        const findNode = () => this.positioned?.nodes.find (n => n.id === nodeID);
        const before = findNode();
        const screenX = before ? before.x * this.transform.k + this.transform.x : 0;
        const screenY = before ? before.y * this.transform.k + this.transform.y : 0;

        if (this.collapsed.has (nodeID))
            this.collapsed.delete (nodeID);
        else
            this.collapsed.add (nodeID);

        await this.relayout();

        const after = findNode();

        if (before && after && after !== before)
        {
            this.transform.x = screenX - after.x * this.transform.k;
            this.transform.y = screenY - after.y * this.transform.k;
            this.applyTransform();
        }
    }

    async setAllCollapsed (shouldCollapse)
    {
        this.collapsed = new Set (shouldCollapse && this.model ? getGraphNodeIDsBelowDepth (this.model, 1) : []);
        await this.relayout();
        this.fit();
    }

    async relayout()
    {
        if (! this.model)
            return;

        const generation = ++this.layoutGeneration;
        const start = performance.now();

        const positioned = await this.layoutEngine.layout (this.model, {
            ...this.layoutOptions,
            collapsedNodeIDs: [...this.collapsed]
        });

        // a newer layout was requested while this one was running
        if (generation !== this.layoutGeneration)
            return;

        this.positioned = positioned;
        this.svg = renderSVGElement (positioned, {}, this.container.ownerDocument);
        this.svg.setAttribute ("width", "100%");
        this.svg.setAttribute ("height", "100%");
        this.svg.removeAttribute ("viewBox");
        this.viewport = this.svg.querySelector (".viewport");
        this.container.replaceChildren (this.svg);
        this.indexEdges();
        this.activity.attach (this.model, positioned, this.svg);
        this.applyTransform();
        this.onLayout ({ positioned, milliseconds: performance.now() - start });
    }

    //==============================================================================
    fit()
    {
        if (! this.positioned)
            return;

        const margin = 20;
        const width = this.container.clientWidth, height = this.container.clientHeight;

        // (a hidden container has no size to fit to)
        if (width <= 2 * margin || height <= 2 * margin)
            return;

        const top = Math.max (margin, this.fitTopMargin);
        const k = Math.min ((width - 2 * margin) / this.positioned.width,
                            (height - top - margin) / this.positioned.height, 1.5);

        // centred horizontally, but at the top, because the bottom of a tall container may be
        // off the bottom of the page
        this.transform = {
            k,
            x: (width - this.positioned.width * k) / 2,
            y: top
        };

        this.applyTransform();
    }

    zoomBy (factor, centreX = this.container.clientWidth / 2, centreY = this.container.clientHeight / 2)
    {
        const { x, y, k } = this.transform;
        const newK = Math.min (Math.max (k * factor, 0.05), 8);

        this.transform = {
            k: newK,
            x: centreX - (centreX - x) * newK / k,
            y: centreY - (centreY - y) * newK / k
        };

        this.applyTransform();
    }

    applyTransform()
    {
        const { x, y, k } = this.transform;
        this.viewport?.setAttribute ("transform", `translate(${x},${y}) scale(${k})`);
    }

    //==============================================================================
    listen (target, type, fn, opts)
    {
        target.addEventListener (type, fn, opts);
        this.listeners.push ([target, type, fn, opts]);
    }

    getInteractiveTarget (element)
    {
        return element?.closest?.("[data-toggle-id], [data-source]") ?? null;
    }

    getLocalPoint (e)
    {
        const r = this.container.getBoundingClientRect();
        return { x: e.clientX - r.left, y: e.clientY - r.top };
    }

    /// Scrolling pans (so a trackpad can move around freely). A pinch, or scrolling with ctrl or
    /// cmd held down, zooms around the pointer.
    handleWheel (e)
    {
        e.preventDefault();

        // deltas may be in lines or pages rather than pixels
        const unit = e.deltaMode === 1 ? 16 : (e.deltaMode === 2 ? this.container.clientHeight : 1);
        const dx = e.deltaX * unit, dy = e.deltaY * unit;

        if (e.ctrlKey || e.metaKey)
        {
            // trackpad pinches arrive as ctrl+wheel; they (and trackpad scrolls) have small deltas,
            // where a mouse wheel moves in big steps
            const p = this.getLocalPoint (e);
            const scale = Math.abs (dy) < 50 ? 0.01 : 0.0015;
            this.zoomBy (Math.exp (-dy * scale), p.x, p.y);
            return;
        }

        // shift+wheel on a mouse scrolls sideways
        this.panBy (e.shiftKey && dx === 0 ? -dy : -dx, e.shiftKey && dx === 0 ? 0 : -dy);
    }

    panBy (dx, dy)
    {
        this.transform.x += dx;
        this.transform.y += dy;
        this.applyTransform();
    }

    handlePointerDown (e)
    {
        if (e.button !== 0)
            return;

        this.drag = { startX: e.clientX, startY: e.clientY, x: this.transform.x, y: this.transform.y,
                      moved: false, pointerId: e.pointerId, target: e.target };
    }

    handlePointerMove (e)
    {
        if (! this.drag)
            return;

        const dx = e.clientX - this.drag.startX, dy = e.clientY - this.drag.startY;

        if (! this.drag.moved && Math.hypot (dx, dy) > 3)
        {
            this.drag.moved = true;
            this.container.setPointerCapture (this.drag.pointerId);
            this.container.style.cursor = "grabbing";
            this.clearHighlight();
        }

        if (this.drag.moved)
        {
            this.transform.x = this.drag.x + dx;
            this.transform.y = this.drag.y + dy;
            this.applyTransform();
        }
    }

    handlePointerUp (e)
    {
        const drag = this.drag;
        this.drag = null;
        this.container.style.cursor = "";

        if (! drag || drag.moved)
            return;

        const target = this.getInteractiveTarget (drag.target);

        if (! target)
            return;

        if (target.dataset.toggleId)
            this.toggleCollapsed (target.dataset.toggleId);
        else if (target.dataset.source)
            this.onOpenSource (target.dataset.source, e);
    }

    //==============================================================================
    // Highlighting follows the signal through boxes (any input of a box may affect any of
    // its outputs) and across graph boundaries (a boundary port has the same ID on both sides).

    indexEdges()
    {
        this.edgesFrom = new Map();
        this.edgesTo = new Map();
        this.boxPorts = new Map();      // port id -> the box node's ports, for nodes drawn as boxes

        for (const edge of this.positioned.edges)
        {
            if (! this.edgesFrom.has (edge.sourcePortId))   this.edgesFrom.set (edge.sourcePortId, []);
            if (! this.edgesTo.has (edge.targetPortId))     this.edgesTo.set (edge.targetPortId, []);

            this.edgesFrom.get (edge.sourcePortId).push (edge);
            this.edgesTo.get (edge.targetPortId).push (edge);
        }

        for (const node of this.positioned.nodes)
            if (! node.isExpandedGraph)
                for (const port of node.ports)
                    this.boxPorts.set (port.id, node.ports);
    }

    findSignalPath (startPortIDs, startEdges = [])
    {
        const edges = new Set (startEdges), ports = new Set();

        const walk = (portID, forwards) =>
        {
            const key = (forwards ? ">" : "<") + portID;

            if (ports.has (key))
                return;

            ports.add (key);

            for (const edge of (forwards ? this.edgesFrom : this.edgesTo).get (portID) ?? [])
            {
                edges.add (edge);
                walk (forwards ? edge.targetPortId : edge.sourcePortId, forwards);
            }

            // pass through a box from its inputs to its outputs (or back)
            for (const port of this.boxPorts.get (portID) ?? [])
                if (port.direction === (forwards ? "out" : "in"))
                    walk (port.id, forwards);
        };

        for (const id of startPortIDs)
        {
            walk (id, true);
            walk (id, false);
        }

        for (const edge of startEdges)
        {
            walk (edge.targetPortId, true);
            walk (edge.sourcePortId, false);
        }

        return edges;
    }

    handlePointerOver (e)
    {
        if (this.drag?.moved || ! this.positioned)
            return;

        const portElement = e.target.closest?.("[data-port-id]");
        const edgeElement = e.target.closest?.("[data-edge-id]");
        const nodeElement = e.target.closest?.(".box[data-node-id]");

        let edges = null;

        if (portElement)
            edges = this.findSignalPath ([portElement.dataset.portId]);
        else if (edgeElement)
            edges = this.findSignalPath ([], this.positioned.edges.filter (x => x.id === edgeElement.dataset.edgeId));
        else if (nodeElement)
            edges = this.findSignalPath (this.positioned.nodes.find (n => n.id === nodeElement.dataset.nodeId)?.ports.map (p => p.id) ?? []);

        if (edges)
            this.setHighlight (edges);
        else
            this.clearHighlight();
    }

    setHighlight (edges)
    {
        this.clearHighlight();

        if (edges.size === 0)
            return;

        const ids = new Set ([...edges].map (e => e.id));
        const portIDs = new Set ([...edges].flatMap (e => [e.sourcePortId, e.targetPortId]));

        this.svg.classList.add ("has-highlight");

        for (const element of this.svg.querySelectorAll ("[data-edge-id]"))
            if (ids.has (element.dataset.edgeId))
                element.classList.add ("highlighted");

        for (const element of this.svg.querySelectorAll ("[data-port-id]"))
            if (portIDs.has (element.dataset.portId))
                element.classList.add ("highlighted");
    }

    clearHighlight()
    {
        if (! this.svg?.classList.contains ("has-highlight"))
            return;

        this.svg.classList.remove ("has-highlight");

        for (const element of this.svg.querySelectorAll (".highlighted"))
            element.classList.remove ("highlighted");
    }
}
