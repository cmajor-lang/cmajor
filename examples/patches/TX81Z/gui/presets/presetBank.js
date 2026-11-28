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
    The TX81Z's default bank of 16 voices, shared by the worker (which loads them into the
    patch when a program change arrives) and the GUI.

    Each voice lists only the settings that differ from the patch's initial voice, which is
    program 1, so it matches the init values in TX81Z.cmajor. getProgramParameterValues()
    fills in the rest, so loading a voice always sets every parameter.

    For now these are placeholder sounds, just to give the program buttons something to step
    through.
*/

export const numPrograms = 16;

//==============================================================================
// The values of the patch's initial voice, as annotated in TX81Z.cmajor. Each operator array
// is in the order [op1, op2, op3, op4]; op1 is the carrier in algorithms 1-4, and op4 is the
// top of the modulator stack.

const initialVoice =
{
    volume:     -6,
    transpose:  -12,
    algorithm:  2,

    operators:
    {
        Waveshape:          [1, 1, 5, 1],
        Level:              [99, 74, 71, 79],
        Fixed:              [0, 0, 0, 0],
        FreqMultiplier:     [0.5, 0.5, 1.0, 1.0],
        Feedback:           [0, 0, 0, 7],
        AttackRate:         [31, 31, 31, 31],
        DecayRate1:         [9, 9, 17, 16],
        DecayLevel:         [0, 0, 0, 3],
        DecayRate2:         [0, 0, 0, 7],
        ReleaseRate:        [8, 8, 8, 8],
        KVS:                [0, 1, 1, 3],
        RateScaling:        [1, 1, 1, 0],
    }
};

//==============================================================================
// Names are up to 10 characters, as on the hardware. Any operator setting that's left out
// keeps the initial voice's value.

