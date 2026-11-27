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
    Paints the printed graphics of the top panel into a 2D canvas, which the renderer
    then uses as the texture for the panel's surface.

    As well as the artwork itself, this bakes in soft contact shadows under each knob
    and key, which is a cheap way of making them look like they're sitting on the panel.
*/

import { panelWidth, panelDepth, tracks, numSteps, layout, labelStrip, positionToAngle } from "./layout.js";
import { wordmarkPath, wordmarkBounds } from "./cmajor-wordmark.js";

const pixelsPerMillimetre = 4;

const colours = {
    panel:      "#242427",
    stepArea:   "#1b1b1d",
    print:      "#dcd6c8",
    printDim:   "#9a968c",
    labelStrip: "#d6d0c1",
    labelText:  "#1d1d1d",
    orange:     "#ef7a22",
    line:       "#3b3b3f"
};

const fontFamily = `"Helvetica Neue", Helvetica, Arial, sans-serif`;

export function paintPanel()
{
    const canvas = document.createElement ("canvas");
    canvas.width = panelWidth * pixelsPerMillimetre;
    canvas.height = panelDepth * pixelsPerMillimetre;

    const g = canvas.getContext ("2d");
    g.scale (pixelsPerMillimetre, pixelsPerMillimetre);

    paintBackground (g);
    paintKnobSection (g);
    paintLabelStrip (g);
    paintStepSection (g);
    paintNameplate (g);
    paintContactShadows (g);

    return canvas;
}

//==============================================================================
function drawText (g, text, x, z, size, { colour = colours.print, align = "center", weight = 700,
                                          italic = false, squeeze = 0.88, spacing = 0 } = {})
{
    g.save();
    g.translate (x, z);
    g.scale (squeeze, 1);
    g.font = `${italic ? "italic " : ""}${weight} ${size}px ${fontFamily}`;
    g.fillStyle = colour;
    g.textAlign = align;
    g.textBaseline = "middle";

    if (spacing > 0 && "letterSpacing" in g)
        g.letterSpacing = `${spacing}px`;

    g.fillText (text, 0, 0);
    g.restore();
}

function roundedRect (g, x, z, width, depth, radius)
{
    g.beginPath();
    g.roundRect (x, z, width, depth, radius);
}

//==============================================================================
function paintBackground (g)
{
    g.fillStyle = colours.panel;
    g.fillRect (0, 0, panelWidth, panelDepth);

    // A faint speckle, so that the paint doesn't look like perfect flat plastic
    const speckle = g.createImageData (256, 256);

    for (let i = 0; i < speckle.data.length; i += 4)
    {
        const v = Math.random() * 255;
        speckle.data[i] = speckle.data[i + 1] = speckle.data[i + 2] = v;
        speckle.data[i + 3] = 10;
    }

    const tile = document.createElement ("canvas");
    tile.width = tile.height = 256;
    tile.getContext ("2d").putImageData (speckle, 0, 0);

    g.save();
    g.scale (1 / pixelsPerMillimetre, 1 / pixelsPerMillimetre);
    g.fillStyle = g.createPattern (tile, "repeat");
    g.fillRect (0, 0, panelWidth * pixelsPerMillimetre, panelDepth * pixelsPerMillimetre);
    g.restore();

    // The darker lower section that holds the step keys
    g.fillStyle = colours.stepArea;
    g.fillRect (0, 212, panelWidth, panelDepth - 212);

    g.fillStyle = colours.orange;
    g.fillRect (0, 211, panelWidth, 1.2);
}

//==============================================================================
function paintKnobScale (g, knob)
{
    const { x, z, style } = knob;
    const inner = style.skirtRadius + 1.2, outer = style.skirtRadius + 3;

    g.strokeStyle = colours.print;
    g.lineCap = "round";

    for (let i = 0; i <= 10; ++i)
    {
        const angle = positionToAngle (i / 10);
        const isMajor = i === 0 || i === 10 || (knob.isBipolar && i === 5);
        const length = isMajor ? outer + 1.2 : outer;

        g.lineWidth = isMajor ? 0.7 : 0.45;
        g.beginPath();
        g.moveTo (x + Math.sin (angle) * inner,  z - Math.cos (angle) * inner);
        g.lineTo (x + Math.sin (angle) * length, z - Math.cos (angle) * length);
        g.stroke();
    }

    const labelRadius = outer + 4.2;
    const [minLabel, maxLabel] = knob.isBipolar ? ["-", "+"] : ["0", "10"];
    const size = style.radius > 10 ? 6 : 5;

    drawText (g, minLabel, x + Math.sin (positionToAngle (0)) * labelRadius, z - Math.cos (positionToAngle (0)) * labelRadius, size, { colour: colours.printDim });
    drawText (g, maxLabel, x + Math.sin (positionToAngle (1)) * labelRadius, z - Math.cos (positionToAngle (1)) * labelRadius, size, { colour: colours.printDim });

    drawText (g, knob.caption, x, z - style.skirtRadius - 10, style.radius > 10 ? 9 : 7.5, { spacing: 0.3 });
}

function paintKnobSection (g)
{
    g.strokeStyle = colours.line;
    g.lineWidth = 0.5;

    for (const x of layout.columnLines)
    {
        g.beginPath();
        g.moveTo (x, 18);
        g.lineTo (x, labelStrip.z - 4);
        g.stroke();
    }

    for (const knob of layout.knobs)
        paintKnobScale (g, knob);

    const { clearKey } = layout;
    drawText (g, clearKey.caption, clearKey.x + clearKey.width / 2, clearKey.z + clearKey.depth + 7, 7, { spacing: 0.3 });
}

