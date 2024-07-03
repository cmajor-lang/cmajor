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

#include "../../../include/cmaj_DefaultFlags.h"

#if CMAJ_ENABLE_PERFORMER_LLVM || CMAJ_ENABLE_CODEGEN_LLVM_WASM

#include "../../../include/cmaj_ErrorHandling.h"
#include "../../../../../include/cmajor/COM/cmaj_EngineFactoryInterface.h"

#include "cmaj_LLVM.h"

#ifndef CMAJ_LLVM_RUN_VERIFIER
 #define CMAJ_LLVM_RUN_VERIFIER 0
#endif

#undef DEBUG
#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

#include "choc/platform/choc_DisableAllWarnings.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/MC/TargetRegistry.h"

#include "choc/platform/choc_ReenableAllWarnings.h"
#include "choc/memory/choc_AlignedMemoryBlock.h"
#include "choc/text/choc_Files.h"
#include "choc/text/choc_OpenSourceLicenseList.h"

#include "../../codegen/cmaj_CodeGenerator.h"
#include "../cmaj_EngineBase.h"

#include "cmaj_LLVMGenerator.h"

namespace cmaj::llvm
{

static void initialiseLLVM()
{
    struct LLVMInit
    {
        LLVMInit()
        {
            LLVMInitializeAllTargetInfos();
            LLVMInitializeAllTargets();
            LLVMInitializeAllTargetMCs();
            LLVMInitializeAllAsmPrinters();
        }
    };

    static LLVMInit initialiser;
}

#if CMAJ_ENABLE_PERFORMER_LLVM

struct LLJITHolder
{
    LLJITHolder (int optimisationLevel)
    {
        ::llvm::sys::DynamicLibrary::LoadLibraryPermanently (nullptr);

        if (auto machineBuilder = ::llvm::orc::JITTargetMachineBuilder::detectHost())
        {
            auto targetTriple = machineBuilder->getTargetTriple();
            auto& opts = machineBuilder->getOptions();
            opts.ExceptionModel = ::llvm::ExceptionHandling::None;
            opts.setFPDenormalMode (::llvm::DenormalMode::getPositiveZero());
            opts.setFP32DenormalMode (::llvm::DenormalMode::getPositiveZero());

            machineBuilder->setCodeGenOptLevel (getCodeGenOptLevel (optimisationLevel));

            ::llvm::orc::LLJITBuilder builder;
            builder.setJITTargetMachineBuilder (machineBuilder.get());

            // Avoid the special case ObjectLinkingLayer created by lljit when it's the wrong thing to do
            if (targetTriple.isOSBinFormatMachO())
            {
                builder.setObjectLinkingLayerCreator ([](::llvm::orc::ExecutionSession &es, const ::llvm::Triple &) -> ::llvm::Expected<std::unique_ptr<::llvm::orc::ObjectLayer>>
                                                      {
                                                          auto memoryManager = []() { return std::make_unique<::llvm::SectionMemoryManager>(); };
                                                          return std::make_unique<::llvm::orc::RTDyldObjectLinkingLayer>(es, std::move(memoryManager));
                                                      });
            }

            if (auto jit = builder.create())
            {
                if (auto e = jit.takeError())
                    throwError (Errors::failedToJit (toString (std::move (e))));

                lljit = std::move (*jit);

                if (auto gen = ::llvm::orc::DynamicLibrarySearchGenerator
                                    ::GetForCurrentProcess (lljit->getDataLayout().getGlobalPrefix()))
                    lljit->getMainJITDylib().addGenerator (std::move (*gen));

                return;
            }
        }

        CMAJ_ASSERT_FALSE;
    }

    void load (::llvm::orc::ThreadSafeModule&& module)
    {
        auto err = lljit->addIRModule (std::move (module));
        CMAJ_ASSERT (! err);
        err = lljit->initialize (lljit->getMainJITDylib());
        CMAJ_ASSERT (! err);
    }

