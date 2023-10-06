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

#include "../../include/cmaj_ErrorHandling.h"
#include "../../../../cmajor/include/cmajor/COM/cmaj_EngineFactoryInterface.h"
#include <iostream>
#include "../AST/cmaj_AST.h"
#include "../codegen/cmaj_GraphGenerator.h"
#include "../transformations/cmaj_Transformations.h"
#include "CPlusPlus/cmaj_CPlusPlus.h"
#include "WebAssembly/cmaj_WebAssembly.h"
#include "LLVM/cmaj_LLVM.h"

namespace cmaj
{

struct EndpointInfo
{
    EndpointHandle handle;
    AST::EndpointDeclaration& endpoint;
    EndpointDetails details;
};


//==============================================================================
template <typename Implementation>
struct EngineBase  : public choc::com::ObjectWithAtomicRefCount<EngineInterface, EngineBase<Implementation>>
{
    EngineBase (const char* engineCreationOptions)
    {
        if (engineCreationOptions != nullptr)
            options = choc::json::parse (engineCreationOptions);

        implementation = std::make_unique<Implementation> (*this);

        buildSettings.setSessionID (createRandomSessionID());
    }

    virtual ~EngineBase()        { unload(); }

    //==============================================================================
    choc::value::Value options;
    BuildSettings buildSettings;
    std::unique_ptr<Implementation> implementation;
    ptr<AST::ProcessorBase> mainProcessor;
    ptr<AST::Program> program;
    ptr<AST::Program> newProgram;
    ProgramPtr loadedProgram;
    choc::com::StringPtr loadedProgramDetailsJSON;
    std::shared_ptr<typename Implementation::LinkedCode> linkedCode;
    CompilePerformanceTimes compilePerformanceTimes;
    std::vector<EndpointInfo> endpointHandles;
    uint32_t nextHandle = 1;

    //==============================================================================
    choc::com::String* getBuildSettings() override
    {
        return choc::com::createRawString (buildSettings.toJSON());
    }

    void setBuildSettings (const char* newSettings) override
    {
        buildSettings.mergeValues (BuildSettings::fromJSON (std::string_view (newSettings)));
    }

    static int32_t createRandomSessionID()
    {
        std::random_device seed;
        std::mt19937 rng (seed());
        std::uniform_int_distribution<> dist (1, 1000000000);
        return (int32_t) dist (rng);
    }

    //==============================================================================
    AST::Program& getProgram() const                    { return *program; }
    AST::ProcessorBase& getMainProcessor() const        { return *mainProcessor; }

    void unload() override
    {
        linkedCode.reset();
        mainProcessor = {};
        endpointHandles.clear();
        loadedProgram.reset();
        newProgram.reset();
        program.reset();
        loadedProgramDetailsJSON = {};
    }

    bool isLoaded() override        { return loadedProgram != nullptr; }
    bool isLinked() override        { return linkedCode != nullptr; }

    choc::com::String* getProgramDetails() override
    {
        if (loadedProgram == nullptr)
            return nullptr;

        return loadedProgramDetailsJSON.getWithIncrementedRefCount();
    }

    choc::com::StringPtr createProgramDetails() const
    {
        auto details = choc::value::createObject ({});

        if (mainProcessor != nullptr)
        {
            details.setMember ("mainProcessor", mainProcessor->getFullyQualifiedReadableName());

            if (auto location = mainProcessor->context.getFullLocation().getLocationDescription(); ! location.empty())
                details.setMember ("mainProcessorLocation", location);
        }

        details.setMember ("inputs",  getProgram().endpointList.inputEndpointDetails.toJSON (true));
        details.setMember ("outputs", getProgram().endpointList.outputEndpointDetails.toJSON (true));

        return choc::com::createString (choc::json::toString (details, true));
    }

