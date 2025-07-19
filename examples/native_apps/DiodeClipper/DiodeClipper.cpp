/*
    Diode Clipper Cmajor native app example

    This runs a Cmajor algorithm simulating a Diode Clipper, which is an analog
    low-pass filter with a resistance, a capacitance, and two parallel diodes
    put in opposite directions. The linear part is simulated using the TPT
    structure, and the nonlinear part uses the Newton-Raphson's method to solve
    the roots of the implicit equation.
*/

#include <iostream>
#include <memory>

#undef CHOC_ASSERT
#define CHOC_ASSERT(x) assert(x)

#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../include/choc/choc/audio/choc_Oscillators.h"

// This is the Cmajor code that we'll compile and run:
static constexpr auto code = R"(

graph Diode  [[ main ]]
{
    input  stream float audioIn;    // just using mono in and out
    output stream float audioOut;

    input event float cutoffFrequency   [[ name: "Cutoff", min: 20, max: 20000, init: 10000, step: 10  ]];
    input event float gaindB            [[ name: "Gain",   min: 0,  max:    40, init:    20, step: 0.1 ]];

    node diodeClipper = DiodeClipper * 4;  // oversampling 4 times

    connection
    {
        audioIn         -> diodeClipper.audioIn;
        cutoffFrequency -> diodeClipper.cutoffFrequencyIn;
        gaindB          -> diodeClipper.gaindBIn;

        diodeClipper -> audioOut;
    }
}

//==============================================================================
processor DiodeClipper
{
    input  stream float audioIn;
    output stream float audioOut;

    input event float cutoffFrequencyIn;
    input event float gaindBIn;

    event cutoffFrequencyIn (float f)   { cutoffFrequency = f; }
    event gaindBIn (float f)            { gaindB = f; }

    // Diode Clipper parameters
    float cutoffFrequency = 10000.0f;
    float gaindB = 40.0f;

    // filter variables
    float G, gain;

    // Main processing function
    void main()
    {
        // internal constants (1N4148)
        let Is          = 2.52e-9;
        let mu          = 1.752;
        let Vt          = 26e-3;
        let R           = 2.2e3;
        let tolerance   = 1e-12;

        // state variables
        float32 s1;
        float64 out;

        let updateInterval = 8;   // number of samples between calls to updateFilterVariables()
        let maxNewtonRaphsonIterations = 64;

        loop
        {
            updateFilterVariables();

            // DAFX15 Capped Step
            let deltaLim = mu * Vt * acosh (mu * Vt / 2.0 / (R * Is * G));

            loop (updateInterval)
            {
                let in = audioIn * gain;
                let p = G * (in - s1) + s1;
                float64 delta = 1e9;

                loop (maxNewtonRaphsonIterations)
                {
                    if (abs (delta) <= tolerance)
                        break;

                    let J = p - (2 * G * R * Is) * sinh (out / (mu * Vt)) - out;
                    let dJ = -1 - G * 2 * R * Is / (mu * Vt) * cosh (out / (mu * Vt));

                    // DAFX15 Capped Step
                    delta = clamp (-J / dJ, -deltaLim, deltaLim);

                    // next iteration
                    out += delta;
                }

                // TPT structure updates
                let v = float (out) - s1;
                s1 = float (out) + v;

                audioOut <- float (out);
                advance();
            }
        }
    }

    void updateFilterVariables()
    {
        // low-pass filter internal variables update
        let cutoff = clamp (cutoffFrequency, 10.0f, float32 (processor.frequency) * 0.49999f);
        let g = tan (float (pi) * cutoff / float32 (processor.frequency));
        G = g / (1 + g);

        // gain update
        gain = std::levels::dBtoGain (gaindB);
    }
}

)";


//==============================================================================
int main ()
{
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

    if (! engine.load (messages, program, {}, {}))
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

    auto inputHandle  = engine.getEndpointHandle ("audioIn");
    auto outputHandle = engine.getEndpointHandle ("audioOut");

    // Retrieve the handles for the parameters we want to control - this needs to be done pre-link
    auto cutoffHandle = engine.getEndpointHandle ("cutoffFrequency");
    auto gainHandle = engine.getEndpointHandle ("gaindB");

    std::cout << "input handle: " << inputHandle << std::endl
              << "output handle: " << outputHandle << std::endl
              << "cutoff handle: " << cutoffHandle << std::endl
              << "gain handle: " << gainHandle << std::endl
              << std::endl;

    if (! engine.link (messages))
    {
        std::cout << "Failed to link!" << std::endl
                  << messages.toString() << std::endl;

        return 1;
    }

    std::cout << "Linked!" << std::endl;

    auto performer = engine.createPerformer();

    uint32_t framesPerBlock = 8,
             totalFramesToRender = 30,
             framesDone = 0;

    // Generates a sinewave buffer that we can send into our input endpoint..
    auto inputData = choc::oscillator::createInterleavedSine<float> ({ 1u, totalFramesToRender }, 1.0, 50.0);

    while (framesDone < totalFramesToRender)
    {
        auto framesThisBlock = std::min ((totalFramesToRender - framesDone), framesPerBlock);

        std::cout << "Rendering frame: " << framesDone
                  << " framesToRender: " << framesThisBlock << std::endl;

        performer.setBlockSize (framesThisBlock);

        if (framesDone == 0)
        {
            // Set parameter values
            // Our parameters are events, so we use the addInputEvent function for this.
            // The values must have types which correspond to the data type of the event to submit,
            // and since event endpoints can take multiple data types, the index of the type is specified in the call -
            // since there is only one data type in this case, this index is 0
            //
            // values are specified in their native range, not a normalised range, so in this case, we're setting
            // the cutoff to 1000Hz, and the gain to +20db
            //
            // Events must be added between prepare() and advance(), and are applied before the first frame of this
            // block

            performer.addInputEvent (cutoffHandle, 0, 1000.0f);
            performer.addInputEvent (gainHandle,   0, 20.0f);
        }

        // Write the next block of input data to our input endpoint
        auto inputBlock = inputData.getFrameRange ({ framesDone, framesDone + framesThisBlock });
        performer.setInputFrames (inputHandle, inputBlock);

        // The magic happens in here!
        performer.advance();

        // Fetch the rendered block of frames from our output endpoint
        auto outputBlock = choc::buffer::InterleavedBuffer<float> (1, framesThisBlock, false); // 1 = number of channels
        performer.copyOutputFrames (outputHandle, outputBlock);

        // just print the samples to prove they exist...
        for (uint32_t frame = 0; frame < outputBlock.getNumFrames(); ++frame)
            std::cout << framesDone + frame << ": " << inputBlock.getSample (0, frame)
                      << " => " << outputBlock.getSample (0, frame) << std::endl;

        std::cout << std::endl;

        framesDone += framesThisBlock;
    }

    return 0;
}