    void addExternalFunctionSymbols (const std::unordered_map<std::string, void*>& functionPointers)
    {
        auto& processSymbols = lljit->getMainJITDylib();

        for (auto& f : functionPointers)
        {
            auto mangledName = lljit->mangleAndIntern (f.first);
            auto pointer = ::llvm::JITEvaluatedSymbol::fromPointer (f.second);

          #if (LLVM_VERSION_MAJOR <= 15)
            if (processSymbols.define (::llvm::orc::absoluteSymbols ({{ mangledName, pointer }})))
            {
                // handle failure?
            }
          #else
            auto symbol = ::llvm::orc::ExecutorSymbolDef (::llvm::orc::ExecutorAddr (pointer.getAddress()), pointer.getFlags());

            if (processSymbols.define (::llvm::orc::absoluteSymbols ({{ mangledName, symbol }})))
            {
                // handle failure?
            }
          #endif
        }
    }

    void* findSymbol (std::string_view name)
    {
        if (auto result = lljit->lookup (std::string (name)))
            return reinterpret_cast<void*> (result.get().getValue());

        return nullptr;
    }

    std::string getTargetTriple() const         { return lljit->getTargetTriple().normalize(); }
    const ::llvm::DataLayout& getDataLayout()   { return lljit->getDataLayout(); }

private:
    std::unique_ptr<::llvm::orc::LLJIT> lljit;

    static ::llvm::CodeGenOpt::Level getCodeGenOptLevel (int level)
    {
        switch (LLVMCodeGenerator::getOptimisationLevelWithDefault (level))
        {
            // NB: If we skip optimisation altogether, then some large array alloca operations
            // spew out millions of instructions and cause llvm to meltdown.. so if asked for -O0,
            // we'll actually do -O1, which seems to mitigate the troublesome examples we've found.
            case 0:    return ::llvm::CodeGenOpt::Level::Less;
            case 1:    return ::llvm::CodeGenOpt::Level::Less;
            case 2:    return ::llvm::CodeGenOpt::Level::Default;
            case 3:    return ::llvm::CodeGenOpt::Level::Aggressive;
            default:   return ::llvm::CodeGenOpt::Level::Default;
        }
    }
};

//==============================================================================
//==============================================================================
struct LLVMEngine
{
    LLVMEngine (EngineBase<LLVMEngine>& e) : engine (e)
    {
        initialiseLLVM();
    }

    EngineBase<LLVMEngine>& engine;

    static std::string getEngineVersion()   { return "llvm1"; }

    static constexpr bool canUseForwardBranches = true;
    static constexpr bool usesDynamicRateAndSessionID = false;
    static constexpr bool allowTopLevelSlices = false;
    static constexpr bool supportsExternalFunctions = true;
    static bool engineSupportsIntrinsic (AST::Intrinsic::Type) { return true; }

    using InitialiseFn       = void*(*)(void*, int32_t*, int32_t, double);
    using AdvanceOneFrameFn  = void(*)(void*, void*);
    using AdvanceBlockFn     = void(*)(void*, void*, uint32_t);
    using SetValueRampFn     = void(*)(void*, const void*, uint32_t);

    //==============================================================================
    struct LinkedCode
    {
        LinkedCode (LLVMEngine& llvmEngine, bool isSingleFrameOnly, double latencyToUse,
                    CacheDatabaseInterface* cache, const char* cacheKey)
           : lljit (llvmEngine.engine.buildSettings.getOptimisationLevel()),
             latency (latencyToUse)
        {
            LLVMCodeGenerator codeGen (*llvmEngine.engine.program,
                                       llvmEngine.engine.options,
                                       llvmEngine.engine.buildSettings,
                                       lljit.getTargetTriple(),
                                       lljit.getDataLayout(),
                                       stringDictionary,
                                       false);

            codeGen.addNativeOverriddenFunctions (llvmEngine.engine.program->externalFunctionManager);

            bool loadedFromCache = loadFromCache (codeGen, cache, cacheKey);

            if (! (loadedFromCache || codeGen.generate()))
            {
                CMAJ_ASSERT_FALSE;
            }

            nativeTypeLayouts.createLayout = [&codeGen] (const AST::TypeBase& t) { return codeGen.createNativeTypeLayout (t); };

            stateSize = codeGen.getStateSize();
            ioSize = codeGen.getIOSize();

            auto alignmentBits = std::max (codeGen.getStateAlignment(), codeGen.getIOAlignment());

            if (alignmentBits > alignmentBytes * 8)
                throwError (Errors::failedToLink ("Memory alignment requirements not met"));

            initialiseEndpointHandlers (codeGen, llvmEngine.engine.endpointHandles);

            if (cache != nullptr && ! loadedFromCache)
                codeGen.saveBitcodeToCache (*cache, cacheKey);

            lljit.addExternalFunctionSymbols (codeGen.externalFunctionPointers);
            lljit.load (codeGen.takeCompiledModule());

            loadFunction (initialiseFn, LLVMCodeGenerator::getInitFunctionName());

            if (isSingleFrameOnly)
                loadFunction (advanceOneFrameFn, LLVMCodeGenerator::getAdvanceOneFrameFunctionName());
            else
                loadFunction (advanceBlockFn, LLVMCodeGenerator::getAdvanceBlockFunctionName());

            for (auto& e : inputValues)
                loadFunction (e.setValue, e.setValueFnName);
        }