    choc::com::String* load (ProgramInterface* programToLoad,
                             void* variableContext, EngineInterface::RequestExternalVariableFn requestExternalVariable,
                             void* functionContext, EngineInterface::RequestExternalFunctionFn requestExternalFunction) override
    {
        unload();

        return AST::catchAllErrorsAsJSON (buildSettings.shouldIgnoreWarnings(), [&]
        {
            auto pc = compilePerformanceTimes.getCounter ("load");

            if (programToLoad == nullptr)
                throwError (Errors::emptyProgram());

            newProgram = AST::getProgram (*programToLoad);

            if (! newProgram->prepareForLoading())
                throwError (Errors::invalidProgram());

            if (options.isObject() && options.hasObjectMember ("validatePrint"))
            {
                auto s = AST::print (*newProgram);
                AST::Program p;

                if (auto result = p.parse ({}, s.data(), s.length()))
                    throwError (Errors::staticAssertionFailureWithMessage ("RoundTrip error: " + std::string (result->get())));
            }

            newProgram->externalVariableManager.setExternalRequestor (requestExternalVariable, variableContext);
            newProgram->externalFunctionManager.setExternalRequestor (requestExternalFunction, functionContext);

            transformations::runBasicResolutionPasses (*newProgram);
            newProgram->setMainProcessor (*newProgram->findMainProcessorCandidate (buildSettings.getMainProcessor()));
            mainProcessor = newProgram->findMainProcessor();

            transformations::prepareForResolution (*newProgram, buildSettings.getMaxStackSize());

            newProgram->endpointList.initialise (*mainProcessor);

            program = newProgram;
            programToLoad->addRef();
            loadedProgram = ProgramPtr (programToLoad);

            loadedProgramDetailsJSON = createProgramDetails();
        });
    }

    choc::com::String* link (CacheDatabaseInterface* cache) override
    {
        if (isLinked())
            return {};

        bool ignoreWarnings = isLoaded() && buildSettings.shouldIgnoreWarnings();
        std::string compileTime, linkTime;

        return AST::catchAllErrorsAsJSON (ignoreWarnings, [&]
        {
            if (! isLoaded())
                throwError (Errors::noProgramLoaded());

            double latency = 0;

            {
                auto pc = compilePerformanceTimes.getCounter ("compile");

                transformations::prepareForCodeGen (*program,
                                                    buildSettings,
                                                    Implementation::canUseForwardBranches,
                                                    Implementation::usesDynamicRateAndSessionID,
                                                    Implementation::allowTopLevelSlices,
                                                    Implementation::supportsExternalFunctions,
                                                    Implementation::engineSupportsIntrinsic,
                                                    latency,
                                                    [this] (const EndpointID& e) { return isEndpointActive (e); });
            }

            {
                auto pc = compilePerformanceTimes.getCounter ("link");

                std::string cacheKey;

                if (cache != nullptr)
                    cacheKey = getCacheKey();

                bool isSingleFrameOnly = buildSettings.getMaxBlockSize() == 1;
                linkedCode = std::make_shared<typename Implementation::LinkedCode> (*implementation, isSingleFrameOnly,
                                                                                    latency, cache, cacheKey.c_str());
            }
        });
    }

    choc::com::String* getLastBuildLog() override
    {
        return choc::com::createRawString (compilePerformanceTimes.getResults());
    }

    std::string getCacheKey()
    {
        auto hash = getProgram().codeHash;
        hash.addInput (implementation->getEngineVersion());
        hash.addInput (BuildSettings (buildSettings).setSessionID (0).toJSON());

        return std::string (mainProcessor->getName()) + "_" + choc::text::createHexString (hash.getHash());
    }

    //==============================================================================
    bool isEndpointActive (const EndpointID& endpointID)
    {
        for (auto& e : endpointHandles)
            if (e.details.endpointID == endpointID)
                return true;

        return false;
    }

    EndpointHandle getEndpointHandle (const char* idToFind) override
    {
        if (idToFind == nullptr)
            return {};

        auto endpointID = EndpointID::create (std::string_view (idToFind));

        for (auto& e : endpointHandles)
            if (e.details.endpointID == endpointID)
                return e.handle;

        for (auto& e : getProgram().endpointList.endpoints)
        {
            if (e.details.endpointID == endpointID)
            {
                EndpointInfo info { nextHandle++, e.endpoint, e.details };
                endpointHandles.push_back (info);
                return info.handle;
            }
        }

        return {};
    }

    bool setExternalVariable (const char* name, const void* serialisedValueData, size_t serialisedValueDataSize) override
    {
        auto data = static_cast<const uint8_t*> (serialisedValueData);
        choc::value::InputData input { data, data + serialisedValueDataSize };
        bool ok = false;

        choc::value::ValueView::deserialise (input, [&] (const choc::value::ValueView& value)
        {
            ok = newProgram->externalVariableManager.setValue (name, value);
        });

        return ok;
    }

