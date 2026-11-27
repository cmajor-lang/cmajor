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
    The 808's step pattern lives in the patch's stored state, under the key "pattern",
    so that it gets saved and restored along with the rest of the patch's state.

    This worker runs whenever the patch is loaded, whether or not its GUI is open. Its
    job is to watch that stored value and forward it to the DSP's "pattern" endpoint.

    The pattern is an array of 17 numbers: one 16-bit mask per sound (in the same order
    as the samples in the manifest), followed by one for the accents. Bit N of each
    mask is set if the track plays on step N.
*/

const numTracks = 17;

function steps (...indexes)
{
    return indexes.reduce ((mask, index) => mask | (1 << index), 0);
}

// A simple beat so that there's something to hear when you first press start
const defaultPattern = [
    steps (0, 6, 10),                    // bass drum
    0,                                   // rim shot
    steps (4, 12),                       // snare drum
    steps (12),                          // clap
    steps (0, 2, 4, 6, 8, 10, 12),       // closed hat
    0,                                   // low tom
    steps (14),                          // open hat
    0, 0, 0,                             // mid tom, high tom, cymbal
    steps (7),                           // cowbell
    0, 0, 0, 0, 0,                       // congas, maracas, claves
    steps (0, 4, 8, 12)                  // accent
];

function isValidPattern (pattern)
{
    return Array.isArray (pattern)
            && pattern.length === numTracks
            && pattern.every (Number.isInteger);
}

export default function runWorker (patchConnection)
{
    patchConnection.addStoredStateValueListener (({ key, value }) =>
    {
        if (key !== "pattern")
            return;

        if (isValidPattern (value))
            patchConnection.sendEventOrValue ("pattern", value);
        else
            patchConnection.sendStoredStateValue ("pattern", defaultPattern);
    });

    // When the patch is (re)connected or reset, the DSP needs the pattern again
    let isConnected = false;

    patchConnection.addStatusListener (status =>
    {
        if (!! status.connected !== isConnected)
        {
            isConnected = !! status.connected;
            patchConnection.requestStoredStateValue ("pattern");
        }
    });

    patchConnection.requestStatusUpdate();
    patchConnection.requestStoredStateValue ("pattern");
}
