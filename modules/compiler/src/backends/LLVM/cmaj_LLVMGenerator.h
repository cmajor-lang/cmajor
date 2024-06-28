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

#include <iostream>

#include "choc/memory/choc_Endianness.h"
#include "../../codegen/cmaj_CodeGenHelpers.h"
#include "../../validation/cmaj_ValidationUtilities.h"

namespace cmaj::llvm
{

//==============================================================================
struct LLVMCodeGenerator
{
    LLVMCodeGenerator (const AST::Program& p,
                       const choc::value::Value& options,
                       const BuildSettings& buildSettingsToUse,
                       const std::string& targetTriple,
                       const ::llvm::DataLayout& layout,
                       choc::value::SimpleStringDictionary& dictionary,
                       bool isWebAssembly)
        : program (p),
          engineOptions (options),
          buildSettings (buildSettingsToUse),
          allocator (program.allocator),
          dataLayout (layout),
          stringDictionary (dictionary),
          webAssemblyMode (isWebAssembly)
    {
        context = std::make_unique<::llvm::LLVMContext>();

        targetModule = std::make_unique<::llvm::Module> ("cmajor", *context);
        targetModule->setDataLayout (getDataLayout());
        targetModule->setTargetTriple (targetTriple);

        useFastMaths = buildSettings.shouldUseFastMaths();

        auto& mainProcessor = program.getMainProcessor();

        stateStruct = mainProcessor.findStruct (mainProcessor.getStrings().stateStructName);
        ioStruct    = mainProcessor.findStruct (mainProcessor.getStrings().ioStructName);

        if (buildSettings.shouldDumpDebugInfo())
            std::cout << cmaj::AST::print (program) << std::endl;
    }

    void addNativeOverriddenFunctions (AST::ExternalFunctionManager& externalFunctionManager)
    {
        // example of how to use this. Remove the commented-out stuff when there's something real in here..

        (void) externalFunctionManager;
        // if (auto f = program.rootNamespace.findQualifiedFunction (std::string_view ("std::intrinsics::wrap"),
        //                                                           allocator.int32Type, allocator.int32Type))
        //     externalFunctionManager.addFunctionWithImplementation (*f,
        //         (void*) +[] (int32_t value, int32_t size)
        //         {
        //             if (size == 0) return 0; auto n = value % size; if (n < 0) return n + size; return n;
        //         });
    }

    bool generate()
    {
        CodeGenerator<LLVMCodeGenerator> codeGen (*this, program.getMainProcessor());
        codeGenerator = codeGen;

        codeGen.emitTypes();
        codeGen.emitGlobals();
        codeGen.emitFunctions();

       #if CMAJ_LLVM_RUN_VERIFIER
        if (verifyModule (*targetModule))
        {
            std::string result;
            ::llvm::raw_string_ostream out (result);
            verifyModule (*targetModule, &out);
            std::cout << result;
            CMAJ_ASSERT_FALSE;
        }
       #endif

        dumpDebugPrintout ("Pre optimisation", false);
        applyOptimisationPasses();
        dumpDebugPrintout ("Post optimisation");
        codeGenerator = nullptr;
        return true;
    }

    bool generateFromBitcode (choc::span<char> bitcode)
    {
        reloadDictionary (bitcode);
        auto buffer = ::llvm::MemoryBuffer::getMemBuffer ({ bitcode.begin(), bitcode.size() }, {}, false);

        if (auto result = ::llvm::parseBitcodeFile (buffer->getMemBufferRef(), *context))
        {
            targetModule = std::move (result.get());
            dumpDebugPrintout ("From bitcode", true);
            return true;
        }

        return false;
    }

    bool reloadDictionary (choc::span<char>& bitcode)
    {
        size_t totalDictionarySize = sizeof (uint32_t);

        if (bitcode.size() < totalDictionarySize)
            return false;

        if (auto dictionaryDataSize = choc::memory::readLittleEndian<uint32_t> (bitcode.data()))
        {
            totalDictionarySize += dictionaryDataSize;

            if (bitcode.size() < totalDictionarySize)
                return false;

            stringDictionary.strings.resize (dictionaryDataSize);
            memcpy (stringDictionary.strings.data(), bitcode.data() + sizeof (uint32_t), dictionaryDataSize);
        }

        bitcode = { bitcode.begin() + totalDictionarySize, bitcode.end() };
        return true;
    }

    void saveBitcodeToCache (CacheDatabaseInterface& cache, const char* key)
    {
        ::llvm::SmallVector<char, 64> bitcode;

        {
            ::llvm::raw_svector_ostream s (bitcode);

            char dictionarySize[sizeof (uint32_t)];
            choc::memory::writeLittleEndian (dictionarySize, static_cast<uint32_t> (stringDictionary.strings.size()));
            s.write (dictionarySize, sizeof (dictionarySize));
            s.write (stringDictionary.strings.data(), stringDictionary.strings.size());

            ::llvm::WriteBitcodeToFile (*targetModule, s);
        }

        cache.store (key, bitcode.data(), bitcode.size());
    }

    void dumpDebugPrintout (const char* description, bool includeAssembly = true)
    {
        if (buildSettings.shouldDumpDebugInfo())
        {
            std::cout << std::endl;
            std::cout << "*********************************************" << std::endl;
            std::cout << description << std::endl;
            std::cout << "*********************************************" << std::endl;
            std::cout << printIR() << std::endl;

            if (includeAssembly)
                std::cout << std::endl << printAssembly() << std::endl;
        }
    }

    ::llvm::orc::ThreadSafeModule takeCompiledModule()
    {
        CMAJ_ASSERT (targetModule != nullptr);
        return { std::move (targetModule), std::move (context) };
    }

    size_t getStateSize()   { return (getTypeSize (*stateStruct) + 7u) & ~7u; }
    size_t getIOSize()      { return (getTypeSize (*ioStruct) + 7u) & ~7u; }

    size_t getStateAlignment() { return (getTypeAlignment (*stateStruct)); }
    size_t getIOAlignment()    { return (getTypeAlignment (*ioStruct)); }

    static std::string getInitFunctionName()              { return "initialise"; }
    static std::string getAdvanceOneFrameFunctionName()   { return "advanceOneFrame"; }
    static std::string getAdvanceBlockFunctionName()      { return "advanceBlock"; }

    // parameter variables bigger than this will be passed as a byval pointer to
    // avoid llvm choking on store operations for large arrays
    static constexpr size_t maxSizeForStoreOperation = 128;

    static constexpr int defaultOptimisationLevel = 3;

    static int getOptimisationLevelWithDefault (int level)
    {
       #if defined(_WIN32) && defined(_M_ARM64)
        // Windows arm fails for O0, avoid this combination for now
        if (level == 0)
            level = 1;
       #endif

        return level >= 0 && level <= 4 ? level : defaultOptimisationLevel;
    }

    //==============================================================================
    const AST::Program& program;
    const choc::value::Value& engineOptions;
    const BuildSettings& buildSettings;
    const AST::Allocator& allocator;
    ptr<AST::StructType> stateStruct, ioStruct;
    ptr<CodeGenerator<LLVMCodeGenerator>> codeGenerator;
    bool useFastMaths = false;

    ::llvm::DataLayout dataLayout;
    choc::value::SimpleStringDictionary& stringDictionary;

    using Block = ::llvm::BasicBlock*;

    std::unique_ptr<::llvm::LLVMContext> context;
    std::unique_ptr<::llvm::Module> targetModule;

    ::llvm::Function* currentFunction = nullptr;
    Block currentBlock = nullptr, functionStartBlock = nullptr;
    std::unique_ptr<::llvm::IRBuilder<>> currentBlockBuilder, functionEntryBlockBuilder;
    std::unordered_map<const AST::VariableDeclaration*, ::llvm::Value*> localVariables;
    std::unordered_map<const AST::Function*, ::llvm::FunctionCallee> functions;
    std::unordered_map<std::string, void*> externalFunctionPointers;
    std::unordered_map<const AST::VariableDeclaration*, ::llvm::GlobalVariable*> globalVariables;
    DuckTypedStructMappings<::llvm::StructType*, false> structTypes;
    std::vector<std::vector<uint8_t>> gloalVariableSpace;
    size_t sliceConstantIndex = 0;
    const bool webAssemblyMode = false;

    struct TypeNameList : public AST::UniqueNameList<AST::Object, TypeNameList>
    {
        std::string getRootName (const AST::Object& t)
        {
            return AST::SignatureBuilder::removeDuplicateUnderscores (makeSafeIdentifier (t.getFullyQualifiedReadableName()));
        }
    };

    TypeNameList typeNames;

    //==============================================================================
    struct FatPointerType
    {
        const AST::TypeBase& elementType;
        ::llvm::StructType* fatPointerType;
    };

    static const char* getFatPointerStructName()   { return "_Slice"; }

    std::vector<FatPointerType> fatPointerTypes;

    ::llvm::StructType* getFatPointerType (const AST::TypeBase& elementType)
    {
        for (auto& p : fatPointerTypes)
            if (p.elementType.isSameType (elementType, AST::TypeBase::ComparisonFlags::ignoreConst
                                                        | AST::TypeBase::ComparisonFlags::duckTypeStructures))
                return p.fatPointerType;

        ::llvm::SmallVector<::llvm::Type*, 32> memberTypes;
        memberTypes.push_back (getLLVMType (elementType)->getPointerTo());
        memberTypes.push_back (getInt32Type());

        auto newType = ::llvm::StructType::create (*context, memberTypes, getFatPointerStructName(), false);
        fatPointerTypes.push_back ({ elementType, newType });
        return newType;
    }