    PerformerInterface* createPerformer() override
    {
        if (linkedCode != nullptr)
            return implementation->createPerformer (linkedCode);

        return {};
    }

    //==============================================================================
    const char* getAvailableCodeGenTargetTypes() override
    {
        static std::string availableTargets;

        if (availableTargets.empty())
        {
            availableTargets = "graph";

           #if CMAJ_ENABLE_CODEGEN_CPP
            availableTargets.append (" cpp");
           #endif

           #if CMAJ_ENABLE_CODEGEN_BINARYEN || CMAJ_ENABLE_CODEGEN_LLVM_WASM
            availableTargets.append (" wasm wast");
           #endif

           #if CMAJ_ENABLE_PERFORMER_LLVM
            availableTargets.append (" " + choc::text::joinStrings(::cmaj::llvm::getAssemberTargets(), " "));
           #endif
        }

        return availableTargets.c_str();
    }

    void generateCode (const char* targetType,
                       const char* optionsJSON,
                       void* callbackContext,
                       EngineInterface::HandleCodeGenOutput callback) override
    {
        cmaj::DiagnosticMessageList messages;
        std::string output, mainClassName;

        cmaj::catchAllErrors (messages, [&]
        {
            if (! isLoaded())
                throwError (Errors::noProgramLoaded());

            if (isLinked())
                throwError (Errors::cannotGenerateIfLinked());

            if (targetType == nullptr || std::string_view (targetType).empty())
                throw std::runtime_error ("Must specify a code generation target type");

            auto type = std::string_view (targetType);
            bool useForwardBranch = (type == "cpp");

            if (type == "graph")
            {
                cmaj::transformations::prepareForGraphGen (*program,
                                                           buildSettings.getFrequency(),
                                                           buildSettings.getMaxStackSize());

                cmaj::GraphVizGenerator generator (*program);
                output = generator.createGraphSVG();
                return;
            }

            double latency;

            std::function<bool(AST::Intrinsic::Type)> engineSupportsIntrinsic
                = [] (AST::Intrinsic::Type) -> bool { return true; };

            if (choc::text::startsWith (type, "javascript") || type == "wast")
                engineSupportsIntrinsic = [] (AST::Intrinsic::Type) -> bool { return false; };

            cmaj::transformations::prepareForCodeGen (*program,
                                                      buildSettings,
                                                      useForwardBranch,
                                                      true, // dynamic rate + session ID
                                                      true,
                                                      true,
                                                      engineSupportsIntrinsic,
                                                      latency,
                                                      [this] (const EndpointID& e) { return isEndpointActive (e); });

            bool outputTypeKnown = false;
            auto optionsString = optionsJSON != nullptr ? std::string_view (optionsJSON) : std::string_view();
            (void) optionsString;

           #if CMAJ_ENABLE_CODEGEN_CPP
            if (type == "cpp")
            {
                auto result = cmaj::cplusplus::generateCPPClass (*program,
                                                                 optionsString,
                                                                 buildSettings.getMaxFrequency(),
                                                                 buildSettings.getMaxBlockSize(),
                                                                 buildSettings.getEventBufferSize(),
                                                                 [this] (const EndpointID& e) { return getEndpointHandle (e); });

                output = result.code;
                mainClassName = result.mainClassName;
                outputTypeKnown = true;
            }
           #endif

           #if CMAJ_ENABLE_CODEGEN_LLVM_WASM || CMAJ_ENABLE_CODEGEN_BINARYEN
            if (choc::text::startsWith (type, "javascript"))
            {
                bool useBinaryen = choc::text::endsWith (type, "binaryen");
                auto result = cmaj::webassembly::generateJavascriptWrapper (*program, buildSettings, useBinaryen);
                output = result.code;
                mainClassName = result.mainClassName;
                outputTypeKnown = true;
            }

            if (type == "wast")
            {
                output = cmaj::webassembly::generateWAST (*program, buildSettings);
                outputTypeKnown = true;
            }
           #endif

           #if CMAJ_ENABLE_PERFORMER_LLVM
            if (choc::text::startsWith (type, "llvm"))
            {
                auto opt = choc::json::parse (optionsString.empty() ? "{}" : optionsString);

                if (type.length() > 5)
                    opt.addMember("targetTriple", type.substr (5));

                output = cmaj::llvm::generateAssembler (*program, buildSettings, opt);
                outputTypeKnown = true;
            }
           #endif

            if (! outputTypeKnown)
                throw std::runtime_error ("Unknown code generation target '" + std::string (type) + "'");
        });

        unload();

        callback (callbackContext,
                  output.data(),
                  output.length(),
                  mainClassName.empty() ? nullptr : mainClassName.c_str(),
                  messages.empty()      ? nullptr : choc::json::toString (messages.toJSON(), true).c_str());
    }
};


//==============================================================================
template <typename JITInstance>
struct PerformerBase  : public choc::com::ObjectWithAtomicRefCount<cmaj::PerformerInterface, PerformerBase<JITInstance>>
{
    template <typename EngineType, typename LinkedCode>
    PerformerBase (std::shared_ptr<LinkedCode> linkedCode, const EngineType& engine)
        : jit (linkedCode, engine.buildSettings.getSessionID(), engine.buildSettings.getFrequency()),
          maxBlockSize (engine.buildSettings.getMaxBlockSize()),
          eventBufferSize (engine.buildSettings.getEventBufferSize()),
          latency (linkedCode->latency)
    {
        initialiseEndpointList (engine.endpointHandles);
    }

