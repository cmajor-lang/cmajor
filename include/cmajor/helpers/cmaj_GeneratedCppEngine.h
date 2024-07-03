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

#include <cstdlib>
#include "../API/cmaj_Engine.h"

namespace cmaj
{

//==============================================================================
/// This function will return a cmaj::Engine object that wraps around a
/// C++ class that was code-generated from a Cmajor program using the code-gen
/// utility.
///
/// For more details, see the GeneratedCppEngine class, which is used as the
/// wrapper.
template <typename GeneratedCppClass>
Engine createEngineForGeneratedCppProgram();

//==============================================================================
///
/// This helper class lets you create a cmaj::Engine object around a C++ class
/// that was code-generated from a Cmajor patch using the command-line tool's
/// code-generation utility.
///
/// Just supply the name of the generated class as the template parameter, and
/// it should allow you to create cmaj::Performer objects that can be used like
/// a jitted one.
///
/// Obviously because this wraps a pre-generated program, there's no point in
/// trying to call load() or link() on the engine, and you can't set external
/// variables, but you can query it for endpoints and set the frequency and
/// session ID via the BuildSettings
///
/// Note that rather than constructing this class directly, you can call the
/// createEngineForGeneratedCppProgram() function which will return an instance
/// that is nicely wrapped in a cmaj::Engine object.
///
template <typename GeneratedCppClass>
struct GeneratedCppEngine  : public choc::com::ObjectWithAtomicRefCount<EngineInterface, GeneratedCppEngine<GeneratedCppClass>>
{
    GeneratedCppEngine() = default;

    virtual ~GeneratedCppEngine() = default;

    //==============================================================================
    choc::com::String* getBuildSettings() override
    {
        return choc::com::createRawString (buildSettings.toJSON());
    }

    void setBuildSettings (const char* newSettings) override
    {
        buildSettings = BuildSettings::fromJSON (std::string_view (newSettings));
        buildSettings.setMaxBlockSize (GeneratedCppClass::maxFramesPerBlock);
    }

    //==============================================================================
    void unload() override          { loaded = linked = false; }
    bool isLoaded() override        { return loaded; }
    bool isLinked() override        { return linked; }

    choc::com::String* load (ProgramInterface*,
                             void*, EngineInterface::RequestExternalVariableFn,
                             void*, EngineInterface::RequestExternalFunctionFn) override   { loaded = true; linked = false; return {}; }
    choc::com::String* link (CacheDatabaseInterface*) override                             { loaded = linked = true; return {}; }
    choc::com::String* getLastBuildLog() override                                          { return {}; }

    PerformerInterface* createPerformer() override
    {
        return choc::com::create<Performer> (getSessionID(), getFrequency()).getWithIncrementedRefCount();
    }

    //==============================================================================
    choc::com::String* getProgramDetails() override
    {
        return choc::com::createRawString (GeneratedCppClass::programDetailsJSON);
    }

    EndpointHandle getEndpointHandle (const char* endpointName) override
    {
        if (endpointName != nullptr)
            return static_cast<EndpointHandle> (GeneratedCppClass::getEndpointHandleForName (endpointName));

        return {};
    }

    bool setExternalVariable (const char*, const void*, size_t) override { return false; }

    const char* getAvailableCodeGenTargetTypes() override   { return ""; }
    void generateCode (const char*, const char*, void*, EngineInterface::HandleCodeGenOutput) override {}

    BuildSettings buildSettings;

private:
    //==============================================================================
    bool loaded = false, linked = false;

    int32_t getSessionID() const
    {
        if (auto sessionID = buildSettings.getSessionID())
            return sessionID;

        return static_cast<int32_t> ((std::rand() & 0xfffff) + 1);
    }

    double getFrequency() const
    {
        auto f = buildSettings.getFrequency();
        return f > 1.0 ? f : 44100.0;
    }

