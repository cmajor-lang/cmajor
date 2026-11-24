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

    The view is in one of three modes (viewMode):
      - "default": the graph's default view (see showDefaultView()), which the defaultView
        option chooses:
          - "fit" (the default): the whole graph, as fit() does
          - "readable": the text at its designed size (scale readableScale, default 1), anchored
            at the graph's top-left, even if the rest spills off to the right or the bottom. A
            graph that fits at that scale is fitted instead (but not enlarged beyond
            maxDefaultScale). Patch graphs tend to be wide and shallow and panels tall and thin,
            so fitting the whole thing usually makes its text unreadably small.
        A new graph starts in this mode.
      - "fitted": the whole graph (fit(), or a double-click on the background)
      - "user": the user has panned or zoomed (a drag, the wheel, or zoomBy()/panBy()), and
        the view is left where they put it.
    applyViewMode() re-applies the default or fitted view, e.g. after the container is resized,
    and leaves a "user" view alone (other than keeping it within the limits below).

    Panning is limited (see getClampedTransform()): there's nothing to find off the edges of the
    graph, so it can only be moved until its far edge (which includes the room left for any
    boundary widgets) is at the fitted view's margin, and along an axis on which it fits the
    container entirely, it doesn't move at all: it stays where the fitted view puts it (centred
    across, at the top).

    onOpenSource (location, event) is given the "file:line:column" string that
    openSourceFile() expects.

    onViewChange ({ viewMode, transform }) is called whenever the zoom, pan, mode or collapsed
    graphs change, e.g. so that a host can remember them (see getCollapsedPaths() and the
    options of setModel()).

    The drawing and the overlay option (an element of the host's, e.g. HTML controls placed at
    layout coordinates over the graph) sit in one "world" element, which carries the pan/zoom
    transform, so the two always move together. (Not an SVG <foreignObject>, which WebKit
    doesn't draw reliably under transforms.) Pointer presses, hovers and double-clicks on the
    overlay are left to it: they don't pan, fit or change the highlight. onLayout ({ positioned })
    is called after each layout, e.g. for the host to place its overlay's contents.
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
        this.onViewChange = options.onViewChange ?? (() => {});
        this.initialCollapseDepth = options.initialCollapseDepth;
        this.fitTopMargin = options.fitTopMargin ?? 20;     // e.g. to leave room for a toolbar
        this.defaultView = options.defaultView ?? "fit";
        this.readableScale = options.readableScale ?? 1;
        this.maxDefaultScale = options.maxDefaultScale ?? 1.25;
        this.activity = new ActivityOverlay();
        this.activitySource = options.activitySource ?? null;
        this.unsubscribeActivity = null;
        this.overlay = options.overlay ?? null;

        this.model = null;
        this.positioned = null;
        this.svg = null;
        this.collapsed = new Set();
        this.transform = { x: 0, y: 0, k: 1 };
        this.viewMode = "default";      // "default" | "fitted" | "user" (see above)
        this.layoutGeneration = 0;

        this.container.style.overflow = "hidden";
        this.container.style.touchAction = "none";

        // the drawing and the overlay, moved and scaled together by one transform
        this.world = container.ownerDocument.createElement ("div");
        this.world.className = "cmaj-patch-graph-world";
        Object.assign (this.world.style, { position: "absolute", left: "0", top: "0", transformOrigin: "0 0" });

        this.listeners = [];
        this.listen (container, "wheel",        (e) => this.handleWheel (e), { passive: false });
        this.listen (container, "pointerdown",  (e) => this.handlePointerDown (e));
        this.listen (container, "pointermove",  (e) => this.handlePointerMove (e));
        this.listen (container, "pointerup",    (e) => this.handlePointerUp (e));
        this.listen (container, "pointerover",  (e) => this.handlePointerOver (e));
        this.listen (container, "pointerleave", () => this.clearHighlight());
        this.listen (container, "dblclick",     (e) => { if (! this.getInteractiveTarget (e.target) && ! this.isOverlayEvent (e)) this.fit(); });
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
        this.world.replaceChildren();
        this.container.replaceChildren();
    }

    //==============================================================================
    /// Shows a new graph. By default it's shown in its default view (see showDefaultView()),
    /// with the graphs below initialCollapseDepth collapsed. Options:
    ///   collapsedPaths: the paths (which, unlike IDs, stay the same from one build to the next)
    ///                   of the graphs to collapse, instead (see getCollapsedPaths())
    ///   transform:      { x, y, k } to show it at (in "user" mode)
    ///   viewMode:       "default" or "fitted", when there's no transform
    async setModel (model, options = {})
    {
        this.viewMode = isValidTransform (options.transform) ? "user" : (options.viewMode === "fitted" ? "fitted" : "default");
        this.unsubscribeFromActivitySource();
        this.activity.clearActivity();
        this.model = model;

        if (options.collapsedPaths)
            this.collapsed = new Set (getGraphNodeIDsForPaths (model, options.collapsedPaths));
        else
            this.collapsed = new Set (this.initialCollapseDepth !== undefined
                                        ? getGraphNodeIDsBelowDepth (model, this.initialCollapseDepth) : []);

        const generation = this.layoutGeneration + 1;
        await this.relayout();

        // (another model or layout has arrived since)
        if (generation !== this.layoutGeneration)
            return;

        if (this.viewMode === "user")
        {
            this.transform = { x: options.transform.x, y: options.transform.y, k: options.transform.k };
            this.applyTransform();
        }
        else
        {
            this.applyViewMode();
        }

        this.subscribeToActivitySource();
    }

    /// Returns the paths of the graphs that are collapsed
    getCollapsedPaths()
    {
        const paths = [];

        const visit = (node) =>
        {
            if (this.collapsed.has (node.id) && typeof node.path === "string")
                paths.push (node.path);

            for (const child of node.nodes ?? [])
                visit (child);
        };

        if (this.model?.root)
            visit (this.model.root);

        return paths;
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

    /// Lays the graph out again with some changed layout options (e.g. { boundaryWidgets }),
    /// which are merged into the current ones: a null value removes that option. A view the
    /// user has moved keeps the root graph's top-left corner at the same place on screen; the
    /// default and fitted views are re-applied.
    async setLayoutOptions (changes)
    {
        this.layoutOptions = mergeLayoutOptions (this.layoutOptions, changes);
        await this.relayoutKeepingNodeInPlace (this.model?.root?.id);

        if (this.viewMode !== "user")
            this.applyViewMode();
    }

    /// Collapses or expands a graph, keeping its top-left corner (where its toggle is) at the
    /// same place on screen
    async toggleCollapsed (nodeID)
    {
        if (this.collapsed.has (nodeID))
            this.collapsed.delete (nodeID);
        else
            this.collapsed.add (nodeID);

        await this.relayoutKeepingNodeInPlace (nodeID);
    }

    async relayoutKeepingNodeInPlace (nodeID)
    {
        const findNode = () => this.positioned?.nodes.find (n => n.id === nodeID);
        const before = findNode();
        const screenX = before ? before.x * this.transform.k + this.transform.x : 0;
        const screenY = before ? before.y * this.transform.k + this.transform.y : 0;

        await this.relayout();

        const after = findNode();

        if (before && after && after !== before)
        {
            this.transform.x = screenX - after.x * this.transform.k;
            this.transform.y = screenY - after.y * this.transform.k;
            this.applyTransform();
        }
    }

    /// Collapses (or expands) every nested graph, and shows the result in the default view
    async setAllCollapsed (shouldCollapse)
    {
        this.collapsed = new Set (shouldCollapse && this.model ? getGraphNodeIDsBelowDepth (this.model, 1) : []);
        await this.relayout();
        this.showDefaultView();
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

        // drawn at its natural size, in layout units, with the world element scaling it
        const width = Math.ceil (positioned.width), height = Math.ceil (positioned.height);
        this.svg = renderSVGElement (positioned, {}, this.container.ownerDocument);
        this.svg.setAttribute ("width", String (width));
        this.svg.setAttribute ("height", String (height));
        this.svg.removeAttribute ("viewBox");
        Object.assign (this.svg.style, { position: "absolute", left: "0", top: "0", overflow: "visible" });
        Object.assign (this.world.style, { width: width + "px", height: height + "px" });
        this.world.replaceChildren (...(this.overlay ? [this.svg, this.overlay] : [this.svg]));

        if (this.world.parentNode !== this.container)
            this.container.replaceChildren (this.world);

        this.indexEdges();
        this.activity.attach (this.model, positioned, this.svg);
        this.applyTransform();
        this.onLayout ({ positioned, milliseconds: performance.now() - start });
    }

    //==============================================================================
    /// Zooms and pans so that the whole graph fits the container, and keeps it fitted (when
    /// applyViewMode() is called, e.g. after a resize)
    fit()
    {
        this.setViewMode ("fitted");
    }

    /// Shows the graph in its default view: fitted, or at a readable size (see the defaultView
    /// option), and keeps it there
    showDefaultView()
    {
        this.setViewMode ("default");
    }

    /// "default", "fitted" or "user" (see above)
    setViewMode (mode)
    {
        this.viewMode = (mode === "fitted" || mode === "user") ? mode : "default";

        if (this.viewMode === "user")
            this.onViewChange (this.getViewState());
        else
            this.applyViewMode();
    }

    /// Re-applies the default or fitted view (e.g. after the container has been resized), or
    /// does nothing in "user" mode, or while the container has no size (e.g. it's hidden)
    applyViewMode()
    {
        const space = this.getFitSpace();

        if (! space)
            return;

        // (a view the user moved stays put, but the container's new size may change its limits)
        if (this.viewMode === "user")
        {
            this.applyTransform();
            return;
        }

        if (this.viewMode === "fitted" || this.defaultView !== "readable")
        {
            this.placeAtTop (Math.min (space.fitScale, 1.5), space);
        }
        else
        {
            // as big as fits, but never smaller than readable, nor bigger than maxDefaultScale
            this.placeAtTop (Math.min (Math.max (space.fitScale, this.readableScale),
                                       Math.max (this.maxDefaultScale, this.readableScale)), space);
        }
    }

    /// What onViewChange() is told
    getViewState()
    {
        return { viewMode: this.viewMode, transform: { ...this.transform } };
    }

    /// The scale that the whole graph would need to fit in the container
    getFitScale()
    {
        return this.getFitSpace()?.fitScale;
    }

    getFitSpace()
    {
        if (! this.positioned)
            return null;

        const margin = 20;
        const width = this.container.clientWidth, height = this.container.clientHeight;

        // (a hidden container has no size to fit to)
        if (width <= 2 * margin || height <= 2 * margin)
            return null;

        const top = Math.max (margin, this.fitTopMargin);

        return { margin, width, height, top,
                 fitScale: Math.min ((width - 2 * margin) / this.positioned.width,
                                     (height - top - margin) / this.positioned.height) };
    }

    /// Puts the graph at the top of the container at scale k: centred horizontally if it's
    /// narrow enough, otherwise with its left edge (where the inputs are) at the margin. (It's at
    /// the top because the bottom of a tall container may be off the bottom of the page.)
    placeAtTop (k, { margin, width, top })
    {
        const drawnWidth = this.positioned.width * k;

        this.transform = {
            k,
            x: drawnWidth <= width - 2 * margin ? (width - drawnWidth) / 2 : margin,
            y: top
        };

        this.applyTransform();
    }

    /// Zooms around a point in the container (by default its centre), switching to "user" mode
    zoomBy (factor, centreX = this.container.clientWidth / 2, centreY = this.container.clientHeight / 2)
    {
        const { x, y, k } = this.transform;
        const newK = Math.min (Math.max (k * factor, 0.05), 8);

        this.viewMode = "user";
        this.transform = {
            k: newK,
            x: centreX - (centreX - x) * newK / k,
            y: centreY - (centreY - y) * newK / k
        };

        this.applyTransform();
    }

    /// Limits where the graph can be panned to at a transform's scale, given the container's size
    /// (see above). Returns the transform unchanged while the container has no size (e.g. it's
    /// hidden), so a saved view isn't lost.
    getClampedTransform ({ x, y, k }, space = this.getFitSpace())
    {
        if (! space || ! this.positioned)
            return { x, y, k };

        const { margin, width, height, top } = space;

        // (at the limits, the graph's near edge is at startMargin and its far edge at endMargin,
        // so where it fits, those meet at fittedPosition)
        const clampAxis = (position, drawnSize, containerSize, startMargin, endMargin, fittedPosition) =>
            drawnSize <= containerSize - startMargin - endMargin
                ? fittedPosition
                : Math.min (startMargin, Math.max (containerSize - endMargin - drawnSize, position));

        const drawnWidth = this.positioned.width * k, drawnHeight = this.positioned.height * k;

        return { k,
                 x: clampAxis (x, drawnWidth, width, margin, margin, (width - drawnWidth) / 2),
                 y: clampAxis (y, drawnHeight, height, top, margin, top) };
    }

    applyTransform()
    {
        this.transform = this.getClampedTransform (this.transform);
        const { x, y, k } = this.transform;
        this.world.style.transform = `translate(${x}px, ${y}px) scale(${k})`;
        this.onViewChange (this.getViewState());
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

    /// True if an event happened on (something inside) the overlay element
    isOverlayEvent (e)
    {
        return this.overlay !== null && (e.composedPath?.() ?? []).includes (this.overlay);
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

    /// Moves the view, switching to "user" mode
    panBy (dx, dy)
    {
        this.viewMode = "user";
        this.transform.x += dx;
        this.transform.y += dy;
        this.applyTransform();
    }

    handlePointerDown (e)
    {
        if (e.button !== 0 || this.isOverlayEvent (e))
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
            this.viewMode = "user";
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
        // (the overlay's contents set their own highlights, with highlightPorts())
        if (this.drag?.moved || ! this.positioned || this.isOverlayEvent (e))
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

    /// Highlights the signal paths through some ports, as hovering over one of them does
    highlightPorts (portIDs)
    {
        if (this.positioned)
            this.setHighlight (this.findSignalPath (portIDs));
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

//==============================================================================
function getGraphNodeIDsForPaths (model, paths)
{
    const wanted = new Set (paths), ids = [];

    const visit = (node) =>
    {
        if (node.isGraph && wanted.has (node.path))
            ids.push (node.id);

        for (const child of node.nodes ?? [])
            visit (child);
    };

    if (model?.root)
        visit (model.root);

    return ids;
}

function isValidTransform (t)
{
    return t != null && Number.isFinite (t.x) && Number.isFinite (t.y) && Number.isFinite (t.k) && t.k > 0;
}

/// Merges some changes into a set of layout options: a key whose value is null (or undefined)
/// is removed
export function mergeLayoutOptions (current, changes)
{
    const merged = { ...current };

    for (const [key, value] of Object.entries (changes ?? {}))
    {
        if (value === null || value === undefined)
            delete merged[key];
        else
            merged[key] = value;
    }

    return merged;
}