    static bool isFatPointer (::llvm::Type* type)
    {
        return type->isStructTy() && type->getStructName().startswith (getFatPointerStructName());
    }

    //==============================================================================
    template <typename DestType, typename SourceType>
    static DestType* checked_cast (SourceType* source)
    {
        auto result = ::llvm::dyn_cast<DestType> (source);
        CMAJ_ASSERT (result != nullptr);
        return result;
    }

    std::string printIR()
    {
        std::string result;
        ::llvm::raw_string_ostream out (result);
        auto p = ::llvm::createPrintModulePass (out);
        p->runOnModule (*targetModule);
        return result;
    }

    static ::llvm::CodeGenOpt::Level getCodeGenOptLevel (int level)
    {
        switch (getOptimisationLevelWithDefault (level))
        {
            case 0:    return ::llvm::CodeGenOpt::Level::None;
            case 1:    return ::llvm::CodeGenOpt::Level::Less;
            case 2:    return ::llvm::CodeGenOpt::Level::Default;
            case 3:    return ::llvm::CodeGenOpt::Level::Aggressive;
            case 4:    return ::llvm::CodeGenOpt::Level::Aggressive;
            default:   return ::llvm::CodeGenOpt::Level::Default;
        }
    }

    std::string printAssembly (::llvm::TargetMachine& targetMachine, bool generateObjectCode)
    {
        // llc still uses the legacy passes for printing assembly - is there not a better way yet?
        // Also used in LLVMTargetMachineEmit

        ::llvm::SmallVector<char, 100000> result;
        ::llvm::raw_svector_ostream ostream (result);

        targetMachine.setOptLevel (getCodeGenOptLevel (buildSettings.getOptimisationLevel()));
        targetMachine.Options.ExceptionModel = ::llvm::ExceptionHandling::None;
        targetMachine.Options.setFPDenormalMode (::llvm::DenormalMode::getPositiveZero());
        targetMachine.Options.MCOptions.AsmVerbose = true;

        ::llvm::legacy::PassManager passManager;

        targetMachine.addPassesToEmitFile (passManager, ostream, nullptr,
                                           generateObjectCode ? ::llvm::CGFT_ObjectFile
                                                              : ::llvm::CGFT_AssemblyFile);

        passManager.run (*targetModule);

        return std::string (result.begin(), result.end());
    }

    std::string printAssembly()
    {
        // llc still uses the legacy passes for printing assembly - is there not a better way yet?
        // Also used in LLVMTargetMachineEmit

        ::llvm::SmallVector<char, 100000> result;
        ::llvm::raw_svector_ostream ostream (result);

        std::unique_ptr<::llvm::TargetMachine> targetMachine;

        if (webAssemblyMode)
        {
            ::llvm::SmallVector<std::string, 16> attributes {};
            targetMachine.reset (::llvm::EngineBuilder().selectTarget (::llvm::Triple (targetModule->getTargetTriple()), {}, {}, attributes));

            if (engineOptions.isObject() && engineOptions.hasObjectMember ("wasm-simd"))
                targetMachine->setTargetFeatureString ("+simd128");
        }
        else
        {
            if (auto machineBuilder = ::llvm::orc::JITTargetMachineBuilder::detectHost())
            {
                auto& opts = machineBuilder->getOptions();
                opts.ExceptionModel = ::llvm::ExceptionHandling::None;
                opts.setFPDenormalMode (::llvm::DenormalMode::getPositiveZero());

                machineBuilder->setCodeGenOptLevel (getCodeGenOptLevel (buildSettings.getOptimisationLevel()));

                if (auto tm = machineBuilder->createTargetMachine())
                    targetMachine = std::move (tm.get());
            }
        }

        ::llvm::legacy::PassManager passManager;

        targetMachine.get()->Options.MCOptions.AsmVerbose = true;
        targetMachine.get()->addPassesToEmitFile (passManager, ostream, nullptr, ::llvm::CGFT_AssemblyFile);
        passManager.run (*targetModule);

        return std::string (result.begin(), result.end());
    }

    void applyOptimisationPasses()
    {
        auto optLevel = getOptimisationLevelWithDefault (buildSettings.getOptimisationLevel());

        ::llvm::LoopAnalysisManager     loopAnalysisManager;
        ::llvm::FunctionAnalysisManager functionAnalysisManager;
        ::llvm::CGSCCAnalysisManager    cGSCCAnalysisManager;
        ::llvm::ModuleAnalysisManager   moduleAnalysisManager;

        functionAnalysisManager.registerPass ([&] { return ::llvm::AAManager(); });

        ::llvm::PassBuilder passBuilder;

        passBuilder.registerLoopAnalyses     (loopAnalysisManager);
        passBuilder.registerFunctionAnalyses (functionAnalysisManager);
        passBuilder.registerCGSCCAnalyses    (cGSCCAnalysisManager);
        passBuilder.registerModuleAnalyses   (moduleAnalysisManager);

        passBuilder.crossRegisterProxies (loopAnalysisManager,
                                          functionAnalysisManager,
                                          cGSCCAnalysisManager,
                                          moduleAnalysisManager);

        if (optLevel > 0)
        {
            auto getOptimisationLevel = [optLevel] () -> ::llvm::OptimizationLevel
            {
                if (optLevel <= 1)   return ::llvm::OptimizationLevel::O1;
                if (optLevel == 2)   return ::llvm::OptimizationLevel::O2;

                return ::llvm::OptimizationLevel::O3;
            };

            passBuilder.buildPerModuleDefaultPipeline (getOptimisationLevel())
                .run (*targetModule, moduleAnalysisManager);
        }
        else
        {
            passBuilder.buildO0DefaultPipeline (::llvm::OptimizationLevel::O0)
                .run (*targetModule, moduleAnalysisManager);
        }
    }

    std::unique_ptr<NativeTypeLayout> createNativeTypeLayout (const AST::TypeBase& targetType)
    {
        auto packer = std::make_unique<NativeTypeLayout> (targetType);

        packer->generate ([this] (const AST::TypeBase& sourceType, uint32_t elementIndex) -> NativeTypeLayout::NativeChunkInfo
        {
            auto type = getLLVMType (sourceType);

            if (type->isArrayTy())
            {
                CMAJ_ASSERT (elementIndex == 0);
                auto elementType = type->getArrayElementType();
                auto elementSize = static_cast<uint32_t> (dataLayout.getTypeAllocSize (elementType));

                return { 0, elementSize, false };
            }

            if (auto structType = ::llvm::dyn_cast<::llvm::StructType> (type))
            {
                auto layout = dataLayout.getStructLayout (structType);
                auto memberType = structType->getElementType (elementIndex);
                auto memberOffset = static_cast<uint32_t> (layout->getElementOffset (elementIndex));
                auto memberSize = static_cast<uint32_t> (dataLayout.getTypeAllocSize (memberType));

                return { memberOffset, memberSize, false };
            }

            if (auto vec = ::llvm::dyn_cast<::llvm::VectorType> (type))
            {
                CMAJ_ASSERT (elementIndex == 0);
                auto elementType = vec->getElementType();

                if (elementType->isIntegerTy (1))
                    return { 0, 1, true };

                auto elementSize = static_cast<uint32_t> (dataLayout.getTypeAllocSize (elementType));
                return { 0, elementSize, false };
            }

            auto size = type->isVoidTy() ? 0 : static_cast<uint32_t> (dataLayout.getTypeAllocSize (type));
            return { 0, size, false };
        });

        return packer;
    }

    //==============================================================================
    struct ValueReader
    {
        ::llvm::Value* value = nullptr;
        ptr<const AST::TypeBase> type;

        operator bool() const   { return value != nullptr; }
        operator int() const = delete;
    };

    struct ValueReference
    {
        ::llvm::Value* value = nullptr;
        ::llvm::Value* vectorIndex = nullptr;
        ptr<const AST::TypeBase> type, vectorType;

        operator bool() const           { return value != nullptr; }
        operator int() const = delete;

        bool isVectorElement() const    { return vectorIndex != nullptr; }
        bool isPointer() const          { return vectorIndex == nullptr; }
    };

    ValueReader makeReader (::llvm::Value* value, const AST::TypeBase& type)
    {
        return { value, type };
    }

    ValueReference makeReference (::llvm::Value* value, const AST::TypeBase& type)
    {
        CMAJ_ASSERT (value->getType()->isPointerTy());
        return { value, nullptr, type };
    }

    ::llvm::Value* dereference (::llvm::Value* value, ::llvm::Type* type)
    {
        CMAJ_ASSERT (value != nullptr);

        if (value->getType()->isPointerTy())
            return getBlockBuilder().CreateLoad (type, value);

        return value;
    }

    ::llvm::Value* dereference (const ValueReader& value)
    {
        if (auto mcr = value.type->getAsMakeConstOrRef())
            return dereference (value.value, getLLVMType (*mcr->getSource()));

        return dereference (value.value, getLLVMType (*value.type));
    }

    ::llvm::Value* dereference (const ValueReference& ref)
    {
        CMAJ_ASSERT (ref);

        if (ref.isVectorElement())
            return getBlockBuilder().CreateExtractElement (dereference (ref.value, getLLVMType (ref.type->skipConstAndRefModifiers())),
                                                           dereference (ref.vectorIndex, ref.vectorIndex->getType()));

        return getBlockBuilder().CreateLoad (getLLVMType (ref.type->skipConstAndRefModifiers()), ref.value);
    }