    //==============================================================================
    struct Performer  : public choc::com::ObjectWithAtomicRefCount<PerformerInterface, Performer>
    {
        Performer (int32_t s, double f) : sessionID (s), frequency (f)
        {
            generatedObject.initialise (sessionID, frequency);
        }

        virtual ~Performer() = default;

        Result setBlockSize (uint32_t numFramesForNextBlock) override
        {
            currentBlockSize = numFramesForNextBlock;

            return Result::Ok;
        }

        Result reset() override
        {
            generatedObject.initialise (sessionID, frequency);
            return Result::Ok;
        }

        Result advance() override
        {
            generatedObject.advance (static_cast<int32_t> (currentBlockSize));
            return Result::Ok;
        }

        Result setInputFrames (EndpointHandle endpoint, const void* frameData, uint32_t numFrames) override
        {
            generatedObject.setInputFrames (endpoint, frameData, numFrames,
                                            currentBlockSize > numFrames ? currentBlockSize - numFrames : 0);
            return Result::Ok;
        }

        Result setInputValue (EndpointHandle endpoint, const void* valueData, uint32_t numFramesToReachValue) override
        {
            generatedObject.setValue (endpoint, valueData, static_cast<int32_t> (numFramesToReachValue));
            return Result::Ok;
        }

        Result addInputEvent (EndpointHandle endpoint, uint32_t typeIndex, const void* eventData) override
        {
            generatedObject.addEvent (endpoint, typeIndex, (const unsigned char*) eventData);
            return Result::Ok;
        }

        Result copyOutputValue (EndpointHandle endpoint, void* dest) override
        {
            generatedObject.copyOutputValue (endpoint, dest);
            return Result::Ok;
        }

        Result copyOutputFrames (EndpointHandle endpoint, void* dest, uint32_t numFramesToCopy) override
        {
            generatedObject.copyOutputFrames (endpoint, dest, numFramesToCopy);
            return Result::Ok;
        }

        Result iterateOutputEvents (EndpointHandle endpoint, void* context, PerformerInterface::HandleOutputEventCallback callback) override
        {
            if (auto numEvents = generatedObject.getNumOutputEvents (endpoint))
            {
                if (numEvents > GeneratedCppClass::eventBufferSize)
                {
                    numEvents = GeneratedCppClass::eventBufferSize;
                    ++xruns;
                }

                for (uint32_t i = 0; i < numEvents; ++i)
                {
                    uint8_t data[GeneratedCppClass::maxOutputEventSize + 1];
                    auto frame = generatedObject.readOutputEvent (endpoint, i, data);
                    auto type = generatedObject.getOutputEventType (endpoint, i);
                    auto dataSize = generatedObject.getOutputEventDataSize (endpoint, type);

                    if (! callback (context, endpoint, type, frame, data, dataSize))
                        break;
                }

                generatedObject.resetOutputEventCount (endpoint);
            }
            return Result::Ok;
        }

        const char* getStringForHandle (uint32_t handle, size_t& stringLength) override
        {
            return generatedObject.getStringForHandle (handle, stringLength);
        }

        uint32_t getXRuns() override            { return xruns; }
        const char* getRuntimeError() override  { return {}; }

        uint32_t getMaximumBlockSize() override { return GeneratedCppClass::maxFramesPerBlock; }
        double getLatency() override            { return GeneratedCppClass::latency; }
        uint32_t getEventBufferSize() override  { return GeneratedCppClass::eventBufferSize; }

        GeneratedCppClass generatedObject;
        uint32_t currentBlockSize = 1;
        uint32_t xruns = 0;
        int32_t sessionID;
        double frequency;
    };
};

//==============================================================================
/// Takes a C++ class that the Cmajor code-generator produced, and returns an
/// Engine instance that represents it.
template <typename GeneratedCppClass>
Engine createEngineForGeneratedCppProgram()
{
    Engine e;
    e.engine = choc::com::create<GeneratedCppEngine<GeneratedCppClass>>();
    return e;
}


} // namespace cmaj
