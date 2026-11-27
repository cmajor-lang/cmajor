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
    Where everything sits on the front panel.

    All positions are in millimetres on the top surface of the machine, with x running
    left to right and z running from the back edge (0) towards the player. The same
    layout is used to paint the panel artwork, to place the 3D controls, and to work
    out what the mouse is pointing at.
*/

export const panelWidth  = 780;
export const panelDepth  = 320;
export const bodyHeight  = 58;

//==============================================================================
// The tracks of the pattern. The first 16 are the sounds, in the same order as the
// samples in the manifest (so the MIDI note for track N is 36 + N), and the last
// one holds the accents.
export const accentTrack = 16;
export const numSteps = 16;

export const tracks = [
    { name: "BASS DRUM" },    { name: "RIM SHOT" },   { name: "SNARE DRUM" }, { name: "HAND CLAP" },
    { name: "CLSD HIHAT" },   { name: "LOW TOM" },    { name: "OPEN HIHAT" }, { name: "MID TOM" },
    { name: "HI TOM" },       { name: "CYMBAL" },     { name: "COW BELL" },   { name: "MID CONGA" },
    { name: "HI CONGA" },     { name: "LOW CONGA" },  { name: "MARACAS" },    { name: "CLAVES" },
    { name: "ACCENT" }
];

export function getMIDINoteForTrack (track)    { return track < accentTrack ? 36 + track : undefined; }
export function getTrackForMIDINote (note)     { return note >= 36 && note < 36 + accentTrack ? note - 36 : undefined; }

//==============================================================================
// Knob rows and the column grid of instrument channels
const levelRow = 46, tuneRow = 96, decayRow = 146;
const firstChannelX = 124, channelWidth = 52;

export const labelStrip = { x: firstChannelX - 4, z: 168, width: channelWidth * 11 + 8, depth: 40 };

export const knobStyles = {
    level:  { radius: 9.5, skirtRadius: 12.5, height: 11, cap: "#ebe5d3", pointer: "#202020" },
    tune:   { radius: 9.5, skirtRadius: 12.5, height: 11, cap: "#ee7a22", pointer: "#202020" },
    decay:  { radius: 9.5, skirtRadius: 12.5, height: 11, cap: "#d8362b", pointer: "#f0f0f0" },
    big:    { radius: 16,  skirtRadius: 20.5, height: 14, cap: "#8c8f93", pointer: "#f4f4f4" }
};

// Each channel is a column of knobs, plus the one or two tracks that it controls.
// The knob ids are the endpoint names of the patch's parameters.
const channels = [
    { label: "BD", tracks: [0],      knobs: { level: "bdLevel", tune: "bdTune", decay: "bdDecay" } },
    { label: "SD", tracks: [2],      knobs: { level: "sdLevel", tune: "sdTune", decay: "sdDecay" } },
    { label: "LT", tracks: [5, 13],  knobs: { level: "ltLevel", tune: "ltTune" } },
    { label: "MT", tracks: [7, 11],  knobs: { level: "mtLevel", tune: "mtTune" } },
    { label: "HT", tracks: [8, 12],  knobs: { level: "htLevel", tune: "htTune" } },
    { label: "RS", tracks: [1, 15],  knobs: { level: "rsLevel" } },
    { label: "CP", tracks: [3, 14],  knobs: { level: "cpLevel" } },
    { label: "CB", tracks: [10],     knobs: { level: "cbLevel" } },
    { label: "CY", tracks: [9],      knobs: { level: "cyLevel", tune: "cyTune", decay: "cyDecay" } },
    { label: "OH", tracks: [6],      knobs: { level: "ohLevel", decay: "ohDecay" } },
    { label: "CH", tracks: [4],      knobs: { level: "chLevel" } },
];

const knobRows = { level: levelRow, tune: tuneRow, decay: decayRow };
const knobCaptions = { level: "LEVEL", tune: "TUNE", decay: "DECAY" };

