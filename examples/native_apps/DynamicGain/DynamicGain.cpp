/*
    This simple example compiles a Cmajor program that applies a gain to
    an input stream and emits the result, and demonstrates how to render
    this using the C++ API.

    Unlike the simpler gain example, this one uses input events and values
    to control the levels, and shows how to send these changes to the processor
    while running.
*/

#include <iostream>
#include <memory>
#include "../../../include/cmajor/API/cmaj_Engine.h"

// This is the Cmajor code that we're going to compile and run:
static constexpr auto code = R"(

processor StereoGain
{
    input stream float in;
    output stream float<2> out;

    // Notice that the left multiplier is a value, whilst the right multipler is an event
    input value float leftMultiplier;
    input event float rightMultiplier;

    event rightMultiplier (float f)
    {
        lastReceivedRightMultiplier = f;
    }

    float lastReceivedRightMultiplier;

    void main()
    {
        loop
        {
            out <- in * float<2> (leftMultiplier, lastReceivedRightMultiplier);
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

    if (! engine.load (messages, program))
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

    // Retrieve the handles for the parameters we want to control - this needs to be done pre-link
    auto leftMultiplierHandle = engine.getEndpointHandle ("leftMultiplier");
    auto rightMultiplierHandle = engine.getEndpointHandle ("rightMultiplier");

    std::cout << "input handle: " << inputHandle << std::endl
              << "output handle: " << outputHandle << std::endl
              << "leftMultiplier handle: " << leftMultiplierHandle << std::endl
              << "rightMultiplier handle: " << rightMultiplierHandle << std::endl
              << std::endl;

    if (! engine.link (messages))
    {
        std::cout << "Failed to link!" << std::endl
                  << messages.toString() << std::endl;

        return 1;
    }

    std::cout << "Linked!" << std::endl;

    auto performer = engine.createPerformer();

    uint32_t framesPerBlock = 10,
             totalFramesToRender = 100,
             framesDone = 0;

    // Create a single-channel audio buffer containing 1.0f throughout
    auto inputData = choc::buffer::createInterleavedBuffer (1, totalFramesToRender, [] { return 1.0f; });

    while (framesDone < totalFramesToRender)
    {
        auto framesThisBlock = std::min ((totalFramesToRender - framesDone), framesPerBlock);

        std::cout << "Rendering frame: " << framesDone
                  << " framesToRender: " << framesThisBlock << std::endl;

        performer.setBlockSize (framesThisBlock);

        {
            // Set the gain to framesDone / totalFramesToRender, so it will be a multiplier between 0 and 1,
            // being sent once per framesPerBlock
            auto newGain = float (framesDone) / totalFramesToRender;

            // Since the leftMultiplier is a value, we use the setInputValue method
            // This doesn't specify a type index (since values can only have one type) but it
            // additionally specifies a number of framesToReachValue, and a linear interpolation
            // is applied between the old and new value for that number of frames
            performer.setInputValue (leftMultiplierHandle, newGain, 5);

            // Right input is an event, so use addInputEvent
            performer.addInputEvent (rightMultiplierHandle, 0, newGain);
        }

        // Write the next block of input data to our input endpoint
        auto inputBlock = inputData.getFrameRange ({ framesDone, framesDone + framesThisBlock });
        performer.setInputFrames (inputHandle, inputBlock);

        // The magic happens in here!
        performer.advance();

        // Fetch the rendered block of frames from our output endpoint
        auto outputBlock = choc::buffer::InterleavedBuffer<float> (2, framesThisBlock);
        performer.copyOutputFrames (outputHandle, outputBlock);

        // just print the samples to prove they exist...
        for (uint32_t frame = 0; frame < outputBlock.getNumFrames(); ++frame)
            std::cout << framesDone + frame << ": " << inputBlock.getSample (0, frame)
                      << " => " << outputBlock.getSample (0, frame) << ", " << outputBlock.getSample (1, frame) << std::endl;

        std::cout << std::endl;

        framesDone += framesThisBlock;
    }

    return 0;
}
