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

#include "../../../include/cmaj_DefaultFlags.h"

#if CMAJ_ENABLE_PERFORMER_CPP || CMAJ_ENABLE_CODEGEN_CPP

#include "../../../include/cmaj_ErrorHandling.h"
#include "../../../../../include/cmajor/COM/cmaj_EngineFactoryInterface.h"

namespace cmaj::cplusplus
{
    static constexpr const char* backendName = "cpp";

    struct GeneratedCPP
    {
        /// Code is an empty string on failure
        std::string code, mainClassName;
    };

    GeneratedCPP generateCPPClass (const ProgramInterface&, std::string_view options,
                                   double maxFrequency, uint32_t maxNumFramesPerBlock, uint32_t eventBufferSize,
                                   const std::function<EndpointHandle(const EndpointID&)>&);

   #if CMAJ_ENABLE_PERFORMER_CPP
    EngineFactoryPtr createEngineFactory();
   #endif // CMAJ_ENABLE_PERFORMER_CPP
}

#endif
