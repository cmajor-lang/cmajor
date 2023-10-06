//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     (C)2024 Cmajor Software Ltd
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     https://cmajor.dev
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88
//                                           ,88
//                                        888P"
//
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

#include <JuceHeader.h>
#include <assert.h>

#define CHOC_ASSERT(x) assert(x)
#include "../../../include/cmajor/helpers/cmaj_JUCEPlugin.h"
#include "../../../include/choc/javascript/choc_javascript_QuickJS.h"


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto patch = std::make_unique<cmaj::Patch> (false, true);
    patch->createEngine = +[] { return cmaj::Engine::create(); };

    return new cmaj::plugin::JITLoaderPlugin (std::move (patch));
}


// The following bodge-a-rama is because JUCE's message loop doesn't
// yet support GTK events. This can all be removed one day when that
// feature is added to JUCE, but in the meantime, it does mean that
// our plugin's web GUI might not work correctly in JUCE-based hosts
// that don't do a similar hack to this one
#if JUCE_LINUX && JucePlugin_Build_Standalone

#include "../../../include/choc/gui/choc_MessageLoop.h"

namespace juce
{
    bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
    extern const char* const* juce_argv;  // declared in juce_core
    extern int juce_argc;
}

extern juce::JUCEApplicationBase* juce_CreateApplication();

int main (int argc, char* argv[])
{
    juce_argc = argc;
    juce_argv = argv;

    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    std::unique_ptr<juce::JUCEApplicationBase> app (juce::JUCEApplicationBase::createInstance());

    if (app->initialiseApp())
    {
        choc::messageloop::Timer t (1000 / 60, +[]
        {
            juce::dispatchNextMessageOnSystemQueue (true);

            if (juce::MessageManager::getInstance()->hasStopMessageBeenSent())
                choc::messageloop::stop();

            return true;
        });

        choc::messageloop::run();
    }

    return app->shutdownApp();
}

#endif