        //==============================================================================
        LLJITHolder lljit;
        choc::value::SimpleStringDictionary stringDictionary;
        NativeTypeLayoutCache nativeTypeLayouts;
        size_t stateSize = 0, ioSize = 0;
        static constexpr size_t alignmentBytes = 128;

        double latency;

        InitialiseFn        initialiseFn = {};
        AdvanceOneFrameFn   advanceOneFrameFn = {};
        AdvanceBlockFn      advanceBlockFn = {};

        //==============================================================================
        struct InputStreamEndpoint
        {
            EndpointHandle handle;
            size_t addressOffset = 0, frameSize = 0, frameStride = 0;
            ptr<const NativeTypeLayout> frameLayout;
        };

        struct InputValueEndpoint
        {
            EndpointHandle handle;
            size_t dataSize = 0;
            ptr<const NativeTypeLayout> layout;
            std::string setValueFnName;
            SetValueRampFn setValue = {};
        };

        struct OutputStreamEndpoint
        {
            EndpointHandle handle;
            size_t addressOffset = 0, frameSize = 0, frameStride = 0;
            ptr<const NativeTypeLayout> frameLayout;
        };

        struct OutputValueEndpoint
        {
            EndpointHandle handle;
            size_t addressOffset = 0;
            ptr<const NativeTypeLayout> layout;
        };

        struct OutputEventEndpoint
        {
            EndpointHandle handle;
            size_t eventCountAddressOffset = 0, eventListStartAddressOffset = 0,
                   eventListElementStride = 0, typeFieldOffset = 0;

            struct EventTypeHandler
            {
                uint32_t offset = 0;
                ptr<const NativeTypeLayout> layout;
            };

            std::vector<EventTypeHandler> eventTypeHandlers;
        };

        std::vector<InputStreamEndpoint>  inputStreams;
        std::vector<InputValueEndpoint>   inputValues;
        std::vector<OutputStreamEndpoint> outputStreams;
        std::vector<OutputValueEndpoint>  outputValues;
        std::vector<OutputEventEndpoint>  outputEvents;

        static bool loadFromCache (LLVMCodeGenerator& codeGen, CacheDatabaseInterface* cache, const char* key)
        {
            if (cache != nullptr)
            {
                if (auto cachedSize = cache->reload (key, nullptr, 0))
                {
                    std::vector<char> loaded;
                    loaded.resize (static_cast<size_t> (cachedSize));

                    if (cache->reload (key, loaded.data(), cachedSize) == cachedSize)
                        return codeGen.generateFromBitcode (loaded);
                }
            }

            return false;
        }