//==============================================================================
function paintLabelStrip (g)
{
    const selectors = layout.trackSelectors;

    g.fillStyle = colours.labelStrip;
    roundedRect (g, labelStrip.x, labelStrip.z, labelStrip.width, labelStrip.depth, 1.5);
    g.fill();

    const accent = selectors.find (s => s.track === tracks.length - 1);
    roundedRect (g, accent.x, labelStrip.z, accent.width, labelStrip.depth, 1.5);
    g.fill();

    g.strokeStyle = "#a9a497";
    g.lineWidth = 0.4;

    for (const x of layout.columnLines)
    {
        g.beginPath();
        g.moveTo (x, labelStrip.z + 3);
        g.lineTo (x, labelStrip.z + labelStrip.depth - 3);
        g.stroke();
    }

    for (const selector of selectors)
    {
        const name = tracks[selector.track].name;
        const textX = selector.x + 9 + (selector.width - 9) / 2;
        const words = name.split (" ");
        const isTall = selector.depth > 20;

        if (isTall && words.length > 1)
        {
            drawText (g, words[0], textX, selector.z + selector.depth / 2 - 5, 8.5, { colour: colours.labelText, squeeze: 0.8 });
            drawText (g, words.slice (1).join (" "), textX, selector.z + selector.depth / 2 + 5, 8.5, { colour: colours.labelText, squeeze: 0.8 });
        }
        else
        {
            drawText (g, name, textX, selector.z + selector.depth / 2, isTall ? 8.5 : 7.5, { colour: colours.labelText, squeeze: 0.74 });
        }
    }

    // A divider between the two tracks of each paired channel
    for (const selector of selectors)
    {
        if (selector.depth < 20 && selector.z > labelStrip.z + 10)
        {
            g.beginPath();
            g.moveTo (selector.x + 3, selector.z);
            g.lineTo (selector.x + selector.width - 3, selector.z);
            g.stroke();
        }
    }
}

//==============================================================================
function paintStepSection (g)
{
    const keys = layout.stepKeys;

    // The recessed slot that the keys sit in
    const slotX = keys[0].x - 3, slotEnd = keys.at (-1).x + keys.at (-1).width + 3;
    g.fillStyle = "#101011";
    roundedRect (g, slotX, keys[0].z - 2, slotEnd - slotX, keys[0].depth + 4, 2);
    g.fill();

    const { startKey } = layout;
    roundedRect (g, startKey.x - 3, startKey.z - 2, startKey.width + 6, startKey.depth + 4, 2);
    g.fill();

    drawText (g, startKey.caption, startKey.x + startKey.width / 2, startKey.z + startKey.depth + 8, 7, { spacing: 0.4 });

    // Step numbers, and the coloured bands that group the steps into fours
    for (const key of keys)
    {
        const centre = key.x + key.width / 2;
        drawText (g, String (key.step + 1), centre, key.z + key.depth + 6.5, 7);
    }

    for (let group = 0; group < numSteps / 4; ++group)
    {
        const first = keys[group * 4], last = keys[group * 4 + 3];
        g.fillStyle = layout.stepColours[group];
        g.fillRect (first.x, first.z + first.depth + 12, last.x + last.width - first.x, 1.6);
    }

}

//==============================================================================
function paintNameplate (g)
{
    const z = panelDepth - 15;

    // The Cmajor wordmark, scaled so that its height fits the strip under the step keys
    const height = 17, scale = height / wordmarkBounds.height;
    const left = 24, width = wordmarkBounds.width * scale;

    g.save();
    g.translate (left, z - height / 2);
    g.scale (scale, scale);
    g.translate (-wordmarkBounds.x, -wordmarkBounds.y);
    g.fillStyle = colours.print;
    g.fill (new Path2D (wordmarkPath));
    g.restore();

    drawText (g, "RHYTHM MACHINE", left + width + 12, z, 8, { colour: colours.orange, align: "left", weight: 600, squeeze: 1, spacing: 1.8 });
}

//==============================================================================
function paintContactShadows (g)
{
    // The key light comes from low at the front-left, so shadows fall back and a little right
    const offsetX = 0.6, offsetZ = -1.4;

    for (const { x, z, style } of layout.knobs)
    {
        const r = style.skirtRadius;
        const shadow = g.createRadialGradient (x + offsetX, z + offsetZ, r * 0.8, x + offsetX, z + offsetZ, r * 1.25);
        shadow.addColorStop (0, "rgba(0,0,0,0.75)");
        shadow.addColorStop (1, "rgba(0,0,0,0)");
        g.fillStyle = shadow;
        g.fillRect (x - r * 1.5, z - r * 1.5, r * 3, r * 3);
    }

    g.save();
    g.shadowColor = "rgba(0,0,0,0.85)";
    g.shadowBlur = 2.5 * pixelsPerMillimetre;
    g.shadowOffsetX = offsetX * pixelsPerMillimetre;
    g.shadowOffsetY = offsetZ * pixelsPerMillimetre;
    g.fillStyle = "#0c0c0d";

    for (const key of [...layout.stepKeys, layout.startKey, layout.clearKey])
    {
        roundedRect (g, key.x + 0.5, key.z + 0.5, key.width - 1, key.depth - 1, 2);
        g.fill();
    }

    g.restore();
}
