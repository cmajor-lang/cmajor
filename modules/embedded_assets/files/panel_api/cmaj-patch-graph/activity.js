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
    Support for showing live activity (signal levels, event traffic) on a drawn graph.

    Activity is reported per *path*: the stable names the JSON gives to every node,
    endpoint and connection (built by cmaj::graph_paths on the C++ side, e.g. "voices.osc",
    "voices.osc.out", "voices.osc.out -> mix.in"). A level is a number from 0 (idle) to 1.

    The drawing is never re-rendered for activity. ActivityOverlay finds the existing
    elements for each path and sets an "active" class and an --activity custom property
    on them, batched into one update per animation frame. What that looks like is up to
    the stylesheet (see defaultStyles in render-svg.js).

    Things that aren't drawn (because they're inside a collapsed graph) show their
    activity on the collapsed graph's box instead, and per-element paths like
    "voices[2].out" fall back to the whole array ("voices.out"). When several paths share
    one element, it shows the loudest of them.

    An activity source, which a PatchGraphView can be given, is anything with:

        subscribe (paths, callback) → unsubscribe function

    where paths is every path in the model (see getActivityPaths), and the source calls
    callback (updates) whenever it has new levels. updates can be a Map, an array of
    [path, level] pairs or a plain object of path → level, and a level of 0, null or
    undefined means the path has gone idle.
*/

//==============================================================================
/// Returns every node, endpoint and connection path in a "graph" model
export function getActivityPaths (model)
{
    const paths = new Set();

    const visit = (node) =>
    {
        paths.add (node.path);

        for (const endpoint of [...(node.inputs ?? []), ...(node.outputs ?? [])])
            paths.add (endpoint.path);

        for (const connection of node.connections ?? [])
        {
            paths.add (connection.path);
            paths.add (connection.from.path);
            paths.add (connection.to.path);
        }

        for (const child of node.nodes ?? [])
            visit (child);
    };

    if (model?.root)
        visit (model.root);

    paths.delete (undefined);
    return [...paths];
}

/// Converts the forms that an activity update can take into [path, level] pairs
function getUpdateEntries (updates)
{
    if (updates instanceof Map || Array.isArray (updates))
        return [...updates];

    return Object.entries (updates ?? {});
}

/// Removes the [n] indexes from a path, so "voices[2].out[1]" becomes "voices.out"
function removeIndexes (path)
{
    return path.replace (/\[\d+\]/g, "");
}

//==============================================================================
export class ActivityOverlay
{
    constructor (requestFrame = globalThis.requestAnimationFrame?.bind (globalThis))
    {
        this.requestFrame = requestFrame ?? ((fn) => setTimeout (fn, 16));
        this.levels = new Map();            // path -> level, for every path that's currently active
        this.elementsForPath = new Map();   // path -> [element]
        this.pathsForElement = new Map();   // element -> Set of paths
        this.dirtyPaths = new Set();
        this.frameRequested = false;
    }

    /// Finds the elements for every path in a newly drawn graph. Call this after each
    /// render: levels are kept, and are re-applied to the new elements.
    attach (model, positioned, svg)
    {
        this.elementsForPath.clear();
        this.pathsForElement.clear();

        if (! model?.root || ! positioned || ! svg)
            return;

        const nodeElements = new Map ([...svg.querySelectorAll (".node[data-node-id]")].map (e => [e.dataset.nodeId, e]));
        const portElements = new Map ([...svg.querySelectorAll (".port[data-port-id]")].map (e => [e.dataset.portId, e]));
        const edgeElements = new Map ([...svg.querySelectorAll (".edge-group[data-edge-id]")].map (e => [e.dataset.edgeId, e]));

        const add = (path, element) =>
        {
            if (path === undefined || ! element)
                return;

            if (! this.elementsForPath.has (path))
                this.elementsForPath.set (path, []);

            const list = this.elementsForPath.get (path);

            if (! list.includes (element))
                list.push (element);

            if (! this.pathsForElement.has (element))
                this.pathsForElement.set (element, new Set());

            this.pathsForElement.get (element).add (path);
        };

        for (const node of positioned.nodes)
        {
            add (node.data.path, nodeElements.get (node.id));

            for (const port of node.ports)
                add (port.endpoint.path, portElements.get (port.id));
        }

        for (const edge of positioned.edges)
            add (edge.connection.path, edgeElements.get (edge.id));

        // everything inside a collapsed graph shows its activity on the collapsed box
        const drawnNodes = new Map (positioned.nodes.map (n => [n.id, n]));

        const addHidden = (node, element) =>
        {
            add (node.path, element);

            for (const endpoint of [...(node.inputs ?? []), ...(node.outputs ?? [])])
                add (endpoint.path, element);

            for (const connection of node.connections ?? [])
                add (connection.path, element);

            for (const child of node.nodes ?? [])
                addHidden (child, element);
        };

        const visit = (node) =>
        {
            const drawn = drawnNodes.get (node.id);

            if (drawn?.isCollapsed)
            {
                const element = nodeElements.get (node.id);

                for (const connection of node.connections ?? [])
                    add (connection.path, element);

                for (const child of node.nodes ?? [])
                    addHidden (child, element);
            }
            else
            {
                for (const child of node.nodes ?? [])
                    visit (child);
            }
        };

        visit (model.root);

        for (const path of this.levels.keys())
            this.dirtyPaths.add (path);

        this.flush();
    }

    /// Returns the elements a path is shown on (which may be an ancestor's, see above)
    getElements (path)
    {
        const elements = this.elementsForPath.get (path);

        if (elements)
            return elements;

        // a per-element path like "voices[2].out" is shown on the whole array's element, so
        // remember it as one of that element's paths, for working out the loudest
        const fallback = this.elementsForPath.get (removeIndexes (path));

        if (! fallback)
            return [];

        this.elementsForPath.set (path, fallback);

        for (const element of fallback)
            this.pathsForElement.get (element)?.add (path);

        return fallback;
    }

    /// Merges in new levels for some paths. The drawing is updated on the next frame.
    setActivity (updates)
    {
        for (const [path, level] of getUpdateEntries (updates))
        {
            const value = Number (level);

            if (Number.isFinite (value) && value > 0)
                this.levels.set (path, Math.min (value, 1));
            else
                this.levels.delete (path);

            this.dirtyPaths.add (path);
        }

        this.scheduleFlush();
    }

    /// Makes everything idle
    clearActivity()
    {
        for (const path of this.levels.keys())
            this.dirtyPaths.add (path);

        this.levels.clear();
        this.scheduleFlush();
    }

    scheduleFlush()
    {
        if (this.frameRequested || this.dirtyPaths.size === 0)
            return;

        this.frameRequested = true;

        this.requestFrame (() =>
        {
            this.frameRequested = false;
            this.flush();
        });
    }

    /// Applies any pending changes to the drawing straight away
    flush()
    {
        const elements = new Set();

        for (const path of this.dirtyPaths)
            for (const element of this.getElements (path))
                elements.add (element);

        this.dirtyPaths.clear();

        for (const element of elements)
        {
            let level = 0;

            for (const path of this.pathsForElement.get (element) ?? [])
                level = Math.max (level, this.levels.get (path) ?? 0);

            if (level > 0)
            {
                element.classList.add ("active");
                element.style.setProperty ("--activity", String (Math.round (level * 1000) / 1000));
            }
            else
            {
                element.classList.remove ("active");
                element.style.removeProperty ("--activity");
            }
        }
    }
}