    virtual ~PerformerBase() = default;

    //==============================================================================
    void setBlockSize (uint32_t numFramesForNextBlock) override
    {
        CMAJ_ASSERT (numFramesForNextBlock != 0 && numFramesForNextBlock <= maxBlockSize);
        numFramesToDo = numFramesForNextBlock;
    }

    void setInputFrames (EndpointHandle handle, const void* frameData, uint32_t numFrames) override
    {
        getEndpointHandler (handle).setInputFrames (frameData, numFrames, numFramesToDo);
    }

    void setInputValue (EndpointHandle handle, const void* valueData, uint32_t numFramesToReachValue) override
    {
        getEndpointHandler (handle).setInputValue (valueData, numFramesToReachValue);
    }

    void addInputEvent (EndpointHandle handle, uint32_t typeIndex, const void* eventData) override
    {
        getEndpointHandler (handle).addInputEvent (typeIndex, eventData);
    }

    void copyOutputValue (EndpointHandle handle, void* dest) override
    {
        getEndpointHandler (handle).copyOutputValue (dest);
    }

    void copyOutputFrames (EndpointHandle handle, void* dest, uint32_t numFramesToCopy) override
    {
        getEndpointHandler (handle).copyOutputFrames (dest, numFramesToCopy);
    }

    void iterateOutputEvents (EndpointHandle handle, void* context, PerformerInterface::HandleOutputEventCallback handler) override
    {
        getEndpointHandler (handle).iterateOutputEvents (context, handler);
    }

    void advance() override
    {
        jit.advance (numFramesToDo);

        for (auto& e : outputEventHandlers)
            e->moveOutputEventsToQueue();
    }

    uint32_t getMaximumBlockSize() override     { return maxBlockSize; }
    double getLatency() override                { return latency; }
    uint32_t getEventBufferSize() override      { return eventBufferSize; }
    uint32_t getXRuns() override                { return xruns; }
    const char* getRuntimeError() override      { return {}; }

    const char* getStringForHandle (uint32_t handle, size_t& stringLength) override
    {
        try
        {
            auto s = jit.getDictionary().getStringForHandle (choc::value::StringDictionary::Handle { handle });
            stringLength = s.length();
            return s.data();
        }
        catch (...) {}

        stringLength = 0;
        return nullptr;
    }

    void registerXRun() { ++xruns; }

private:
    JITInstance jit;

    uint32_t numFramesToDo = 0,
             xruns = 0;

    const uint32_t maxBlockSize, eventBufferSize;
    const double latency;