        //==============================================================================
        void initialiseEndpointHandlers (LLVMCodeGenerator& codeGen, const std::vector<EndpointInfo>& endpointArray)
        {
            for (auto& endpoint : endpointArray)
            {
                auto handle = endpoint.handle;
                auto endpointID = endpoint.details.endpointID.toString();

                if (endpoint.details.isInput)
                {
                    if (endpoint.details.isStream())
                    {
                        auto& frameType = endpoint.endpoint.getSingleDataType();

                        inputStreams.push_back ({ handle,
                                                  codeGen.getStructMemberOffset (*codeGen.ioStruct, endpointID),
                                                  frameType.toChocType().getValueDataSize(),
                                                  codeGen.getPaddedTypeSize (frameType),
                                                  nativeTypeLayouts.get (frameType) });
                    }
                    else if (endpoint.details.isValue())
                    {
                        inputValues.push_back ({ handle,
                                                 codeGen.getPaddedTypeSize (endpoint.endpoint.getSingleDataType()),
                                                 nativeTypeLayouts.get (endpoint.endpoint.getSingleDataType()),
                                                 AST::getSetValueFunctionName (endpoint.endpoint),
                                                 nullptr });
                    }
                    else if (endpoint.details.isEvent())
                    {
                        for (auto& dataType : endpoint.endpoint.getDataTypes())
                            nativeTypeLayouts.get (dataType);
                    }
                }
                else
                {
                    if (endpoint.details.isStream())
                    {
                        auto& frameType = endpoint.endpoint.getSingleDataType();

                        outputStreams.push_back ({ handle,
                                                   codeGen.getStructMemberOffset (*codeGen.ioStruct, endpointID),
                                                   frameType.toChocType().getValueDataSize(),
                                                   codeGen.getPaddedTypeSize (frameType),
                                                   nativeTypeLayouts.get (frameType) });
                    }
                    else if (endpoint.details.isValue())
                    {
                        outputValues.push_back ({ handle,
                                                  codeGen.getStructMemberOffset (*codeGen.stateStruct, StreamUtilities::getValueEndpointStructMemberName (endpointID)),
                                                  nativeTypeLayouts.get (endpoint.endpoint.getSingleDataType()) });
                    }
                    else if (endpoint.details.isEvent())
                    {
                        auto& eventListType = *codeGen.stateStruct->getTypeForMember (endpointID);
                        auto eventEntryType = AST::castTo<AST::StructType> (eventListType.getArrayOrVectorElementType());

                        outputEvents.push_back ({ handle,
                                                  codeGen.getStructMemberOffset (*codeGen.stateStruct, EventHandlerUtilities::getEventCountStateMemberName (endpointID)),
                                                  codeGen.getStructMemberOffset (*codeGen.stateStruct, endpointID),
                                                  codeGen.getStructPaddedSize (*eventEntryType),
                                                  codeGen.getStructMemberOffset (*eventEntryType, 1) });

                        for (uint32_t i = 0; i < endpoint.details.dataTypes.size(); ++i)
                        {
                            auto memberIndex = eventEntryType->indexOfMember ("value_" + std::to_string (i));

                            auto& type = (memberIndex >= 0) ? eventEntryType->getMemberType (static_cast<size_t> (memberIndex)) : eventEntryType->context.allocator.createVoidType();

                            if (memberIndex < 0)
                                memberIndex = 0;

                            outputEvents.back().eventTypeHandlers.push_back ({ static_cast<uint32_t> (codeGen.getStructMemberOffset (*eventEntryType, static_cast<uint32_t> (memberIndex))),
                                                                               nativeTypeLayouts.get (type) });
                        }
                    }
                }
            }
        }

        template <typename List>
        auto& getEndpointInfo (const List& endpoints, EndpointHandle handle)
        {
            for (auto& e : endpoints)
                if (e.handle == handle)
                    return e;

            CMAJ_ASSERT_FALSE;
        }

        template <typename Fn>
        void loadFunction (Fn& f, std::string_view name)
        {
            f = reinterpret_cast<Fn> (lljit.findSymbol (name));
            CMAJ_ASSERT (f != nullptr);
        }
    };


    //==============================================================================
    struct JITInstance
    {
        JITInstance (std::shared_ptr<LinkedCode> cc, int32_t s, double f) : code (std::move (cc)), sessionID (s), frequency (f)
        {
            stateMemory.resize (code->stateSize);
            statePointer = static_cast<uint8_t*> (stateMemory.data());

            ioMemory.resize (code->ioSize);
            ioPointer = static_cast<uint8_t*> (ioMemory.data());

            advanceOneFrameFn = code->advanceOneFrameFn;
            advanceBlockFn = code->advanceBlockFn;

            reset();
        }

        //==============================================================================
        std::shared_ptr<LinkedCode> code;
        choc::AlignedMemoryBlock<LinkedCode::alignmentBytes> stateMemory, ioMemory;

        AdvanceOneFrameFn advanceOneFrameFn = {};
        AdvanceBlockFn    advanceBlockFn = {};

        uint8_t* statePointer = nullptr;
        uint8_t* ioPointer = nullptr;
        const int sessionID;
        const double frequency;

        //==============================================================================
        Result reset() noexcept
        {
            stateMemory.clear();
            ioMemory.clear();

            int processorID = 0;
            code->initialiseFn (statePointer, &processorID, sessionID, frequency);

            return Result::Ok;
        }

