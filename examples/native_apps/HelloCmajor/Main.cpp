/*
    Tiny "hello world" for creating a Cmajor performer and pushing
    some data through it..
*/

#include <iostream>
#include <memory>
#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../include/choc/audio/choc_Oscillators.h"

static constexpr auto code = R"(

processor Gain
{
    input stream float in;
    output stream float out;

    void main()
    {
        loop
        {
            out <- in * 0.5f;
            advance();
        }
    }
}

)";

//==============================================================================
int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Error: Specify the location of your " << cmaj::Library::getDLLName() << " shared library file as the first argument" << std::endl;
        exit (-1);
    }

    if (! cmaj::Library::initialise (argv[1]))
    {
        std::cout << "Failed to load the " << cmaj::Library::getDLLName() << " DLL from " << argv[1] << "!" << std::endl;
        return 1;
    }

    std::cout << "Engine types available: "
              << choc::text::joinStrings (cmaj::Engine::getAvailableEngineTypes(), ", ") << std::endl;

    auto engine = cmaj::Engine::create();

    cmaj::DiagnosticMessageList messages;

    cmaj::Program program;

    if (! program.parse (messages, "internal", code))
    {
        std::cout << "Failed to parse!" << std::endl
                  << messages.toString() << std::endl;
        return 1;
    }

    engine.setBuildSettings (cmaj::BuildSettings()
                                .setFrequency (44100)
                                .setSessionID (123456));

    if (! engine.load (messages, program, [&] (const cmaj::ExternalVariable&) -> choc::value::Value { return {}; }))
    {
        std::cout << "Failed to load!" << std::endl
                  << messages.toString() << std::endl;

        return 1;
    }

    std::cout << "Loaded!" << std::endl;

    std::cout << "Input endpoints:" << std::endl
              << engine.getInputEndpoints().getDescription() << std::endl
              << std::endl
              << "Output endpoints:" << std::endl
              << engine.getOutputEndpoints().getDescription() << std::endl
              << std::endl;

    auto inputHandle  = engine.getEndpointHandle ("in");
    auto outputHandle = engine.getEndpointHandle ("out");

    std::cout << "input handle: " << inputHandle << std::endl
              << "output handle: " << outputHandle << std::endl
              << std::endl;

    if (! engine.link (messages))
    {
        std::cout << "Failed to link!" << std::endl
                  << messages.toString() << std::endl;

        return 1;
    }

    auto performer = engine.createPerformer();

    std::cout << "Linked!" << std::endl;

    uint32_t framesPerBlock = 8,
             totalFramesToRender = 30,
             framesDone = 0;

    // Generates a buffer with a sinewave that we can send into our input endpoint..
    auto inputData = choc::oscillator::createInterleavedSine<float> ({ 1u, totalFramesToRender }, 1.0, 50.0);

    while (framesDone < totalFramesToRender)
    {
        auto framesThisBlock = std::min ((totalFramesToRender - framesDone), framesPerBlock);

        std::cout << "Rendering frame: " << framesDone
                  << " framesToRender: " << framesThisBlock << std::endl;

        performer.setBlockSize (framesThisBlock);

        // Write the next block of input data to our input endpoint
        auto inputBlock = inputData.getFrameRange ({ framesDone, framesDone + framesThisBlock });
        performer.setInputFrames (inputHandle, inputBlock);

        // The magic happens in here!
        performer.advance();

        // Fetch the rendered block of frames from our output endpoint
        auto outputBlock = choc::buffer::InterleavedBuffer<float> (1, framesThisBlock); // 1 = number of channels
        performer.copyOutputFrames (outputHandle, outputBlock);

        // Now we'll print the samples to prove they exist...
        for (uint32_t frame = 0; frame < outputBlock.getNumFrames(); ++frame)
            std::cout << framesDone + frame << ": " << inputBlock.getSample (0, frame)
                      << " => " << outputBlock.getSample (0, frame) << std::endl;

        std::cout << std::endl;

        framesDone += framesThisBlock;
    }

    return 0;
}