//==============================================================================
function createLayout()
{
    const knobs = [], trackSelectors = [], stepKeys = [], stepLEDs = [], columnLines = [];

    // Default ranges, used until the patch tells us the real ones
    const ranges = {
        level:  { min: 0,   max: 1,  init: 0.8 },
        tune:   { min: -12, max: 12, init: 0, isBipolar: true },
        decay:  { min: 0,   max: 1,  init: 1 },
    };

    channels.forEach ((channel, column) =>
    {
        const x = firstChannelX + channelWidth * (column + 0.5);

        for (const [kind, id] of Object.entries (channel.knobs))
            knobs.push ({ id, x, z: knobRows[kind], style: knobStyles[kind], caption: knobCaptions[kind], ...ranges[kind] });

        // One or two rows in the label strip, each of which selects a track
        const rowDepth = (labelStrip.depth - 8) / channel.tracks.length;

        channel.tracks.forEach ((track, row) =>
        {
            trackSelectors.push ({ track,
                                   x: x - channelWidth / 2 + 2,
                                   z: labelStrip.z + 4 + rowDepth * row,
                                   width: channelWidth - 4,
                                   depth: rowDepth });
        });

        if (column > 0)
            columnLines.push (x - channelWidth / 2);
    });

    // The left-hand column holds the accent and tempo controls..
    const leftColumnX = 64;

    knobs.push ({ id: "accent", x: leftColumnX, z: levelRow, style: knobStyles.level, caption: "ACCENT", min: 0, max: 1, init: 0.5 });
    knobs.push ({ id: "tempo",  x: leftColumnX, z: 118, style: knobStyles.big, caption: "TEMPO", min: 40, max: 300, mid: 120, init: 120, units: "bpm", decimals: 0 });
    trackSelectors.push ({ track: accentTrack, x: leftColumnX - 26, z: labelStrip.z + 4, width: 52, depth: labelStrip.depth - 8 });

    // ..and the right-hand one has the master volume and the clear key
    const rightColumnX = 738;

    knobs.push ({ id: "volume", x: rightColumnX, z: 70, style: knobStyles.big, caption: "VOLUME", min: -85, max: 6, mid: -12, init: 0, units: "dB", decimals: 1 });

    const clearKey = { x: rightColumnX - 18, z: 150, width: 36, depth: 20, colour: "#b7b4ac", caption: "CLEAR" };

    // The row of step keys, each with an LED above it, and the start/stop key at the left
    const stepsX = 124, stepPitch = (panelWidth - 10 - stepsX) / numSteps;
    const stepColours = ["#d33a2c", "#ee7b22", "#f1c232", "#ebe5d3"];

    for (let i = 0; i < numSteps; ++i)
    {
        const x = stepsX + stepPitch * i + 3;
        stepKeys.push ({ step: i, x, z: 232, width: stepPitch - 6, depth: 34, colour: stepColours[i >> 2] });
        stepLEDs.push ({ step: i, x: x + (stepPitch - 6) / 2, z: 222 });
    }

    const startKey = { x: 24, z: 232, width: 80, depth: 34, colour: "#b7b4ac", caption: "START / STOP" };

    // Each track selector has an LED to show which track the step keys are editing
    const trackLEDs = trackSelectors.map (s => ({ track: s.track, x: s.x + 5, z: s.z + s.depth / 2 }));

    return { knobs, trackSelectors, trackLEDs, stepKeys, stepLEDs, stepColours, stepPitch,
             startKey, clearKey, columnLines, knobCaptions };
}

export const layout = createLayout();

//==============================================================================
// Knob positions are 0 to 1. Knobs with a 'mid' value have a skewed response so that
// the mid value sits at the 12 o'clock position, like the Cmajor generic GUI's knobs.
function getSkew (knob)
{
    if (knob.mid === undefined)
        return 1;

    return Math.log (0.5) / Math.log ((knob.mid - knob.min) / (knob.max - knob.min));
}

export function valueToPosition (knob, value)
{
    const proportion = Math.min (1, Math.max (0, (value - knob.min) / (knob.max - knob.min)));
    return Math.pow (proportion, getSkew (knob));
}

export function positionToValue (knob, position)
{
    const proportion = Math.pow (Math.min (1, Math.max (0, position)), 1 / getSkew (knob));
    return knob.min + (knob.max - knob.min) * proportion;
}

/// Knobs turn through 270 degrees; this returns the clockwise angle from 12 o'clock
export function positionToAngle (position)
{
    return (position - 0.5) * 1.5 * Math.PI;
}