const programs =
[
    { name: "LatelyBass" },

    { name: "E.Piano 1", transpose: 0, algorithm: 5,
      operators: { Waveshape:      [1, 1, 1, 1],
                   Level:          [99, 70, 95, 60],
                   FreqMultiplier: [1, 1, 1, 14],
                   Feedback:       [0, 3, 0, 0],
                   AttackRate:     [31, 31, 31, 31],
                   DecayRate1:     [6, 9, 5, 14],
                   DecayLevel:     [4, 0, 4, 0],
                   DecayRate2:     [4, 0, 4, 0],
                   ReleaseRate:    [7, 8, 7, 8],
                   KVS:            [2, 5, 2, 6] } },

    { name: "Brass Sect", transpose: 0, algorithm: 1,
      operators: { Level:          [99, 82, 70, 64],
                   FreqMultiplier: [1, 1, 1, 1],
                   Feedback:       [0, 0, 0, 5],
                   AttackRate:     [18, 20, 22, 24],
                   DecayRate1:     [4, 6, 8, 8],
                   DecayLevel:     [12, 10, 9, 8],
                   DecayRate2:     [0, 0, 0, 0],
                   ReleaseRate:    [7, 7, 7, 7],
                   KVS:            [1, 2, 2, 3] } },

    { name: "Wood Piano", transpose: 0, algorithm: 3,
      operators: { Level:          [99, 76, 68, 72],
                   FreqMultiplier: [1, 3, 1, 7],
                   Feedback:       [0, 0, 0, 2],
                   DecayRate1:     [8, 12, 14, 16],
                   DecayLevel:     [2, 0, 0, 0],
                   DecayRate2:     [5, 6, 6, 8],
                   ReleaseRate:    [6, 8, 8, 8],
                   KVS:            [2, 4, 4, 5] } },

    { name: "Organ 1", transpose: 0, algorithm: 8,
      operators: { Level:          [94, 90, 84, 78],
                   FreqMultiplier: [0.5, 1, 2, 3],
                   Feedback:       [0, 0, 0, 0],
                   DecayRate1:     [0, 0, 0, 0],
                   DecayLevel:     [15, 15, 15, 15],
                   DecayRate2:     [0, 0, 0, 0],
                   ReleaseRate:    [12, 12, 12, 12],
                   KVS:            [0, 0, 0, 0],
                   RateScaling:    [0, 0, 0, 0] } },

    { name: "Mallet", transpose: 0, algorithm: 5,
      operators: { Level:          [99, 74, 92, 66],
                   FreqMultiplier: [1, 4, 3, 9],
                   Feedback:       [0, 0, 0, 0],
                   DecayRate1:     [12, 18, 14, 20],
                   DecayLevel:     [0, 0, 0, 0],
                   DecayRate2:     [8, 0, 8, 0],
                   ReleaseRate:    [6, 9, 6, 9],
                   KVS:            [3, 5, 3, 5] } },

    { name: "Synth Lead", transpose: 0, algorithm: 1,
      operators: { Waveshape:      [2, 1, 1, 1],
                   Level:          [99, 80, 76, 70],
                   FreqMultiplier: [1, 2, 1, 1],
                   Feedback:       [0, 0, 0, 6],
                   DecayRate1:     [2, 4, 4, 4],
                   DecayLevel:     [14, 12, 12, 12],
                   DecayRate2:     [0, 0, 0, 0],
                   ReleaseRate:    [9, 9, 9, 9],
                   KVS:            [0, 1, 1, 1] } },

    { name: "Pluck Bass", algorithm: 4,
      operators: { Level:          [99, 82, 72, 68],
                   FreqMultiplier: [0.5, 1, 0.5, 2],
                   Feedback:       [0, 0, 0, 4],
                   DecayRate1:     [12, 20, 22, 24],
                   DecayLevel:     [3, 0, 0, 0],
                   DecayRate2:     [8, 0, 0, 0],
                   ReleaseRate:    [9, 10, 10, 10],
                   KVS:            [1, 3, 3, 4] } },

    { name: "Warm Pad", transpose: 0, algorithm: 5,
      operators: { Waveshape:      [1, 3, 1, 3],
                   Level:          [96, 62, 96, 58],
                   FreqMultiplier: [1, 1, 1.01, 2],
                   Feedback:       [0, 0, 0, 0],
                   AttackRate:     [9, 8, 9, 8],
                   DecayRate1:     [2, 2, 2, 2],
                   DecayLevel:     [13, 12, 13, 12],
                   DecayRate2:     [0, 0, 0, 0],
                   ReleaseRate:    [4, 4, 4, 4],
                   KVS:            [0, 0, 0, 0],
                   RateScaling:    [0, 0, 0, 0] } },

    { name: "Bells", transpose: 0, algorithm: 5,
      operators: { Level:          [94, 78, 90, 72],
                   FreqMultiplier: [1, 3.5, 2, 5.19],
                   Feedback:       [0, 0, 0, 0],
                   DecayRate1:     [4, 6, 5, 8],
                   DecayLevel:     [0, 0, 0, 0],
                   DecayRate2:     [3, 4, 3, 4],
                   ReleaseRate:    [4, 5, 4, 5],
                   KVS:            [2, 3, 2, 3] } },

    { name: "Strings", transpose: 0, algorithm: 2,
      operators: { Waveshape:      [1, 1, 1, 2],
                   Level:          [99, 70, 66, 62],
                   FreqMultiplier: [1, 1, 3, 1],
                   Feedback:       [0, 0, 0, 3],
                   AttackRate:     [12, 13, 14, 14],
                   DecayRate1:     [3, 3, 3, 3],
                   DecayLevel:     [13, 12, 11, 11],
                   DecayRate2:     [0, 0, 0, 0],
                   ReleaseRate:    [5, 5, 5, 5],
                   KVS:            [0, 1, 1, 1] } },

    { name: "Clav", transpose: 0, algorithm: 1,
      operators: { Waveshape:      [3, 1, 1, 1],
                   Level:          [99, 78, 74, 70],
                   FreqMultiplier: [1, 3, 1, 5],
                   Feedback:       [0, 0, 0, 5],
                   DecayRate1:     [10, 14, 16, 18],
                   DecayLevel:     [6, 2, 0, 0],
                   DecayRate2:     [6, 8, 0, 0],
                   ReleaseRate:    [11, 11, 11, 11],
                   KVS:            [2, 4, 4, 4] } },

    { name: "Flute", transpose: 0, algorithm: 7,
      operators: { Level:          [96, 0, 0, 50],
                   FreqMultiplier: [1, 1, 1, 1],
                   Feedback:       [0, 0, 0, 0],
                   AttackRate:     [16, 31, 31, 18],
                   DecayRate1:     [2, 0, 0, 2],
                   DecayLevel:     [14, 0, 0, 13],
                   DecayRate2:     [0, 0, 0, 0],
                   ReleaseRate:    [9, 9, 9, 9],
                   KVS:            [1, 0, 0, 2] } },

    { name: "Sync Saw", transpose: 0, algorithm: 1,
      operators: { Waveshape:      [2, 1, 4, 1],
                   Level:          [99, 86, 80, 74],
                   FreqMultiplier: [1, 1, 2, 3],
                   Feedback:       [0, 0, 0, 7],
                   DecayRate1:     [3, 6, 8, 10],
                   DecayLevel:     [12, 10, 8, 6],
                   DecayRate2:     [0, 2, 2, 2],
                   ReleaseRate:    [8, 8, 8, 8] } },

    { name: "Metal Hit", transpose: 0, algorithm: 6,
      operators: { Level:          [90, 88, 86, 84],
                   Fixed:          [0, 0, 0, 220],
                   FreqMultiplier: [1, 1.41, 2.82, 3.5],
                   Feedback:       [0, 0, 0, 6],
                   DecayRate1:     [14, 15, 16, 18],
                   DecayLevel:     [0, 0, 0, 0],
                   DecayRate2:     [10, 10, 10, 10],
                   ReleaseRate:    [8, 8, 8, 8],
                   KVS:            [3, 3, 3, 4] } },

    { name: "Sub Bass", algorithm: 7,
      operators: { Level:          [99, 0, 0, 40],
                   FreqMultiplier: [0.5, 1, 1, 1],
                   Feedback:       [0, 0, 0, 2],
                   DecayRate1:     [0, 0, 0, 14],
                   DecayLevel:     [15, 0, 0, 2],
                   DecayRate2:     [0, 0, 0, 4],
                   ReleaseRate:    [10, 10, 10, 10],
                   KVS:            [0, 0, 0, 2] } },
];

