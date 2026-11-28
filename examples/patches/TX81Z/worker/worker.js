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
    The TX81Z patch's worker, which runs for as long as the patch is loaded, whether or not
    its GUI is open.

    It watches the MIDI input for program changes, and when one arrives, loads that voice from
    the preset bank by sending all of its parameter values to the patch. The GUI's program
    buttons work by sending a program change into the same MIDI input, so they come through
    here too.

    The program number and the voice's name go into the stored state, so that the GUI can show
    them and hosts save them with the session. The worker doesn't respond to changes in that
    state: when a host restores a session it sets the parameters and then the stored state, so
    loading a voice then would overwrite the restored parameters.
*/

import * as presets from "../gui/presets/presetBank.js"

export default function runWorker (patchConnection)
{
    const midi = patchConnection.utilities.midi;

    const loadProgram = (programNumber) =>
    {
        const name = presets.getProgramName (programNumber);

        if (name === undefined)
            return;

        // The program number goes first, so the GUI knows that the parameter changes that
        // follow are a new voice being loaded
        patchConnection.sendStoredStateValue ("currentProgram", programNumber);
        presets.sendProgramToPatchConnection (patchConnection, programNumber);
        patchConnection.sendStoredStateValue ("voiceName", name);
    };

    patchConnection.addEndpointListener ("midiIn", ({ message }) =>
    {
        // The bank has fewer than 128 voices, so any program change beyond the end of it is
        // ignored, as is the MIDI channel
        if (midi.isProgramChange (message))
            loadProgram (midi.getProgramChangeNumber (message) + 1);
    });
}
