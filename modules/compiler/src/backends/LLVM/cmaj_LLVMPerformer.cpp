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
                    loaded.resize (cachedSize);

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
        JITInstance (std::shared_ptr<LinkedCode> cc, int32_t sessionID, double frequency) : code (std::move (cc))
        {
            stateMemory.resize (code->stateSize);
            stateMemory.clear();
            statePointer = static_cast<uint8_t*> (stateMemory.data());

            ioMemory.resize (code->ioSize);
            ioMemory.clear();
            ioPointer = static_cast<uint8_t*> (ioMemory.data());

            int processorID = 0;
            code->initialiseFn (statePointer, &processorID, sessionID, frequency);

            advanceOneFrameFn = code->advanceOneFrameFn;
            advanceBlockFn = code->advanceBlockFn;
        }

        //==============================================================================
        std::shared_ptr<LinkedCode> code;
        choc::AlignedMemoryBlock<LinkedCode::alignmentBytes> stateMemory, ioMemory;

        AdvanceOneFrameFn advanceOneFrameFn = {};
        AdvanceBlockFn    advanceBlockFn = {};

        uint8_t* statePointer = nullptr;
        uint8_t* ioPointer = nullptr;

        //==============================================================================
        void advance (uint32_t framesToAdvance) noexcept
        {
            if (advanceOneFrameFn)
                advanceOneFrameFn (statePointer, ioPointer);
            else
                advanceBlockFn (statePointer, ioPointer, framesToAdvance);
        }

        std::function<void(void*, uint32_t)> createCopyOutputValueFunction (const EndpointInfo& e)
        {
            if (e.details.isStream())
            {
                auto& info = code->getEndpointInfo (code->outputStreams, e.handle);
                auto* source = ioPointer + info.addressOffset;
                auto destStride = info.frameSize;
                auto sourceStride = info.frameStride;

                if (destStride == sourceStride)
                {
                    return [source, destStride] (void* destBuffer, uint32_t numFrames)
                    {
                        memcpy (destBuffer, source, destStride * numFrames);
                        memset (source, 0, destStride * numFrames);
                    };
                }
                else
                {
                    auto* frameLayout = info.frameLayout.get();

                    return [source, destStride, sourceStride, frameLayout] (void* destBuffer, uint32_t numFrames)
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
                    };
                }
            }
            else
            {
                auto& info = code->getEndpointInfo (code->outputValues, e.handle);
                auto* source = statePointer + info.addressOffset;
                auto layout = info.layout.get();

                return [layout, source] (void* destBuffer, uint32_t)
                {
                    layout->copyNativeToPacked (destBuffer, source);
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
webassembly::WebAssemblyModule generateWebAssembly (const ProgramInterface& p, const BuildSettings& buildSettings, bool createWAST)
{
    initialiseLLVM();

    std::unique_ptr<::llvm::TargetMachine> targetMachine;
    ::llvm::SmallVector<std::string, 16> attributes {};
    targetMachine.reset (::llvm::EngineBuilder().selectTarget (::llvm::Triple ("wasm32"), {}, {}, attributes));

    if (! targetMachine)
        throw std::runtime_error ("Failed to create WASM target machine");

    auto targetTriple = targetMachine->getTargetTriple();
    auto dataLayout = targetMachine->createDataLayout();

    auto& program = AST::getProgram (p);
    auto& mainProcessor = program.getMainProcessor();

    webassembly::WebAssemblyModule m;
    m.stateStructType = *mainProcessor.findStruct (mainProcessor.getStrings().stateStructName);
    m.ioStructType    = *mainProcessor.findStruct (mainProcessor.getStrings().ioStructName);

    auto generator = std::make_shared<LLVMCodeGenerator> (program,
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

} // namespace cmaj::llvm

#endif // CMAJ_ENABLE_PERFORMER_LLVM || CMAJ_ENABLE_CODEGEN_LLVM_WASM