    ::llvm::Value* getPointer (const ValueReference& ref)
    {
        CMAJ_ASSERT (ref && ref.vectorIndex == nullptr);
        return ref.value;
    }

    ::llvm::Value* getPointer (const ValueReader& reader)
    {
        CMAJ_ASSERT (reader);

        if (reader.value->getType()->isPointerTy())
            return reader.value;

        auto pointer = functionEntryBlockBuilder->CreateAlloca (reader.value->getType());
        createStoreOrMemcpy (pointer, reader.value, getLLVMType (reader.type->skipConstAndRefModifiers()));
        return pointer;
    }

    ValueReader createReaderForReference (ValueReference ref)
    {
        return makeReader (dereference (ref), *ref.type);
    }

    Block createBlock()
    {
        CMAJ_ASSERT (currentFunction != nullptr);
        return ::llvm::BasicBlock::Create (*context, {}, currentFunction);
    }

    void setCurrentBlock (Block block)
    {
        CMAJ_ASSERT (block != nullptr && currentBlock == nullptr);
        currentBlockBuilder = std::make_unique<::llvm::IRBuilder<>> (block);

        if (useFastMaths)
        {
            ::llvm::FastMathFlags flags;
            flags.setFast();
            currentBlockBuilder->setFastMathFlags (flags);
        }

        currentBlock = block;
    }

    void resetCurrentBlock()
    {
        currentBlockBuilder.reset();
        currentBlock = nullptr;
    }

    ::llvm::IRBuilder<>& getBlockBuilder()
    {
        if (currentBlockBuilder == nullptr)
            setCurrentBlock (createBlock());

        return *currentBlockBuilder;
    }

    void terminateWithBranch (Block target, Block nextBlock)
    {
        CMAJ_ASSERT (currentBlockBuilder != nullptr && currentBlock != nullptr && currentBlock->getTerminator() == nullptr);
        currentBlockBuilder->CreateBr (target);
        currentBlockBuilder.reset();
        currentBlock = nullptr;

        if (nextBlock != nullptr)
            setCurrentBlock (nextBlock);
    }

    void terminateWithBranchIf (ValueReader condition, Block trueBlock, Block falseBlock, Block nextBlock)
    {
        CMAJ_ASSERT (currentBlockBuilder != nullptr && currentBlock != nullptr && currentBlock->getTerminator() == nullptr);
        currentBlockBuilder->CreateCondBr (dereference (condition), trueBlock, falseBlock);
        resetCurrentBlock();
        setCurrentBlock (nextBlock);
    }

    void terminateWithReturnVoid()
    {
        CMAJ_ASSERT (currentBlock != nullptr && currentBlock->getTerminator() == nullptr);
        getBlockBuilder().CreateRetVoid();
        resetCurrentBlock();
    }

    static std::string makeSafeIdentifier (std::string_view name) { return cmaj::makeSafeIdentifierName (std::string (name)); }