    //==============================================================================
    void initialiseEndpointList (const std::vector<EndpointInfo>& endpoints)
    {
        if (endpoints.empty())
            return;

        firstHandle = endpoints.front().handle;
        lastHandle = firstHandle;

        for (auto& endpoint : endpoints)
        {
            CMAJ_ASSERT (endpoint.handle == lastHandle); // handles must be in order
            ++lastHandle;

            if (endpoint.details.isInput)
            {
                if (endpoint.details.isEvent())
                    endpointHandlers.push_back (std::make_unique<InputEventHandler> (*this, endpoint));
                else if (endpoint.details.isStream())
                    endpointHandlers.push_back (std::make_unique<InputStreamHandler> (*this, endpoint));
                else
                    endpointHandlers.push_back (std::make_unique<InputValueHandler> (*this, endpoint));
            }
            else if (endpoint.details.isEvent())
            {
                auto h = std::make_unique<OutputEventHandler> (*this, endpoint);
                outputEventHandlers.push_back (h.get());
                endpointHandlers.push_back (std::move (h));
            }
            else
            {
                auto h = std::make_unique<OutputStreamOrValueHandler> (*this, endpoint);
                endpointHandlers.push_back (std::move (h));
            }
        }
    }

    //==============================================================================
    struct EndpointHandler
    {
        EndpointHandler() = default;
        virtual ~EndpointHandler() = default;

        virtual void setInputFrames (const void*, uint32_t, uint32_t)           { CMAJ_ASSERT_FALSE; }
        virtual void setInputValue (const void*, uint32_t)                      { CMAJ_ASSERT_FALSE; }
        virtual void addInputEvent (uint32_t, const void*)                      { CMAJ_ASSERT_FALSE; }
        virtual void copyOutputValue (void*)                                    { CMAJ_ASSERT_FALSE; }
        virtual void copyOutputFrames (void*, uint32_t)                         { CMAJ_ASSERT_FALSE; }
        virtual void iterateOutputEvents (void*, PerformerInterface::HandleOutputEventCallback)  { CMAJ_ASSERT_FALSE; }
    };

    //==============================================================================
    struct InputStreamHandler  : public EndpointHandler
    {
        InputStreamHandler (PerformerBase& p, const EndpointInfo& endpoint) : owner (p)
        {
            setInputStreamFrames = owner.jit.createSetInputStreamFramesFunction (endpoint);
        }

        void setInputFrames (const void* frameData, uint32_t numFrames, uint32_t framesForBlock) override
        {
            if (numFrames == framesForBlock)
            {
                setInputStreamFrames (frameData, numFrames, 0);
            }
            else
            {
                owner.registerXRun();

                if (numFrames > framesForBlock)
                    numFrames = framesForBlock;

                setInputStreamFrames (frameData, numFrames, framesForBlock - numFrames);
            }
        }

        PerformerBase& owner;
        std::function<void(const void*, uint32_t, uint32_t)> setInputStreamFrames;
    };

    //==============================================================================
    struct InputValueHandler  : public EndpointHandler
    {
        InputValueHandler (PerformerBase& owner, const EndpointInfo& endpoint)
        {
            setInputValueFn = owner.jit.createSetInputValueFunction (endpoint);
        }

        void setInputValue (const void* valueData, uint32_t numFramesToReachValue) override
        {
            setInputValueFn (valueData, numFramesToReachValue);
        }

        std::function<void(const void*, uint32_t)> setInputValueFn;
        uint32_t dataTypeSize = 0;
    };

    //==============================================================================
    struct InputEventHandler : public EndpointHandler
    {
        InputEventHandler (PerformerBase& owner, const EndpointInfo& endpoint)
        {
            for (auto& dataType : endpoint.endpoint.dataTypes)
            {
                auto& t = AST::castToRefSkippingReferences<AST::TypeBase> (dataType);
                std::function<void(const void*)> handler;

                if (auto handlerFunction = AST::findEventHandlerFunction (endpoint.endpoint, t))
                    handler = owner.jit.createSendEventFunction (endpoint, t, *handlerFunction);
                else
                    handler = [] (const void*) {};

                auto type = t.toChocType();
                auto size = static_cast<uint32_t> (type.getValueDataSize());

                typeHandlers.push_back ({ std::move (type), size, std::move (handler) });
            }
        }

        void addInputEvent (uint32_t typeIndex, const void* eventData) override
        {
            CMAJ_ASSERT (typeIndex < typeHandlers.size());
            typeHandlers[typeIndex].handler (eventData);
        }

        struct TypeHandler
        {
            choc::value::Type type;
            uint32_t dataSize = 0;
            std::function<void(const void*)> handler;
        };

        std::vector<TypeHandler> typeHandlers;
    };

