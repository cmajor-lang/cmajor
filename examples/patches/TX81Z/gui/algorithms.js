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
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

/*
    The chart of the eight algorithms, in the style printed on the panels of 4-operator FM synths.
    Clicking a diagram selects that algorithm. The connections here must match the
    AlgorithmMatrix processor in TX81Z.cmajor.
*/

const svgNS = "http://www.w3.org/2000/svg";

// For each algorithm: where each operator sits (column, and row counting up from the bottom),
// which operators modulate which, and which operators are heard
export const algorithms =
[
    { positions: { 1: [0, 0],   2: [0, 1],   3: [0, 2], 4: [0, 3] }, modulations: [[4, 3], [3, 2], [2, 1]], carriers: [1] },
    { positions: { 1: [0.5, 0], 2: [0.5, 1], 3: [0, 2], 4: [1, 2] }, modulations: [[4, 2], [3, 2], [2, 1]], carriers: [1] },
    { positions: { 1: [0.5, 0], 2: [0, 1],   3: [0, 2], 4: [1, 1] }, modulations: [[4, 1], [3, 2], [2, 1]], carriers: [1] },
    { positions: { 1: [0.5, 0], 2: [0, 1],   3: [1, 1], 4: [1, 2] }, modulations: [[4, 3], [3, 1], [2, 1]], carriers: [1] },
    { positions: { 1: [0, 0],   2: [0, 1],   3: [1, 0], 4: [1, 1] }, modulations: [[4, 3], [2, 1]],         carriers: [1, 3] },
    { positions: { 1: [0, 0],   2: [1, 0],   3: [2, 0], 4: [1, 1] }, modulations: [[4, 1], [4, 2], [4, 3]], carriers: [1, 2, 3] },
    { positions: { 1: [0, 0],   2: [1, 0],   3: [2, 0], 4: [2, 1] }, modulations: [[4, 3]],                 carriers: [1, 2, 3] },
    { positions: { 1: [0, 0],   2: [1, 0],   3: [2, 0], 4: [3, 0] }, modulations: [],                       carriers: [1, 2, 3, 4] },
];

export function isCarrier (algorithm, operator)
{
    return algorithms[algorithm - 1]?.carriers.includes (operator) ?? false;
}

function createSVGElement (tag, attributes, parent)
{
    const e = document.createElementNS (svgNS, tag);

    for (const [name, value] of Object.entries (attributes))
        e.setAttribute (name, value);

    parent?.appendChild (e);
    return e;
}

//==============================================================================
export class AlgorithmChart
{
    constructor ({ onSelect, cellWidth = 84, cellHeight = 112, columns = 4 })
    {
        this.onSelect = onSelect;
        this.cellWidth = cellWidth;
        this.cellHeight = cellHeight;
        this.columns = columns;
        this.feedback = [0, 0, 0, 0];
        this.selected = 0;

        const rows = Math.ceil (algorithms.length / columns);
        this.element = createSVGElement ("svg", { class: "algorithm-chart",
                                                  width: cellWidth * columns, height: cellHeight * rows,
                                                  viewBox: `0 0 ${cellWidth * columns} ${cellHeight * rows}` });

        this.cells = algorithms.map ((_, i) =>
        {
            const g = createSVGElement ("g", { class: "algorithm",
                                               transform: `translate(${(i % columns) * cellWidth}, ${Math.floor (i / columns) * cellHeight})` }, this.element);
            g.addEventListener ("click", () => this.onSelect?.(i + 1));
            return g;
        });

        this.paint();
    }

    setAlgorithm (algorithm)
    {
        this.selected = algorithm;
        this.paint();
    }

    setFeedback (operator, amount)
    {
        this.feedback[operator - 1] = amount;
        this.paint();
    }

    paint()
    {
        algorithms.forEach ((algorithm, i) => this.paintCell (this.cells[i], algorithm, i + 1));
    }

    paintCell (g, algorithm, number)
    {
        const { cellWidth, cellHeight } = this;
        const box = 15, spacingX = 20, spacingY = 21, feedbackLoopWidth = 8;

        g.replaceChildren();
        g.classList.toggle ("selected", number === this.selected);

        createSVGElement ("rect", { class: "algorithm-background", x: 3, y: 3, width: cellWidth - 6, height: cellHeight - 6, rx: 3 }, g);

        const label = createSVGElement ("text", { class: "algorithm-number", x: 10, y: 19 }, g);
        label.textContent = number;

        // Centre the diagram (leaving room for a feedback loop on its right) in the space under
        // the number, with the output bus at the bottom
        const positions = Object.values (algorithm.positions);
        const maxColumn = Math.max (...positions.map (p => p[0]));
        const left = (cellWidth - maxColumn * spacingX - feedbackLoopWidth) / 2;
        const busY = cellHeight - 16;
        const bottomRowY = busY - 18;

        const centreOf = (op) =>
        {
            const [column, row] = algorithm.positions[op];
            return { x: left + column * spacingX, y: bottomRowY - row * spacingY };
        };

        const line = (points, className) =>
            createSVGElement ("polyline", { class: className, points: points.map (p => p.join (",")).join (" ") }, g);

        // Modulation goes from the bottom of the modulator to the top of the operator it modulates
        for (const [from, to] of algorithm.modulations)
        {
            const a = centreOf (from), b = centreOf (to);
            const startY = a.y + box / 2, endY = b.y - box / 2;
            const midY = (startY + endY) / 2;

            line ([[a.x, startY], [a.x, midY], [b.x, midY], [b.x, endY]], "algorithm-wire");
        }

        // The carriers all feed the output bus
        const carrierXs = algorithm.carriers.map (op => centreOf (op).x);

        for (const op of algorithm.carriers)
        {
            const c = centreOf (op);
            line ([[c.x, c.y + box / 2], [c.x, busY]], "algorithm-wire");
        }

        const busLeft = Math.min (...carrierXs), busRight = Math.max (...carrierXs), outputX = (busLeft + busRight) / 2;
        line ([[busLeft, busY], [busRight, busY]], "algorithm-wire");
        line ([[outputX, busY], [outputX, busY + 7]], "algorithm-wire");
        line ([[outputX - 3, busY + 4], [outputX, busY + 7], [outputX + 3, busY + 4]], "algorithm-wire");

        // An operator with feedback gets a loop from its output back round to its top
        for (let op = 1; op <= 4; ++op)
        {
            if (this.feedback[op - 1] > 0)
            {
                const c = centreOf (op);
                const x = c.x + box / 2, top = c.y - box / 2 - 3, bottom = c.y + box / 2 + 3;

                line ([[c.x, c.y + box / 2], [c.x, bottom], [x + 4, bottom], [x + 4, top], [c.x, top], [c.x, c.y - box / 2]],
                      "algorithm-wire algorithm-feedback");
            }
        }

        for (let op = 1; op <= 4; ++op)
        {
            const c = centreOf (op);
            const isCarrierOp = algorithm.carriers.includes (op);

            createSVGElement ("rect", { class: isCarrierOp ? "algorithm-op carrier" : "algorithm-op",
                                        x: c.x - box / 2, y: c.y - box / 2, width: box, height: box }, g);

            const text = createSVGElement ("text", { class: "algorithm-op-number", x: c.x, y: c.y + 4 }, g);
            text.textContent = op;
        }
    }
}