        void advance (uint32_t framesToAdvance) noexcept
        {
            if (advanceOneFrameFn)
                advanceOneFrameFn (statePointer, ioPointer);
            else
                advanceBlockFn (statePointer, ioPointer, framesToAdvance);
        }

        std::function<Result(void*, uint32_t)> createCopyOutputValueFunction (const EndpointInfo& e)
        {
            if (e.details.isStream())
            {
                auto& info = code->getEndpointInfo (code->outputStreams, e.handle);
                auto* source = ioPointer + info.addressOffset;
                auto destStride = info.frameSize;
                auto sourceStride = info.frameStride;

                if (destStride == sourceStride)
                {
                    return [source, destStride] (void* destBuffer, uint32_t numFrames) -> Result
                    {
                        memcpy (destBuffer, source, destStride * numFrames);
                        memset (source, 0, destStride * numFrames);
                        return Result::Ok;
                    };
                }
                else
                {
                    auto* frameLayout = info.frameLayout.get();

                    return [source, destStride, sourceStride, frameLayout] (void* destBuffer, uint32_t numFrames) -> Result
                    {
                        auto dest = static_cast<uint8_t*> (destBuffer);
                        auto src = source;

                        for (uint32_t i = 0; i < numFrames; ++i)
                        {
                            frameLayout->copyNativeToPacked (dest, src);
                            dest += destStride;
                            src += sourceStride;
                        }

                        memset (source, 0, sourceStride * numFrames);
                        return Result::Ok;
                    };
                }
            }
            else
            {
                auto& info = code->getEndpointInfo (code->outputValues, e.handle);
                auto* source = statePointer + info.addressOffset;
                auto layout = info.layout.get();

                return [layout, source] (void* destBuffer, uint32_t) -> Result
                {
                    layout->copyNativeToPacked (destBuffer, source);
                    return Result::Ok;
                };
            }
        }

