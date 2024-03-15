
#undef CHOC_ASSERT
#define CHOC_ASSERT(x) assert(x)

#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../include/cmajor/helpers/cmaj_Patch.h"
#include "../../../include/choc/javascript/choc_javascript_QuickJS.h"

#include <iostream>
#include <memory>

uint32_t getAudioChannelCount (const cmaj::EndpointDetailsList& endpoints)
{
    uint32_t channels = 0;

    for (auto i : endpoints)
        channels += i.getNumAudioChannels();

    return channels;
}

std::string getFrameValue (choc::buffer::ChannelArrayBuffer<float>& buffer, uint32_t frame)
{
    std::ostringstream oss;

    auto channels = buffer.getNumChannels();

    if (channels > 1)
        oss << "(";

    for (uint32_t n = 0; n < channels; n++)
    {
        if (n != 0)
            oss << ", ";

        oss << buffer.getSample (n, frame);
    }

    if (channels > 1)
        oss << ")";

    return oss.str();
}

//==============================================================================
int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << argv[0] << ": patchfile" << std::endl
                  << std::endl
                  << "Specify a patch to load as the first argument" << std::endl;
        exit (-1);
    }

    std::cout << "Loading patch " << argv[1] << std::endl;

    cmaj::PatchManifest patchManifest;

    try
    {
        patchManifest.initialiseWithFile (argv[1]);
    }
    catch (const std::runtime_error& e)
    {
        std::cout << "Initialising failed: " << e.what() << std::endl;
        return 1;
    }

    cmaj::Patch patch;

    patch.createEngine = [&] { return cmaj::Engine::create(); };

    if (! patch.preload (patchManifest))
    {
        std::cerr << "Failed to preload patch" << std::endl;
        return 1;
    }

    auto inputChannels = getAudioChannelCount (patch.getInputEndpoints());
    auto outputChannels = getAudioChannelCount (patch.getOutputEndpoints());

    std::cout << "Name: " << patch.getName() << std::endl
              << "Description: " << patch.getDescription() << std::endl
              << "Input channels: " << inputChannels << std::endl
              << "Output channels: " << outputChannels << std::endl
              << std::endl
              << "Input endpoints:" << std::endl
              << patch.getInputEndpoints().getDescription() << std::endl
              << std::endl
              << "Output endpoints:" << std::endl
              << patch.getOutputEndpoints().getDescription() << std::endl
              << std::endl;

    uint32_t maxBlockSize = 256;
    uint32_t totalFramesToRender = 32;

    patch.setPlaybackParams({44100.0, maxBlockSize, inputChannels, outputChannels});

    cmaj::Patch::LoadParams loadParams;

    loadParams.manifest = patchManifest;

    if (! patch.loadPatch (loadParams, true))
    {
        std::cerr << "Failed to load patch" << std::endl;
        return 1;
    }

    if (! patch.isPlayable())
    {
        std::cerr << "Patch is not playable" << std::endl;
        return 1;
    }

    choc::audio::AudioMIDIBlockDispatcher blockDispatcher;

    blockDispatcher.reset (44100.0);

    auto inputData = choc::buffer::ChannelArrayBuffer<float> (inputChannels, totalFramesToRender);
    auto outputData = choc::buffer::ChannelArrayBuffer<float> (outputChannels, totalFramesToRender);

    inputData.clear();

    for (uint32_t i = 0; i < inputChannels; i++)
        inputData.getSample (i, 0) = 1.0f / float (1 + i);

    blockDispatcher.setAudioBuffers (inputData, outputData);

    blockDispatcher.processInChunks ([&] (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
                                     {
                                         patch.process (block, true);
                                     });

    for (uint32_t i = 0; i < totalFramesToRender; i++)
        std::cout << i << ": " << getFrameValue (inputData, i) << " -> " << getFrameValue (outputData, i) << std::endl;

    return 0;
}