//==============================================================================
/** Program numbers run from 1 to numPrograms. */
export function isValidProgramNumber (programNumber)
{
    return Number.isInteger (programNumber) && programNumber >= 1 && programNumber <= numPrograms;
}

/** Moves by delta programs, wrapping round from the last program to the first and vice versa. */
export function getProgramNumberAfterStep (programNumber, delta)
{
    const index = isValidProgramNumber (programNumber) ? programNumber - 1 : 0;
    return ((index + delta) % numPrograms + numPrograms) % numPrograms + 1;
}

export function getProgramName (programNumber)
{
    return programs[programNumber - 1]?.name;
}

/** Returns an object mapping every one of the patch's parameter endpoint IDs to the value it
    should have for this program, or undefined if there's no such program.
*/
export function getProgramParameterValues (programNumber)
{
    const program = programs[programNumber - 1];

    if (! program)
        return undefined;

    const values =
    {
        volumeIn:       program.volume    ?? initialVoice.volume,
        pitchOffsetIn:  program.transpose ?? initialVoice.transpose,
        algorithmIn:    program.algorithm ?? initialVoice.algorithm,
    };

    for (const [setting, initialValues] of Object.entries (initialVoice.operators))
    {
        const programValues = program.operators?.[setting] ?? initialValues;

        for (let op = 0; op < initialValues.length; ++op)
            values["osc" + (op + 1) + setting + "In"] = programValues[op] ?? initialValues[op];
    }

    return values;
}

/** Sends all of a program's parameter values to the patch. */
export function sendProgramToPatchConnection (patchConnection, programNumber)
{
    const values = getProgramParameterValues (programNumber);

    if (values)
        for (const [endpointID, value] of Object.entries (values))
            patchConnection.sendEventOrValue (endpointID, value);
}
