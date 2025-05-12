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

// This is the "worker" for our Pro54 patch, and it's code that will be
// executed when the patch is created.

import * as presets from "../gui/presets/presetBank.js"
import * as controllers from "../gui/controllers/controllerMapping.js"

let patchConnection;
let presetBank = new presets.PresetBank();
let controllerMappings = new controllers.ControllerMappings();

let currentParameterValues = new Map();
let programNumber = 0;
let recording = 0;

let isSessionConnected = false;

const stateValueChangeListener = event =>
{
    if (recording == 1)
        presetBank.setPresetParameterValues (programNumber, currentParameterValues);

    if (event.key == "currentProgram")
    {
        if (! event.value)
        {
            patchConnection.sendStoredStateValue ("currentProgram", 111);
            return;
        }

        const oldProgramNumber = programNumber;
        programNumber = event.value ^ 0;
        const info = presetBank.getPatch (programNumber);

        if (info)
        {
            currentParameterValues.clear();

            if (oldProgramNumber != 0 && programNumber != oldProgramNumber)
                presetBank.sendParameterValuesToPatchConnection (patchConnection, programNumber);

            patchConnection.sendStoredStateValue ("patchName", info.PatchName);
        }
    }

    if (event.key == "recordEnabled")
    {
        if (recording == 1 && event.value == 0)
            patchConnection.sendStoredStateValue ("patchDetails", presetBank.getPatchDetails());

        recording = event.value;
    }

    if (event.key == "setPatchName")
    {
        presetBank.setPatchName (programNumber, event.value);
    }

    if (event.key == "patchDetails")
    {
        if (event.value)
            presetBank.setPatchDetails (event.value);

        patchConnection.sendStoredStateValue ("patchList", presetBank.getPatchList());
    }
}

const statusListener = status =>
{
    if (status.connected != isSessionConnected)
    {
        isSessionConnected = !! status.connected;
        patchConnection.requestStoredStateValue ("currentProgram");
    }

    if (status.details?.inputs)
    {
        for (const endpointInfo of status.details.inputs)
            controllerMappings.addEndpoint (endpointInfo)
    }
}

let lastBank = 0;

const parameterListener = event =>
{
    currentParameterValues.set (event.endpointID, event.value);
}

export default function runWorker (pc)
{
    patchConnection = pc;

    const midi = patchConnection.utilities.midi;

    const midiInListener = event =>
    {
        if (midi.isController (event.message))
        {
            if (midi.getControllerNumber (event.message) == 0) // bank select
                lastBank = midi.getControllerValue (event.message);

            controllerMappings.applyController (patchConnection, midi.getControllerNumber (event.message), midi.getControllerValue (event.message))
        }

        if (midi.isProgramChange (event.message))
        {
            const programIndex = lastBank * 128 + midi.getProgramChangeNumber (event.message);
            patchConnection.sendStoredStateValue ("currentProgram", presets.getIDOfIndex (programIndex));
        }
    };

    patchConnection.addStatusListener (statusListener);
    patchConnection.requestStatusUpdate();

    patchConnection.addStoredStateValueListener (stateValueChangeListener);
    patchConnection.addEndpointListener ("midiIn", midiInListener);
    patchConnection.addAllParameterListener (parameterListener);

    patchConnection.requestStoredStateValue ("patchDetails");
    patchConnection.requestStoredStateValue ("currentProgram");
}
