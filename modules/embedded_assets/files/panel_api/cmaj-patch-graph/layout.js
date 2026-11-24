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

import { layoutLevel } from "./layered-layout.js";

/*
    Lays out a patch structure description (the JSON produced by the "graph"
    code generation target) and returns a "positioned model" that a renderer can draw
    without knowing anything about the layout engine.

    A layout engine is any object with this shape:

        { name, async layout (model, options) => PositionedGraph }

    createLayeredLayoutEngine() is the built-in one: this file works out the sizes of boxes,
    ports and labels, and layered-layout.js positions them, one graph level at a time,
    bottom-up. Nothing else depends on how it works, so a different engine can be swapped in.

    PositionedGraph (all coordinates are absolute, in SVG user units):
    {
        width, height,
        metrics,                    // sizes and fonts the layout assumed for text
        nodes: [PositionedNode],    // parents always come before their children
        edges: [PositionedEdge]
    }

    PositionedNode:
    {
        id, parentId, depth, data,  // data = the node object from the JSON
        isGraph, isCollapsed,       // isCollapsed: a graph drawn as a box, without its contents
        isExpandedGraph,            // a graph drawn as a container of its child nodes
        canCollapse,                // a nested graph that has child nodes to show or hide
        x, y, width, height, headerHeight,
        title, subtitle,            // header text lines (subtitle may be empty)
        badges: [{ kind: "array" | "clock", text }],
        ports: [PositionedPort]
    }

    PositionedPort:
    {
        id, nodeId, direction: "in" | "out", endpoint (from the JSON),
        endpointType, isBoundary,   // isBoundary: an expanded graph's own endpoint
        x, y, width, height,        // the little square drawn on the node's border
        label, typeLabel,           // text to draw next to it
        typeParts: [{ text, dataType }],    // typeLabel split up, so each data type can link to its own
                                    // declaration (dataType is null for the separators between them)
        labelX, labelY, labelAnchor // where the label goes ("start" | "end")
    }

    PositionedEdge:
    {
        id, ownerId, connection (from the JSON),
        sourcePortId, targetPortId, endpointType,
        isDelayed, points: [{x, y}],
        labels: [{ kind: "delay" | "index", text, x, y, width, height }]
    }
*/

//==============================================================================
export const defaultMetrics =
{
    fontFamily:     "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace",
    charWidth:      0.6,     // width of a monospace character, as a fraction of its font size
    titleSize:      12,
    subtitleSize:   10,
    portLabelSize:  10,
    typeLabelSize:  9,
    edgeLabelSize:  9,
    badgeSize:      9,
    rowHeight:      16,
    portWidth:      7,
    portHeight:     9,
    nodePadding:    8,
    graphPadding:   14,
    maxTypeLabelLength: 22,
};

/// Width of some text in the (monospace) font that the layout assumes
export function measureText (text, fontSize, metrics = defaultMetrics)
{
    return Math.ceil (String (text).length * fontSize * metrics.charWidth);
}

/// Returns the IDs of all graph nodes at or below the given nesting depth (the root is
/// depth 0). Passing these as collapsedNodeIDs is a good default for big patches.
export function getGraphNodeIDsBelowDepth (model, depth)
{
    const ids = [];

    const visit = (node, d) =>
    {
        if (node.isGraph && d >= depth)
            ids.push (node.id);

        for (const child of node.nodes ?? [])
            visit (child, d + 1);
    };

    visit (model.root, 0);
    return ids;
}

//==============================================================================
/// The built-in layout engine (see layered-layout.js). options.spacing can override
/// any of the layered layout's defaultSpacing values.
export function createLayeredLayoutEngine (engineOptions = {})
{
    return {
        name: "layered",

        async layout (model, options = {})
        {
            return layoutPatchGraph (model, { ...engineOptions, ...options });
        }
    };
}

