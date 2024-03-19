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

#include "cmaj_Patch.h"
#include "../../choc/javascript/choc_javascript_QuickJS.h"
#include "../../choc/javascript/choc_javascript_Timer.h"
#include "../../choc/javascript/choc_javascript_Console.h"


namespace cmaj
{

/// When you create a Patch object, you need to set its createContextForPatchWorker
/// property so that it knows what kind of javascript context to create to run
/// any patch workers that may be needed. This function sets up a QuickJS context
/// for that purpose, and gives it the appropriate library functions that it needs.
inline void enableQuickJSPatchWorker (Patch& patch)
{
    patch.createContextForPatchWorker = [&patch]
    {
        auto context = choc::javascript::createQuickJSContext();

        registerTimerFunctions (context);
        registerConsoleFunctions (context);

        context.registerFunction ("_internalReadResource", [&patch] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            try
            {
                if (auto path = args.get<std::string>(0); ! path.empty())
                {
                    if (auto manifest = patch.getManifest())
                    {
                        if (auto content = manifest->readFileContent (path))
                        {
                            if (choc::text::findInvalidUTF8Data (content->data(), content->length()) == nullptr)
                                return choc::value::Value (*content);

                            return choc::value::createArray (static_cast<uint32_t> (content->length()),
                                                            [&] (uint32_t i) { return static_cast<int32_t> ((*content)[i]); });
                        }
                    }
                }
            }
            catch (...)
            {}

            return {};
        });

        context.registerFunction ("_internalReadResourceAsAudioData", [&patch] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            try
            {
                if (auto path = args.get<std::string>(0); ! path.empty())
                    if (auto manifest = patch.getManifest())
                        return readManifestResourceAsAudioData (*manifest, path, args[1] != nullptr ? *args[1] : choc::value::Value());
            }
            catch (...)
            {}

            return {};
        });

        return context;
    };
}

} // namespace cmaj