    //==============================================================================
    struct OutputStreamOrValueHandler  : public EndpointHandler
    {
        OutputStreamOrValueHandler (PerformerBase& owner, const EndpointInfo& endpoint)
        {
            copyOutputValueFn = owner.jit.createCopyOutputValueFunction (endpoint);
            isStream = endpoint.details.isStream();
        }

        void copyOutputValue (void* dest) override
        {
            copyOutputValueFn (dest, 1);
        }

        void copyOutputFrames (void* dest, uint32_t numFramesToCopy) override
        {
            copyOutputValueFn (dest, numFramesToCopy);
        }

        uint32_t dataTypeSize = 0;
        bool isStream = false;

        std::function<void(void*, uint32_t)> copyOutputValueFn;
    };

    //==============================================================================
    struct OutputEventHandler : public EndpointHandler
    {
        OutputEventHandler (PerformerBase& p, const EndpointInfo& endpoint)
            : owner (p), handle (endpoint.handle)
        {
            getNumOutputEvents = owner.jit.createGetNumOutputEventsFunction (endpoint);
            getEventTypeIndex  = owner.jit.createGetEventTypeIndexFunction (endpoint);
            readOutputEvent    = owner.jit.createReadOutputEventFunction (endpoint);
            resetEventCount    = owner.jit.createResetEventCountFunction (endpoint);

            queue.initialise (endpoint.details, owner.eventBufferSize);
        }

        void iterateOutputEvents (void* context, PerformerInterface::HandleOutputEventCallback handler) override
        {
            auto numEvents = queue.numEvents;

            for (uint32_t i = 0; i < numEvents; ++i)
            {
                auto& event = queue.getEvent (i);

                if (! handler (context, handle, event.type, event.frame, event.data, queue.eventSizes[event.type]))
                    break;
            }
        }

        void moveOutputEventsToQueue()
        {
            CMAJ_ASSERT (getNumOutputEvents != nullptr);

            if (auto numEvents = getNumOutputEvents())
            {
                if (numEvents > queue.maxNumEvents)
                {
                    numEvents = queue.maxNumEvents;
                    owner.registerXRun();
                }

                for (uint32_t i = 0; i < numEvents; ++i)
                {
                    auto& event = queue.getEvent (i);
                    event.type  = getEventTypeIndex (i);
                    event.frame = readOutputEvent (i, event.data);
                }

                queue.numEvents = numEvents;
                resetEventCount();
            }
            else
            {
                queue.numEvents = 0;
            }
        }

        struct OutputEventQueue
        {
            struct Event
            {
                uint32_t frame = 0, type = 0;
                uint64_t data[1];
            };

            void initialise (const EndpointDetails& details, uint32_t maxNumEventsToUse)
            {
                numEvents = 0;
                maxNumEvents = maxNumEventsToUse;
                size_t maxEventDataSize = 0;

                for (auto& t : details.dataTypes)
                {
                    auto size = t.getValueDataSize();
                    maxEventDataSize = std::max (maxEventDataSize, size);
                    eventSizes.push_back (static_cast<uint32_t> (size));
                }

                eventStride = ((sizeof (Event) + maxEventDataSize) + 7u) & ~7u;
                eventSpace.resize (maxNumEvents * eventStride);
            }

            Event& getEvent (size_t index) noexcept
            {
                return *reinterpret_cast<Event*> (eventSpace.data() + eventStride * index);
            }

            uint32_t numEvents = 0;
            uint32_t maxNumEvents = 0;

            std::vector<uint32_t> eventSizes;

        private:
            size_t eventStride = 0;
            std::vector<uint8_t> eventSpace;
        };

        PerformerBase& owner;
        EndpointHandle handle;
        OutputEventQueue queue;

        std::function<uint32_t()>                 getNumOutputEvents;
        std::function<uint32_t(uint32_t)>         getEventTypeIndex;
        std::function<uint32_t(uint32_t, void*)>  readOutputEvent;
        std::function<void()>                     resetEventCount;
    };

    //==============================================================================
    std::vector<std::unique_ptr<EndpointHandler>> endpointHandlers;
    uint32_t firstHandle = 0, lastHandle = 0;
    std::vector<OutputEventHandler*> outputEventHandlers;

    EndpointHandler& getEndpointHandler (EndpointHandle handle)
    {
        CMAJ_ASSERT (handle >= firstHandle && handle < lastHandle);
        return *endpointHandlers[handle - firstHandle];
    }
};

}
