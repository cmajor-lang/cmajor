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

// This class holds the mappings from CC numbers to synth parameters, and is used by the worker
// to convert midi CC messages into parameter changes
//
// It is called with each endpointInfo, and if the endpoint annotation includes a 'cc' value, uses
// this to define the CC to map to this control

export class ControllerMappings
{
    constructor()
    {
        this.controllers = new Map();
    }

    addEndpoint (endpointInfo)
    {
        if (endpointInfo.annotation?.cc)
        {
            this.controllers.set (endpointInfo.annotation.cc, { parameter: endpointInfo.endpointID,
                                                                min: endpointInfo.annotation.min,
                                                                max: endpointInfo.annotation.max })
        }
    }

    applyController (patchConnection, controller, value)
    {
      let entry = this.controllers.get (controller)

      if (entry !== undefined)
          patchConnection.sendEventOrValue (entry.parameter, entry.min + ((entry.max - entry.min) * value / 127))
    }
}

