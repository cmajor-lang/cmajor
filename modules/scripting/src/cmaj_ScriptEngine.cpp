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

#include <filesystem>
#include <functional>
#include <thread>

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "../include/cmaj_ScriptEngine.h"

#include "cmaj_javascript_Helpers.h"
#include "cmaj_javascript_ObjectHandle.h"
#include "cmaj_javascript_File.h"
#include "cmaj_javascript_Performer.h"
#include "cmaj_javascript_Patch.h"

#include "choc/javascript/choc_javascript_Timer.h"
#include "choc/javascript/choc_javascript_Console.h"
#include "choc/javascript/choc_javascript_QuickJS.h"

namespace cmaj::javascript
{

void executeScript (const std::string& file,
                    const choc::value::Value& engineOptions,
                    cmaj::BuildSettings& buildSettings,
                    const cmaj::audio_utils::AudioDeviceOptions&)
{
    JavascriptEngine engine (buildSettings, engineOptions);
    engine.getContext().evaluate (choc::file::loadFileAsString (file));
    choc::messageloop::run();
}

//==============================================================================
struct JavascriptEngine::Pimpl
{
    Pimpl (const cmaj::BuildSettings& buildSettings, const choc::value::Value& engineOptions)
        : performerLibrary (buildSettings)
    {
        try
        {
            if (engineOptions.isObject() && engineOptions.hasObjectMember("engine"))
            {
                auto type = engineOptions["engine"].getString();
                performerLibrary.setEngineType (type);
                patchLibrary.engineType = type;
            }

            registerTimerFunctions (context);
            registerConsoleFunctions (context);
            std::make_shared<cmaj::javascript::StandardFileSystem>()->bind (context);
            performerLibrary.bind (context);
            patchLibrary.bind (context);
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Internal Error" << std::endl;
        }
    }

    choc::javascript::Context context { choc::javascript::createQuickJSContext() };

    cmaj::javascript::PerformerLibrary performerLibrary;
    cmaj::javascript::PatchLibrary patchLibrary;
};

JavascriptEngine::JavascriptEngine (const cmaj::BuildSettings& buildSettings,
                                    const choc::value::Value& engineOptions)
    : pimpl (std::make_unique<Pimpl> (buildSettings, engineOptions))
{
}

JavascriptEngine::~JavascriptEngine() = default;

choc::javascript::Context& JavascriptEngine::getContext()
{
    return pimpl->context;
}

void JavascriptEngine::resetPerformerLibrary()
{
    return pimpl->performerLibrary.reset();
}

std::string JavascriptEngine::getEngineTypeName()
{
    return pimpl->performerLibrary.getEngineTypeName();
}

}