        std::function<void(const void*, uint32_t, uint32_t)> createSetInputStreamFramesFunction (const EndpointInfo& e)
        {
            auto& info = code->getEndpointInfo (code->inputStreams, e.handle);
            auto* dest = ioPointer + info.addressOffset;
            auto destStride = info.frameStride;
            auto sourceStride = info.frameSize;

            if (destStride == sourceStride)
            {
                return [dest, destStride] (const void* sourceData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
                {
                    auto source = static_cast<const uint8_t*> (sourceData);
                    auto size = destStride * numFrames;
                    memcpy (dest, source, size);

                    if (numTrailingFramesToClear != 0)
                        memset (dest + size, 0, numTrailingFramesToClear * destStride);
                };
            }
            else
            {
                auto* frameLayout = info.frameLayout.get();

                return [dest, destStride, sourceStride, frameLayout] (const void* sourceData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
                {
                    auto source = static_cast<const uint8_t*> (sourceData);
                    auto d = dest;

                    for (uint32_t i = 0; i < numFrames; ++i)
                    {
                        frameLayout->copyPackedToNative (d, source);
                        d += destStride;
                        source += sourceStride;
                    }

                    if (numTrailingFramesToClear != 0)
                        memset (d, 0, numTrailingFramesToClear * destStride);
                };
            }
        }

        auto createSetInputValueFunction (const EndpointInfo& e)
        {
            auto& info = code->getEndpointInfo (code->inputValues, e.handle);
            auto* layout = info.layout.get();
            auto setValueFn = info.setValue;
            auto state = statePointer;
            choc::AlignedMemoryBlock<16> tempBuffer (info.dataSize);

            return [setValueFn, state, layout, tempBuffer] (const void* valueData, uint32_t numFramesToReachValue) mutable
            {
                auto* buffer = tempBuffer.data();
                layout->copyPackedToNative (buffer, valueData);
                setValueFn (state, buffer, numFramesToReachValue);
            };
        }

        std::function<void(const void*)> createSendEventFunction (const EndpointInfo&, const AST::TypeBase& type, const AST::Function& f)
        {
            auto eventType = type.toChocType();
            auto name = AST::getEventHandlerFunctionName (f);
            auto state = statePointer;

            void* call = code->lljit.findSymbol (name);
            CMAJ_ASSERT (call != nullptr);

            if (type.isVoid())              return [call, state] (const void*)      { using F = void(*)(void*);           reinterpret_cast<F> (call) (state); };
            if (type.isPrimitiveInt32())    return [call, state] (const void* data) { using F = void(*)(void*, int32_t);  reinterpret_cast<F> (call) (state, *static_cast<const int32_t*>  (data)); };
            if (type.isPrimitiveInt64())    return [call, state] (const void* data) { using F = void(*)(void*, int64_t);  reinterpret_cast<F> (call) (state, *static_cast<const int64_t*>  (data)); };
            if (type.isPrimitiveFloat32())  return [call, state] (const void* data) { using F = void(*)(void*, float);    reinterpret_cast<F> (call) (state, *static_cast<const float*>    (data)); };
            if (type.isPrimitiveFloat64())  return [call, state] (const void* data) { using F = void(*)(void*, double);   reinterpret_cast<F> (call) (state, *static_cast<const double*>   (data)); };
            if (type.isPrimitiveBool())     return [call, state] (const void* data) { using F = void(*)(void*, int32_t);  reinterpret_cast<F> (call) (state, *static_cast<const int32_t*>  (data)); };
            if (type.isPrimitiveString())   return [call, state] (const void* data) { using F = void(*)(void*, uint32_t); reinterpret_cast<F> (call) (state, *static_cast<const uint32_t*> (data)); };

            auto& layout = *code->nativeTypeLayouts.find (type);

            if (layout.requiresPacking())
            {
                choc::AlignedMemoryBlock<16> scratch (layout.getNativeSize());

                return [call, state, scratch, &layout] (const void* data) mutable
                {
                    layout.copyPackedToNative (scratch.data(), data);
                    using F = void(*)(void*, const void*);
                    reinterpret_cast<F> (call) (state, scratch.data());
                };
            }

            return [call, state] (const void* data)
            {
                using F = void(*)(void*, const void*);
                reinterpret_cast<F> (call) (state, data);
            };
        }

        auto createGetNumOutputEventsFunction (const EndpointInfo& e)
        {
            auto& info = code->getEndpointInfo (code->outputEvents, e.handle);
            auto* count = reinterpret_cast<uint32_t*> (statePointer + info.eventCountAddressOffset);
            return [count] { return *count; };
        }

        auto createResetEventCountFunction (const EndpointInfo& e)
        {
            auto& info = code->getEndpointInfo (code->outputEvents, e.handle);
            auto* count = reinterpret_cast<uint32_t*> (statePointer + info.eventCountAddressOffset);
            return [count] { *count = 0; };
        }

        auto createGetEventTypeIndexFunction (const EndpointInfo& e)
        {
            auto& info = code->getEndpointInfo (code->outputEvents, e.handle);
            auto* firstTypeEntry = statePointer + info.eventListStartAddressOffset + info.typeFieldOffset;
            auto stride = info.eventListElementStride;

            return [firstTypeEntry, stride] (uint32_t index)
            {
                return *reinterpret_cast<uint32_t*> (firstTypeEntry + (index * stride));
            };
        }

        auto createReadOutputEventFunction (const EndpointInfo& e)
        {
            auto& info = code->getEndpointInfo (code->outputEvents, e.handle);
            auto* firstListEntry = statePointer + info.eventListStartAddressOffset;
            auto typeFieldOffset = info.typeFieldOffset;
            auto stride = info.eventListElementStride;
            choc::span<LinkedCode::OutputEventEndpoint::EventTypeHandler> eventTypeHandlers (info.eventTypeHandlers);

            return [firstListEntry, stride, typeFieldOffset, eventTypeHandlers] (uint32_t index, void* dataBuffer)
            {
                auto eventEntry = firstListEntry + index * stride;

                auto frame = *reinterpret_cast<uint32_t*> (eventEntry);
                auto type = *reinterpret_cast<uint32_t*> (eventEntry + typeFieldOffset);

                CMAJ_ASSERT (type < eventTypeHandlers.size());
                auto& handler = eventTypeHandlers[type];
                handler.layout->copyNativeToPacked (dataBuffer, eventEntry + handler.offset);

                return frame;
            };
        }

        choc::value::StringDictionary& getDictionary()  { return code->stringDictionary; }
    };

    PerformerInterface* createPerformer (std::shared_ptr<LinkedCode> code)
    {
        return choc::com::create<PerformerBase<JITInstance>> (code, engine)
                 .getWithIncrementedRefCount();
    }
};

//==============================================================================
struct Factory : public choc::com::ObjectWithAtomicRefCount<EngineFactoryInterface, Factory>
{
    virtual ~Factory() = default;
    const char* getName() override      { return "llvm"; }