/// Lays out a model synchronously, returning a PositionedGraph
export function layoutPatchGraph (model, options = {})
{
    return new PatchGraphLayout (model, options).run();
}

//==============================================================================
class PatchGraphLayout
{
    constructor (model, options)
    {
        if (model?.format !== "cmajor-patch-graph")
            throw new Error ("Expected a cmajor-patch-graph structure");

        this.model = model;
        this.metrics = { ...defaultMetrics, ...(options.metrics ?? {}) };
        this.spacing = options.spacing ?? {};
        this.collapsed = new Set (options.collapsedNodeIDs ?? []);
        this.showTypes = options.showTypes ?? true;

        this.nodes = [];            // PositionedNodes, parents first
        this.ports = new Map();     // id -> PositionedPort
        this.edges = [];            // PositionedEdges
        this.nextEdgeID = 0;
    }

    run()
    {
        const margin = 10;
        const root = this.layoutNode (this.model.root, null, 0);
        root.place (margin, margin);
        this.combineIndexLabels();

        return {
            width: root.width + 2 * margin,
            height: root.height + 2 * margin,
            metrics: this.metrics,
            nodes: this.nodes,
            edges: this.edges.filter (e => e.points.length > 1)
        };
    }

    //==============================================================================
    static getPortID (nodeID, direction, endpointName)     { return `${nodeID}:${direction}:${endpointName}`; }

    getTitle (node)
    {
        return node.name;
    }

    getSubtitle (node)
    {
        if (node.isImplicit || node.name === node.processor)
            return node.processorFullName !== node.name ? node.processorFullName : "";

        return (node.isGraph ? "graph " : "") + node.processorFullName;
    }

    getBadges (node)
    {
        const badges = [];

        if (node.arraySize !== undefined)
            badges.push ({ kind: "array", text: `×${node.arraySize}` });

        if (node.clockMultiplier !== undefined)
            badges.push ({ kind: "clock", text: `rate ×${node.clockMultiplier}` });

        if (node.clockDivider !== undefined)
            badges.push ({ kind: "clock", text: `rate ÷${node.clockDivider}` });

        return badges;
    }

    getPortLabel (endpoint)
    {
        return endpoint.arraySize !== undefined ? `${endpoint.name}[${endpoint.arraySize}]` : endpoint.name;
    }

    /// The endpoint's data types as separate pieces of text, so that each one can link to
    /// its own declaration. The whole list is shortened to maxTypeLabelLength characters.
    getTypeParts (endpoint)
    {
        if (! this.showTypes)
            return [];

        const max = this.metrics.maxTypeLabelLength;
        const parts = [];
        let length = 0;

        const add = (text, dataType) =>
        {
            if (length + text.length <= max)
            {
                parts.push ({ text, dataType });
                length += text.length;
                return true;
            }

            const remaining = max - 1 - length;

            if (remaining > 0)
                parts.push ({ text: text.substring (0, remaining) + "…", dataType });
            else if (parts.length > 0)
                parts[parts.length - 1].text += "…";

            return false;
        };

        for (const [index, dataType] of (endpoint.dataTypes ?? []).entries())
            if ((index > 0 && ! add (", ", null)) || ! add (dataType.name, dataType))
                break;

        return parts;
    }

    getTypeLabel (endpoint)
    {
        return this.getTypeParts (endpoint).map (p => p.text).join ("");
    }

    getHeaderSize (title, subtitle, badges, hasToggle)
    {
        const m = this.metrics;
        const toggleWidth = hasToggle ? m.titleSize + 2 : 0;
        const badgeWidth = badges.reduce ((w, b) => w + measureText (b.text, m.badgeSize, m) + 12, 0);
        const width = Math.max (measureText (title, m.titleSize, m) + (badgeWidth > 0 ? badgeWidth + 8 : 0),
                                measureText (subtitle, m.subtitleSize, m));
        const height = m.nodePadding + m.titleSize + (subtitle ? m.subtitleSize + 4 : 0) + m.nodePadding;

        return { width: width + toggleWidth + 2 * m.nodePadding, height };
    }

