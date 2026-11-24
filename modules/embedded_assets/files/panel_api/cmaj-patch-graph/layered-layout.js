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
    A left-to-right layered layout of ONE level of a patch graph: a set of fixed-size
    boxes with ports at fixed offsets on their left and right sides, plus the level's own
    boundary ports (inputs down the left edge, outputs down the right edge), and the
    connections between them. It knows nothing about Cmajor: layout.js turns the JSON
    into these levels, and does nested graphs by laying out each child level first
    (bottom-up) and then treating it as a box here.

    This works because Cmajor graphs are tame: connections never cross a level of the
    hierarchy, every graph is acyclic apart from delayed connections, and the ports are on
    fixed sides in a fixed order. The steps are the classic layered ("Sugiyama") ones:

      1. Layering: longest-path layer assignment. Delayed connections that would close a
         cycle (and self-loops) are drawn as feedback loops instead of constraining layers.
      2. Ordering: connections that span several layers get a dummy point in each layer
         they pass through, then barycentre sweeps (using the real port offsets) reduce
         crossings. The order with the fewest crossings wins; ties keep declaration order.
      3. Vertical placement: each layer is placed by an exact weighted fit to its neighbours'
         port positions, subject to the order and minimum gaps (pool-adjacent-violators),
         alternating left-to-right and right-to-left passes. By default the fit minimises the
         total distance (weighted medians) rather than its square, which leaves many more
         connections exactly straight. Dummies carry more weight, so long connections tend to
         run straight too. Connections between the same two ports share one route.
      4. Routing: the gap before each layer is a channel of vertical tracks. Connections
         from the same port share a track; the others are ordered so that pairs that
         don't have to cross don't, and packed into as few tracks as will fit.

    level:
    {
        nodes:   [{ width, height, protrudeLeft, protrudeRight,   // protrude: how far the ports stick out
                    ports: [{ id, direction: "in" | "out", offsetY }] }],  // offsetY: port centre, from the node's top
        inputs:  [portID],      // the level's own input ports, top to bottom
        outputs: [portID],      // the level's own output ports, top to bottom
        edges:   [{ id, source, target, isDelayed, kind }], // port IDs: a source is a node output or a level
                                                            // input, a target is a node input or a level output.
                                                            // Edges between the same ports with different kinds
                                                            // are drawn slightly apart (see offsetRoute)
        portWidth, portHeight, portSpacing,             // size and pitch of the level's own ports
        inputSeparations, outputSeparations,            // optional: [minimum distance from each of the level's own
                                                        // ports to the next], where some need more than portSpacing
        contentTop, portTop,    // the lowest y allowed for nodes, and for the level's own ports
        leftPadding, rightPadding, bottomPadding, minWidth, minHeight
    }

    returns (all coordinates relative to the level's top-left corner):
    {
        width, height,
        nodes:   [{ x, y }],    // in the same order as level.nodes
        inputs:  [y],           // top of each input port
        outputs: [y],           // top of each output port
        routes:  Map (edge id → [{ x, y }])     // orthogonal, from the source port's face to the target's
    }
*/

export const defaultSpacing =
{
    nodeNode:           20,     // vertical gap between two boxes in a layer
    nodeDummy:          12,     // ...between a box and a connection passing through the layer
    dummyDummy:         8,      // ...between two connections passing through the layer
    track:              8,      // horizontal pitch of the vertical tracks in a channel
    minChannel:         32,     // minimum width of a channel between two layers
    minOuterChannel:    14,     // ...and of the channels next to the level's own ports
    orderingSweeps:     12,
    placementPasses:    10,
    placementNorm:      "L1",   // "L1" (weighted medians: more wires exactly straight) or "L2" (least squares)
};

export function layoutLevel (level, spacing = {})
{
    return new LevelLayout (level, { ...defaultSpacing, ...spacing }).run();
}

//==============================================================================
class LevelLayout
{
    constructor (level, spacing)
    {
        this.level = level;
        this.spacing = spacing;
    }

    run()
    {
        this.createItems();
        this.assignLayers();
        this.createChains();
        this.orderLayers();
        this.placeVertically();
        this.assignTracks();
        this.placeHorizontally();

        return {
            width: this.width,
            height: this.height,
            nodes: this.nodeItems.map (item => ({ x: item.x, y: item.y })),
            inputs: this.inputItems.map (item => item.y),
            outputs: this.outputItems.map (item => item.y),
            routes: this.createRoutes()
        };
    }

    //==============================================================================
    // Everything that gets a vertical position is an "item": a node, a dummy point where a
    // connection passes through a layer, or one of the level's own ports. A connection
    // attaches to an item at an "anchor": { item, offset }, offset being from the item's top.

    createItems()
    {
        const level = this.level;
        const createItem = (kind, height, extra) => ({ kind, height, y: 0, left: [], right: [], ...extra });

        this.nodeItems   = level.nodes.map ((node, index) => createItem ("node", node.height, { index, node }));
        this.inputItems  = level.inputs.map (() => createItem ("input", level.portHeight));
        this.outputItems = level.outputs.map (() => createItem ("output", level.portHeight));

        this.anchors = new Map();   // port id -> { item, offset, direction }

        for (const item of this.nodeItems)
            for (const port of item.node.ports)
                this.anchors.set (port.id, { item, offset: port.offsetY, direction: port.direction });

        level.inputs.forEach ((id, i) => this.anchors.set (id, { item: this.inputItems[i], offset: level.portHeight / 2, direction: "out" }));
        level.outputs.forEach ((id, i) => this.anchors.set (id, { item: this.outputItems[i], offset: level.portHeight / 2, direction: "in" }));

        // only connections from something that sends to something that receives can be drawn
        this.edges = level.edges.filter (edge =>
        {
            const from = this.anchors.get (edge.source), to = this.anchors.get (edge.target);
            return from?.direction === "out" && to?.direction === "in";
        });
    }

    //==============================================================================
    assignLayers()
    {
        const numNodes = this.nodeItems.length;
        const successors = Array.from ({ length: numNodes }, () => []);
        const predecessors = Array.from ({ length: numNodes }, () => []);

        const canReach = (from, to) =>
        {
            const visited = new Set ([from]), stack = [from];

            while (stack.length > 0)
            {
                const n = stack.pop();

                if (n === to)
                    return true;

                for (const next of successors[n])
                    if (! visited.has (next))
                    {
                        visited.add (next);
                        stack.push (next);
                    }
            }

            return false;
        };

        // Undelayed connections can't form a cycle in a valid program, so add those first,
        // then any delayed ones that don't close a cycle. The rest become feedback loops.
        this.feedbackEdges = new Set();

        const nodeEdges = this.edges.filter (e => this.anchors.get (e.source).item.kind === "node"
                                                   && this.anchors.get (e.target).item.kind === "node");

        for (const edge of [...nodeEdges.filter (e => ! e.isDelayed), ...nodeEdges.filter (e => e.isDelayed)])
        {
            const from = this.anchors.get (edge.source).item.index, to = this.anchors.get (edge.target).item.index;

            if (from === to || canReach (to, from))
            {
                this.feedbackEdges.add (edge);
            }
            else if (! successors[from].includes (to))
            {
                successors[from].push (to);
                predecessors[to].push (from);
            }
        }

        // longest-path layering, in topological order
        const order = [], numPending = predecessors.map (p => p.length);
        const ready = [];

        for (let i = 0; i < numNodes; ++i)
            if (numPending[i] === 0)
                ready.push (i);

        while (ready.length > 0)
        {
            const n = ready.shift();
            order.push (n);

            for (const next of successors[n])
                if (--numPending[next] === 0)
                    ready.push (next);
        }

        const layer = new Array (numNodes).fill (0);

        for (const n of order)
            for (const next of successors[n])
                layer[next] = Math.max (layer[next], layer[n] + 1);

        // a node with nothing feeding it sits just before the first node it feeds, rather
        // than in the first layer with a long connection running from it
        for (const n of [...order].reverse())
            if (predecessors[n].length === 0 && successors[n].length > 0)
                layer[n] = Math.min (...successors[n].map (next => layer[next])) - 1;

        this.numLayers = Math.max (1, ...layer.map (l => l + 1));
        this.layers = Array.from ({ length: this.numLayers }, () => []);

        for (const item of this.nodeItems)
        {
            item.layer = layer[item.index];
            this.layers[item.layer].push (item);
        }

        this.inputItems.forEach (item => item.layer = -1);
        this.outputItems.forEach (item => item.layer = this.numLayers);
    }

    //==============================================================================
    // Each connection becomes a chain of "hops", one per channel it crosses. Channel c is the
    // gap before layer c (channel 0 is next to the level's inputs, channel numLayers next to
    // its outputs). A normal hop joins an anchor on the channel's left to one on its right.
    // A feedback loop also has two "turn" hops, which leave and return to the same side.

    createChains()
    {
        this.hops = [];
        this.chains = new Map();    // edge id -> hops, in route order

        const createDummy = (layerIndex) =>
        {
            const item = { kind: "dummy", height: 0, y: 0, left: [], right: [], layer: layerIndex };
            this.layers[layerIndex].push (item);
            return { item, offset: 0 };
        };

        // connections of the same kind between the same two ports share one chain, and so one route
        const chainsByBundle = new Map();

        for (const edge of this.edges)
        {
            const bundle = `${edge.source}\n${edge.target}\n${edge.kind ?? edge.id}`;

            if (chainsByBundle.has (bundle))
            {
                this.chains.set (edge.id, chainsByBundle.get (bundle));
                continue;
            }

            const from = this.anchors.get (edge.source), to = this.anchors.get (edge.target);
            const hops = [];
            chainsByBundle.set (bundle, hops);

            const addHop = (channel, a, b, type) =>
            {
                const hop = { channel, from: a, to: b, type, edge };

                if (type === "forwards")        { hop.leftAnchor = a; hop.rightAnchor = b; }
                else if (type === "backwards")  { hop.leftAnchor = b; hop.rightAnchor = a; }

                hops.push (hop);
                this.hops.push (hop);
            };

            if (! this.feedbackEdges.has (edge))
            {
                const fromLayer = from.item.layer, toLayer = to.item.layer;
                let previous = from;

                for (let l = fromLayer + 1; l < toLayer; ++l)
                {
                    const dummy = createDummy (l);
                    addHop (l, previous, dummy, "forwards");
                    previous = dummy;
                }

                addHop (toLayer, previous, to, "forwards");
            }
            else
            {
                // out of the source to the right, back leftwards through the layers, then
                // round into the target from its left
                const fromLayer = from.item.layer, toLayer = to.item.layer;
                let previous = createDummy (fromLayer);
                addHop (fromLayer + 1, from, previous, "turn-left");

                for (let l = fromLayer - 1; l >= toLayer; --l)
                {
                    const dummy = createDummy (l);
                    addHop (l + 1, previous, dummy, "backwards");
                    previous = dummy;
                }

                addHop (toLayer, previous, to, "turn-right");
            }

            this.chains.set (edge.id, hops);
        }

        // each item's neighbours on either side, for ordering and placement
        for (const hop of this.hops)
        {
            if (! hop.leftAnchor)
                continue;

            const { leftAnchor: l, rightAnchor: r } = hop;
            const weight = (l.item.kind === "dummy" ? 3 : 1) * (r.item.kind === "dummy" ? 3 : 1);

            r.item.left.push  ({ own: r.offset, other: l, weight });
            l.item.right.push ({ own: l.offset, other: r, weight });
        }
    }

    //==============================================================================
    getGap (a, b)
    {
        const s = this.spacing;

        if (a.kind === "dummy" && b.kind === "dummy")   return s.dummyDummy;
        if (a.kind === "dummy" || b.kind === "dummy")   return s.nodeDummy;

        return s.nodeNode;
    }

    /// Minimum distance from the top of each item to the top of the next one
    getSeparations (items)
    {
        if (items.length > 0 && (items[0].kind === "input" || items[0].kind === "output"))
        {
            const separations = items[0].kind === "input" ? this.level.inputSeparations : this.level.outputSeparations;
            return items.slice (1).map ((_, i) => Math.max (this.level.portSpacing, separations?.[i] ?? 0));
        }

        return items.slice (1).map ((item, i) => items[i].height + this.getGap (items[i], item));
    }

    stack (items)
    {
        const separations = this.getSeparations (items);
        let y = 0;

        items.forEach ((item, i) =>
        {
            if (i > 0)
                y += separations[i - 1];

            item.y = y;
        });
    }

    static getCentre (anchor)     { return anchor.item.y + anchor.offset; }

    /// Where an item would like its top to be, judging by its neighbours on the given sides
    getDesiredPosition (item, sides)
    {
        let sum = 0, totalWeight = 0;

        for (const side of sides)
            for (const n of item[side])
            {
                sum += n.weight * (LevelLayout.getCentre (n.other) - n.own);
                totalWeight += n.weight;
            }

        return totalWeight > 0 ? { y: sum / totalWeight, weight: totalWeight } : null;
    }

    //==============================================================================
    orderLayers()
    {
        this.stack (this.inputItems);
        this.stack (this.outputItems);
        this.layers.forEach (items => this.stack (items));

        const sweep = (layerIndexes, side) =>
        {
            for (const l of layerIndexes)
            {
                const items = this.layers[l];
                const keys = new Map (items.map (item => [item, this.getDesiredPosition (item, [side])?.y ?? item.y]));

                items.sort ((a, b) => keys.get (a) - keys.get (b));     // (sort is stable)
                this.stack (items);
            }
        };

        const forwards = this.layers.map ((_, i) => i), backwards = [...forwards].reverse();
        let best = { crossings: this.countCrossings(), layers: this.layers.map (items => [...items]) };

        for (let i = 0; i < this.spacing.orderingSweeps && best.crossings > 0; ++i)
        {
            if (i % 2 === 0)
                sweep (forwards, "left");
            else
                sweep (backwards, "right");

            const crossings = this.countCrossings();

            if (crossings < best.crossings)
                best = { crossings, layers: this.layers.map (items => [...items]) };
        }

        this.layers = best.layers;
        this.layers.forEach (items => this.stack (items));
    }

    countCrossings()
    {
        const byChannel = new Map();

        for (const hop of this.hops)
        {
            if (! hop.leftAnchor)
                continue;

            if (! byChannel.has (hop.channel))
                byChannel.set (hop.channel, []);

            byChannel.get (hop.channel).push ([LevelLayout.getCentre (hop.leftAnchor), LevelLayout.getCentre (hop.rightAnchor)]);
        }

        let crossings = 0;

        for (const ends of byChannel.values())
            for (let i = 0; i < ends.length; ++i)
                for (let j = i + 1; j < ends.length; ++j)
                    if ((ends[i][0] - ends[j][0]) * (ends[i][1] - ends[j][1]) < 0)
                        ++crossings;

        return crossings;
    }

    //==============================================================================
    placeVertically()
    {
        // Each item would like its top to be where each of its neighbours' anchors lines up with
        // its own: one { y, weight } per neighbour.
        // In the final placement, a level's own port that fans out lines up with the highest thing
        // it's connected to instead, so that its wire to that runs straight and the rest branch
        // downwards. (Otherwise the ports of a big graph tend to sink to the bottom.) That isn't done
        // in the earlier passes, because its upward bias would make the whole level drift upwards
        // on every pass, leaving behind anything that isn't connected.
        const getWantedPositions = (item, sides, isFinal) =>
        {
            const wanted = sides.flatMap (side => item[side].map (n => ({ y: LevelLayout.getCentre (n.other) - n.own, weight: n.weight })));

            if (wanted.length === 0)
                return [{ y: item.y, weight: 0.001 }];

            if (isFinal && (item.kind === "input" || item.kind === "output"))
                return [{ y: Math.min (...wanted.map (w => w.y)), weight: wanted.reduce ((total, w) => total + w.weight, 0) }];

            return wanted;
        };

        const place = (items, sides, isFinal = false) =>
        {
            if (items.length === 0)
                return;

            const ys = placeInOrder (items.map (item => getWantedPositions (item, sides, isFinal)), this.getSeparations (items),
                                     this.spacing.placementNorm === "L1");
            items.forEach ((item, i) => item.y = ys[i]);
        };

        // The passes only fix where connected things are relative to each other, so they can all
        // creep up or down together from one pass to the next, while anything without connections
        // stays where it is. Moving them back after each pass stops those being left behind.
        const connectedItems = [...this.inputItems, ...this.layers.flat(), ...this.outputItems]
                                  .filter (item => item.left.length + item.right.length > 0);
        const getTop = () => Math.min (...connectedItems.map (item => item.y));
        const initialTop = getTop();

        const reanchor = () =>
        {
            const shift = initialTop - getTop();
            connectedItems.forEach (item => item.y += shift);
        };

        for (let pass = 0; pass < this.spacing.placementPasses; ++pass)
        {
            this.layers.forEach (items => place (items, ["left"]));
            place (this.outputItems, ["left"]);

            [...this.layers].reverse().forEach (items => place (items, ["right"]));
            place (this.inputItems, ["right"]);
            reanchor();
        }

        this.layers.forEach (items => place (items, ["left", "right"]));
        place (this.inputItems, ["right"], true);
        place (this.outputItems, ["left"], true);

        // move everything down to clear the header
        const level = this.level;
        const contentItems = this.layers.flat(), portItems = [...this.inputItems, ...this.outputItems];
        const minY = (items) => Math.min (...items.map (item => item.y));

        let shift = contentItems.length > 0 ? level.contentTop - minY (contentItems) : 0;

        if (portItems.length > 0)
            shift = Math.max (shift, level.portTop - minY (portItems));

        for (const item of [...contentItems, ...portItems])
            item.y += shift;

        const bottom = Math.max (level.contentTop, ...[...contentItems, ...portItems].map (item => item.y + item.height));
        this.height = Math.max (bottom + level.bottomPadding, level.minHeight);
    }

    //==============================================================================
    assignTracks()
    {
        this.channels = Array.from ({ length: this.numLayers + 1 }, () => ({ groups: [], numTracks: 0 }));

        const groupsByKey = new Map();
        const singletons = [];

        for (const hop of this.hops)
        {
            hop.track = null;

            if (! hop.leftAnchor)
            {
                this.channels[hop.channel].groups.push (this.createGroup ([hop], hop.type));
                continue;
            }

            const yL = LevelLayout.getCentre (hop.leftAnchor), yR = LevelLayout.getCentre (hop.rightAnchor);

            if (Math.abs (yL - yR) < 0.5)
                continue;   // straight across: no track needed

            // connections fanning out of the same port share a track
            if (hop.type === "forwards" && hop.leftAnchor.item.kind !== "dummy")
            {
                const key = `${hop.channel}:` + this.getAnchorID (hop.leftAnchor);

                if (! groupsByKey.has (key))
                    groupsByKey.set (key, []);

                groupsByKey.get (key).push (hop);
            }
            else
            {
                singletons.push (hop);
            }
        }

        // ...and so do connections converging on the same port, if they aren't fanning out
        const convergingByKey = new Map();

        for (const hops of groupsByKey.values())
            if (hops.length === 1)
                singletons.push (hops[0]);

        for (const hop of singletons)
        {
            const key = hop.type === "forwards" && hop.rightAnchor.item.kind !== "dummy"
                          ? `${hop.channel}:` + this.getAnchorID (hop.rightAnchor) : hop;

            if (! convergingByKey.has (key))
                convergingByKey.set (key, []);

            convergingByKey.get (key).push (hop);
        }

        for (const hops of groupsByKey.values())
            if (hops.length > 1)
                this.channels[hops[0].channel].groups.push (this.createGroup (hops, "fan-out"));

        for (const hops of convergingByKey.values())
            this.channels[hops[0].channel].groups.push (this.createGroup (hops, hops.length > 1 ? "fan-in" : "single"));

        for (const channel of this.channels)
        {
            // Left to right: loops leaving the left side, then downward connections (lowest
            // start first), then upward ones (highest start first), then loops arriving at
            // the right side. Packing in that order means two connections only cross where
            // their ends are in opposite orders anyway.
            const rank = { "turn-left": 0, down: 1, up: 2, "turn-right": 3 };

            channel.groups.sort ((a, b) =>
            {
                if (a.direction !== b.direction)  return rank[a.direction] - rank[b.direction];
                if (a.direction === "down")       return b.start - a.start;
                if (a.direction === "up")         return a.start - b.start;
                if (a.direction === "turn-left")  return (a.max - a.min) - (b.max - b.min);
                return (b.max - b.min) - (a.max - a.min);
            });

            const margin = this.spacing.track / 2;

            channel.groups.forEach ((group, i) =>
            {
                group.track = 0;

                for (const other of channel.groups.slice (0, i))
                    if (group.min < other.max + margin && other.min < group.max + margin)
                        group.track = Math.max (group.track, other.track + 1);

                channel.numTracks = Math.max (channel.numTracks, group.track + 1);

                for (const hop of group.hops)
                    hop.group = group;
            });
        }
    }

    getAnchorID (anchor)
    {
        anchor.item.id ??= `item${this.nextItemID = (this.nextItemID ?? 0) + 1}`;
        return `${anchor.item.id}@${anchor.offset}`;
    }

    createGroup (hops, kind)
    {
        const ys = hops.flatMap (h => [LevelLayout.getCentre (h.from), LevelLayout.getCentre (h.to)]);
        const group = { hops, min: Math.min (...ys), max: Math.max (...ys) };
        const mean = (values) => values.reduce ((a, b) => a + b, 0) / values.length;

        if (kind === "turn-left" || kind === "turn-right")
        {
            group.direction = kind;
            return group;
        }

        const lefts = hops.map (h => LevelLayout.getCentre (h.leftAnchor));
        const rights = hops.map (h => LevelLayout.getCentre (h.rightAnchor));

        group.start = mean (lefts);
        group.direction = mean (rights) > group.start ? "down" : "up";
        return group;
    }

    //==============================================================================
    placeHorizontally()
    {
        const level = this.level, s = this.spacing;
        const getOccupiedWidth = (item) => item.kind === "node" ? item.node.protrudeLeft + item.node.width + item.node.protrudeRight : 0;
        const layerWidths = this.layers.map (items => Math.max (0, ...items.map (getOccupiedWidth)));

        this.layerLeft = [];
        this.layerRight = [];

        let x = level.leftPadding;

        this.channels.forEach ((channel, c) =>
        {
            const isOuter = (c === 0 || c === this.numLayers);
            channel.start = x;
            channel.width = Math.max (isOuter ? s.minOuterChannel : s.minChannel, (channel.numTracks + 1) * s.track);
            x += channel.width;

            if (c < this.numLayers)
            {
                this.layerLeft[c] = x;
                x += layerWidths[c];
                this.layerRight[c] = x;
            }
        });

        // a level narrower than its header widens its last channel
        this.width = Math.max (x + level.rightPadding, level.minWidth);
        this.channels[this.numLayers].width += this.width - (x + level.rightPadding);

        for (const channel of this.channels)
        {
            const pitch = Math.min (channel.width / (channel.numTracks + 1), s.track * 1.5);
            const first = channel.start + (channel.width - pitch * (channel.numTracks - 1)) / 2;
            channel.trackX = (track) => first + track * pitch;
        }

        this.layers.forEach ((items, l) =>
        {
            for (const item of items)
                if (item.kind === "node")
                    item.x = this.layerLeft[l] + (layerWidths[l] - getOccupiedWidth (item)) / 2 + item.node.protrudeLeft;
        });
    }

    /// Where a connection meets an anchor, given which side of the channel the anchor is on
    getAnchorX (anchor, isOnLeftOfChannel)
    {
        const item = anchor.item;

        switch (item.kind)
        {
            case "input":   return this.level.portWidth;
            case "output":  return this.width - this.level.portWidth;
            case "dummy":   return isOnLeftOfChannel ? this.layerRight[item.layer] : this.layerLeft[item.layer];
            default:        return isOnLeftOfChannel ? item.x + item.node.width + item.node.protrudeRight
                                                     : item.x - item.node.protrudeLeft;
        }
    }

    createRoutes()
    {
        const routes = new Map();
        const parallelIndexes = this.getParallelIndexes();

        for (const [edgeID, hops] of this.chains)
        {
            const points = [];

            for (const hop of hops)
            {
                const fromIsLeft = hop.type === "forwards" || hop.type === "turn-left";
                const toIsLeft = hop.type === "backwards" || hop.type === "turn-left";

                const a = { x: this.getAnchorX (hop.from, fromIsLeft), y: LevelLayout.getCentre (hop.from) };
                const b = { x: this.getAnchorX (hop.to, toIsLeft),     y: LevelLayout.getCentre (hop.to) };

                if (hop.group)
                {
                    const trackX = this.channels[hop.channel].trackX (hop.group.track);
                    points.push (a, { x: trackX, y: a.y }, { x: trackX, y: b.y }, b);
                }
                else
                {
                    points.push (a, b);
                }
            }

            routes.set (edgeID, simplifyRoute (offsetRoute (simplifyRoute (points), parallelIndexes.get (edgeID) ?? 0)));
        }

        return routes;
    }

    /// Connections between the same two ports would be drawn on top of each other. Those of the
    /// same kind (edge.kind) may as well be, but each extra kind gets an index, so that it can be
    /// drawn a little apart from the others.
    getParallelIndexes()
    {
        const kindsByPorts = new Map(), indexes = new Map();

        for (const edge of this.edges)
        {
            const key = `${edge.source}\n${edge.target}`;

            if (! kindsByPorts.has (key))
                kindsByPorts.set (key, []);

            const kinds = kindsByPorts.get (key);
            const kind = edge.kind ?? edge.id;

            if (! kinds.includes (kind))
                kinds.push (kind);

            indexes.set (edge.id, kinds.indexOf (kind));
        }

        return indexes;
    }
}

//==============================================================================
/// Places items in a fixed order as close as possible to where they'd like to be, keeping each
/// at least separations[i] below the one before. wantedPositions[i] is a list of { y, weight }
/// for item i, and the fit is weighted least squares, or (if useMedian) weighted least total
/// distance. Shifting item i up by the sum of the separations before it turns this into
/// isotonic regression, which pool-adjacent-violators solves exactly in both cases.
export function placeInOrder (wantedPositions, separations, useMedian = false)
{
    const offsets = [0];

    for (const s of separations)
        offsets.push (offsets[offsets.length - 1] + s);

    const getValue = (block) => useMedian ? getWeightedMedian (block.points) : block.sum / block.weight;
    const blocks = [];

    wantedPositions.forEach ((wanted, i) =>
    {
        const points = wanted.map (w => ({ y: w.y - offsets[i], weight: w.weight }));

        blocks.push ({ weight: points.reduce ((total, p) => total + p.weight, 0),
                       sum: points.reduce ((total, p) => total + p.weight * p.y, 0),
                       count: 1, points });

        while (blocks.length > 1)
        {
            const last = blocks[blocks.length - 1], previous = blocks[blocks.length - 2];

            if (getValue (previous) <= getValue (last))
                break;

            previous.weight += last.weight;
            previous.sum += last.sum;
            previous.count += last.count;
            previous.points.push (...last.points);
            blocks.pop();
        }
    });

    const result = [];

    for (const block of blocks)
    {
        const value = getValue (block);

        for (let i = 0; i < block.count; ++i)
            result.push (value + offsets[result.length]);
    }

    return result;
}

/// The weighted median of some { y, weight } points (the lower one where two are tied)
function getWeightedMedian (points)
{
    const sorted = [...points].sort ((a, b) => a.y - b.y);
    const half = sorted.reduce ((total, p) => total + p.weight, 0) / 2;
    let total = 0;

    for (const p of sorted)
    {
        total += p.weight;

        if (total >= half)
            return p.y;
    }

    return sorted[sorted.length - 1].y;
}

/// Moves the middle of a (simplified, so horizontal at both ends) route down and to the right
/// by a few units per parallelIndex, with a short jog at each end so that it still leaves and
/// arrives at the same points
function offsetRoute (points, parallelIndex)
{
    if (parallelIndex === 0 || points.length < 2)
        return points;

    const offset = 4 * parallelIndex, jog = 8;
    const first = points[0], last = points[points.length - 1];

    return [first,
            { x: first.x + jog, y: first.y },
            { x: first.x + jog, y: first.y + offset },
            ...points.slice (1, -1).map (p => ({ x: p.x + offset, y: p.y + offset })),
            { x: last.x - jog, y: last.y + offset },
            { x: last.x - jog, y: last.y },
            last];
}

/// Removes repeated points, and points in the middle of a straight run
function simplifyRoute (points)
{
    const result = [];
    const same = (a, b) => Math.abs (a - b) < 0.01;

    for (const p of points)
    {
        const last = result[result.length - 1];

        if (last && same (last.x, p.x) && same (last.y, p.y))
            continue;

        const beforeLast = result[result.length - 2];

        if (beforeLast && ((same (beforeLast.x, last.x) && same (last.x, p.x))
                            || (same (beforeLast.y, last.y) && same (last.y, p.y))))
            result.pop();

        result.push (p);
    }

    return result;
}
