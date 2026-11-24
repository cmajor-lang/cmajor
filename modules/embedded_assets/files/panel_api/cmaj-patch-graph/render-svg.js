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
    Draws a positioned patch graph (see layout.js) as SVG.

    The drawing is built as a small virtual tree first, which can then be turned into
    either live DOM elements (renderSVGElement) or a standalone SVG string
    (renderSVGString). The string form needs no DOM, so it also works headlessly.

    Elements carry data attributes for interaction code to hook into:
      data-node-id, data-port-id, data-edge-id, data-from-port, data-to-port,
      data-toggle-id (a graph's collapse/expand control),
      data-source (a source location, formatted by formatSourceLocation as "file:line:column"),
      data-path (the stable path of a node, endpoint or connection, from the JSON)

    The node, port and edge elements that carry a data-path are also the ones that live
    activity is shown on: PatchGraphView sets an "active" class and an --activity custom
    property (0 to 1) on them, and the styles below decide what that looks like.

    Colours and fonts come from CSS custom properties (see defaultStyles), so a host
    page can restyle it without touching this file.
*/

//==============================================================================
export const defaultStyles = `
.cmaj-patch-graph {
    --graph-background:     var(--cmaj-graph-background, #2a3331);
    --graph-fill-0:         var(--cmaj-graph-fill-0, #34403d);
    --graph-fill-1:         var(--cmaj-graph-fill-1, #3e4b47);
    --graph-fill-2:         var(--cmaj-graph-fill-2, #485652);
    --graph-stroke:         var(--cmaj-graph-stroke, #6f827c);
    --node-fill:            var(--cmaj-graph-node-fill, #1e2523);
    --node-header-fill:     var(--cmaj-graph-node-header-fill, #28312f);
    --node-stroke:          var(--cmaj-graph-node-stroke, #8a9e98);
    --text:                 var(--cmaj-graph-text, #e4ebe9);
    --text-dim:             var(--cmaj-graph-text-dim, #93a49f);
    --stream:               var(--cmaj-graph-stream, #4fc3f7);
    --value:                var(--cmaj-graph-value, #aed581);
    --event:                var(--cmaj-graph-event, #ffb74d);
    --badge-array:          var(--cmaj-graph-badge-array, #ce93d8);
    --badge-clock:          var(--cmaj-graph-badge-clock, #80cbc4);
    --badge-text:           var(--cmaj-graph-badge-text, #1b1b1b);
    --highlight:            var(--cmaj-graph-highlight, #ffffff);
    --activity-colour:      var(--cmaj-graph-activity, #fff59d);
    background: var(--graph-background);
    user-select: none;
}
.cmaj-patch-graph text           { fill: var(--text); }
.cmaj-patch-graph .dim           { fill: var(--text-dim); }
.cmaj-patch-graph .title         { font-weight: bold; }
.cmaj-patch-graph .graph-box     { stroke: var(--graph-stroke); stroke-width: 1; }
.cmaj-patch-graph .depth-0 > .graph-box { fill: var(--graph-fill-0); }
.cmaj-patch-graph .depth-1 > .graph-box { fill: var(--graph-fill-1); }
.cmaj-patch-graph .depth-2 > .graph-box,
.cmaj-patch-graph .depth-3 > .graph-box,
.cmaj-patch-graph .depth-4 > .graph-box { fill: var(--graph-fill-2); }
.cmaj-patch-graph .node-box      { fill: var(--node-fill); stroke: var(--node-stroke); stroke-width: 1; }
.cmaj-patch-graph .node-header   { fill: var(--node-header-fill); }
.cmaj-patch-graph .collapsed-graph > .node-box { stroke-width: 2; stroke-dasharray: none; }
.cmaj-patch-graph .implicit > .node-box  { stroke-dasharray: 3 2; }
.cmaj-patch-graph .array-shadow  { fill: var(--node-fill); stroke: var(--node-stroke); stroke-width: 1; opacity: 0.55; }
.cmaj-patch-graph .graph > .array-shadow { fill: var(--graph-fill-1); stroke: var(--graph-stroke); }
.cmaj-patch-graph .badge-array   { fill: var(--badge-array); }
.cmaj-patch-graph .badge-clock   { fill: var(--badge-clock); }
.cmaj-patch-graph .badge-text    { fill: var(--badge-text); font-weight: bold; }
.cmaj-patch-graph .toggle        { cursor: pointer; }
.cmaj-patch-graph .toggle rect   { fill: transparent; }
.cmaj-patch-graph .toggle:hover text { fill: var(--highlight); }
.cmaj-patch-graph .port.stream   { fill: var(--stream); }
.cmaj-patch-graph .port.value    { fill: var(--value); }
.cmaj-patch-graph .port.event    { fill: var(--event); }
.cmaj-patch-graph .edge          { fill: none; stroke-width: 1.5; stroke-linejoin: round; }
.cmaj-patch-graph .edge.stream   { stroke: var(--stream); stroke-width: 2; }
.cmaj-patch-graph .edge.value    { stroke: var(--value); }
.cmaj-patch-graph .edge.event    { stroke: var(--event); }
.cmaj-patch-graph .edge.delayed  { stroke-dasharray: 6 4; }
.cmaj-patch-graph .edge-hit      { fill: none; stroke: transparent; stroke-width: 9; }
.cmaj-patch-graph .arrow.stream  { fill: var(--stream); }
.cmaj-patch-graph .arrow.value   { fill: var(--value); }
.cmaj-patch-graph .arrow.event   { fill: var(--event); }
.cmaj-patch-graph .edge-label rect { fill: var(--graph-background); opacity: 0.85; rx: 2; }
.cmaj-patch-graph .edge-label.delay text { fill: var(--highlight); }
.cmaj-patch-graph [data-source]  { cursor: pointer; }
.cmaj-patch-graph tspan.type[data-source]:hover { text-decoration: underline; fill: var(--highlight); }
.cmaj-patch-graph.has-highlight .edge:not(.highlighted) { opacity: 0.18; }
.cmaj-patch-graph.has-highlight .edge-label:not(.highlighted) { opacity: 0.25; }
.cmaj-patch-graph .edge.highlighted { stroke-width: 3; }
.cmaj-patch-graph .port.highlighted { stroke: var(--highlight); stroke-width: 1.5; }
.cmaj-patch-graph .edge-group.active > .edge { stroke-width: calc(2px + 3px * var(--activity, 0)); }
.cmaj-patch-graph .node.active > .node-box,
.cmaj-patch-graph .node.active > .graph-box { stroke: var(--activity-colour); stroke-opacity: calc(0.3 + 0.7 * var(--activity, 0)); stroke-width: 2; }
.cmaj-patch-graph .port.active   { stroke: var(--activity-colour); stroke-opacity: var(--activity, 0); stroke-width: 2; }
`;

//==============================================================================
/// Returns the drawing as a standalone SVG document string
export function renderSVGString (positioned, options = {})
{
    return toString (buildSVGTree (positioned, { ...options, includeXMLNamespace: true }));
}

/// Returns the drawing as a live <svg> element
export function renderSVGElement (positioned, options = {}, doc = globalThis.document)
{
    return toElement (buildSVGTree (positioned, options), doc);
}

//==============================================================================
function h (tag, attrs = {}, ...children)
{
    return { tag, attrs, children: children.flat().filter (c => c !== null && c !== undefined && c !== false) };
}

const svgNamespace = "http://www.w3.org/2000/svg";

function toElement (node, doc)
{
    if (typeof node === "string")
        return doc.createTextNode (node);

    const element = doc.createElementNS (svgNamespace, node.tag);

    for (const [name, value] of Object.entries (node.attrs))
        if (value !== undefined && value !== null && value !== false)
            element.setAttribute (name, String (value));

    for (const child of node.children)
        element.appendChild (toElement (child, doc));

    return element;
}

function escapeXML (s)
{
    return String (s).replace (/&/g, "&amp;").replace (/</g, "&lt;").replace (/>/g, "&gt;").replace (/"/g, "&quot;");
}

function toString (node)
{
    if (typeof node === "string")
        return escapeXML (node);

    const attrs = Object.entries (node.attrs)
                    .filter (([, v]) => v !== undefined && v !== null && v !== false)
                    .map (([k, v]) => ` ${k}="${escapeXML (v)}"`).join ("");

    if (node.children.length === 0)
        return `<${node.tag}${attrs}/>`;

    // style content mustn't be escaped (it contains '>' selectors)
    const content = node.tag === "style" ? `<![CDATA[${node.children.join ("")}]]>`
                                         : node.children.map (toString).join ("");

    return `<${node.tag}${attrs}>${content}</${node.tag}>`;
}

//==============================================================================
const round = (v) => Math.round (v * 10) / 10;

function textWidth (text, fontSize, metrics)
{
    return Math.ceil (String (text).length * fontSize * metrics.charWidth);
}

/// An orthogonal polyline with slightly rounded corners
function createPath (points, radius = 4)
{
    if (points.length < 2)
        return "";

    let d = `M${round (points[0].x)},${round (points[0].y)}`;

    for (let i = 1; i < points.length - 1; ++i)
    {
        const prev = points[i - 1], p = points[i], next = points[i + 1];
        const lenIn = Math.hypot (p.x - prev.x, p.y - prev.y), lenOut = Math.hypot (next.x - p.x, next.y - p.y);
        const r = Math.min (radius, lenIn / 2, lenOut / 2);

        if (r < 0.5)
        {
            d += ` L${round (p.x)},${round (p.y)}`;
            continue;
        }

        const ax = p.x - (p.x - prev.x) / lenIn * r, ay = p.y - (p.y - prev.y) / lenIn * r;
        const bx = p.x + (next.x - p.x) / lenOut * r, by = p.y + (next.y - p.y) / lenOut * r;
        d += ` L${round (ax)},${round (ay)} Q${round (p.x)},${round (p.y)} ${round (bx)},${round (by)}`;
    }

    const last = points[points.length - 1];
    return d + ` L${round (last.x)},${round (last.y)}`;
}

/// Turns a { file, line, column } location from the JSON into the "file:line:column" form
/// that openSourceFile() and the VS Code extension expect (or undefined if there isn't one)
export function formatSourceLocation (location)
{
    if (! location?.file)
        return undefined;

    return location.line ? `${location.file}:${location.line}:${location.column ?? 1}` : location.file;
}

function describeEndpoint (endpoint)
{
    const array = endpoint.arraySize !== undefined ? `[${endpoint.arraySize}]` : "";
    return `${endpoint.endpointType} ${(endpoint.dataTypes ?? []).map (t => t.fullName ?? t.name).join (", ")} ${endpoint.name}${array}`;
}

/// A type's full name, plus what it resolves to if it's written using an alias
function describeDataType (dataType)
{
    const name = dataType.fullName ?? dataType.name;
    return dataType.resolvedName && dataType.resolvedName !== name ? `${name} = ${dataType.resolvedName}` : name;
}

function describeSource (location)
{
    const s = formatSourceLocation (location);
    return s ? `\n${s}` : "";
}

/// The type names next to a port label, each linking to its own declaration where it has one
function createTypeLabel (port, typeLabelSize, isBeforeName)
{
    if (! port.typeLabel)
        return null;

    return h ("tspan", { class: "dim types", "font-size": typeLabelSize },
              isBeforeName ? null : " ",
              ...port.typeParts.map (part => part.dataType
                  ? h ("tspan", { class: "type", "data-source": formatSourceLocation (part.dataType.source) },
                       h ("title", {}, describeDataType (part.dataType) + describeSource (part.dataType.source)),
                       part.text)
                  : part.text),
              isBeforeName ? " " : null);
}

//==============================================================================
function buildSVGTree (positioned, options)
{
    const m = positioned.metrics;
    const idPrefix = options.idPrefix ?? "cmaj-graph";
    const margin = 10;
    const nodesByID = new Map (positioned.nodes.map (n => [n.id, n]));
    const portsByID = new Map (positioned.nodes.flatMap (n => n.ports).map (p => [p.id, p]));

    const getEndName = (portID, end) =>
    {
        const port = portsByID.get (portID);
        const node = nodesByID.get (port.nodeId);
        const nodeIndex = end.nodeIndex !== undefined ? `[${end.nodeIndex}]` : "";
        const endpointIndex = end.endpointIndex !== undefined ? `[${end.endpointIndex}]` : "";
        const endpoint = port.endpoint.name + endpointIndex;
        return port.isBoundary ? endpoint : `${node.title}${nodeIndex}.${endpoint}`;
    };

    //==============================================================================
    const createBadges = (node) =>
    {
        let right = node.x + node.width - m.nodePadding;
        const top = node.y + m.nodePadding - 2;

        return node.badges.map (badge =>
        {
            const width = textWidth (badge.text, m.badgeSize, m) + 8;
            right -= width;
            const x = right;
            right -= 4;

            return h ("g", { class: "badge" },
                      h ("rect", { class: `badge-${badge.kind}`, x, y: top, width, height: m.badgeSize + 5, rx: 3 }),
                      h ("text", { class: "badge-text", x: x + width / 2, y: top + m.badgeSize + 1,
                                   "font-size": m.badgeSize, "text-anchor": "middle" }, badge.text));
        });
    };

    const createArrayShadows = (node, className) =>
    {
        if (node.data.arraySize === undefined)
            return [];

        return [6, 3].map (offset => h ("rect", { class: `array-shadow`, x: node.x + offset, y: node.y - offset,
                                                  width: node.width, height: node.height, rx: className === "graph-box" ? 6 : 4 }));
    };

    const createHeaderText = (node, x) =>
    {
        const titleY = node.y + m.nodePadding + m.titleSize - 2;
        const nodeSource = node.data.nodeSource ?? node.data.source;
        const processorSource = node.data.source;

        return [
            h ("text", { class: "title", x, y: titleY, "font-size": m.titleSize, "data-source": formatSourceLocation (nodeSource) },
               h ("title", {}, `${node.title}${describeSource (nodeSource)}`),
               node.title),
            node.subtitle ? h ("text", { class: "dim subtitle", x, y: titleY + m.subtitleSize + 4, "font-size": m.subtitleSize,
                                         "data-source": formatSourceLocation (processorSource) },
                               h ("title", {}, `${node.data.processorFullName}${describeSource (processorSource)}`),
                               node.subtitle)
                          : null
        ];
    };

    const createToggle = (node) =>
    {
        const size = m.titleSize + 4;
        const x = node.x + 4, y = node.y + m.nodePadding - 3;

        return h ("g", { class: "toggle", "data-toggle-id": node.id },
                  h ("title", {}, node.isCollapsed ? "Expand this graph" : "Collapse this graph"),
                  h ("rect", { x, y, width: size, height: size }),
                  h ("text", { class: "dim", x: x + size / 2, y: y + size - 4, "font-size": m.titleSize, "text-anchor": "middle" },
                     node.isCollapsed ? "▸" : "▾"));
    };

    //==============================================================================
    const graphLayers = [], boxLayers = [], portLayers = [], edgeLayers = [], labelLayers = [];

    for (const node of positioned.nodes)
    {
        if (node.isExpandedGraph)
        {
            graphLayers.push (h ("g", { class: `node graph depth-${node.depth}`, "data-node-id": node.id, "data-path": node.data.path },
                ...createArrayShadows (node, "graph-box"),
                h ("rect", { class: "graph-box", x: node.x, y: node.y, width: node.width, height: node.height, rx: 6 }),
                ...createHeaderText (node, node.x + m.nodePadding + (node.canCollapse ? m.titleSize + 2 : 0)),
                ...createBadges (node),
                node.canCollapse ? createToggle (node) : null));
        }
        else
        {
            const classes = ["node", "box", node.isGraph ? "collapsed-graph" : "", node.data.isImplicit ? "implicit" : ""].join (" ");
            const textX = node.x + m.nodePadding + (node.canCollapse ? m.titleSize + 2 : 0);

            boxLayers.push (h ("g", { class: classes, "data-node-id": node.id, "data-path": node.data.path },
                ...createArrayShadows (node, "node-box"),
                h ("rect", { class: "node-box", x: node.x, y: node.y, width: node.width, height: node.height, rx: 4 }),
                h ("rect", { class: "node-header", x: node.x + 0.5, y: node.y + 0.5, width: node.width - 1, height: node.headerHeight - 0.5, rx: 3.5 }),
                ...createHeaderText (node, textX),
                ...createBadges (node),
                node.canCollapse ? createToggle (node) : null,
                ...node.ports.map (port => h ("text", { class: "port-label", x: port.labelX, y: port.labelY, "font-size": m.portLabelSize,
                                                         "text-anchor": port.labelAnchor, "data-source": formatSourceLocation (port.endpoint.source) },
                    port.labelAnchor === "end" ? createTypeLabel (port, m.typeLabelSize, true) : null,
                    port.label,
                    port.labelAnchor === "start" ? createTypeLabel (port, m.typeLabelSize, false) : null))));
        }

        for (const port of node.ports)
        {
            portLayers.push (h ("rect", { class: `port ${port.endpointType}`, x: port.x, y: port.y, width: port.width, height: port.height,
                                          rx: 1.5, "data-port-id": port.id, "data-path": port.endpoint.path,
                                          "data-source": formatSourceLocation (port.endpoint.source) },
                                h ("title", {}, describeEndpoint (port.endpoint) + describeSource (port.endpoint.source))));

            if (port.isBoundary)
                labelLayers.push (h ("text", { class: "port-label boundary", x: port.labelX, y: port.labelY, "font-size": m.portLabelSize,
                                               "text-anchor": port.labelAnchor, "data-source": formatSourceLocation (port.endpoint.source) },
                    port.labelAnchor === "end" ? createTypeLabel (port, m.typeLabelSize, true) : null,
                    port.label,
                    port.labelAnchor === "start" ? createTypeLabel (port, m.typeLabelSize, false) : null));
        }
    }

    for (const edge of positioned.edges)
    {
        const d = createPath (edge.points);
        const source = formatSourceLocation (edge.connection.source);
        const description = `${getEndName (edge.sourcePortId, edge.connection.from)} → ${getEndName (edge.targetPortId, edge.connection.to)}`
                              + (edge.isDelayed ? `\ndelay: ${edge.connection.delay}` : "")
                              + (edge.connection.interpolation ? `\ninterpolation: ${edge.connection.interpolation}` : "")
                              + describeSource (edge.connection.source);

        const attrs = { "data-edge-id": edge.id, "data-from-port": edge.sourcePortId, "data-to-port": edge.targetPortId };

        edgeLayers.push (h ("g", { class: "edge-group", ...attrs, "data-path": edge.connection.path, "data-source": source },
            h ("title", {}, description),
            h ("path", { class: "edge-hit", d }),
            h ("path", { class: `edge ${edge.endpointType}${edge.isDelayed ? " delayed" : ""}`, d, ...attrs,
                         "marker-end": `url(#${idPrefix}-arrow-${edge.endpointType})` })));

        for (const label of edge.labels)
        {
            if (! Number.isFinite (label.x))
                continue;

            labelLayers.push (h ("g", { class: `edge-label ${label.kind}`, ...attrs },
                h ("rect", { x: label.x, y: label.y, width: label.width, height: label.height }),
                h ("text", { x: label.x + label.width / 2, y: label.y + label.height - 3.5,
                             "font-size": m.edgeLabelSize, "text-anchor": "middle" }, label.text)));
        }
    }

    //==============================================================================
    const markers = ["stream", "value", "event"].map (kind =>
        h ("marker", { id: `${idPrefix}-arrow-${kind}`, viewBox: "0 0 8 8", refX: 7, refY: 4,
                       markerWidth: 6, markerHeight: 6, markerUnits: "userSpaceOnUse", orient: "auto" },
           h ("path", { class: `arrow ${kind}`, d: "M0,0 L8,4 L0,8 z" })));

    const width = Math.ceil (positioned.width + 2 * margin), height = Math.ceil (positioned.height + 2 * margin);

    return h ("svg", {
            xmlns: options.includeXMLNamespace ? svgNamespace : undefined,
            class: "cmaj-patch-graph",
            width, height,
            viewBox: `${-margin} ${-margin} ${width} ${height}`,
            "font-family": m.fontFamily,
        },
        h ("style", {}, options.styles ?? defaultStyles),
        h ("defs", {}, ...markers),
        h ("g", { class: "viewport" },
           h ("g", { class: "layer-graphs" }, ...graphLayers),
           h ("g", { class: "layer-boxes" }, ...boxLayers),
           h ("g", { class: "layer-edges" }, ...edgeLayers),
           h ("g", { class: "layer-labels" }, ...labelLayers),
           h ("g", { class: "layer-ports" }, ...portLayers)));
}