    getRowLabelWidth (endpoints)
    {
        const m = this.metrics;
        let width = 0;

        for (const e of endpoints)
        {
            const type = this.getTypeLabel (e);
            width = Math.max (width, measureText (this.getPortLabel (e), m.portLabelSize, m)
                                       + (type ? 6 + measureText (type, m.typeLabelSize, m) : 0));
        }

        return width;
    }

    createPort (node, endpoint, direction, isBoundary)
    {
        const m = this.metrics;

        const port = {
            id: PatchGraphLayout.getPortID (node.id, direction, endpoint.name),
            nodeId: node.id, direction, endpoint,
            endpointType: endpoint.endpointType,
            isBoundary,
            label: this.getPortLabel (endpoint),
            typeLabel: this.getTypeLabel (endpoint),
            typeParts: this.getTypeParts (endpoint),
            labelAnchor: direction === "in" ? "start" : "end",
            width: m.portWidth,
            height: m.portHeight,
        };

        this.ports.set (port.id, port);
        return port;
    }

    //==============================================================================
    // layoutNode() works out a node's size and its ports' positions relative to its top-left
    // corner (laying out a nested graph's contents first), and returns:
    //   { width, height, protrudeLeft, protrudeRight, ports: [{ id, direction, offsetY }], place (x, y) }
    // place() then fills in the absolute coordinates of it and everything inside it.

    layoutNode (node, parentId, depth)
    {
        const hasChildNodes = (node.nodes?.length ?? 0) > 0;
        const isExpandedGraph = node.isGraph && hasChildNodes && ! this.collapsed.has (node.id);

        const info = {
            id: node.id,
            parentId,
            depth,
            data: node,
            isGraph: node.isGraph,
            canCollapse: node.isGraph && hasChildNodes && depth > 0,
            isCollapsed: node.isGraph && hasChildNodes && ! isExpandedGraph,
            isExpandedGraph,
            title: this.getTitle (node),
            subtitle: this.getSubtitle (node),
            badges: this.getBadges (node),
            ports: []
        };

        return isExpandedGraph ? this.layoutGraphNode (node, info, depth)
                               : this.layoutBoxNode (node, info);
    }

    /// A processor, or a collapsed graph: a fixed-size box with a row per endpoint, and its
    /// ports sticking out of its sides
    layoutBoxNode (node, info)
    {
        const m = this.metrics;
        const header = this.getHeaderSize (info.title, info.subtitle, info.badges, info.canCollapse);
        const inputs = node.inputs ?? [], outputs = node.outputs ?? [];
        const numRows = Math.max (inputs.length, outputs.length);
        const inWidth = this.getRowLabelWidth (inputs), outWidth = this.getRowLabelWidth (outputs);
        const rowsWidth = 2 * m.nodePadding + inWidth + outWidth + (inWidth > 0 && outWidth > 0 ? 20 : 0);

        const width = Math.max (header.width, rowsWidth, 60);
        const height = header.height + numRows * m.rowHeight + (numRows > 0 ? m.nodePadding / 2 : 0);

        info.headerHeight = header.height;

        const createPorts = (endpoints, direction) => endpoints.map ((endpoint, index) =>
        {
            const port = this.createPort (node, endpoint, direction, false);
            port.relX = direction === "in" ? -m.portWidth : width;
            port.relY = header.height + index * m.rowHeight + m.rowHeight / 2 - m.portHeight / 2;
            info.ports.push (port);
            return port;
        });

        const ports = [...createPorts (inputs, "in"), ...createPorts (outputs, "out")];

        return {
            width,
            height,
            protrudeLeft: inputs.length > 0 ? m.portWidth : 0,
            protrudeRight: outputs.length > 0 ? m.portWidth : 0,
            ports: ports.map (p => ({ id: p.id, direction: p.direction, offsetY: p.relY + m.portHeight / 2 })),

            place: (x, y) =>
            {
                Object.assign (info, { x, y, width, height });
                this.nodes.push (info);

                for (const port of ports)
                {
                    port.x = x + port.relX;
                    port.y = y + port.relY;
                    delete port.relX;
                    delete port.relY;
                    this.placePortLabel (info, port);
                }
            }
        };
    }

