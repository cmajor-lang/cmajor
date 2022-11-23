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

#include "../../../include/cmajor/helpers/cmaj_JUCEPlugin.h"


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto patch = std::make_unique<cmaj::Patch> (false, true);
    patch->createEngine = +[] { return cmaj::Engine::create(); };

    return new cmaj::plugin::JITLoaderPlugin (std::move (patch));
}