    EngineInterface* createEngine (const char* engineCreationOptions) override
    {
        try
        {
            return choc::com::create<EngineBase<LLVMEngine>> (engineCreationOptions).getWithIncrementedRefCount();
        }
        catch (...) {}

        return {};
    }
};

EngineFactoryPtr createEngineFactory()   { return choc::com::create<Factory>(); }

//==============================================================================
std::string generateAssembler (const cmaj::ProgramInterface& p,
                               const cmaj::BuildSettings& buildSettings,
                               const choc::value::Value& options)
{
    std::unique_ptr<::llvm::TargetMachine> targetMachine;

    if (! options.hasObjectMember ("targetTriple"))
    {
        if (auto machineBuilder = ::llvm::orc::JITTargetMachineBuilder::detectHost())
        {
            auto& opts = machineBuilder->getOptions();
            opts.ExceptionModel = ::llvm::ExceptionHandling::None;
            opts.setFPDenormalMode (::llvm::DenormalMode::getPositiveZero());

            machineBuilder->setCodeGenOptLevel (LLVMCodeGenerator::getCodeGenOptLevel (buildSettings.getOptimisationLevel()));

            if (auto t = machineBuilder->createTargetMachine())
                targetMachine = std::move (*t);
        }
    }
    else
    {
        ::llvm::SmallVector<std::string, 16> attributes {};
        targetMachine.reset (::llvm::EngineBuilder().selectTarget (::llvm::Triple (options["targetTriple"].toString()), {}, {}, attributes));

        if (! targetMachine)
            return "Failed to create target machine - is the target triple valid?";
    }

    std::string targetFormat;

    if (options.hasObjectMember ("targetFormat"))
        targetFormat = options["targetFormat"].toString();

    auto targetTriple = targetMachine->getTargetTriple();
    auto dataLayout = targetMachine->createDataLayout();

    choc::value::SimpleStringDictionary stringDictionary;

    LLVMCodeGenerator generator (AST::getProgram (p),
                                 options,
                                 buildSettings,
                                 targetTriple.str(),
                                 dataLayout,
                                 stringDictionary,
                                 false);

    if (generator.generate())
        return generator.printAssembly (*targetMachine, targetFormat == "obj");

    return {};
}

#endif // CMAJ_ENABLE_PERFORMER_LLVM

void addTargetIfAvailable (std::vector<std::string>& targets, std::string target)
{
    ::llvm::SmallVector<std::string, 16> attributes {};

    std::unique_ptr<::llvm::TargetMachine> targetMachine (::llvm::EngineBuilder().selectTarget (::llvm::Triple (std::string (target)), {}, {}, attributes));

    if (targetMachine)
        targets.push_back ("llvm-" + target);
}

std::vector<std::string> getAssemberTargets()
{
    std::vector<std::string> result;

    result.push_back ("llvm");

    addTargetIfAvailable (result, "arm64");
    addTargetIfAvailable (result, "x86_64");
    addTargetIfAvailable (result, "wasm32");
    addTargetIfAvailable (result, "riscv64");
    addTargetIfAvailable (result, "hexagon");

    return result;
}

//==============================================================================
template <typename Type>
static uint32_t roundUp (Type size, uint32_t granularity)
{
    return (static_cast<uint32_t> (size) + (granularity - 1u)) & ~(granularity - 1u);
}

#if CMAJ_ENABLE_CODEGEN_LLVM_WASM
webassembly::WebAssemblyModule generateWebAssembly (const ProgramInterface& p, const BuildSettings& buildSettings,
                                                    bool useSIMD, bool createWAST)
{
    initialiseLLVM();

    std::unique_ptr<::llvm::TargetMachine> targetMachine;
    ::llvm::SmallVector<std::string, 16> attributes {};
    targetMachine.reset (::llvm::EngineBuilder().selectTarget (::llvm::Triple ("wasm32"), {}, {}, attributes));

    if (useSIMD)
        targetMachine->setTargetFeatureString ("+simd128");

    if (! targetMachine)
        throw std::runtime_error ("Failed to create WASM target machine");

    auto targetTriple = targetMachine->getTargetTriple();
    auto dataLayout = targetMachine->createDataLayout();

    auto& program = AST::getProgram (p);
    auto& mainProcessor = program.getMainProcessor();

    webassembly::WebAssemblyModule m;
    m.stateStructType = *mainProcessor.findStruct (mainProcessor.getStrings().stateStructName);
    m.ioStructType    = *mainProcessor.findStruct (mainProcessor.getStrings().ioStructName);

    auto options = choc::value::createObject ("options");

    if (useSIMD)
        options.addMember ("wasm-simd", true);

    auto generator = std::make_shared<LLVMCodeGenerator> (program,
                                                          options,
                                                          buildSettings,
                                                          targetTriple.str(),
                                                          dataLayout,
                                                          m.stringDictionary,
                                                          true);

    if (generator->generate())
    {
        m.binaryWASMData = generator->printAssembly (*targetMachine, ! createWAST);

        m.nativeTypeLayouts.createLayout = [generator] (const AST::TypeBase& type)
        {
            return generator->createNativeTypeLayout (type);
        };

        m.getChocType = [] (const AST::TypeBase& t)
        {
            return t.toChocType();
        };

        auto getStackSize = [&]
        {
            AST::FunctionInfoGenerator functionInfo;
            functionInfo.generate (program);
            return roundUp (functionInfo.maximumStackSize * 2, 65536);
        };

        auto stateStructSize = roundUp (m.nativeTypeLayouts.get (*m.stateStructType)->getNativeSize(), 16);
        auto ioStructSize    = roundUp (m.nativeTypeLayouts.get (*m.ioStructType)->getNativeSize(), 16);
        auto scratchSpace    = 65536u;

        // Seem to need to leave plenty of space at the bottom of the address range for
        // the wasm to use for its static data
        m.stackTop = roundUp (m.binaryWASMData.size(), 16) + getStackSize();
        m.stateStructAddress = m.stackTop;
        m.ioStructAddress = m.stateStructAddress + stateStructSize;
        m.scratchSpaceAddress = m.ioStructAddress + ioStructSize;
        m.initialNumMemPages = (m.scratchSpaceAddress + scratchSpace + 65535) / 65536;

        return m;
    }

    return {};
}
#endif

CHOC_REGISTER_OPEN_SOURCE_LICENCE (LLVM, R"(
==============================================================================
The LLVM Project is under the Apache License v2.0 with LLVM Exceptions:
==============================================================================

                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

    TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

    1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

    2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

    3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

    4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

    5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

    6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

    7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

    8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

    9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

    END OF TERMS AND CONDITIONS

    APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

    Copyright [yyyy] [name of copyright owner]

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.


---- LLVM Exceptions to the Apache 2.0 License ----

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into an Object form of such source code, you
may redistribute such embedded portions in such Object form without complying
with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

In addition, if you combine or link compiled forms of this Software with
software that is licensed under the GPLv2 ("Combined Software") and if a
court of competent jurisdiction determines that the patent provision (Section
3), the indemnity provision (Section 9) or other Section of the License
conflicts with the conditions of the GPLv2, you may retroactively and
prospectively choose to deem waived or otherwise exclude such Section(s) of
the License, but only in their entirety and only with respect to the Combined
Software.

==============================================================================
Software from third parties included in the LLVM Project:
==============================================================================
The LLVM Project contains third party software which is under different license
terms. All such code will be identified clearly using at least one of two
mechanisms:
1) It will be in a separate directory tree with its own `LICENSE.txt` or
   `LICENSE` file at the top containing the specific license and restrictions
   which apply to that software, or
2) It will contain specific license and restriction terms at the top of every
   file.

==============================================================================
Legacy LLVM License (https://llvm.org/docs/DeveloperPolicy.html#legacy):
==============================================================================
University of Illinois/NCSA
Open Source License

Copyright (c) 2003-2019 University of Illinois at Urbana-Champaign.
All rights reserved.

Developed by:

    LLVM Team

    University of Illinois at Urbana-Champaign

    http://llvm.org

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal with
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimers.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimers in the
      documentation and/or other materials provided with the distribution.

    * Neither the names of the LLVM Team, University of Illinois at
      Urbana-Champaign, nor the names of its contributors may be used to
      endorse or promote products derived from this Software without specific
      prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.
)")

} // namespace cmaj::llvm

#endif // CMAJ_ENABLE_PERFORMER_LLVM || CMAJ_ENABLE_CODEGEN_LLVM_WASM