    /// An expanded graph: a container whose own endpoints are ports just inside its left
    /// and right edges, with the graph's nodes and connections laid out inside it
    layoutGraphNode (node, info, depth)
    {
        const m = this.metrics;
        const header = this.getHeaderSize (info.title, info.subtitle, info.badges, depth > 0);
        const inputs = node.inputs ?? [], outputs = node.outputs ?? [];

        info.headerHeight = header.height;

        const inputPorts  = inputs.map (endpoint => this.createPort (node, endpoint, "in", true));
        const outputPorts = outputs.map (endpoint => this.createPort (node, endpoint, "out", true));
        info.ports.push (...inputPorts, ...outputPorts);

        // the ports (ours and our children's) must all exist before the edges are created
        const children = (node.nodes ?? []).map (child => this.layoutNode (child, node.id, depth + 1));
        const edges = (node.connections ?? []).map (c => this.createEdge (node, c)).filter (e => e !== null);

        // Boundary labels sit just above the wire that leaves each port, inside the box,
        // so the box needs that much padding on each side
        const leftPadding  = m.graphPadding + this.getRowLabelWidth (inputs);
        const rightPadding = m.graphPadding + this.getRowLabelWidth (outputs);

        const result = layoutLevel ({
            nodes: children,
            inputs: inputPorts.map (p => p.id),
            outputs: outputPorts.map (p => p.id),
            // connections between the same two ports that differ only in their indexes are drawn
            // as one wire, but ones that differ in some other way (e.g. a delay) are drawn apart
            edges: edges.map (e => ({ id: e.id, source: e.sourcePortId, target: e.targetPortId, isDelayed: e.isDelayed,
                                      kind: `${e.connection.delay ?? ""}/${e.connection.interpolation ?? ""}` })),
            portWidth: m.portWidth,
            portHeight: m.portHeight,
            portSpacing: m.rowHeight,
            contentTop: header.height + m.graphPadding,
            portTop: header.height + 6,
            leftPadding,
            rightPadding,
            bottomPadding: m.graphPadding,
            minWidth: header.width + leftPadding + rightPadding,
            minHeight: header.height + m.rowHeight,
        }, this.spacing);

        const { width, height } = result;

        return {
            width,
            height,
            protrudeLeft: 0,
            protrudeRight: 0,
            ports: [...inputPorts.map  ((p, i) => ({ id: p.id, direction: "in",  offsetY: result.inputs[i]  + m.portHeight / 2 })),
                    ...outputPorts.map ((p, i) => ({ id: p.id, direction: "out", offsetY: result.outputs[i] + m.portHeight / 2 }))],

            place: (x, y) =>
            {
                Object.assign (info, { x, y, width, height });
                this.nodes.push (info);

                inputPorts.forEach ((port, i)  => Object.assign (port, { x, y: y + result.inputs[i] }));
                outputPorts.forEach ((port, i) => Object.assign (port, { x: x + width - m.portWidth, y: y + result.outputs[i] }));
                info.ports.forEach (port => this.placePortLabel (info, port));

                children.forEach ((child, i) => child.place (x + result.nodes[i].x, y + result.nodes[i].y));

                for (const edge of edges)
                {
                    edge.points = (result.routes.get (edge.id) ?? []).map (p => ({ x: x + p.x, y: y + p.y }));
                    this.placeEdgeLabels (edge);
                }
            }
        };
    }

