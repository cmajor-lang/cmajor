//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"

#include <JuceHeader.h>

#define CHOC_ASSERT(x) assert(x)
#include "../../../include/cmajor/helpers/cmaj_JUCEPlugin.h"


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
