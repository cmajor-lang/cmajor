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

#pragma once

#include <random>

#include "../../../include/cmajor/helpers/cmaj_Patch.h"

namespace cmaj::javascript
{

//==============================================================================
struct PatchLibrary
{
    void bind (choc::javascript::Context& context)
    {
        context.registerFunction ("loadAndTestPatch", [this] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            choc::value::Value lastError;
            cmaj::Patch patch;

            patch.createEngine = [this]
            {
                auto engine = cmaj::Engine::create (engineType);
                engine.setBuildSettings (buildSettings);
                return engine;
            };

            patch.statusChanged = [&lastError] (const cmaj::Patch::Status& s)
            {
                lastError = createErrorObject (s.messageList);
            };

            auto params = patch.getPlaybackParams();

            if (auto rate = args[1])
                params.sampleRate = rate->getWithDefault<double> (0);

            if (auto blockSize = args[2])
                params.blockSize = static_cast<uint32_t> (blockSize->getWithDefault<int32_t> (0));

            patch.setPlaybackParams (params);

            if (auto file = args[0])
            {
                if (patch.loadPatchFromFile (file->toString(), true))
                    lastError = choc::value::Value();
            }
            else
            {
                lastError = createErrorObject ("No file parameter");
            }

            patch.unload();
            return lastError;
        });
    }

    std::string engineType;
    cmaj::BuildSettings buildSettings;
};

}