    //==============================================================================
    createEdge (graph, connection)
    {
        const m = this.metrics;

        // a "node": null end refers to the graph's own endpoint: its inputs act as sources
        // inside the graph, and its outputs as destinations
        const sourcePortId = connection.from.node === null
                                ? PatchGraphLayout.getPortID (graph.id, "in", connection.from.endpoint)
                                : PatchGraphLayout.getPortID (connection.from.node, "out", connection.from.endpoint);

        const targetPortId = connection.to.node === null
                                ? PatchGraphLayout.getPortID (graph.id, "out", connection.to.endpoint)
                                : PatchGraphLayout.getPortID (connection.to.node, "in", connection.to.endpoint);

        const sourcePort = this.ports.get (sourcePortId);

        if (! sourcePort || ! this.ports.get (targetPortId))
        {
            console.warn ("cmaj-patch-graph: skipping connection with unknown endpoint", connection);
            return null;
        }

        const labels = [];
        const addLabel = (kind, text, end) => labels.push ({ kind, end, text, width: measureText (text, m.edgeLabelSize, m) + 4,
                                                             height: m.edgeLabelSize + 4 });

        const isDelayed = connection.delay !== undefined;

        if (isDelayed || connection.interpolation)
        {
            const parts = [];

            if (isDelayed)
                parts.push (formatDelay (connection.delay));

            if (connection.interpolation)
                parts.push (connection.interpolation);

            addLabel ("delay", parts.join (" "));
        }

        // kept very short because several of these often converge on one port: "#2" is
        // instance 2 of a node array, "[2]" is element 2 of an endpoint array (the full
        // indexed names are in the connection's tooltip)
        const formatIndex = (end) =>
        {
            let s = "";

            if (end.nodeIndex !== undefined)       s += `#${end.nodeIndex}`;
            if (end.endpointIndex !== undefined)   s += `[${end.endpointIndex}]`;

            return s;
        };

        if (hasIndex (connection.from))
            addLabel ("index", formatIndex (connection.from), "tail");

        if (hasIndex (connection.to))
            addLabel ("index", formatIndex (connection.to), "head");

        for (const label of labels)
            if (label.kind === "index")
                label.indexEnd = label.end === "tail" ? connection.from : connection.to;

        const edge = {
            id: `e${this.nextEdgeID++}`,
            ownerId: graph.id,
            connection,
            sourcePortId,
            targetPortId,
            endpointType: sourcePort.endpointType,
            isDelayed,
            points: [],
            labels
        };

        this.edges.push (edge);
        return edge;
    }

    /// Puts a delay label just below the middle of the wire's longest horizontal run, and index
    /// labels just above it near the ends, but past the first and before the last bend, where
    /// wires that share a port have separated
    placeEdgeLabels (edge)
    {
        const points = edge.points;

        if (points.length < 2)
            return;

        const segments = points.slice (1).map ((p, i) => ({ a: points[i], b: p }))
                                         .filter (s => Math.abs (s.a.y - s.b.y) < 0.5);

        for (const label of edge.labels)
        {
            const above = (x, y) => Object.assign (label, { x, y: y - label.height - 1 });

            if (label.kind === "delay")
            {
                if (segments.length === 0)
                    continue;

                // (below the wire, because a graph's own port labels sit above the wires leaving them)
                const longest = segments.reduce ((best, s) => Math.abs (s.b.x - s.a.x) > Math.abs (best.b.x - best.a.x) ? s : best);
                Object.assign (label, { x: (longest.a.x + longest.b.x - label.width) / 2, y: longest.a.y + 2 });
            }
            else
            {
                // routes alternate horizontal and vertical runs, starting and ending horizontally
                const n = points.length;
                const [from, to] = label.end === "tail" ? (n > 3 ? [points[2], points[3]] : [points[0], points[1]])
                                                        : (n > 3 ? [points[n - 3], points[n - 4]] : [points[n - 1], points[n - 2]]);

                above (to.x >= from.x ? from.x + 3 : from.x - label.width - 3, from.y);
            }
        }
    }