    ::llvm::Type* getLLVMType (const AST::TypeBase& type)
    {
        if (auto mcr = type.getAsMakeConstOrRef())
        {
            auto sourceType = getLLVMType (*mcr->getSource());

            if (mcr->makeRef)
                return sourceType->getPointerTo();

            return sourceType;
        }

        if (type.isVoid())              return ::llvm::Type::getVoidTy   (*context);
        if (type.isPrimitiveInt32())    return ::llvm::Type::getInt32Ty  (*context);
        if (type.isPrimitiveInt64())    return ::llvm::Type::getInt64Ty  (*context);
        if (type.isPrimitiveFloat32())  return ::llvm::Type::getFloatTy  (*context);
        if (type.isPrimitiveFloat64())  return ::llvm::Type::getDoubleTy (*context);
        if (type.isPrimitiveBool())     return ::llvm::Type::getInt1Ty   (*context);
        if (type.isPrimitiveString())   return ::llvm::Type::getInt32Ty  (*context);
        if (type.isEnum())              return ::llvm::Type::getInt32Ty  (*context);

        if (auto v = type.getAsVectorType())
        {
            auto elementType = getLLVMType (v->getElementType());
            auto numElements = static_cast<unsigned> (v->resolveSize());

            if (numElements == 1) // vector size 1 is not allowed
                return elementType;

            return ::llvm::VectorType::get (elementType, numElements, false);
        }

        if (auto a = type.getAsArrayType())
        {
            if (a->isSlice())
                return getFatPointerType (*type.getArrayOrVectorElementType());

            return ::llvm::ArrayType::get (getLLVMType (a->getInnermostElementTypeRef()),
                                           static_cast<uint64_t> (a->resolveSize()));
        }

        if (auto str = type.getAsStructType())
        {
            return structTypes.getOrCreate (*str, [this] (const AST::StructType& s)
            {
                ::llvm::SmallVector<::llvm::Type*, 32> memberTypes;

                for (size_t i = 0; i < s.memberTypes.size(); ++i)
                    memberTypes.push_back (getLLVMType (s.getMemberType(i)));

                return ::llvm::StructType::create (*context, memberTypes, typeNames.getName (s), false);
            });
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    const ::llvm::DataLayout& getDataLayout()           { return dataLayout; }

    size_t getTypeSize (::llvm::Type* type)             { return static_cast<size_t> (getDataLayout().getTypeStoreSize (type)); }
    size_t getTypeSize (const AST::TypeBase& type)      { return static_cast<size_t> (getTypeSize (getLLVMType (type))); }

    size_t getTypeAlignment (::llvm::Type* type)        { return static_cast<size_t> (getDataLayout().getABITypeAlign (type).value()); }
    size_t getTypeAlignment (const AST::TypeBase& type) { return static_cast<size_t> (getTypeAlignment (getLLVMType (type))); }

    static bool isZero (::llvm::Value* value)
    {
        if (value == nullptr)
            return true;

        if (auto c = ::llvm::dyn_cast<::llvm::Constant> (value))
            return c->isZeroValue();

        return false;
    }

    void createStoreOrMemcpy (::llvm::Value* dest, ::llvm::Value* source, ::llvm::Type* type)
    {
        auto& b = getBlockBuilder();
        auto size = getTypeSize (type);

        if (isZero (source))
        {
            if (size >= maxSizeForStoreOperation)
                b.CreateMemSet (dest, ::llvm::ConstantInt::get (::llvm::Type::getInt8Ty (*context), 0), size, {});
            else
                b.CreateStore (createNullConstant (type), dest);

            return;
        }

        if (size >= maxSizeForStoreOperation && source->getType()->isPointerTy())
            b.CreateMemCpy (dest, {}, source, {}, size);
        else
            b.CreateStore (dereference (source, type), dest);
    }

    size_t getStructMemberOffset (const AST::StructType& structType, uint32_t index)
    {
        auto layout = getDataLayout().getStructLayout (checked_cast<::llvm::StructType> (getLLVMType (structType)));
        return static_cast<size_t> (layout->getElementOffset (static_cast<unsigned> (index)));
    }

    size_t getStructMemberOffset (const AST::StructType& structType, std::string_view member)
    {
        auto layout = getDataLayout().getStructLayout (checked_cast<::llvm::StructType> (getLLVMType (structType)));
        auto index = structType.indexOfMember (member);
        CMAJ_ASSERT (index >= 0);
        return static_cast<size_t> (layout->getElementOffset (static_cast<unsigned> (index)));
    }

    size_t getStructPaddedSize (const AST::StructType& structType)
    {
        auto layout = getDataLayout().getStructLayout (checked_cast<::llvm::StructType> (getLLVMType (structType)));
        return static_cast<size_t> (layout->getSizeInBytes());
    }

    size_t getPaddedTypeSize (const AST::TypeBase& type)
    {
        return static_cast<size_t> (getDataLayout().getTypeAllocSize (getLLVMType (type)));
    }

    void addStruct (const AST::StructType&) {}
    void addAlias (const AST::Alias&, const AST::TypeBase&) {}

    void addGlobalVariable (const AST::VariableDeclaration& v, const AST::TypeBase& type,
                            std::string_view name, ValueReader initialValue)
    {
        ::llvm::Constant* initialiser = nullptr;
        auto llvmType = getLLVMType (type);

        if (initialValue)
        {
            CMAJ_ASSERT (! initialValue.value->getType()->isPointerTy());
            initialiser = checked_cast<::llvm::Constant> (initialValue.value);
        }
        else
        {
            initialiser = createNullConstant (llvmType);
        }

        auto global = new ::llvm::GlobalVariable (*targetModule, llvmType, v.isCompileTimeConstant(),
                                                  ::llvm::GlobalValue::LinkageTypes::PrivateLinkage,
                                                  initialiser, std::string (name));

        globalVariables[std::addressof (v)] = global;
    }

    bool isExportedFunction (const AST::Function& f) const
    {
        return f.isExportedFunction() && f.isChildOf (program.getMainProcessor());
    }

    std::string getFunctionName (const AST::Function& f)
    {
        if (isExportedFunction (f))
        {
            if (f.isSystemInitFunction())       return getInitFunctionName();
            if (f.isMainFunction())             return getAdvanceOneFrameFunctionName();
            if (f.isSystemAdvanceFunction())    return getAdvanceBlockFunctionName();
            if (f.isEventHandler)               return codeGenerator->getFunctionName (f);

            return std::string (f.getName());
        }

        return codeGenerator->getFunctionName (f);
    }

    ::llvm::FunctionCallee createFunction (std::string name, ::llvm::Type* returnType,
                                           ::llvm::ArrayRef<::llvm::Type*> paramTypes)
    {
        CMAJ_ASSERT (::llvm::FunctionType::isValidReturnType (returnType));
        auto fnType = ::llvm::FunctionType::get (returnType, paramTypes, false);
        return targetModule->getOrInsertFunction (name, fnType);
    }

    ::llvm::FunctionCallee createFunction (std::string name, const AST::TypeBase& returnType,
                                           const AST::ObjectRefVector<AST::TypeBase>& paramTypes)
    {
        auto llvmReturnType = getLLVMType (returnType);
        CMAJ_ASSERT (::llvm::FunctionType::isValidReturnType (llvmReturnType));

        ::llvm::SmallVector<::llvm::Type*, 32> llvmParamTypes;

        if (functionShouldReturnTypeAsArgument (returnType))
        {
            llvmParamTypes.push_back (llvmReturnType->getPointerTo());
            llvmReturnType = ::llvm::Type::getVoidTy (*context);
        }

        for (auto& param : paramTypes)
        {
            auto type = getLLVMType (param);

            if (! type->isPointerTy() && getTypeSize (type) >= maxSizeForStoreOperation)
                type = type->getPointerTo();

            llvmParamTypes.push_back (type);
        }

        return createFunction (name, llvmReturnType, llvmParamTypes);
    }

    AST::ObjectRefVector<AST::TypeBase> getParameterTypesAddingRefsWhereNeeded (const AST::Function& f) const
    {
        auto paramTypes = f.getParameterTypes();

        if (f.isEventHandler && isExportedFunction (f) && f.getNumParameters() > 1 && ! paramTypes[1]->isPrimitive())
        {
            auto& eventValueParam = paramTypes[1];

            if (! eventValueParam->isReference())
            {
                auto& makeRef = f.context.allocate<AST::MakeConstOrRef>();
                makeRef.source.referTo (eventValueParam);
                makeRef.makeRef = true;
                eventValueParam = makeRef;
            }
        }

        return paramTypes;
    }

    ::llvm::FunctionCallee getOrAddFunction (const AST::Function& f)
    {
        auto found = functions.find (std::addressof (f));

        if (found != functions.end())
            return found->second;

        auto name = getFunctionName (f);
        auto callee = createFunction (name, AST::castToTypeBaseRef (f.returnType), getParameterTypesAddingRefsWhereNeeded (f));
        functions[std::addressof (f)] = callee;

        if (auto customImplementation = program.externalFunctionManager.findResolvedFunction (f))
            externalFunctionPointers[name] = customImplementation;

        return callee;
    }

    void startNewFunction (::llvm::FunctionCallee callee, bool isExported)
    {
        currentFunction = checked_cast<::llvm::Function> (callee.getCallee());

        if (! isExported)
        {
            currentFunction->setOnlyAccessesArgMemory();
            currentFunction->addFnAttr (::llvm::Attribute::AttrKind::NoUnwind);
            currentFunction->setLinkage (::llvm::GlobalValue::LinkageTypes::PrivateLinkage);
        }
        else
        {
            currentFunction->addFnAttr ("wasm-export-name", currentFunction->getName());
        }
    }

    void beginFunction (const AST::Function& fn, std::string_view, const AST::TypeBase&)
    {
        localVariables.clear();
        CMAJ_ASSERT (currentFunction == nullptr && currentBlock == nullptr);

        startNewFunction (getOrAddFunction (fn), isExportedFunction (fn));

        functionEntryBlockBuilder = std::make_unique<::llvm::IRBuilder<>> (createBlock());
        functionStartBlock = createBlock();
        setCurrentBlock (functionStartBlock);

        unsigned index = 0;

        if (functionShouldReturnTypeAsArgument (AST::castToTypeBaseRef (fn.returnType)))
        {
            ::llvm::Value* paramVariable = currentFunction->getArg (index);
            paramVariable->setName ("_return");

            auto llvmType = getLLVMType (AST::castToTypeBaseRef (fn.returnType));

            ::llvm::AttrBuilder ab (functionStartBlock->getContext());

            ab.addDereferenceableAttr (getTypeSize (llvmType))
              .addAttribute (::llvm::Attribute::AttrKind::NonNull)
              .addAttribute (::llvm::Attribute::AttrKind::NoCapture);

            currentFunction->addParamAttrs (index, ab);

            index++;
        }

        auto paramTypes = getParameterTypesAddingRefsWhereNeeded (fn);
        unsigned paramIndex = 0;

        for (auto& param : fn.iterateParameters())
        {
            ::llvm::Value* paramVariable = currentFunction->getArg (index);
            paramVariable->setName (std::string (param.getName()));

            auto& paramType = paramTypes[paramIndex].get();
            auto isReference = paramType.isReference();

            if (paramVariable->getType()->isPointerTy())
            {
                ::llvm::AttrBuilder ab (functionStartBlock->getContext());

                auto llvmType = getLLVMType (paramType);

                if (auto mcr = paramType.getAsMakeConstOrRef())
                    llvmType = getLLVMType (*mcr->getSource());

                if (! isReference)
                    ab.addByValAttr (llvmType);

                ab.addDereferenceableAttr (getTypeSize (llvmType))
                  .addAttribute (::llvm::Attribute::AttrKind::NonNull)
                  .addAttribute (::llvm::Attribute::AttrKind::NoCapture);

                if (param.isAliasFree)
                    ab.addAttribute (::llvm::Attribute::AttrKind::NoAlias);

                currentFunction->addParamAttrs (index, ab);
            }
            else if (! isReference)
            {
                if (codeGenerator->variableMustHaveAddress (param)
                     || validation::isVariableWrittenWithinFunction (fn, param))
                {
                    auto newLocal = functionEntryBlockBuilder->CreateAlloca (getLLVMType (paramType));
                    functionEntryBlockBuilder->CreateStore (paramVariable, newLocal);
                    paramVariable = newLocal;
                }
            }

            localVariables[std::addressof (param)] = paramVariable;
            ++index;
            ++paramIndex;
        }
    }

    void endFunction()
    {
        functionEntryBlockBuilder->CreateBr (functionStartBlock);
        functionEntryBlockBuilder.reset();

        if (currentBlockBuilder != nullptr)
        {
            if (currentBlock->hasNPredecessorsOrMore (1))
            {
                terminateWithReturnVoid();
            }
            else
            {
                // if there's an orphan block with stuff in, it must be unreachable code,
                // so just terminate it with a branch to stop anything complaining, and leave it
                terminateWithBranch (currentBlock, nullptr);
            }
        }

        for (auto& block : *currentFunction)
            CMAJ_ASSERT (block.getTerminator() != nullptr);

        currentFunction = nullptr;
    }

    void beginBlock() {}
    void endBlock() {}

    struct IfStatus
    {
        Block trueBlock = nullptr,
              falseBlock = nullptr,
              continueBlock = nullptr;
    };

    IfStatus beginIfStatement (ValueReader condition, bool, bool hasFalseBranch)
    {
        IfStatus i;
        i.trueBlock = createBlock();
        i.continueBlock = createBlock();

        if (! hasFalseBranch)
        {
            terminateWithBranchIf (condition, i.trueBlock, i.continueBlock, i.trueBlock);
        }
        else
        {
            i.falseBlock = createBlock();
            terminateWithBranchIf (condition, i.trueBlock, i.falseBlock, i.trueBlock);
        }

        return i;
    }

    void addElseStatement (IfStatus& i)
    {
        if (currentBlock != nullptr)
            terminateWithBranch (i.continueBlock, i.falseBlock);
        else
            setCurrentBlock (i.falseBlock);
    }

    void endIfStatement (IfStatus& i)
    {
        if (currentBlock != nullptr)
            terminateWithBranch (i.continueBlock, i.continueBlock);
        else
            setCurrentBlock (i.continueBlock);
    }

    struct LoopStatus
    {
        Block startBlock = nullptr;
    };

    LoopStatus beginLoop()
    {
        getBlockBuilder();
        LoopStatus l;
        l.startBlock = createBlock();
        terminateWithBranch (l.startBlock, l.startBlock);
        return l;
    }

    void endLoop (LoopStatus& l)
    {
        if (currentBlock != nullptr)
            terminateWithBranch (l.startBlock, nullptr);
    }

    bool addBreakFromCurrentLoop()  { return false; }

    using BreakInstruction = Block;

    BreakInstruction addBreak()
    {
        CMAJ_ASSERT (currentBlock != nullptr);
        auto unterminatedBlock = currentBlock;
        resetCurrentBlock();
        return unterminatedBlock;
    }

    void resolveBreak (BreakInstruction unterminatedBlock)
    {
        CMAJ_ASSERT (unterminatedBlock != nullptr);
        auto continueBlock = createBlock();

        if (currentBlock != nullptr)
            terminateWithBranch (continueBlock, unterminatedBlock);
        else
            setCurrentBlock (unterminatedBlock);

        terminateWithBranch (continueBlock, continueBlock);
    }

    using ForwardBranchPlaceholder = ::llvm::SwitchInst*;
    using ForwardBranchTarget = Block;

    ForwardBranchPlaceholder beginForwardBranch (ValueReader condition, size_t numBranches)
    {
        CMAJ_ASSERT (currentBlockBuilder != nullptr && currentBlock != nullptr && currentBlock->getTerminator() == nullptr);
        auto defaultBlock = createBlock();
        auto switchInst = currentBlockBuilder->CreateSwitch (dereference (condition), defaultBlock,
                                                             static_cast<unsigned> (numBranches));
        resetCurrentBlock();
        setCurrentBlock (defaultBlock);
        return switchInst;
    }

    ForwardBranchTarget createForwardBranchTarget (ForwardBranchPlaceholder, size_t)
    {
        auto newBlock = createBlock();

        if (currentBlock != nullptr)
            terminateWithBranch (newBlock, newBlock);
        else
            setCurrentBlock (newBlock);

        return newBlock;
    }

    void resolveForwardBranch (ForwardBranchPlaceholder switchInst, const std::vector<ForwardBranchTarget>& targets)
    {
        int64_t index = 1;

        for (auto& targetBlock : targets)
            switchInst->addCase (::llvm::ConstantInt::getSigned (getInt32Type(), index++), targetBlock);
    }

    ValueReference createLocalTempVariable (const AST::VariableDeclaration& variable, ValueReader value, bool ensureZeroInitialised)
    {
        addLocalVariableDeclaration (variable, value, ensureZeroInitialised);
        return createVariableReference (variable);
    }

    void addLocalVariableDeclaration (const AST::VariableDeclaration& v, ValueReader optionalInitialiser, bool ensureZeroInitialised)
    {
        auto pointer = getVariable (v);

        if (optionalInitialiser)
            createStoreOrMemcpy (pointer, optionalInitialiser.value,  getLLVMType (*v.getType()));
        else if (ensureZeroInitialised)
            createStoreOrMemcpy (pointer, nullptr, getLLVMType (*v.getType()));
    }

    ::llvm::Value* getVariable (const AST::VariableDeclaration& v)
    {
        if (v.isLocal() || v.isParameter())
        {
            auto i = localVariables.find (std::addressof (v));

            if (i != localVariables.end())
                return i->second;

            auto type = getLLVMType (*v.getType());
            CMAJ_ASSERT (! type->isPointerTy());
            auto pointer = functionEntryBlockBuilder->CreateAlloca (type);
            localVariables[std::addressof (v)] = pointer;
            return pointer;
        }

        auto i = globalVariables.find (std::addressof (v));
        CMAJ_ASSERT (i != globalVariables.end());
        return i->second;
    }

    void addAssignToReference (ValueReference target, ValueReader value)
    {
        if (target.isVectorElement())
        {
            auto source = value ? dereference (value)
                                : createNullConstant (getLLVMType (*target.type));

            auto& b = getBlockBuilder();
            source = b.CreateInsertElement (dereference (target.value, getLLVMType (*target.vectorType)),
                                            source,
                                            dereference (target.vectorIndex, target.vectorIndex->getType()));
            b.CreateStore (source, target.value);
            return;
        }

        createStoreOrMemcpy (target.value, value.value, getLLVMType (value.type->skipConstAndRefModifiers()));
    }

    void addAddValueToInteger (ValueReference target, int32_t delta)
    {
        auto& b = getBlockBuilder();
        auto lhs = b.CreateLoad (getLLVMType (target.type->skipConstAndRefModifiers()), getPointer (target));
        auto rhs = ::llvm::ConstantInt::getSigned (lhs->getType(), delta);
        auto result = b.CreateBinOp (::llvm::Instruction::BinaryOps::Add, lhs, rhs);
        createStoreOrMemcpy (getPointer (target), result, getLLVMType (*target.type));
    }

    void addExpressionAsStatement (ValueReader)
    {
        // Not needed
    }

    void addReturnValue (ValueReader value)
    {
        CMAJ_ASSERT (currentBlock == nullptr || currentBlock->getTerminator() == nullptr);

        if (functionShouldReturnTypeAsArgument (*value.type))
        {
            // Copy the return value to the first argument
            ::llvm::Value* returnVariable = currentFunction->getArg (0);
            createStoreOrMemcpy (returnVariable, getPointer (value), getLLVMType (value.type->skipConstAndRefModifiers()));
            terminateWithReturnVoid();
        }
        else
        {
            getBlockBuilder().CreateRet (dereference (value));
            resetCurrentBlock();
        }
    }

    void addReturnVoid()
    {
        terminateWithReturnVoid();
    }

    ::llvm::IntegerType* getInt32Type()    { return ::llvm::IntegerType::getInt32Ty (*context); }

    ValueReader createConstantBool (bool b)       { return makeReader (::llvm::ConstantInt::getSigned (::llvm::Type::getInt1Ty (*context), b ? 1 : 0), allocator.boolType); }
    ValueReader createConstantInt32 (int32_t n)   { return makeReader (::llvm::ConstantInt::getSigned (getInt32Type(), static_cast<int64_t> (n)), allocator.int32Type); }
    ValueReader createConstantInt64 (int64_t n)   { return makeReader (::llvm::ConstantInt::getSigned (::llvm::Type::getInt64Ty (*context), n), allocator.int64Type); }
    ValueReader createConstantFloat32 (float n)   { return makeReader (::llvm::ConstantFP::get (::llvm::Type::getFloatTy (*context), static_cast<double> (n)), allocator.float32Type); }
    ValueReader createConstantFloat64 (double n)  { return makeReader (::llvm::ConstantFP::get (::llvm::Type::getDoubleTy (*context), n), allocator.float64Type); }

    ValueReader createConstantString (std::string_view s)
    {
        auto v = createConstantInt32 (static_cast<int32_t> (stringDictionary.getHandleForString (s).handle));
        v.type = allocator.stringType;
        return v;
    }

    template <typename ElementType, typename LLVMType>
    ::llvm::Constant* createConstantArrayOrVector (const AST::ConstantAggregate& agg, uint32_t numElements, bool isVector)
    {
        ::llvm::SmallVector<LLVMType, 32> elements;
        elements.reserve (numElements);

        for (size_t i = 0; i < numElements; ++i)
            elements.push_back (static_cast<LLVMType> (*agg.getElementValueRef (i).castToPrimitive<ElementType>()));

        if (isVector)
            return ::llvm::ConstantDataVector::get (*context, elements);

        return ::llvm::ConstantDataArray::get (*context, elements);
    }

    ValueReader createConstantAggregate (const AST::ConstantAggregate& agg)
    {
        auto& type = agg.getType().skipConstAndRefModifiers();
        bool isVector = type.isVectorType();
        auto numElements = agg.getNumElements();

        if (numElements == 1 && isVector) // vector size 1 is not allowed
            return makeReader (dereference (codeGenerator->createConstant (agg.getElementValueRef (0))), type);

        bool isArray = type.isArrayType();
        ::llvm::Constant* arrayOrVectorData = nullptr;

        if (isVector || isArray)
        {
            auto& elementType = *type.getArrayOrVectorElementType();

            if (elementType.isPrimitiveFloat32())        arrayOrVectorData = createConstantArrayOrVector<float,   float>    (agg, numElements, isVector);
            else if (elementType.isPrimitiveFloat64())   arrayOrVectorData = createConstantArrayOrVector<double,  double>   (agg, numElements, isVector);
            else if (elementType.isPrimitiveInt32())     arrayOrVectorData = createConstantArrayOrVector<int32_t, uint32_t> (agg, numElements, isVector);
            else if (elementType.isPrimitiveInt64())     arrayOrVectorData = createConstantArrayOrVector<int64_t, uint64_t> (agg, numElements, isVector);
        }

        auto createElementConstants = [&]
        {
            ::llvm::SmallVector<::llvm::Constant*, 32> elements;
            elements.reserve (numElements);

            for (size_t i = 0; i < numElements; ++i)
            {
                auto element = codeGenerator->createConstant (agg.getElementValueRef (i));
                elements.push_back (checked_cast<::llvm::Constant> (dereference (element)));
            }

            return elements;
        };

        if (isVector)
        {
            if (arrayOrVectorData == nullptr)
                arrayOrVectorData = ::llvm::ConstantVector::get (createElementConstants());

            return makeReader (arrayOrVectorData, type);
        }

        auto llvmType = getLLVMType (type);

        if (type.isStructType())
        {
            auto structType = checked_cast<::llvm::StructType> (llvmType);
            return makeReader (::llvm::ConstantStruct::get (structType, createElementConstants()), type);
        }

        if (type.isSlice())
        {
            if (arrayOrVectorData == nullptr)
            {
                auto& fixedSizeArray = type.context.allocator.createDeepClone (*type.getAsArrayType());
                fixedSizeArray.setArraySize ({ static_cast<int32_t> (numElements) });

                auto arrayType = checked_cast<::llvm::ArrayType> (getLLVMType (fixedSizeArray));
                arrayOrVectorData = ::llvm::ConstantArray::get (arrayType, createElementConstants());
            }

            auto sourceDataConstant = new ::llvm::GlobalVariable (*targetModule, arrayOrVectorData->getType(), true,
                                                                  ::llvm::GlobalValue::PrivateLinkage, arrayOrVectorData,
                                                                  "_slice_const" + std::to_string (++sliceConstantIndex));

            auto& elementType = *type.getArrayOrVectorElementType();
            auto pointerType = getLLVMType (elementType)->getPointerTo();

            ::llvm::SmallVector<::llvm::Constant*, 32> fatPointerMembers;
            fatPointerMembers.push_back (::llvm::ConstantExpr::getPointerCast (sourceDataConstant, pointerType));
            fatPointerMembers.push_back (::llvm::ConstantInt::getSigned (getInt32Type(), static_cast<int64_t> (numElements)));

            return makeReader (::llvm::ConstantStruct::get (checked_cast<::llvm::StructType> (llvmType), fatPointerMembers), type);
        }

        if (isArray)
        {
            auto arrayType = checked_cast<::llvm::ArrayType> (llvmType);

            if (arrayOrVectorData == nullptr)
                arrayOrVectorData = ::llvm::ConstantArray::get (arrayType, createElementConstants());

            return makeReader (arrayOrVectorData, type);
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    ValueReader createNullConstant (const AST::TypeBase& type)
    {
        return makeReader (createNullConstant (getLLVMType (type)), type);
    }

    ::llvm::Constant* createNullConstant (::llvm::Type* type)
    {
        if (type->isIntegerTy())        return ::llvm::ConstantInt::getSigned (type, 0);
        if (type->isFloatingPointTy())  return ::llvm::ConstantFP::get (type, 0);
        if (type->isAggregateType())    return ::llvm::ConstantAggregateZero::get (type);
        if (type->isPointerTy())        return ::llvm::ConstantPointerNull::get (checked_cast<::llvm::PointerType> (type));

        if (type->isVectorTy())
        {
            auto v = checked_cast<::llvm::VectorType> (type);
            return ::llvm::ConstantVector::getSplat (v->getElementCount(), createNullConstant (v->getElementType()));
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    static bool canPerformVectorUnaryOp()    { return true; }
    static bool canPerformVectorBinaryOp()   { return true; }

    ValueReader createUnaryOp (AST::UnaryOpTypeEnum::Enum opType, const AST::TypeBase&, ValueReader input)
    {
        auto& b = getBlockBuilder();
        auto inputValue = dereference (input);

        if (opType == AST::UnaryOpTypeEnum::Enum::negate)
            return makeReader (input.type->isFloatOrVectorOfFloat() ? b.CreateFNeg (inputValue)
                                                                    : b.CreateNeg  (inputValue),
                               *input.type);

        if (opType == AST::UnaryOpTypeEnum::Enum::bitwiseNot)
            return makeReader (b.CreateBinOp (::llvm::Instruction::BinaryOps::Xor, inputValue,
                                              ::llvm::ConstantInt::getSigned (inputValue->getType(), -1)),
                               *input.type);

        if (opType == AST::UnaryOpTypeEnum::Enum::logicalNot)
            return makeReader (b.CreateNot (inputValue), allocator.boolType);

        CMAJ_ASSERT_FALSE;
        return {};
    }

    ValueReader createArithmeticOp (::llvm::Instruction::BinaryOps op, ValueReader lhs, ValueReader rhs, const AST::TypeBase& resultType)
    {
        return makeReader (getBlockBuilder().CreateBinOp (op, dereference (lhs), dereference (rhs)), resultType);
    }

    ValueReader createComparisonOp (::llvm::CmpInst::Predicate op, ValueReader lhs, ValueReader rhs, const AST::TypeBase& resultType)
    {
        return makeReader (getBlockBuilder().CreateCmp (op, dereference (lhs), dereference (rhs)), resultType);
    }

    ValueReader createBinaryOp (AST::BinaryOpTypeEnum::Enum opType, AST::TypeRules::BinaryOperatorTypes opTypes, ValueReader lhs, ValueReader rhs)
    {
        if (opTypes.operandType.isFloatOrVectorOfFloat())
        {
            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::add:                  return createArithmeticOp (::llvm::Instruction::BinaryOps::FAdd, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::subtract:             return createArithmeticOp (::llvm::Instruction::BinaryOps::FSub, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::multiply:             return createArithmeticOp (::llvm::Instruction::BinaryOps::FMul, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::divide:               return createArithmeticOp (::llvm::Instruction::BinaryOps::FDiv, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::modulo:               return createArithmeticOp (::llvm::Instruction::BinaryOps::FRem, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::equals:               return createComparisonOp (::llvm::CmpInst::Predicate::FCMP_OEQ, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return createComparisonOp (::llvm::CmpInst::Predicate::FCMP_ONE, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::lessThan:             return createComparisonOp (::llvm::CmpInst::Predicate::FCMP_OLT, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::lessThanOrEqual:      return createComparisonOp (::llvm::CmpInst::Predicate::FCMP_OLE, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::greaterThan:          return createComparisonOp (::llvm::CmpInst::Predicate::FCMP_OGT, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual:   return createComparisonOp (::llvm::CmpInst::Predicate::FCMP_OGE, lhs, rhs, opTypes.resultType);

                case AST::BinaryOpTypeEnum::Enum::exponent:
                case AST::BinaryOpTypeEnum::Enum::bitwiseOr:
                case AST::BinaryOpTypeEnum::Enum::bitwiseAnd:
                case AST::BinaryOpTypeEnum::Enum::bitwiseXor:
                case AST::BinaryOpTypeEnum::Enum::logicalOr:
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:
                case AST::BinaryOpTypeEnum::Enum::leftShift:
                case AST::BinaryOpTypeEnum::Enum::rightShift:
                case AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                default:                                                break;
            }
        }
        else if (opTypes.operandType.isPrimitiveInt() || (opTypes.operandType.isVector() && opTypes.operandType.getArrayOrVectorElementType()->isPrimitiveInt()))
        {
            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::add:                  return createArithmeticOp (::llvm::Instruction::BinaryOps::Add,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::subtract:             return createArithmeticOp (::llvm::Instruction::BinaryOps::Sub,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::multiply:             return createArithmeticOp (::llvm::Instruction::BinaryOps::Mul,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::divide:               return createArithmeticOp (::llvm::Instruction::BinaryOps::SDiv, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::modulo:               return createArithmeticOp (::llvm::Instruction::BinaryOps::SRem, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::bitwiseOr:            return createArithmeticOp (::llvm::Instruction::BinaryOps::Or,   lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::bitwiseAnd:           return createArithmeticOp (::llvm::Instruction::BinaryOps::And,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::bitwiseXor:           return createArithmeticOp (::llvm::Instruction::BinaryOps::Xor,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::leftShift:            return createArithmeticOp (::llvm::Instruction::BinaryOps::Shl,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::rightShift:           return createArithmeticOp (::llvm::Instruction::BinaryOps::AShr, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned:   return createArithmeticOp (::llvm::Instruction::BinaryOps::LShr, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::equals:               return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_EQ,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_NE,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::lessThan:             return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::lessThanOrEqual:      return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::greaterThan:          return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual:   return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_SGE, lhs, rhs, opTypes.resultType);

                case AST::BinaryOpTypeEnum::Enum::exponent:
                case AST::BinaryOpTypeEnum::Enum::logicalOr:
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:
                default:                                                break;
            }
        }
        else if (opTypes.operandType.isPrimitiveBool() || (opTypes.operandType.isVector() && opTypes.operandType.getArrayOrVectorElementType()->isPrimitiveBool()))
        {
            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::logicalOr:            return createArithmeticOp (::llvm::Instruction::BinaryOps::Or,  lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:           return createArithmeticOp (::llvm::Instruction::BinaryOps::And, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::equals:               return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_EQ, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_NE, lhs, rhs, opTypes.resultType);

                case AST::BinaryOpTypeEnum::Enum::add:
                case AST::BinaryOpTypeEnum::Enum::subtract:
                case AST::BinaryOpTypeEnum::Enum::multiply:
                case AST::BinaryOpTypeEnum::Enum::divide:
                case AST::BinaryOpTypeEnum::Enum::modulo:
                case AST::BinaryOpTypeEnum::Enum::exponent:
                case AST::BinaryOpTypeEnum::Enum::bitwiseOr:
                case AST::BinaryOpTypeEnum::Enum::bitwiseAnd:
                case AST::BinaryOpTypeEnum::Enum::bitwiseXor:
                case AST::BinaryOpTypeEnum::Enum::lessThan:
                case AST::BinaryOpTypeEnum::Enum::lessThanOrEqual:
                case AST::BinaryOpTypeEnum::Enum::greaterThan:
                case AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual:
                case AST::BinaryOpTypeEnum::Enum::leftShift:
                case AST::BinaryOpTypeEnum::Enum::rightShift:
                case AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                default:                                                break;
            }
        }
        else if (opTypes.operandType.isPrimitiveString() || opTypes.operandType.isEnum())
        {
            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::equals:               return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_EQ, lhs, rhs, opTypes.resultType);
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return createComparisonOp (::llvm::CmpInst::Predicate::ICMP_NE, lhs, rhs, opTypes.resultType);

                case AST::BinaryOpTypeEnum::Enum::logicalOr:
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:
                case AST::BinaryOpTypeEnum::Enum::add:
                case AST::BinaryOpTypeEnum::Enum::subtract:
                case AST::BinaryOpTypeEnum::Enum::multiply:
                case AST::BinaryOpTypeEnum::Enum::divide:
                case AST::BinaryOpTypeEnum::Enum::modulo:
                case AST::BinaryOpTypeEnum::Enum::exponent:
                case AST::BinaryOpTypeEnum::Enum::bitwiseOr:
                case AST::BinaryOpTypeEnum::Enum::bitwiseAnd:
                case AST::BinaryOpTypeEnum::Enum::bitwiseXor:
                case AST::BinaryOpTypeEnum::Enum::lessThan:
                case AST::BinaryOpTypeEnum::Enum::lessThanOrEqual:
                case AST::BinaryOpTypeEnum::Enum::greaterThan:
                case AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual:
                case AST::BinaryOpTypeEnum::Enum::leftShift:
                case AST::BinaryOpTypeEnum::Enum::rightShift:
                case AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                default:                                                break;
            }
        }

        CMAJ_ASSERT_FALSE;
    }

    ValueReader createAddInt32 (ValueReader lhs, int32_t rhs)
    {
        return makeReader (getBlockBuilder().CreateBinOp (::llvm::Instruction::BinaryOps::Add,
                                                          dereference (lhs), createConstantInt32 (rhs).value),
                           allocator.int32Type);
    }

    ValueReader createTernaryOp (ValueReader condition, ValueReader trueValue, ValueReader falseValue)
    {
        auto& type = *trueValue.type;
        auto llvmType = getLLVMType (type);
        auto continueBlock = createBlock();
        auto trueBlock = createBlock();
        auto falseBlock = createBlock();

        auto t = dereference (trueValue);
        auto f = dereference (falseValue);

        auto resultVar = functionEntryBlockBuilder->CreateAlloca (t->getType());

        terminateWithBranchIf (condition, trueBlock, falseBlock, trueBlock);

        createStoreOrMemcpy (resultVar, t, llvmType);
        terminateWithBranch (continueBlock, falseBlock);
        createStoreOrMemcpy (resultVar, f, llvmType);
        terminateWithBranch (continueBlock, continueBlock);
        return makeReader (resultVar, type);
    }

    ValueReader createIntrinsic_isNanOrInf (::llvm::Value* input, bool isNaN)
    {
        auto& b = getBlockBuilder();
        auto savedFlags = b.getFastMathFlags();
        b.clearFastMathFlags();
        ::llvm::Value* op;

        if (isNaN)
        {
            op = b.CreateFCmp (::llvm::CmpInst::Predicate::FCMP_UNO, input, ::llvm::ConstantFP::get (input->getType(), 0.0));
        }
        else
        {
            ::llvm::Type* overloads[] = { input->getType() };
            auto absFn = ::llvm::Intrinsic::getDeclaration (targetModule.get(), ::llvm::Intrinsic::fabs, overloads);
            CMAJ_ASSERT (absFn != nullptr);
            auto absInput = b.CreateCall (absFn, { input });
            op = b.CreateFCmp (::llvm::CmpInst::Predicate::FCMP_OEQ, absInput, ::llvm::ConstantFP::getInfinity (input->getType()));
        }

        b.setFastMathFlags (savedFlags);
        return makeReader (op, allocator.boolType);
    }

    ValueReader createIntrinsic_reinterpretFloatToInt (::llvm::Value* value)
    {
        auto& b = getBlockBuilder();
        auto sourceType = value->getType();

        if (sourceType->isFloatTy())
            return makeReader (b.CreateBitCast (value, ::llvm::Type::getInt32Ty (*context)), allocator.int32Type);

        CMAJ_ASSERT (sourceType->isDoubleTy());
        return makeReader (b.CreateBitCast (value, ::llvm::Type::getInt64Ty (*context)), allocator.int64Type);
    }

    ValueReader createIntrinsic_reinterpretIntToFloat (::llvm::Value* value)
    {
        auto& b = getBlockBuilder();
        auto sourceType = value->getType();

        if (sourceType->isIntegerTy (32))
            return makeReader (b.CreateBitCast (value, ::llvm::Type::getFloatTy (*context)), allocator.float32Type);

        CMAJ_ASSERT (sourceType->isIntegerTy (64));
        return makeReader (b.CreateBitCast (value, ::llvm::Type::getDoubleTy (*context)), allocator.float64Type);
    }

    ValueReader createIntrinsic_select (::llvm::ArrayRef<::llvm::Value*> args, const AST::TypeBase& returnType)
    {
        return makeReader (getBlockBuilder().CreateSelect (args[0], args[1], args[2]), returnType);
    }

    ValueReader createIntrinsicCall (::llvm::Intrinsic::ID intrinsicID, ::llvm::ArrayRef<::llvm::Value*> args, const AST::TypeBase& returnType)
    {
        if (webAssemblyMode)
            return {};

        if (! returnType.isFloatOrVectorOfFloat())
            return {};

        ::llvm::Type* overloads[] = { args[0]->getType() };

        if (auto intrinsicFn = ::llvm::Intrinsic::getDeclaration (targetModule.get(), intrinsicID, overloads))
            return makeReader (getBlockBuilder().CreateCall (intrinsicFn, args), returnType);

        return {};
    }

    template <typename FunctionCallArgList>
    ValueReader createIntrinsicCall (AST::Intrinsic::Type intrinsic, FunctionCallArgList argValues, const AST::TypeBase& returnType)
    {
        ::llvm::SmallVector<::llvm::Value*, 32> args;

        for (auto& arg : argValues)
        {
            if (arg.valueReference)
                args.push_back (dereference (arg.valueReference));
            else
                args.push_back (dereference (arg.valueReader));
        }

        switch (intrinsic)
        {
            case AST::Intrinsic::Type::abs:           return createIntrinsicCall (::llvm::Intrinsic::fabs,   args, returnType);
            case AST::Intrinsic::Type::min:           return createIntrinsicCall (::llvm::Intrinsic::minnum, args, returnType);
            case AST::Intrinsic::Type::max:           return createIntrinsicCall (::llvm::Intrinsic::maxnum, args, returnType);
            case AST::Intrinsic::Type::floor:         return createIntrinsicCall (::llvm::Intrinsic::floor,  args, returnType);
            case AST::Intrinsic::Type::ceil:          return createIntrinsicCall (::llvm::Intrinsic::ceil,   args, returnType);
            case AST::Intrinsic::Type::rint:          return createIntrinsicCall (::llvm::Intrinsic::rint,   args, returnType);
            case AST::Intrinsic::Type::sqrt:          return createIntrinsicCall (::llvm::Intrinsic::sqrt,   args, returnType);
            case AST::Intrinsic::Type::log:           return createIntrinsicCall (::llvm::Intrinsic::log,    args, returnType);
            case AST::Intrinsic::Type::log10:         return createIntrinsicCall (::llvm::Intrinsic::log10,  args, returnType);
            case AST::Intrinsic::Type::exp:           return createIntrinsicCall (::llvm::Intrinsic::exp,    args, returnType);
            case AST::Intrinsic::Type::pow:           return createIntrinsicCall (::llvm::Intrinsic::pow,    args, returnType);
            case AST::Intrinsic::Type::sin:           return createIntrinsicCall (::llvm::Intrinsic::sin,    args, returnType);
            case AST::Intrinsic::Type::cos:           return createIntrinsicCall (::llvm::Intrinsic::cos,    args, returnType);
            case AST::Intrinsic::Type::isnan:         return createIntrinsic_isNanOrInf (args.front(), true);
            case AST::Intrinsic::Type::isinf:         return createIntrinsic_isNanOrInf (args.front(), false);
            case AST::Intrinsic::Type::reinterpretFloatToInt:  return createIntrinsic_reinterpretFloatToInt (args.front());
            case AST::Intrinsic::Type::reinterpretIntToFloat:  return createIntrinsic_reinterpretIntToFloat (args.front());

            case AST::Intrinsic::Type::select:        return createIntrinsic_select (args, returnType);

            case AST::Intrinsic::Type::fmod:
            case AST::Intrinsic::Type::tan:
            case AST::Intrinsic::Type::addModulo2Pi:
            case AST::Intrinsic::Type::remainder:
            case AST::Intrinsic::Type::clamp:
            case AST::Intrinsic::Type::wrap:
            case AST::Intrinsic::Type::sinh:
            case AST::Intrinsic::Type::cosh:
            case AST::Intrinsic::Type::tanh:
            case AST::Intrinsic::Type::asinh:
            case AST::Intrinsic::Type::acosh:
            case AST::Intrinsic::Type::atanh:
            case AST::Intrinsic::Type::asin:
            case AST::Intrinsic::Type::acos:
            case AST::Intrinsic::Type::atan:
            case AST::Intrinsic::Type::atan2:
                return {}; // fall back to the library implementations for ones we can't handle

            case AST::Intrinsic::Type::unknown:
            default:                                  CMAJ_ASSERT_FALSE; return {};
        }
    }

    template <typename FunctionCallArgList>
    ValueReader createFunctionCall (const AST::Function& fn, std::string_view, FunctionCallArgList argValues)
    {
        ::llvm::SmallVector<::llvm::Value*, 32> args;
        auto callee = getOrAddFunction (fn);
        auto destParam = callee.getFunctionType()->param_begin();

        ::llvm::Value* returnValue = nullptr;

        if (functionShouldReturnTypeAsArgument (AST::castToTypeBaseRef (fn.returnType)))
        {
            returnValue = functionEntryBlockBuilder->CreateAlloca (getLLVMType (AST::castToTypeBaseRef (fn.returnType)));
            args.push_back (returnValue);
            destParam++;
        }

        for (auto& arg : argValues)
        {
            if ((*destParam++)->isPointerTy())
            {
                if (arg.valueReference)
                    args.push_back (getPointer (arg.valueReference));
                else
                    args.push_back (getPointer (arg.valueReader));
            }
            else
            {
                args.push_back (dereference (arg.valueReader));
            }
        }

        if (returnValue != nullptr)
        {
            getBlockBuilder().CreateCall (callee, args);

            auto& returnTypeRef = fn.context.allocate<AST::MakeConstOrRef>();
            returnTypeRef.source.referTo (AST::castToTypeBaseRef (fn.returnType));
            returnTypeRef.makeRef = true;

            return makeReader (returnValue, returnTypeRef);
        }

        return makeReader (getBlockBuilder().CreateCall (callee, args),
                           AST::castToTypeBaseRef (fn.returnType));
    }

    ValueReader createVariableReader (const AST::VariableDeclaration& v)
    {
        return makeReader (getVariable (v), *v.getType());
    }

    ValueReference createVariableReference (const AST::VariableDeclaration& v)
    {
        return makeReference (getVariable (v), *v.getType());
    }

    ValueReader createElementReader (ValueReader parent, ValueReader index)
    {
        auto& b = getBlockBuilder();
        auto& resultType = *parent.type->getArrayOrVectorElementType();
        auto pointeeType = getLLVMType (parent.type->skipConstAndRefModifiers());

        if (pointeeType->isVectorTy())
            return makeReader (b.CreateExtractElement (dereference (parent), dereference (index)), resultType);

        if (! pointeeType->isAggregateType()) // a vector size 1 might have been reduced to a primitive type
            return { parent.value, resultType };

        if (isFatPointer (pointeeType))
        {
            auto pointer = b.CreateConstInBoundsGEP2_32 (pointeeType, getPointer (parent), 0, 0);

            ::llvm::Value* indexes[] = { dereference (index) };
            return makeReader (b.CreateGEP (getLLVMType (resultType),
                                            dereference (pointer, getLLVMType (resultType)->getPointerTo()), indexes), resultType);
        }

        ::llvm::Value* indexes[] = { createConstantInt32 (0).value, dereference (index) };
        return makeReader (b.CreateInBoundsGEP (pointeeType, getPointer (parent), indexes), resultType);
    }

    ValueReference createElementReference (ValueReference parent, ValueReader index)
    {
        auto& resultType = *parent.type->getArrayOrVectorElementType();
        auto pointeeType = getLLVMType (parent.type->skipConstAndRefModifiers());

        if (pointeeType->isVectorTy())
        {
            ValueReference result { parent.value, dereference (index), resultType };
            result.vectorType = parent.type->skipConstAndRefModifiers();
            return result;
        }

        if (! pointeeType->isAggregateType()) // a vector size 1 might have been reduced to a primitive type
            return { parent.value, nullptr, resultType };

        ::llvm::Value* indexes[] = { createConstantInt32 (0).value, dereference (index) };
        return makeReference (getBlockBuilder().CreateInBoundsGEP (pointeeType, getPointer (parent), indexes), resultType);
    }

    ValueReader createStructMemberReader (const AST::StructType& type, ValueReader object, std::string_view, int64_t memberIndex)
    {
        return makeReader (getBlockBuilder().CreateStructGEP (getLLVMType (type), getPointer (object), static_cast<unsigned> (memberIndex)),
                           type.getMemberType (static_cast<size_t> (memberIndex)));
    }

    ValueReference createStructMemberReference (const AST::StructType& type, ValueReference object, std::string_view, int64_t memberIndex)
    {
        return makeReference (getBlockBuilder().CreateStructGEP (getLLVMType (type), getPointer (object), static_cast<unsigned> (memberIndex)),
                              type.getMemberType (static_cast<size_t> (memberIndex)));
    }

    ValueReader createCastToBool (ValueReader source)
    {
        auto& b = getBlockBuilder();
        auto sourceLoad = dereference (source);
        auto zero = createNullConstant (sourceLoad->getType());

        return makeReader (sourceLoad->getType()->isFloatingPointTy()
                                ? b.CreateFCmp (::llvm::CmpInst::Predicate::FCMP_ONE, sourceLoad, zero)
                                : b.CreateICmp (::llvm::CmpInst::Predicate::ICMP_NE,  sourceLoad, zero),
                           allocator.boolType);
    }

    ValueReader createNumericCast (::llvm::Instruction::CastOps op, ValueReader source, const AST::TypeBase& targetType)
    {
        auto& b = getBlockBuilder();
        return makeReader (b.CreateCast (op, dereference (source), getLLVMType (targetType)), targetType);
    }

    ValueReader createValueCast (const AST::TypeBase& targetType, const AST::TypeBase& sourceType, ValueReader source)
    {
        if (targetType.isIdentical (sourceType))
            return source;

        bool isTargetVectorSize1 = targetType.isVectorSize1();
        bool isSourceVectorSize1 = sourceType.isVectorSize1();

        if (isTargetVectorSize1 || isSourceVectorSize1)
        {
            if (isSourceVectorSize1)
            {
                source.type = sourceType.getArrayOrVectorElementType();
                source.value = getBlockBuilder().CreateBitCast (dereference (source), getLLVMType (*source.type));
                return createValueCast (targetType, *source.type, source);
            }

            auto element = createValueCast (*targetType.getArrayOrVectorElementType(), sourceType, source);
            element.type = targetType;
            element.value = getBlockBuilder().CreateBitCast (dereference (element), getLLVMType (targetType));
            return element;
        }

        if (targetType.isPrimitiveBool())
            return createCastToBool (source);

        if (targetType.isPrimitiveInt64())
        {
            if (sourceType.isPrimitiveInt32())     return createNumericCast (::llvm::Instruction::SExt,   source, targetType);
            if (sourceType.isPrimitiveFloat())     return createNumericCast (::llvm::Instruction::FPToSI, source, targetType);
            if (sourceType.isPrimitiveBool())      return createNumericCast (::llvm::Instruction::ZExt,   source, targetType);
        }

        if (targetType.isPrimitiveInt32())
        {
            if (sourceType.isPrimitiveInt64())     return createNumericCast (::llvm::Instruction::Trunc,  source, targetType);
            if (sourceType.isPrimitiveFloat())     return createNumericCast (::llvm::Instruction::FPToSI, source, targetType);
            if (sourceType.isPrimitiveBool())      return createNumericCast (::llvm::Instruction::ZExt,   source, targetType);
        }

        if (targetType.isPrimitiveFloat32())
        {
            if (sourceType.isPrimitiveInt())       return createNumericCast (::llvm::Instruction::SIToFP,  source, targetType);
            if (sourceType.isPrimitiveFloat64())   return createNumericCast (::llvm::Instruction::FPTrunc, source, targetType);
            if (sourceType.isPrimitiveBool())      return createNumericCast (::llvm::Instruction::UIToFP,  source, targetType);
        }

        if (targetType.isPrimitiveFloat64())
        {
            if (sourceType.isPrimitiveInt())       return createNumericCast (::llvm::Instruction::SIToFP, source, targetType);
            if (sourceType.isPrimitiveFloat32())   return createNumericCast (::llvm::Instruction::FPExt,  source, targetType);
            if (sourceType.isPrimitiveBool())      return createNumericCast (::llvm::Instruction::UIToFP, source, targetType);
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    ValueReader createSliceFromArray (const AST::TypeBase& elementType, ValueReader sourceArray,
                                      uint32_t offset, uint32_t numElements)
    {
        auto fatPointerType = getFatPointerType (elementType);
        auto fatPointer = functionEntryBlockBuilder->CreateAlloca (fatPointerType);

        auto& b = getBlockBuilder();

        auto ptr = getPointer (sourceArray);
        ptr = b.CreateConstInBoundsGEP2_32 (getLLVMType (sourceArray.type->skipConstAndRefModifiers()), ptr, 0, offset);

        auto size = dereference (createConstantInt32 (static_cast<int32_t> (numElements)));

        b.CreateStore (ptr,  b.CreateConstInBoundsGEP2_32 (fatPointerType, fatPointer, 0, 0));
        b.CreateStore (size, b.CreateConstInBoundsGEP2_32 (fatPointerType, fatPointer, 0, 1));

        return makeReader (fatPointer, AST::createSliceOfType (elementType.context, elementType));
    }

    ValueReader createGetSliceSize (ValueReader slice)
    {
        auto& b = getBlockBuilder();
        auto sizeField = b.CreateConstInBoundsGEP2_32 (getFatPointerType (*slice.type->getArrayOrVectorElementType()),
                                                       getPointer (slice), 0, 1);
        return makeReader (b.CreateLoad (getInt32Type(), sizeField), allocator.int32Type);
    }

    ValueReference createStateUpcast (const AST::StructType& parentType, const AST::StructType& childType, ValueReference value)
    {
        auto path = AST::getPathToChildOfStruct (parentType, childType);
        CMAJ_ASSERT (path.size() == 1);
        auto index = static_cast<uint32_t> (path.front());
        auto& indexedType = AST::castToTypeBaseRef (parentType.memberTypes[index]).skipConstAndRefModifiers();

        auto& b = getBlockBuilder();
        ::llvm::Value* valuePointer = nullptr;

        if (indexedType.isArray())
        {
            auto memberIndex = childType.indexOfMember ("_instanceIndex");
            CMAJ_ASSERT (memberIndex >= 0);  // Child type should have a member indicating which array instance this is

            auto instanceIndex = makeReader (b.CreateStructGEP (getLLVMType (childType), getPointer (value),
                                                                static_cast<unsigned> (memberIndex)),
                                             childType.getMemberType (static_cast<size_t> (memberIndex)));

            auto negIndex = b.CreateNeg (dereference (instanceIndex));

            ::llvm::Value* arrayIndexes[] = { negIndex };
            valuePointer = b.CreateInBoundsGEP (getLLVMType (childType), getPointer (value), arrayIndexes);
        }
        else
        {
            valuePointer = getPointer (value);
        }

        auto address = b.CreateBitCast (valuePointer, ::llvm::Type::getInt8PtrTy (*context));
        ::llvm::Value* indexes[] = { createConstantInt32 (-static_cast<int32_t> (getStructMemberOffset (parentType, index))).value };
        auto offsetAddress = b.CreateInBoundsGEP (::llvm::Type::getInt8Ty (*context), address, indexes);
        return makeReference (b.CreateBitCast (offsetAddress, getLLVMType (parentType)->getPointerTo()), parentType);
    }

    bool functionShouldReturnTypeAsArgument (const AST::TypeBase& returnType)
    {
        return returnType.isFixedSizeArray();
    }

};

} // namespace cmaj::llvm
