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

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "choc/javascript/choc_javascript.h"
#include "choc/audio/io/choc_AudioMIDIPlayer.h"
#include "../../../include/cmajor/helpers/cmaj_Patch.h"


namespace cmaj::javascript
{
    void executeScript (const std::string& file,
                        const choc::value::Value& engineOptions,
                        cmaj::BuildSettings& buildSettings,
                        const choc::audio::io::AudioDeviceOptions& audioOptions);

    //==============================================================================
    struct JavascriptEngine
    {
        JavascriptEngine (const cmaj::BuildSettings&,
                          const choc::value::Value& engineOptions);
        ~JavascriptEngine();

        choc::javascript::Context& getContext();
        void resetPerformerLibrary();
        std::string getEngineTypeName();

    private:
        struct Pimpl;
        std::unique_ptr<Pimpl> pimpl;
    };

}