    /// Where several indexed connections meet the same port (e.g. every element of a node
    /// array feeding one input), their index labels would pile up, so they're replaced by one
    /// label next to the port that lists them all, e.g. "#0–7". (Each connection's own
    /// indexes are still in its tooltip.)
    combineIndexLabels()
    {
        const groups = new Map();   // "portID:end" -> [{ edge, label }]

        for (const edge of this.edges)
            for (const label of edge.labels)
                if (label.kind === "index")
                {
                    const key = `${label.end === "tail" ? edge.sourcePortId : edge.targetPortId}:${label.end}`;

                    if (! groups.has (key))
                        groups.set (key, []);

                    groups.get (key).push ({ edge, label });
                }

        const m = this.metrics;

        for (const items of groups.values())
        {
            if (items.length < 2)
                continue;

            const { edge, label } = items[0];
            const isTail = label.end === "tail";
            const port = this.ports.get (isTail ? edge.sourcePortId : edge.targetPortId);

            label.text = formatIndexList (items.map (i => i.label.indexEnd));
            label.width = measureText (label.text, m.edgeLabelSize, m) + 4;
            label.x = isTail ? port.x + port.width + 3 : port.x - label.width - 3;

            // a graph's own ports have their names above the wire, so the label goes below it
            const centreY = port.y + port.height / 2;
            label.y = port.isBoundary ? centreY + 2 : centreY - label.height - 1;

            for (const other of items.slice (1))
                other.edge.labels = other.edge.labels.filter (l => l !== other.label);
        }
    }

    placePortLabel (node, port)
    {
        const m = this.metrics;
        const isInput = port.direction === "in";
        const centreY = port.y + port.height / 2;

        if (port.isBoundary)
        {
            // inside the graph box, sitting just above the wire that leaves the port
            port.labelX = isInput ? port.x + port.width + 4 : port.x - 4;
            port.labelY = centreY - 3;
        }
        else
        {
            port.labelX = isInput ? node.x + m.nodePadding : node.x + node.width - m.nodePadding;
            port.labelY = centreY + m.portLabelSize * 0.35;
        }
    }
}

function hasIndex (end)
{
    return end.nodeIndex !== undefined || end.endpointIndex !== undefined;
}

/// Formats the indexes of several connection ends compactly, e.g. "#0–3,6" or "#0–3[1]"
function formatIndexList (ends)
{
    const formatRanges = (values) =>
    {
        const sorted = [...new Set (values)].sort ((a, b) => a - b);
        const ranges = [];

        for (const v of sorted)
        {
            const last = ranges[ranges.length - 1];

            if (last && v === last[1] + 1)
                last[1] = v;
            else
                ranges.push ([v, v]);
        }

        return ranges.map (([a, b]) => a === b ? `${a}` : `${a}${b === a + 1 ? "," : "–"}${b}`).join (",");
    };

    const nodeIndexes = ends.map (e => e.nodeIndex).filter (i => i !== undefined);
    const endpointIndexes = ends.map (e => e.endpointIndex).filter (i => i !== undefined);

    return (nodeIndexes.length > 0 ? `#${formatRanges (nodeIndexes)}` : "")
         + (endpointIndexes.length > 0 ? `[${formatRanges (endpointIndexes)}]` : "");
}

/// Formats a delay length like a z-transform delay, e.g. z⁻¹⁰
export function formatDelay (delay)
{
    if (typeof delay !== "number")
        return `delay ${delay}`;

    const superscripts = { "0": "⁰", "1": "¹", "2": "²", "3": "³", "4": "⁴", "5": "⁵", "6": "⁶", "7": "⁷", "8": "⁸", "9": "⁹", ".": "·" };
    return "z⁻" + String (delay).split ("").map (c => superscripts[c] ?? c).join ("");
}
