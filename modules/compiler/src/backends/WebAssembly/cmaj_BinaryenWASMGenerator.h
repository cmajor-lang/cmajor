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

#if CMAJ_ENABLE_CODEGEN_BINARYEN

#include "choc/platform/choc_DisableAllWarnings.h"

#include "binaryen/src/wasm.h"
#include "binaryen/src/wasm-builder.h"
#include "binaryen/src/wasm-io.h"
#include "binaryen/src/wasm-binary.h"
#include "binaryen/src/wasm-s-parser.h"
#include "binaryen/src/ir/trapping.h"
#include "binaryen/src/ir/utils.h"

#include "choc/platform/choc_ReenableAllWarnings.h"

//==============================================================================
#include "../../codegen/cmaj_CodeGenerator.h"

#include "cmaj_WebAssembly.h"

namespace cmaj::webassembly::binaryen
{

//==============================================================================
struct WebAssemblyGenerator
{
    WebAssemblyGenerator (const AST::Program& p) : program (p) {}
    ~WebAssemblyGenerator() = default;

    bool generate (const BuildSettings& buildSettings)
    {
        moduleInfo.getChocType = [typeMappings = std::unordered_map<const AST::TypeBase*, choc::value::Type>()] (const AST::TypeBase& t) mutable
        {
            return getChocType (typeMappings, t);
        };

        mainClassName = makeSafeIdentifier (program.getMainProcessor().name.get());
        currentModule = std::make_unique<wasm::Module>();
        currentModule->features.setBulkMemory();

        currentModule->memory.initial = 1;
        currentModule->memory.exists = true;
        currentModule->memory.shared = false;
        currentModule->memory.name = "memory";
        addExport (currentModule->memory.name, wasm::ExternalKind::Memory);

        return generateFromLinkedProgram (buildSettings);
    }

    WebAssemblyModule takeModuleInfo()
    {
        CMAJ_ASSERT (currentModule != nullptr);
        moduleInfo.binaryWASMData = createWASM (*currentModule);
        return std::move (moduleInfo);
    }

    //==============================================================================
    struct ValueReader
    {
        // wasm::Expression mustn't be used in more than one place in the syntax tree, so this
        // holder keeps track of whether the expression is already in use, so that getExpression()
        // can make a new copy of it if needed
        struct ExpressionHolder
        {
            ExpressionHolder (wasm::Expression& e) : expression (std::addressof (e)) {}

            wasm::Expression* expression = nullptr;
            bool isInUse = false;
        };

        ExpressionHolder* expression = nullptr;
        bool isPointer = false;
        ptr<const AST::TypeBase> type;

        operator bool() const   { return expression != nullptr; }
        operator int() const = delete;

        wasm::Expression& getExpression() const
        {
            CMAJ_ASSERT (expression != nullptr);
            return *expression->expression;
        }

        bool isZero() const
        {
            if (expression == nullptr)
                return true;

            if (auto c = getExpression().dynCast<wasm::Const>())
                return c->value.isZero();

            return false;
        }

        bool isConstIntValue (int32_t value) const
        {
            if (value == 0)
                return isZero();

            if (expression != nullptr)
            {
                if (auto c = getExpression().dynCast<wasm::Const>())
                {
                    if (c->value.type == wasm::Type::i32)  return c->value.geti32() == value;
                    if (c->value.type == wasm::Type::i64)  return c->value.geti64() == value;
                }
            }

            return false;
        }
    };

    struct ValueReference
    {
        int32_t localIndex = -1;
        ValueReader memoryOffset;
        ptr<const AST::TypeBase> type;

        ValueReader getPointerAddress() const   { CMAJ_ASSERT (isPointer() && memoryOffset.isPointer); return memoryOffset; }
        bool isPointer() const                  { return memoryOffset; }

        operator bool() const                   { return localIndex >= 0 || memoryOffset; }
        operator int() const = delete;
    };

    //==============================================================================
    static constexpr const char* stackPointerGlobalName      = "__stack_pointer";
    static constexpr const char* memoryBaseGlobalName        = "__memory_base";
    static constexpr const char* returnValuePointerParamName = "__out";

    const AST::Program& program;
    AST::Allocator allocator;
    uint64_t stackSize = 0;

    std::string mainClassName;
    ptr<AST::StructType> stateStruct, ioStruct;
    CodeGenerator<WebAssemblyGenerator>* codeGenerator = nullptr;

    std::unique_ptr<wasm::Module> currentModule;
    std::unordered_map<const AST::Function*, wasm::Function*> functions;
    std::unordered_map<const AST::VariableDeclaration*, ValueReference> globalVariables;

    //==============================================================================
    static std::string createWASM (wasm::Module& module)
    {
        wasm::BufferWithRandomAccess buffer;
        wasm::WasmBinaryWriter writer (std::addressof (module), buffer);
        writer.setNamesSection (false);
        writer.write();
        return std::string (reinterpret_cast<const char*> (buffer.data()), buffer.size());
    }

    void addExport (wasm::Name name, wasm::ExternalKind kind)
    {
        auto ex = std::make_unique<wasm::Export>();
        ex->name  = name;
        ex->value = name;
        ex->kind = kind;
        currentModule->addExport (std::move (ex));
    }

    void addGlobalInt (const char* name, int32_t value)
    {
        auto g = std::make_unique<wasm::Global>();
        g->setExplicitName (name);
        g->type = wasm::Type::i32;
        g->mutable_ = true;
        g->init = getExpression (createConstantInt32 (value));
        currentModule->addGlobal (g.release());
    }

    //==============================================================================
    WebAssemblyModule moduleInfo;

    //==============================================================================
    struct GlobalData
    {
        int32_t addEmptyChunk (const choc::value::Type& type)
        {
            return addEmptyChunk (type.getValueDataSize());
        }

        int32_t addEmptyChunk (size_t size)
        {
            return addChunk (size).getAddress();
        }

        int32_t addContentChunk (const choc::value::ValueView& value, choc::value::SimpleStringDictionary& dictionary)
        {
            auto size = value.getType().getValueDataSize();
            auto& c = addChunk (size);
            auto resultAddress = c.getAddress(); // take a copy as the dictionary merge may resize the chunk vector
            CMAJ_ASSERT (value.getRawData() != nullptr);
            memcpy (c.content.data(), value.getRawData(), size);

            // this will update any string handles in the data to our string dictionary
            choc::value::ValueView inPlaceView (value.getType(), c.content.data(), value.getDictionary());
            inPlaceView.setDictionary (std::addressof (dictionary));

            return resultAddress;
        }

        int32_t getNullConstant (const choc::value::Type& type)
        {
            auto size = type.getValueDataSize();

            if (currentNullConstantChunkIndex < 0 || size > chunks[static_cast<uint32_t> (currentNullConstantChunkIndex)].size)
            {
                currentNullConstantChunkIndex = static_cast<int> (chunks.size());
                addChunk ((size + 256u) & ~255u);
            }

            return chunks[static_cast<uint32_t> (currentNullConstantChunkIndex)].getAddress();
        }

        size_t getSize() const        { return totalSize; }

        void addToModule (WebAssemblyGenerator& owner)
        {
            for (auto& c : chunks)
            {
                CMAJ_ASSERT (c.size == c.content.size());
                auto newSegment = std::make_unique<wasm::DataSegment>();
                newSegment->isPassive = false;
                newSegment->offset = owner.createAddressExpression (c.start);
                newSegment->data = c.content;
                owner.currentModule->dataSegments.push_back (std::move (newSegment));
            }
        }

        size_t baseAddress = 16; // using address 0 causes unexplained problems

    private:
        struct Chunk
        {
            size_t start = 0, size = 0;
            std::vector<char> content;

            int32_t getAddress() const      { return static_cast<int32_t> (start); }
        };

        std::vector<Chunk> chunks;
        size_t totalSize = 0;
        int currentNullConstantChunkIndex = -1;

        Chunk& addChunk (size_t size)
        {
            size = (size + 4u) & ~3u;
            auto start = baseAddress + getSize();
            chunks.push_back ({});
            auto& c = chunks.back();
            c.start = start;
            c.size = size;
            c.content.resize (size);
            totalSize += size;
            return c;
        }
    };

    GlobalData globalData;

    //==============================================================================
    struct FunctionInfo
    {
        wasm::Function* function = nullptr;
        size_t stackSizeNeeded = 0;
        int32_t stackPointerLocal = -1;
        bool hasCheckedForGlobalStackUse = false, usesGlobalStack = false,
             stackCorrectionCodePresent = false;
    };

    std::unordered_map<wasm::Function*, FunctionInfo> functionInfo;
    FunctionInfo* currentFunction = nullptr;

    FunctionInfo& getFunctionInfo (wasm::Function& f)              { return functionInfo[std::addressof (f)]; }

    //==============================================================================
    bool generateFromLinkedProgram (const BuildSettings& buildSettings)
    {
        auto& mainProcessor = program.getMainProcessor();
        stateStruct = *mainProcessor.findStruct (mainProcessor.getStrings().stateStructName);
        ioStruct    = *mainProcessor.findStruct (mainProcessor.getStrings().ioStructName);

        moduleInfo.stateStructType = stateStruct;
        moduleInfo.ioStructType    = ioStruct;

        currentModule->features.setTruncSat();

        findLibraryFunctionsUsingStackPointer();

        CodeGenerator<WebAssemblyGenerator> codeGen (*this, mainProcessor);
        codeGenerator = std::addressof (codeGen);

        codeGen.emitTypes();
        codeGen.emitGlobals();
        codeGen.emitFunctions();

        allocateGlobalState();
        generateInitialiseFunction();
        generateAdvanceFunctions();
        addStackPointerCorrectionToFunctions();
        calculateStackAndMemorySize();
        globalData.addToModule (*this);
        runAutodrop();
        setClampMode();
        runOptimisations (buildSettings.getOptimisationLevel());

        if (buildSettings.shouldDumpDebugInfo())
        {
            std::cout << cmaj::AST::print (program) << std::endl
                      << *currentModule << std::endl;
        }

       #if CMAJ_DEBUG
        if (! wasm::WasmValidator().validate (*currentModule, wasm::WasmValidator::FlagValues::Globally
                                                               | wasm::WasmValidator::FlagValues::Web))
        {
            std::cout << *currentModule;
            CMAJ_ASSERT_FALSE;
        }
       #endif

        moduleInfo.nativeTypeLayouts.createLayout = [] (const AST::TypeBase& t)
        {
            auto packer = std::make_unique<NativeTypeLayout> (t);
            packer->generateWithoutPacking();
            return packer;
        };

        return true;
    }

    void allocateGlobalState()
    {
        moduleInfo.stateStructAddress  = static_cast<uint32_t> (globalData.addEmptyChunk (moduleInfo.getChocType (*stateStruct)));
        moduleInfo.ioStructAddress     = static_cast<uint32_t> (globalData.addEmptyChunk (moduleInfo.getChocType (*ioStruct)));
        moduleInfo.scratchSpaceAddress = static_cast<uint32_t> (globalData.addEmptyChunk (65536));
    }

    void generateInitialiseFunction()
    {
        auto& initFn = addNewFunction ("initialise", allocator.voidType, true);
        auto stateParam = wasm::Builder::addParam (std::addressof (initFn), "state", wasm::Type::i32);
        auto processorIDParam = wasm::Builder::addParam (std::addressof (initFn), "processorID", wasm::Type::i32);
        auto sessionIDParam = wasm::Builder::addParam (std::addressof (initFn), "sessionID", wasm::Type::i32);
        auto frequencyParam = wasm::Builder::addParam (std::addressof (initFn), "frequency", wasm::Type::f64);

        auto& mainBlock = allocateBlock ("_body");
        initFn.body = std::addressof (mainBlock);

        auto& systemInit = *program.getMainProcessor().findSystemInitFunction();
        auto& call = createCallExpression (getFunction (systemInit));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (stateParam), wasm::Type::i32));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (processorIDParam), wasm::Type::i32));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (sessionIDParam), wasm::Type::i32));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (frequencyParam), wasm::Type::f64));
        call.finalize();
        mainBlock.list.push_back (std::addressof (call));
    }

    void generateAdvanceFunctions()
    {
        if (auto fn = program.getMainProcessor().findSystemAdvanceFunction())
            generateAdvanceFunction (*fn);

        if (auto fn = program.getMainProcessor().findMainFunction())
            generateAdvanceOneFrameFunction (*fn);
    }

    void generateAdvanceOneFrameFunction (const AST::Function& fn)
    {
        auto& advanceFn = addNewFunction ("advanceOneFrame", allocator.voidType, true);
        auto stateParam = wasm::Builder::addParam (std::addressof (advanceFn), "state", wasm::Type::i32);
        auto ioParam = wasm::Builder::addParam (std::addressof (advanceFn), "io", wasm::Type::i32);

        auto& mainBlock = allocateBlock ("_body");
        advanceFn.body = std::addressof (mainBlock);

        auto& call = createCallExpression (getFunction (fn));

        call.operands.push_back (createLocalGet (static_cast<int32_t> (stateParam), wasm::Type::i32));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (ioParam), wasm::Type::i32));

        call.finalize();
        mainBlock.list.push_back (std::addressof (call));
    }

    void generateAdvanceFunction (const AST::Function& fn)
    {
        auto& advanceFn = addNewFunction ("advanceBlock", allocator.voidType, true);
        auto stateParam = wasm::Builder::addParam (std::addressof (advanceFn), "state", wasm::Type::i32);
        auto ioParam = wasm::Builder::addParam (std::addressof (advanceFn), "io", wasm::Type::i32);
        auto numFramesParamIndex = wasm::Builder::addParam (std::addressof (advanceFn), "numFrames", wasm::Type::i32);

        auto& mainBlock = allocateBlock ("_body");
        advanceFn.body = std::addressof (mainBlock);

        auto& call = createCallExpression (getFunction (fn));

        call.operands.push_back (createLocalGet (static_cast<int32_t> (stateParam), wasm::Type::i32));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (ioParam), wasm::Type::i32));
        call.operands.push_back (createLocalGet (static_cast<int32_t> (numFramesParamIndex), wasm::Type::i32));

        call.finalize();
        mainBlock.list.push_back (std::addressof (call));
    }

    void runAutodrop()
    {
        wasm::PassRunner runner (currentModule.get());
        wasm::AutoDrop().run (&runner, currentModule.get());
    }

    void setClampMode()
    {
        wasm::PassRunner runner (currentModule.get());
        wasm::addTrapModePass (runner, wasm::TrapMode::Clamp);
        runner.run();
    }

    void runOptimisations (int level)
    {
        if (level <= 0)
            return;

        wasm::PassOptions options;
        // options.optimizeLevel = std::min (2, level);
        // options.shrinkLevel = 0;

        wasm::PassRunner runner (currentModule.get(), std::move (options));
        runner.addDefaultOptimizationPasses();
        runner.run();
    }

    static uint64_t roundTo4 (uint64_t size)             { return (size + 3u) & ~3ull; }
    static uint64_t roundUpMemorySize (uint64_t size)    { return (size + (size / 4u) + 255u) & ~255ull; }

    void iterateCallSequencesForMaxStackSize (uint64_t currentStack, AST::Function& f)
    {
        if (f.isGenericOrParameterised())
            return;

        if (auto wasmFn = functions.find (std::addressof (f)); wasmFn != functions.end())
        {
            if (auto info = functionInfo.find (wasmFn->second); info != functionInfo.end())
            {
                currentStack += info->second.stackSizeNeeded;
                stackSize = std::max (stackSize, currentStack);
            }
        }

        f.visitObjectsInScope ([this, currentStack] (const AST::Object& s)
        {
            if (auto fc = s.getAsFunctionCall())
                if (auto targetFn = fc->getTargetFunction())
                    iterateCallSequencesForMaxStackSize (currentStack, *targetFn);
        });
    }

    void calculateStackAndMemorySize()
    {
        program.visitAllFunctions (true, [this] (AST::Function& f) { iterateCallSequencesForMaxStackSize (0, f); });
        stackSize = roundUpMemorySize (stackSize);

        auto stateMemorySize = roundUpMemorySize (globalData.getSize());
        auto totalMemory = stateMemorySize + stackSize;
        moduleInfo.stackTop = static_cast<uint32_t> (stateMemorySize);

        addGlobalInt (stackPointerGlobalName, static_cast<int32_t> (stateMemorySize));
        addGlobalInt (memoryBaseGlobalName, 0);

        moduleInfo.initialNumMemPages = static_cast<uint32_t> (1u + totalMemory / 65536u);
        currentModule->memory.initial = static_cast<wasm::Address> (moduleInfo.initialNumMemPages);
        currentModule->memory.max     = static_cast<wasm::Address> (moduleInfo.initialNumMemPages);
    }

    //==============================================================================
    struct StackFrame
    {
        StackFrame (WebAssemblyGenerator& g) : owner (g) {}

        WebAssemblyGenerator& owner;
        std::unordered_map<const AST::VariableDeclaration*, wasm::Index> parameters;
        std::unordered_map<const AST::VariableDeclaration*, ValueReference> localVariables;
        ptr<const AST::TypeBase> functionReturnType;
        bool hasPrimitiveReturnType = false;
        std::vector<size_t> stackSizes;
        size_t maxStackSize = 0, currentStackSize = 0;

        void beginFunction (const AST::TypeBase& returnType)
        {
            hasPrimitiveReturnType = isWASMPrimitive (returnType);
            functionReturnType = returnType;
            maxStackSize = currentStackSize = hasPrimitiveReturnType ? 0 : owner.getTypeSize (returnType);
            stackSizes.clear();
            parameters.clear();
            localVariables.clear();

            if (! hasPrimitiveReturnType)
                owner.getOrCreateLocalVariableIndexForStackPointer();
        }

        void beginBlock()
        {
            stackSizes.push_back (currentStackSize);
        }

        void endBlock()
        {
            currentStackSize = stackSizes.back();
            stackSizes.pop_back();
        }

        ValueReference getVariableReference (const AST::VariableDeclaration& v)
        {
            auto local = localVariables.find (std::addressof (v));

            if (local != localVariables.end())
                return local->second;

            auto& type = *v.getType();
            bool canLiveInLocalStack = isWASMPrimitive (type) && ! owner.codeGenerator->variableMustHaveAddress (v);
            ValueReference ref;

            if (v.isParameter())
            {
                auto param = parameters.find (std::addressof (v));
                CMAJ_ASSERT (param != parameters.end());
                auto index = static_cast<int32_t> (param->second);

                if (type.isReference() || ! isWASMPrimitive (type))
                {
                    ref = makeReference (owner.createLocalGet (index, owner.allocator.int32Type), type);
                }
                else if (canLiveInLocalStack)
                {
                    ref = { index, {}, type };
                }
                else
                {
                    ref = allocateLocalStackSpace (type);
                    owner.insertAtStartOfFunction (owner.createStore (ref, owner.createLocalGet (index, type)));
                }
            }
            else
            {
                CMAJ_ASSERT (v.isLocal());

                if (canLiveInLocalStack)
                {
                    auto index = wasm::Builder::addVar (owner.currentFunction->function,
                                                        owner.codeGenerator->getVariableName (v), owner.getType (type));
                    ref = ValueReference { static_cast<int32_t> (index), {}, type };
                }
                else
                {
                    ref = allocateLocalStackSpace (type);
                }
            }

            localVariables[std::addressof (v)] = ref;
            return ref;
        }

        ValueReference createLocalValuePointer (int32_t offset, const AST::TypeBase& type)
        {
            auto localStackPointerIndex = owner.getOrCreateLocalVariableIndexForStackPointer();
            auto localStackPointer = owner.createLocalGet (localStackPointerIndex, owner.allocator.int32Type);
            auto calcStackOffset = owner.createAddInt32 (localStackPointer, owner.createConstantInt32 (offset));
            return makeReference (calcStackOffset, type);
        }

        ValueReference allocateLocalStackSpace (const AST::TypeBase& type)
        {
            return createLocalValuePointer (allocateLocalStackSpace (owner.getTypeSize (type)), type);
        }

        int32_t allocateLocalStackSpace (size_t size)
        {
            auto address = static_cast<int32_t> (currentStackSize);
            currentStackSize += size;
            maxStackSize = std::max (maxStackSize, currentStackSize);
            return address;
        }

        ValueReference createReturnValuePointer()
        {
            return makeReference (owner.createLocalGet (0, owner.allocator.int32Type), *functionReturnType);
        }
    };

    //==============================================================================
    StackFrame stackFrame { *this };

    std::vector<wasm::Block*> blockStack;
    wasm::Block* lastBlock = nullptr;
    size_t nextBlockIndex = 0, nextLoopIndex = 0;

    std::string getNewBlockName()    { return "_b" + std::to_string (++nextBlockIndex); }

    ValueReader createReaderForReference (ValueReference ref)
    {
        if (ref.isPointer())
            return { ref.memoryOffset.expression, true, ref.type };

        CMAJ_ASSERT (ref.localIndex >= 0);
        return createLocalGet (ref.localIndex, *ref.type);
    }

    void pushBlock (wasm::Block& block)
    {
        blockStack.push_back (std::addressof (block));
    }

    wasm::Block& popBlock()
    {
        auto& b = *blockStack.back();
        blockStack.pop_back();
        b.finalize();
        lastBlock = std::addressof (b);
        return b;
    }

    void append (wasm::Expression* e)
    {
        if (blockStack.empty())
            currentFunction->function->body = e;
        else
            blockStack.back()->list.push_back (e);
    }

    void append (wasm::Expression& e)
    {
        append (std::addressof (e));
    }

    void insertAtStartOfFunction (wasm::Expression& e)
    {
        CMAJ_ASSERT (! blockStack.empty());
        blockStack.front()->list.insertAt (0, std::addressof (e));
    }

    template <typename Type> Type& allocate()       { return *currentModule->allocator.alloc<Type>(); }

    template <typename Type> Type& allocate (wasm::Name name)
    {
        auto& o = allocate<Type>();
        o.name = std::move (name);
        return o;
    }

    wasm::Block& allocateBlock (std::string name)   { return allocate<wasm::Block> (std::move (name)); }
    wasm::Block& allocateBlock()                    { return allocateBlock (getNewBlockName()); }

    ValueReader makeReader (wasm::Expression& e, const AST::TypeBase& type, bool isPointer)
    {
        return { std::addressof (allocator.allocate<ValueReader::ExpressionHolder> (e)), isPointer, type };
    }

    ValueReader makeReader (ValueReference ref)
    {
        return { ref.getPointerAddress().expression, true, ref.type };
    }

    static ValueReference makeReference (ValueReader pointer, const AST::TypeBase& type)
    {
        pointer.type = type;
        pointer.isPointer = true;
        return { -1, pointer, type };
    }

    ValueReader reinterpretPointer (ValueReader pointer, const AST::TypeBase& pointeeType)
    {
        CMAJ_ASSERT (pointer.isPointer);
        pointer.type = pointeeType;
        return pointer;
    }

    ValueReference reinterpretPointer (ValueReference pointer, const AST::TypeBase& pointeeType)
    {
        CMAJ_ASSERT (pointer.isPointer());
        pointer.type = pointeeType;
        return pointer;
    }

    // Accessing the expression via this function makes sure that the expression returned isn't
    // already in use somewhere else in the syntax tree.
    wasm::Expression* getExpression (ValueReader r)
    {
        if (r.expression == nullptr)
            return {};

        if (r.expression->isInUse)
            return wasm::ExpressionManipulator::copy (r.expression->expression, *currentModule);

        r.expression->isInUse = true;
        return r.expression->expression;
    }

    wasm::Expression& getExpressionRef (ValueReader r)
    {
        auto e = getExpression (r);
        CMAJ_ASSERT (e != nullptr);
        return *e;
    }

    //==============================================================================
    int32_t getOrCreateLocalVariableIndexForStackPointer()
    {
        if (currentFunction->stackPointerLocal >= 0)
            return currentFunction->stackPointerLocal;

        auto index = static_cast<int32_t> (wasm::Builder::addVar (currentFunction->function, "__stack", wasm::Type::i32));
        currentFunction->stackPointerLocal = index;
        currentFunction->usesGlobalStack = true;
        currentFunction->hasCheckedForGlobalStackUse = true;
        return index;
    }

    void addStackPointerCorrectionToFunction (FunctionInfo& info)
    {
        if (info.stackCorrectionCodePresent)
            return;

        info.stackCorrectionCodePresent = true;
        CMAJ_ASSERT (info.stackPointerLocal >= 0);
        auto oldBody = info.function->body->dynCast<wasm::Block>();
        CMAJ_ASSERT (oldBody != nullptr);

        auto& mainBlock = allocateBlock ("_body");
        info.function->body = std::addressof (mainBlock);

        mainBlock.list.push_back (createStackPointerMove (info));

        if (! oldBody->name.is() || oldBody->name == mainBlock.name)
            oldBody->name = "_inner";

        replaceReturnsWithBreaks (oldBody);

        auto returnType = info.function->getResults();

        if (returnType != wasm::Type::none)
        {
            auto& setResult = allocate<wasm::LocalSet>();
            setResult.index = wasm::Builder::addVar (info.function, "__result", returnType);
            setResult.value = oldBody;
            setResult.makeSet(); // (calls finalize)

            mainBlock.list.push_back (std::addressof (setResult));
            mainBlock.list.push_back (createStackPointerReset (info));
            mainBlock.list.push_back (createLocalGet (static_cast<int32_t> (setResult.index), returnType));
        }
        else
        {
            mainBlock.list.push_back (oldBody);
            mainBlock.list.push_back (createStackPointerReset (info));
        }

        mainBlock.finalize();
    }

    wasm::Expression* createStackPointerMove (FunctionInfo& info)
    {
        auto& localTee = allocate<wasm::LocalSet>();
        localTee.index = static_cast<wasm::Index> (info.stackPointerLocal);
        localTee.value = createGetGlobalStackPointer();
        localTee.makeTee (wasm::Type::i32); // (calls finalize)

        auto& add = allocate<wasm::Binary>();
        add.op    = wasm::BinaryOp::AddInt32;
        add.left  = std::addressof (localTee);
        add.right = createAddressExpression (info.stackSizeNeeded);
        add.finalize();

        auto& globalSet = allocate<wasm::GlobalSet> (stackPointerGlobalName);
        globalSet.value = std::addressof (add);
        globalSet.finalize();

        return std::addressof (globalSet);
    }

    wasm::Expression* createStackPointerReset (FunctionInfo& info)
    {
        auto& globalSet = allocate<wasm::GlobalSet> (stackPointerGlobalName);
        globalSet.value = createLocalGet (info.stackPointerLocal, wasm::Type::i32);
        globalSet.finalize();

        return std::addressof (globalSet);
    }

    void replaceReturnsWithBreaks (wasm::Block* body)
    {
        struct ReturnReplacer  : public wasm::PostWalker<ReturnReplacer, wasm::Visitor<ReturnReplacer>>
        {
            ReturnReplacer (WebAssemblyGenerator& o, wasm::Name b) : owner (o), blockToBreakFrom (std::move (b)) {}

            WebAssemblyGenerator& owner;
            wasm::Name blockToBreakFrom;

            void visitReturn (wasm::Return* r)
            {
                auto& br = owner.allocate<wasm::Break> (blockToBreakFrom);
                br.value = r->value;
                br.finalize();
                replaceCurrent (std::addressof (br));
            }
        };

        CMAJ_ASSERT (body->name.is());
        ReturnReplacer r (*this, body->name);
        wasm::Expression* e = body;
        r.walk (e);
        body->finalize();
    }

    wasm::GlobalGet* createGetGlobalStackPointer()
    {
        auto& g = allocate<wasm::GlobalGet> (stackPointerGlobalName);
        g.type = wasm::Type::i32;
        g.finalize();
        return std::addressof (g);
    }

    bool usesGlobalStack (FunctionInfo& info)
    {
        if (info.hasCheckedForGlobalStackUse)
            return info.usesGlobalStack;

        info.hasCheckedForGlobalStackUse = true;

        if (info.stackSizeNeeded != 0)
        {
            info.usesGlobalStack = true;
            return true;
        }

        return info.usesGlobalStack;
    }

    void addStackPointerCorrectionToFunctions()
    {
        for (auto& info : functionInfo)
            if (usesGlobalStack (info.second))
                addStackPointerCorrectionToFunction (info.second);
    }

    void findLibraryFunctionsUsingStackPointer()
    {
        for (auto& f : currentModule->functions)
        {
            auto& info = getFunctionInfo (*f);
            info.hasCheckedForGlobalStackUse = true;
            info.stackCorrectionCodePresent = true;
            info.usesGlobalStack = expressionUsesStackPointer (f->body);
        }
    }

    static bool expressionUsesStackPointer (wasm::Expression* e)
    {
        struct StackPointerVisitor  : public wasm::PostWalker<StackPointerVisitor, wasm::Visitor<StackPointerVisitor>>
        {
            bool anyFound = false;

            void visitGlobalGet (wasm::GlobalGet* e)   { if (e->name == stackPointerGlobalName) anyFound = true; }
            void visitGlobalSet (wasm::GlobalSet* e)   { if (e->name == stackPointerGlobalName) anyFound = true; }
        };

        StackPointerVisitor v;
        v.walk (e);
        return v.anyFound;
    }

    //==============================================================================
    static choc::value::Type getChocType (std::unordered_map<const AST::TypeBase*, choc::value::Type>& typeMappings,
                                          const AST::TypeBase& type)
    {
        auto t = typeMappings.find (std::addressof (type));

        if (t != typeMappings.end())
            return t->second;

        auto& ct = typeMappings[std::addressof (type)];

        // NB: must recurse the type manually rather than just calling TypeBase::toChocType() so
        // that we can convert nested slices to fat pointer objects
        if (type.isPrimitiveBool() || type.isPrimitiveString())
            ct = choc::value::Type::createInt32();
        else if (type.isSlice())
            ct = getFatPointerType();
        else if (type.isArray())
            ct = choc::value::Type::createArray (getChocType (typeMappings, *type.getArrayOrVectorElementType()), type.getArrayOrVectorSize (0));
        else if (auto s = AST::castTo<AST::StructType> (type.skipConstAndRefModifiers()))
        {
            ct = choc::value::Type::createObject (s->name.get());

            for (size_t i = 0; i < s->memberNames.size(); ++i)
                ct.addObjectMember (s->getMemberName (i), getChocType (typeMappings, s->getMemberType (i)));
        }
        else
            ct = type.toChocType();

        return ct;
    }

    static choc::value::Type getFatPointerType()
    {
        auto t = choc::value::Type::createObject ("_Slice");
        t.addObjectMember ("address", choc::value::Type::createInt32());
        t.addObjectMember ("size", choc::value::Type::createInt32());
        return t;
    }

    static choc::value::Value createFatPointer (int32_t address, size_t size)
    {
        return choc::value::createObject ("_Slice",
                                          "address", choc::value::createInt32 (address),
                                          "size", choc::value::createInt32 (static_cast<int32_t> (size)));
    }

    static wasm::Type getType (const AST::TypeBase& type)
    {
        auto& t = type.skipConstAndRefModifiers();

        if (auto p = t.getAsPrimitiveType())
        {
            if (p->isVoid())                 return wasm::Type::none;
            if (p->isPrimitiveBool())        return wasm::Type::i32;
            if (p->isPrimitiveInt32())       return wasm::Type::i32;
            if (p->isPrimitiveInt64())       return wasm::Type::i64;
            if (p->isPrimitiveFloat32())     return wasm::Type::f32;
            if (p->isPrimitiveFloat64())     return wasm::Type::f64;
        }

        return wasm::Type::i32;
    }

    size_t getTypeSize (const AST::TypeBase& type)          { return getTypeSize (moduleInfo.getChocType (type)); }
    size_t getTypeSize (const choc::value::Type& type)      { return type.getValueDataSize(); }

    static bool areTypeRepresentationsEquivalent (const AST::TypeBase& t1, const AST::TypeBase& t2)
    {
        if (t1.isVectorSize1())  return areTypeRepresentationsEquivalent (*t1.getArrayOrVectorElementType(), t2);
        if (t2.isVectorSize1())  return areTypeRepresentationsEquivalent (t1, *t2.getArrayOrVectorElementType());

        if (t1.isPrimitiveInt32()) return t2.isPrimitiveInt32() || t2.isPrimitiveBool() || t2.isPrimitiveString() || t2.isEnum();
        if (t2.isPrimitiveInt32()) return t1.isPrimitiveInt32() || t1.isPrimitiveBool() || t1.isPrimitiveString() || t1.isEnum();

        if (t1.isSlice()) return t2.isSlice();

        return t1.isSameType (t2, AST::TypeBase::ComparisonFlags::ignoreConst
                                    | AST::TypeBase::ComparisonFlags::ignoreReferences
                                    | AST::TypeBase::ComparisonFlags::duckTypeStructures);
    }

    //==============================================================================
    void addStruct (const AST::StructType&) {}
    void addAlias (const AST::Alias&, const AST::TypeBase&) {}

    void addGlobalVariable (const AST::VariableDeclaration& v, const AST::TypeBase& type, std::string_view, ValueReader)
    {
        int32_t address = 0;

        if (auto constantValue = AST::getAsFoldedConstant (v.initialValue))
            address = globalData.addContentChunk (constantValue->toValue (getSliceToValueFn()), moduleInfo.stringDictionary);
        else
            address = globalData.addEmptyChunk (moduleInfo.getChocType (type));

        globalVariables[std::addressof (v)] = makeReference (createConstantInt32 (address), type);
    }

    //==============================================================================
    wasm::Function& addNewFunction (std::string_view name, const AST::TypeBase& returnType, bool shouldExport)
    {
        auto fn = std::make_unique<wasm::Function>();
        fn->setExplicitName (std::string (name));

        if (isWASMPrimitive (returnType) || returnType.isVoid())
        {
            fn->setResults (getType (returnType));
        }
        else
        {
            // For complex return types, we use an extra parameter which tell the function where
            // to write the result, and the function then returns this pointer for a caller to use
            fn->setResults (wasm::Type::i32);
            wasm::Builder::addParam (fn.get(), returnValuePointerParamName, wasm::Type::i32);
        }

        if (shouldExport)
            addExport (fn->name, wasm::ExternalKind::Function);

        return *currentModule->addFunction (std::move (fn));
    }

    wasm::Function& getFunction (const AST::Function& fn) const
    {
        auto found = functions.find (std::addressof (fn));
        CMAJ_ASSERT (found != functions.end());
        return *found->second;
    }

    wasm::Function& getOrCreateFunction (const AST::Function& fn, std::string_view functionName)
    {
        auto found = functions.find (std::addressof (fn));

        if (found != functions.end())
            return *found->second;

        auto& f = addNewFunction (functionName, AST::castToTypeBaseRef (fn.returnType),
                                  (fn.isEventHandler && fn.findParentModule() == program.getMainProcessor()) || fn.isExported);
        functions[std::addressof (fn)] = std::addressof (f);

        for (auto& param : fn.iterateParameters())
        {
            auto& paramType = *param.getType();
            // NB: can't use getVariableName to get the parameter name here, because this may be called during
            // the building of a different function, at which point the name table will be wrong
            wasm::Builder::addParam (std::addressof (f), std::string (param.getName()),
                                     paramType.isReference() ? wasm::Type::i32
                                                             : getType (paramType));
        }

        return f;
    }

    void beginFunction (const AST::Function& fn, std::string_view functionName, const AST::TypeBase& returnType)
    {
        CMAJ_ASSERT (currentFunction == nullptr);
        auto& f = getOrCreateFunction (fn, functionName);
        currentFunction = std::addressof (getFunctionInfo (f));
        currentFunction->function = std::addressof (f);
        nextBlockIndex = 0;
        nextLoopIndex = 0;

        stackFrame.beginFunction (returnType);

        for (auto& param : fn.iterateParameters())
        {
            auto index = f.localIndices.find (codeGenerator->getVariableName (param));
            CMAJ_ASSERT (index != f.localIndices.end());
            stackFrame.parameters[std::addressof (param)] = index->second;
        }
    }

    void endFunction()
    {
        currentFunction->stackSizeNeeded = stackFrame.maxStackSize;

        auto body = currentFunction->function->body->dynCast<wasm::Block>();

        if (! body->list.empty())
            body->finalize (body->list.back()->type);

        currentFunction = nullptr;
    }

    void beginBlock()
    {
        pushBlock (allocateBlock());
        stackFrame.beginBlock();
    }

    void endBlock()
    {
        append (popBlock());
        stackFrame.endBlock();
    }

    struct IfStatus
    {
        wasm::If& i;
        bool hasTrueBranch, hasFalseBranch;
    };

    IfStatus beginIfStatement (ValueReader condition, bool hasTrueBranch, bool hasFalseBranch)
    {
        IfStatus i { allocate<wasm::If>(), hasTrueBranch, hasFalseBranch };

        i.i.condition = getContent (condition);
        i.i.ifTrue = nullptr;
        i.i.ifFalse = nullptr;

        if (i.hasTrueBranch)
        {
            auto& b = allocateBlock();
            pushBlock (b);
            i.i.ifTrue = std::addressof (b);
        }
        else
        {
            i.i.ifTrue = std::addressof (allocate<wasm::Nop>());
        }

        return i;
    }

    void addElseStatement (IfStatus& i)
    {
        CMAJ_ASSERT (i.hasFalseBranch);

        if (i.hasTrueBranch)
            popBlock();

        auto& b = allocateBlock();
        i.i.ifFalse = std::addressof (b);
        pushBlock (b);
    }

    void endIfStatement (IfStatus& i)
    {
        if (i.hasTrueBranch || i.hasFalseBranch)
            popBlock();

        i.i.finalize (wasm::Type::none);
        append (i.i);
    }

    struct LoopStatus
    {
        wasm::Block& outerBlock;
        wasm::Loop& loop;
    };

    LoopStatus beginLoop()
    {
        LoopStatus l { allocateBlock(), allocate<wasm::Loop> ("_loop" + std::to_string (++nextLoopIndex)) };
        auto& body = allocateBlock();
        l.loop.body = std::addressof (body);
        pushBlock (l.outerBlock);
        pushBlock (body);
        return l;
    }

    void endLoop (LoopStatus& l)
    {
        auto& branchToStart = allocate<wasm::Break> (l.loop.name);
        branchToStart.finalize();
        append (branchToStart);
        popBlock();
        l.loop.finalize();
        append (l.loop);
        popBlock();
        append (l.outerBlock);
        lastBlock = std::addressof (l.outerBlock);
    }

    bool addBreakFromCurrentLoop()  { return false; }

    using BreakInstruction = wasm::Break*;

    BreakInstruction addBreak()
    {
        auto& br = allocate<wasm::Break>();
        append (br);
        return std::addressof (br);
    }

    void resolveBreak (BreakInstruction br)
    {
        CMAJ_ASSERT (lastBlock != nullptr);

        if (! lastBlock->name.is())
            lastBlock->name = getNewBlockName();

        br->name = lastBlock->name;
        br->finalize();
        lastBlock->finalize();
    }

    struct ForwardBranchPlaceholder {};
    using ForwardBranchTarget = int;
    ForwardBranchPlaceholder beginForwardBranch (ValueReader, size_t)                   { CMAJ_ASSERT_FALSE; return {}; }
    ForwardBranchTarget createForwardBranchTarget (ForwardBranchPlaceholder, size_t)    { CMAJ_ASSERT_FALSE; return {}; }
    void resolveForwardBranch (ForwardBranchPlaceholder, const std::vector<ForwardBranchTarget>&)    { CMAJ_ASSERT_FALSE; }

    ValueReference createLocalTempVariable (const AST::VariableDeclaration& variable, ValueReader value, bool ensureZeroInitialised)
    {
        addLocalVariableDeclaration (variable, value, ensureZeroInitialised);
        return createVariableReference (variable);
    }

    void addLocalVariableDeclaration (const AST::VariableDeclaration& variable,
                                      ValueReader optionalInitialiser,
                                      bool ensureZeroInitialised)
    {
        if (optionalInitialiser || ensureZeroInitialised)
            addAssignToReference (createVariableReference (variable), optionalInitialiser);
    }

    void addAssignToReference (ValueReference targetRef, ValueReader value)
    {
        auto& targetType = *targetRef.type;

        if (value)
        {
            CMAJ_ASSERT (areTypeRepresentationsEquivalent (targetType, *value.type));

            if (targetRef.isPointer())
            {
                if (value.isPointer)
                    addAssignToPointer (targetRef, value);
                else
                    append (createStore (targetRef, value));
            }
            else
            {
                append (createLocalSet (targetRef.localIndex, getContent (value)));
            }
        }
        else
        {
            if (targetRef.isPointer())
            {
                addMemoryClear (targetRef, getTypeSize (targetType));
            }
            else
            {
                auto& localSet = allocate<wasm::LocalSet>();
                localSet.index = static_cast<wasm::Index> (targetRef.localIndex);
                localSet.value = getExpression (createConst (wasm::Literal::makeZero (getType (targetType)), targetType));
                localSet.makeSet(); // (calls finalize)
                append (localSet);
            }
        }
    }

    void addAssignToPointer (ValueReference destPointer, ValueReader source)
    {
        if (source.isPointer)
        {
            auto size = static_cast<int32_t> (getTypeSize (*destPointer.type));

            if (size > 0)
            {
                auto& copy = allocate<wasm::MemoryCopy>();
                copy.dest = getExpression (destPointer.getPointerAddress());
                copy.source = getExpression (source);
                copy.size = getExpression (createConstantInt32 (size));
                copy.finalize();
                append (copy);
            }
        }
        else
        {
            append (createStore (destPointer, source));
        }
    }

    wasm::Expression* createLocalGet (int32_t index, const wasm::Type type)
    {
        CMAJ_ASSERT (index >= 0);
        auto& g = allocate<wasm::LocalGet>();
        g.index = static_cast<wasm::Index> (index);
        g.type = type;
        g.finalize();
        return std::addressof (g);
    }

    ValueReader createLocalGet (int32_t index, const AST::TypeBase& type)
    {
        return makeReader (*createLocalGet (index, getType (type)), type, false);
    }

    wasm::LocalSet& createLocalSet (int32_t localIndex, wasm::Expression* value)
    {
        CMAJ_ASSERT (localIndex >= 0);
        auto& localSet = allocate<wasm::LocalSet>();
        localSet.index = static_cast<wasm::Index> (localIndex);
        localSet.value = value;
        localSet.makeSet(); // (calls finalize)
        return localSet;
    }

    ValueReader createLoad (const ValueReference& ref)
    {
        return createLoad (getExpressionRef (ref.getPointerAddress()), *ref.type);
    }

    ValueReader createLoad (wasm::Expression& pointer, const AST::TypeBase& type)
    {
        CMAJ_ASSERT (isWASMPrimitive (type) || type.isVectorSize1() || type.isEnum());
        auto& load = allocate<wasm::Load>();
        load.type = getType (type);
        load.bytes = static_cast<uint8_t> (getTypeSize (type));
        load.signed_ = false;
        load.offset = {};
        load.align = 2;
        load.isAtomic = false;
        load.ptr = std::addressof (pointer);
        load.finalize();
        return makeReader (load, type, false);
    }

    wasm::Store& createStore (ValueReference target, ValueReader value)
    {
        auto& type = value.type->isVectorSize1() ? *value.type->getArrayOrVectorElementType() : *value.type;
        CMAJ_ASSERT (isWASMPrimitive (type) && areTypeRepresentationsEquivalent (type, *target.type));

        auto& store = allocate<wasm::Store>();
        store.isAtomic = false;
        store.bytes = static_cast<uint8_t> (getTypeSize (type));
        store.offset = {};
        store.align = 2;
        store.ptr = getExpression (target.getPointerAddress());
        store.value = getContent (value);
        store.valueType = getType (type);
        store.finalize();
        return store;
    }

    wasm::Expression* getContent (ValueReader value)
    {
        if (value.isPointer)
            return getExpression (createLoad (getExpressionRef (value), *value.type));

        return getExpression (value);
    }

    void addReturnValue (ValueReader value)
    {
        auto& ret = allocate<wasm::Return>();

        if (stackFrame.hasPrimitiveReturnType)
        {
            ret.value = getContent (value);
        }
        else
        {
            addAssignToPointer (stackFrame.createReturnValuePointer(), value);
            ret.value = getExpression (stackFrame.createReturnValuePointer().getPointerAddress());
        }

        ret.type = ret.value->type;
        ret.finalize();
        append (ret);
    }

    void addReturnVoid()
    {
        auto& ret = allocate<wasm::Return>();
        ret.value = nullptr;
        ret.finalize();
        append (ret);
    }

    void addExpressionAsStatement (ValueReader expression)
    {
        append (getExpression (expression));
    }

    void addAddValueToInteger (ValueReference target, int32_t delta)
    {
        if (delta != 0)
        {
            CMAJ_ASSERT (target.type->isPrimitiveInt());

            auto load = target.isPointer() ? createLoad (target)
                                           : createLocalGet (target.localIndex, *target.type);

            auto result = target.type->isPrimitiveInt32() ? createAddInt32 (load, delta)
                                                          : createAddInt64 (load, delta);

            if (target.isPointer())
                append (createStore (target, result));
            else
                append (createLocalSet (target.localIndex, getContent (result)));
        }
    }

    template <typename Type>
    wasm::Expression* createAddressExpression (Type address)     { return getExpression (createConstantInt32 (static_cast<int32_t> (address))); }

    ValueReader createConst (wasm::Literal value, const AST::TypeBase& type)
    {
        auto& c = allocate<wasm::Const>();
        c.value = std::move (value);
        c.type = c.value.type;
        c.finalize();
        return makeReader (c, type, false);
    }

    ValueReader createConstantInt32   (int32_t v)   { return createConst (wasm::Literal (v), allocator.int32Type); }
    ValueReader createConstantInt64   (int64_t v)   { return createConst (wasm::Literal (v), allocator.int64Type); }
    ValueReader createConstantFloat32 (float v)     { return createConst (wasm::Literal (v), allocator.float32Type); }
    ValueReader createConstantFloat64 (double v)    { return createConst (wasm::Literal (v), allocator.float64Type); }
    ValueReader createConstantBool    (bool b)      { return createConst (wasm::Literal (static_cast<int32_t> (b ? 1 : 0)), allocator.boolType); }

    ValueReader createConstantString (std::string_view s)
    {
        auto handle = static_cast<int32_t> (moduleInfo.stringDictionary.getHandleForString (s).handle);
        return createConst (wasm::Literal (handle), allocator.stringType);
    }

    AST::ConstantValueBase::SliceToValueFn sliceToValueFn;

    AST::ConstantValueBase::SliceToValueFn* getSliceToValueFn()
    {
        if (sliceToValueFn == nullptr)
        {
            sliceToValueFn = [this] (const choc::value::Value& array) -> choc::value::Value
            {
                if (array.size() == 0)
                    return createFatPointer (0, 0);

                return createFatPointer (globalData.addContentChunk (array, moduleInfo.stringDictionary), array.size());
            };
        }

        return std::addressof (sliceToValueFn);
    }

    ValueReader createConstantAggregate (const AST::ConstantAggregate& agg)
    {
        auto address = globalData.addContentChunk (agg.toValue (getSliceToValueFn()), moduleInfo.stringDictionary);
        return makeReader (*createAddressExpression (address), *agg.getResultType(), true);
    }

    ValueReader createNullConstant (const AST::TypeBase& type)
    {
        CMAJ_ASSERT (! isWASMPrimitive (type));
        auto address = globalData.getNullConstant (moduleInfo.getChocType (type));
        return makeReader (*createAddressExpression (address), type, true);
    }

    void addMemoryClear (ValueReference address, size_t numBytes)
    {
        auto& fill = allocate<wasm::MemoryFill>();
        fill.dest  = getExpression (address.getPointerAddress());
        fill.value = getExpression (createConstantInt32 (0));
        fill.size  = createAddressExpression (numBytes);
        fill.finalize();
        append (fill);
    }

    ValueReader createValueCast (const AST::TypeBase& targetType, const AST::TypeBase& sourceType, ValueReader source)
    {
        if (areTypeRepresentationsEquivalent (targetType, sourceType)
             && ! (sourceType.isPrimitiveInt32() && targetType.isPrimitiveBool()))
        {
            source.type = targetType;
            return source;
        }

        bool isTargetVectorSize1 = targetType.isVectorSize1();
        bool isSourceVectorSize1 = sourceType.isVectorSize1();

        if (isTargetVectorSize1 != isSourceVectorSize1)
        {
            if (isSourceVectorSize1)
            {
                source.type = *sourceType.getArrayOrVectorElementType();
                return createValueCast (targetType, *source.type, source);
            }

            auto element = createValueCast (*targetType.getArrayOrVectorElementType(), sourceType, source);
            element.type = targetType;
            return element;
        }

        if (targetType.isPrimitiveInt64())
        {
            if (sourceType.isPrimitiveInt32())     return createUnary (wasm::UnaryOp::ExtendSInt32,         targetType, source);
            if (sourceType.isPrimitiveFloat32())   return createUnary (wasm::UnaryOp::TruncSFloat32ToInt64, targetType, source);
            if (sourceType.isPrimitiveFloat64())   return createUnary (wasm::UnaryOp::TruncSFloat64ToInt64, targetType, source);
            if (sourceType.isPrimitiveBool())      return createUnary (wasm::UnaryOp::ExtendSInt32,         targetType, source);
        }

        if (targetType.isPrimitiveInt32())
        {
            if (sourceType.isPrimitiveInt64())     return createUnary (wasm::UnaryOp::WrapInt64,            targetType, source);
            if (sourceType.isPrimitiveFloat32())   return createUnary (wasm::UnaryOp::TruncSFloat32ToInt32, targetType, source);
            if (sourceType.isPrimitiveFloat64())   return createUnary (wasm::UnaryOp::TruncSFloat64ToInt32, targetType, source);
        }

        if (targetType.isPrimitiveFloat32())
        {
            if (sourceType.isPrimitiveInt32())     return createUnary (wasm::UnaryOp::ConvertSInt32ToFloat32, targetType, source);
            if (sourceType.isPrimitiveInt64())     return createUnary (wasm::UnaryOp::ConvertSInt64ToFloat32, targetType, source);
            if (sourceType.isPrimitiveFloat64())   return createUnary (wasm::UnaryOp::DemoteFloat64,          targetType, source);
            if (sourceType.isPrimitiveBool())      return createUnary (wasm::UnaryOp::ConvertSInt32ToFloat32, targetType, source);
        }

        if (targetType.isPrimitiveFloat64())
        {
            if (sourceType.isPrimitiveInt32())     return createUnary (wasm::UnaryOp::ConvertSInt32ToFloat64, targetType, source);
            if (sourceType.isPrimitiveInt64())     return createUnary (wasm::UnaryOp::ConvertSInt64ToFloat64, targetType, source);
            if (sourceType.isPrimitiveFloat32())   return createUnary (wasm::UnaryOp::PromoteFloat32,         targetType, source);
            if (sourceType.isPrimitiveBool())      return createUnary (wasm::UnaryOp::ConvertSInt32ToFloat64, targetType, source);
        }

        if (targetType.isPrimitiveBool())
            return createBinaryOp (AST::BinaryOpTypeEnum::Enum::notEquals,
                                   { targetType, sourceType }, source,
                                   codeGenerator->createNullConstant (sourceType));

        CMAJ_ASSERT_FALSE;
        return {};
    }

    ValueReader createSliceFromArray (const AST::TypeBase& elementType, ValueReader sourceArray,
                                      uint32_t offset, uint32_t numElements)
    {
        auto sliceAddress = stackFrame.createLocalValuePointer (stackFrame.allocateLocalStackSpace (getFatPointerType().getValueDataSize()),
                                                                allocator.int32Type);

        CMAJ_ASSERT (sourceArray.isPointer && sliceAddress.isPointer());
        auto elementSize = getTypeSize (elementType);

        auto pointer = makeReader (getExpressionRef (sourceArray), allocator.int32Type, false);
        addAssignToReference (sliceAddress, createAddInt32 (pointer, static_cast<int32_t> (offset * elementSize)));
        addAssignToReference (createPointerAdd (sliceAddress, 4, allocator.int32Type), createConstantInt32 (static_cast<int32_t> (numElements)));

        CMAJ_ASSERT (sliceAddress.memoryOffset);
        return makeReader (getExpressionRef (sliceAddress.memoryOffset), AST::createSliceOfType (elementType.context, elementType), true);
    }

    ValueReference createStateUpcast (const AST::StructType& parentType, const AST::StructType& childType, ValueReference value)
    {
        auto path = AST::getPathToChildOfStruct (parentType, childType);
        CMAJ_ASSERT (path.size() == 1);
        auto index = static_cast<uint32_t> (path.front());

        ValueReader dynamicOffsetTotal;

        auto& indexedType = AST::castToTypeBaseRef (parentType.memberTypes[index]).skipConstAndRefModifiers();
        auto memberStartOffset = moduleInfo.getChocType (parentType).getElementTypeAndOffset (index).offset;

        if (indexedType.isArray())
        {
            auto& elementType = indexedType.getArrayOrVectorElementType()->skipConstAndRefModifiers();
            auto instanceIndex = createStructMemberReader (childType, createReaderForReference (value),
                                                           "_instanceIndex",
                                                           childType.indexOfMember ("_instanceIndex"));

            dynamicOffsetTotal = createMulInt32 (instanceIndex, -static_cast<int32_t> (getTypeSize (elementType)));
        }

        auto totalOffset = createConstantInt32 (-static_cast<int32_t> (memberStartOffset));

        if (dynamicOffsetTotal)
            totalOffset = createAddInt32 (dynamicOffsetTotal, totalOffset);

        return createPointerAdd (value, totalOffset, parentType);
    }

    wasm::Call& createCallExpression (wasm::Function& targetFn)
    {
        auto& call = allocate<wasm::Call>();
        call.type = wasm::Type (targetFn.getResults());
        call.target = targetFn.name;
        call.isReturn = false;

        return call;
    }

    template <typename ArgList>
    ValueReader createIntrinsicCall (AST::Intrinsic::Type intrinsic, ArgList& argValues, const AST::TypeBase&)
    {
        auto getArg = [&] (size_t index)
        {
            auto& arg = argValues[index];

            if (arg.valueReference)
                return arg.valueReference.getPointerAddress();

            return arg.valueReader;
        };

        auto arg0 = getArg (0);
        auto& argType = argValues.front().paramType;

        if (argType.isVector())
            return {};

        auto isFloat32 = [&]
        {
            if (argType.isPrimitiveFloat32())
                return true;

            CMAJ_ASSERT (argType.isPrimitiveFloat64());
            return false;
        };

        switch (intrinsic)
        {
            case AST::Intrinsic::Type::abs:           return createInstrinsic_abs   (argType, arg0);
            case AST::Intrinsic::Type::min:           return createInstrinsic_min   (argType, arg0, getArg (1));
            case AST::Intrinsic::Type::max:           return createInstrinsic_max   (argType, arg0, getArg (1));
            case AST::Intrinsic::Type::floor:         return createInstrinsic_floor (isFloat32(), arg0);
            case AST::Intrinsic::Type::ceil:          return createInstrinsic_ceil  (isFloat32(), arg0);
            case AST::Intrinsic::Type::rint:          return createInstrinsic_rint  (isFloat32(), arg0);
            case AST::Intrinsic::Type::sqrt:          return createInstrinsic_sqrt  (isFloat32(), arg0);
            case AST::Intrinsic::Type::reinterpretFloatToInt:  return createIntrinsic_reinterpretFloatToInt (argType, arg0);
            case AST::Intrinsic::Type::reinterpretIntToFloat:  return createIntrinsic_reinterpretIntToFloat (argType, arg0);

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
            case AST::Intrinsic::Type::select:
            case AST::Intrinsic::Type::isnan:
            case AST::Intrinsic::Type::isinf:
                return {}; // fall back to the library implementations for these

            case AST::Intrinsic::Type::log:
            case AST::Intrinsic::Type::log10:
            case AST::Intrinsic::Type::pow:
            case AST::Intrinsic::Type::sin:
            case AST::Intrinsic::Type::cos:
            case AST::Intrinsic::Type::tan:
            case AST::Intrinsic::Type::fmod:
            case AST::Intrinsic::Type::exp:  // these should have all been replaced before we get here

            case AST::Intrinsic::Type::unknown:
            default:                                  CMAJ_ASSERT_FALSE; return {};
        }
    }

    template <typename ArgList>
    ValueReader createFunctionCall (const AST::Function& fn, std::string_view functionName, ArgList& argValues)
    {
        auto& targetFunction = getOrCreateFunction (fn, functionName);
        auto& returnType = AST::castToTypeBaseRef (fn.returnType);

        auto& call = createCallExpression (targetFunction);

        bool returnsPointer = ! isWASMPrimitive (returnType);

        if (returnsPointer)
        {
            auto returnValuePointer = stackFrame.allocateLocalStackSpace (returnType);
            call.operands.push_back (getExpression (returnValuePointer.getPointerAddress()));
        }

        for (auto& arg : argValues)
        {
            if (arg.valueReference)
            {
                call.operands.push_back (getExpression (arg.valueReference.getPointerAddress()));
            }
            else if (isWASMPrimitive (*arg.valueReader.type))
            {
                call.operands.push_back (getContent (arg.valueReader));
            }
            else
            {
                auto argCopySpace = stackFrame.allocateLocalStackSpace (*arg.valueReader.type);
                call.operands.push_back (getExpression (argCopySpace.getPointerAddress()));
                addAssignToReference (argCopySpace, arg.valueReader);
            }
        }

        call.finalize();
        return makeReader (call, returnType, returnsPointer);
    }

    ValueReader createUnary (wasm::UnaryOp op, const AST::TypeBase& resultType, ValueReader input)
    {
        auto& u = allocate<wasm::Unary>();
        u.op = op;
        u.value = getContent (input);
        u.finalize();
        return makeReader (u, resultType, false);
    }

    ValueReader createBinary (wasm::BinaryOp op, const AST::TypeBase& type, ValueReader lhs, ValueReader rhs)
    {
        auto& b = allocate<wasm::Binary>();
        b.op    = op;
        b.left  = getContent (lhs);
        b.right = getContent (rhs);
        b.finalize();
        return makeReader (b, type, false);
    }

    ValueReader createAddInt32 (ValueReader lhs, int32_t rhs)       { return rhs == 0 ? lhs : createAddInt32 (lhs, createConstantInt32 (rhs)); }
    ValueReader createAddInt64 (ValueReader lhs, int64_t rhs)       { return rhs == 0 ? lhs : createAddInt64 (lhs, createConstantInt64 (rhs)); }
    ValueReader createAddInt32 (ValueReader lhs, ValueReader rhs)   { return rhs.isZero() ? lhs : createBinary (wasm::BinaryOp::AddInt32, allocator.int32Type, lhs, rhs); }
    ValueReader createAddInt64 (ValueReader lhs, ValueReader rhs)   { return rhs.isZero() ? lhs : createBinary (wasm::BinaryOp::AddInt64, allocator.int64Type, lhs, rhs); }

    ValueReader createMulInt32 (ValueReader lhs, int32_t rhs)
    {
        if (lhs.isZero() || rhs == 0) return createConstantInt32 (0);
        if (lhs.isConstIntValue (1)) return createConstantInt32 (rhs);
        if (rhs == 1) return lhs;

        return createBinary (wasm::BinaryOp::MulInt32, allocator.int32Type, lhs, createConstantInt32 (rhs));
    }

    ValueReader createPointerAdd (ValueReader pointer, ValueReader offset, const AST::TypeBase& elementType)
    {
        CMAJ_ASSERT (pointer && pointer.isPointer);

        auto& b = allocate<wasm::Binary>();
        b.op    = wasm::BinaryOp::AddInt32;
        b.left  = getExpression (pointer);
        b.right = getContent (offset);
        b.finalize();
        return makeReader (b, elementType, true);
    }

    ValueReader createPointerAdd (ValueReader pointer, int32_t constOffset, const AST::TypeBase& elementType)
    {
        if (constOffset != 0)
            return createPointerAdd (pointer, createConstantInt32 (constOffset), elementType);

        return reinterpretPointer (pointer, elementType);
    }

    ValueReference createPointerAdd (ValueReference ref, ValueReader offset, const AST::TypeBase& elementType)
    {
        return makeReference (createPointerAdd (ref.getPointerAddress(), offset, elementType), elementType);
    }

    ValueReference createPointerAdd (ValueReference ref, int32_t constOffset, const AST::TypeBase& elementType)
    {
        if (constOffset != 0)
            ref.memoryOffset = createPointerAdd (ref.getPointerAddress(), createConstantInt32 (constOffset), elementType);

        return reinterpretPointer (ref, elementType);
    }

    ValueReader createUnaryOp (AST::UnaryOpTypeEnum::Enum opType, const AST::TypeBase& sourceType, ValueReader input)
    {
        if (opType == AST::UnaryOpTypeEnum::Enum::negate)
        {
            if (sourceType.isPrimitiveFloat32()) return createUnary (wasm::UnaryOp::NegFloat32, sourceType, input);
            if (sourceType.isPrimitiveFloat64()) return createUnary (wasm::UnaryOp::NegFloat64, sourceType, input);

            if (sourceType.isPrimitiveInt32())  return createBinary (wasm::BinaryOp::SubInt32, sourceType, createConstantInt32 (0), input);
            if (sourceType.isPrimitiveInt64())  return createBinary (wasm::BinaryOp::SubInt64, sourceType, createConstantInt64 (0), input);
        }
        else if (opType == AST::UnaryOpTypeEnum::Enum::bitwiseNot)
        {
            if (sourceType.isPrimitiveInt32())  return createBinary (wasm::BinaryOp::XorInt32, sourceType, createConstantInt32 (-1), input);
            if (sourceType.isPrimitiveInt64())  return createBinary (wasm::BinaryOp::XorInt64, sourceType, createConstantInt64 (-1LL), input);
        }
        else if (opType == AST::UnaryOpTypeEnum::Enum::logicalNot)
        {
            if (sourceType.isPrimitiveBool())  return createUnary (wasm::UnaryOp::EqZInt32, sourceType, input);
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    static wasm::BinaryOp getWASMBinaryOp (AST::BinaryOpTypeEnum::Enum opType, const AST::TypeRules::BinaryOperatorTypes& opTypes)
    {
        if (opTypes.operandType.isPrimitiveInt())
        {
            bool is32 = opTypes.operandType.isPrimitiveInt32();

            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::add:                  return is32 ? wasm::BinaryOp::AddInt32  : wasm::BinaryOp::AddInt64;
                case AST::BinaryOpTypeEnum::Enum::subtract:             return is32 ? wasm::BinaryOp::SubInt32  : wasm::BinaryOp::SubInt64;
                case AST::BinaryOpTypeEnum::Enum::multiply:             return is32 ? wasm::BinaryOp::MulInt32  : wasm::BinaryOp::MulInt64;
                case AST::BinaryOpTypeEnum::Enum::divide:               return is32 ? wasm::BinaryOp::DivSInt32 : wasm::BinaryOp::DivSInt64;
                case AST::BinaryOpTypeEnum::Enum::modulo:               return is32 ? wasm::BinaryOp::RemSInt32 : wasm::BinaryOp::RemSInt64;
                case AST::BinaryOpTypeEnum::Enum::bitwiseOr:            return is32 ? wasm::BinaryOp::OrInt32   : wasm::BinaryOp::OrInt64;
                case AST::BinaryOpTypeEnum::Enum::bitwiseAnd:           return is32 ? wasm::BinaryOp::AndInt32  : wasm::BinaryOp::AndInt64;
                case AST::BinaryOpTypeEnum::Enum::bitwiseXor:           return is32 ? wasm::BinaryOp::XorInt32  : wasm::BinaryOp::XorInt64;
                case AST::BinaryOpTypeEnum::Enum::logicalOr:            return is32 ? wasm::BinaryOp::OrInt32   : wasm::BinaryOp::OrInt64;
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:           return is32 ? wasm::BinaryOp::AndInt32  : wasm::BinaryOp::AndInt64;
                case AST::BinaryOpTypeEnum::Enum::equals:               return is32 ? wasm::BinaryOp::EqInt32   : wasm::BinaryOp::EqInt64;
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return is32 ? wasm::BinaryOp::NeInt32   : wasm::BinaryOp::NeInt64;
                case AST::BinaryOpTypeEnum::Enum::lessThan:             return is32 ? wasm::BinaryOp::LtSInt32  : wasm::BinaryOp::LtSInt64;
                case AST::BinaryOpTypeEnum::Enum::lessThanOrEqual:      return is32 ? wasm::BinaryOp::LeSInt32  : wasm::BinaryOp::LeSInt64;
                case AST::BinaryOpTypeEnum::Enum::greaterThan:          return is32 ? wasm::BinaryOp::GtSInt32  : wasm::BinaryOp::GtSInt64;
                case AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual:   return is32 ? wasm::BinaryOp::GeSInt32  : wasm::BinaryOp::GeSInt64;
                case AST::BinaryOpTypeEnum::Enum::leftShift:            return is32 ? wasm::BinaryOp::ShlInt32  : wasm::BinaryOp::ShlInt64;
                case AST::BinaryOpTypeEnum::Enum::rightShift:           return is32 ? wasm::BinaryOp::ShrSInt32 : wasm::BinaryOp::ShrSInt64;
                case AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned:   return is32 ? wasm::BinaryOp::ShrUInt32 : wasm::BinaryOp::ShrUInt64;

                case AST::BinaryOpTypeEnum::Enum::exponent:
                default:                                                break;
            }
        }
        else if (opTypes.operandType.isPrimitiveFloat())
        {
            bool is32 = opTypes.operandType.isPrimitiveFloat32();

            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::add:                  return is32 ? wasm::BinaryOp::AddFloat32 : wasm::BinaryOp::AddFloat64;
                case AST::BinaryOpTypeEnum::Enum::subtract:             return is32 ? wasm::BinaryOp::SubFloat32 : wasm::BinaryOp::SubFloat64;
                case AST::BinaryOpTypeEnum::Enum::multiply:             return is32 ? wasm::BinaryOp::MulFloat32 : wasm::BinaryOp::MulFloat64;
                case AST::BinaryOpTypeEnum::Enum::divide:               return is32 ? wasm::BinaryOp::DivFloat32 : wasm::BinaryOp::DivFloat64;
                case AST::BinaryOpTypeEnum::Enum::equals:               return is32 ? wasm::BinaryOp::EqFloat32  : wasm::BinaryOp::EqFloat64;
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return is32 ? wasm::BinaryOp::NeFloat32  : wasm::BinaryOp::NeFloat64;
                case AST::BinaryOpTypeEnum::Enum::lessThan:             return is32 ? wasm::BinaryOp::LtFloat32  : wasm::BinaryOp::LtFloat64;
                case AST::BinaryOpTypeEnum::Enum::lessThanOrEqual:      return is32 ? wasm::BinaryOp::LeFloat32  : wasm::BinaryOp::LeFloat64;
                case AST::BinaryOpTypeEnum::Enum::greaterThan:          return is32 ? wasm::BinaryOp::GtFloat32  : wasm::BinaryOp::GtFloat64;
                case AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual:   return is32 ? wasm::BinaryOp::GeFloat32  : wasm::BinaryOp::GeFloat64;

                case AST::BinaryOpTypeEnum::Enum::modulo:
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
        else if (opTypes.operandType.isPrimitiveBool())
        {
            switch (opType)
            {
                case AST::BinaryOpTypeEnum::Enum::logicalOr:            return wasm::BinaryOp::OrInt32;
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:           return wasm::BinaryOp::AndInt32;
                case AST::BinaryOpTypeEnum::Enum::equals:               return wasm::BinaryOp::EqInt32;
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return wasm::BinaryOp::NeInt32;

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
                case AST::BinaryOpTypeEnum::Enum::equals:               return wasm::BinaryOp::EqInt32;
                case AST::BinaryOpTypeEnum::Enum::notEquals:            return wasm::BinaryOp::NeInt32;

                case AST::BinaryOpTypeEnum::Enum::logicalOr:
                case AST::BinaryOpTypeEnum::Enum::logicalAnd:
                case AST::BinaryOpTypeEnum::Enum::add:
                case AST::BinaryOpTypeEnum::Enum::subtract:
                case AST::BinaryOpTypeEnum::Enum::multiply:
                case AST::BinaryOpTypeEnum::Enum::exponent:
                case AST::BinaryOpTypeEnum::Enum::divide:
                case AST::BinaryOpTypeEnum::Enum::modulo:
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
        return wasm::BinaryOp::InvalidBinary;
    }

    static bool canPerformVectorUnaryOp()   { return false; }
    static bool canPerformVectorBinaryOp()  { return false; }

    ValueReader createBinaryOp (AST::BinaryOpTypeEnum::Enum opType,
                                AST::TypeRules::BinaryOperatorTypes opTypes,
                                ValueReader lhs, ValueReader rhs)
    {
        return createBinary (getWASMBinaryOp (opType, opTypes), opTypes.resultType, lhs, rhs);
    }

    ValueReader createTernaryOp (ValueReader condition, ValueReader trueValue, ValueReader falseValue)
    {
        auto& i = allocate<wasm::If>();
        i.condition = getContent (condition);
        bool resultIsPointer = false;

        if (trueValue.isPointer && falseValue.isPointer)
        {
            i.ifTrue  = getExpression (trueValue);
            i.ifFalse = getExpression (falseValue);
            resultIsPointer = true;
        }
        else
        {
            i.ifTrue  = getContent (trueValue);
            i.ifFalse = getContent (falseValue);
        }

        i.finalize();
        return makeReader (i, *trueValue.type, resultIsPointer);
    }

    ValueReader createVariableReader (const AST::VariableDeclaration& v)
    {
        return createReaderForReference (createVariableReference (v));
    }

    ValueReference createVariableReference (const AST::VariableDeclaration& v)
    {
        if (v.isLocal() || v.isParameter())
            return stackFrame.getVariableReference (v);

        auto i = globalVariables.find (std::addressof (v));
        CMAJ_ASSERT (i != globalVariables.end());
        return i->second;
    }

    ValueReader createElementReader (ValueReader parent, ValueReader index)
    {
        auto& elementType = *parent.type->getArrayOrVectorElementType();
        auto offset = createMulInt32 (index, static_cast<int32_t> (getTypeSize (elementType)));

        if (parent.type->isSlice())
        {
            CMAJ_ASSERT (parent.isPointer);
            auto arrayStart = makeReference (createLoad (getExpressionRef (parent), allocator.int32Type), elementType);
            return createReaderForReference (createPointerAdd (arrayStart, offset, elementType));
        }

        return createPointerAdd (parent, offset, elementType);
    }

    ValueReference createElementReference (ValueReference parent, size_t index)
    {
        if (parent.type->isSlice())
            return createElementReference (parent, createConstantInt32 (static_cast<int32_t> (index)));

        auto& elementType = *parent.type->getArrayOrVectorElementType();
        auto elementSize = static_cast<int32_t> (getTypeSize (elementType));

        return createPointerAdd (parent, static_cast<int32_t> (index) * elementSize, elementType);
    }

    ValueReference createElementReference (ValueReference parent, ValueReader index)
    {
        CMAJ_ASSERT (! parent.type->isSlice());

        auto& elementType = *parent.type->getArrayOrVectorElementType();
        auto offset = createMulInt32 (index, static_cast<int32_t> (getTypeSize (elementType)));

        return createPointerAdd (parent, offset, elementType);
    }

    ValueReader createStructMemberReader (const AST::StructType& type, ValueReader object, std::string_view, int64_t memberIndex)
    {
        auto index = static_cast<size_t> (memberIndex);
        auto& elementType = type.getMemberType (index);
        auto offset = moduleInfo.getChocType (type).getElementTypeAndOffset (static_cast<uint32_t> (index)).offset;
        return createPointerAdd (object, static_cast<int32_t> (offset), elementType);
    }

    ValueReference createStructMemberReference (const AST::StructType& type, ValueReference object, std::string_view, int64_t memberIndex)
    {
        auto index = static_cast<size_t> (memberIndex);
        auto& elementType = type.getMemberType (index);
        auto offset = moduleInfo.getChocType (type).getElementTypeAndOffset (static_cast<uint32_t> (index)).offset;
        return createPointerAdd (object, static_cast<int32_t> (offset), elementType);
    }

    ValueReader createGetSliceSize (ValueReader slice)
    {
        CMAJ_ASSERT (slice.isPointer && slice.type->isSlice());
        auto sizeAddress = createPointerAdd (slice, 4, allocator.int32Type);
        return createLoad (getExpressionRef (sizeAddress), allocator.int32Type);
    }

    //==============================================================================
    ValueReader createInstrinsic_sqrt  (bool is32, ValueReader arg0)                   { return createUnary (is32 ? wasm::UnaryOp::SqrtFloat32    : wasm::UnaryOp::SqrtFloat64,    *arg0.type, arg0); }
    ValueReader createInstrinsic_floor (bool is32, ValueReader arg0)                   { return createUnary (is32 ? wasm::UnaryOp::FloorFloat32   : wasm::UnaryOp::FloorFloat64,   *arg0.type, arg0); }
    ValueReader createInstrinsic_ceil  (bool is32, ValueReader arg0)                   { return createUnary (is32 ? wasm::UnaryOp::CeilFloat32    : wasm::UnaryOp::CeilFloat64,    *arg0.type, arg0); }
    ValueReader createInstrinsic_rint  (bool is32, ValueReader arg0)                   { return createUnary (is32 ? wasm::UnaryOp::NearestFloat32 : wasm::UnaryOp::NearestFloat64, *arg0.type, arg0); }

    ValueReader createInstrinsic_abs (const AST::TypeBase& argType, ValueReader arg0)
    {
        if (argType.isPrimitiveFloat32())  return createUnary (wasm::UnaryOp::AbsFloat32, argType, arg0);
        if (argType.isPrimitiveFloat64())  return createUnary (wasm::UnaryOp::AbsFloat64, argType, arg0);

        return {}; // fall back to library implementation
    }

    ValueReader createInstrinsic_min (const AST::TypeBase& argType, ValueReader arg0, ValueReader arg1)
    {
        if (argType.isPrimitiveFloat32())  return createBinary (wasm::BinaryOp::MinFloat32, argType, arg0, arg1);
        if (argType.isPrimitiveFloat64())  return createBinary (wasm::BinaryOp::MinFloat64, argType, arg0, arg1);

        return {}; // fall back to library implementation
    }

    ValueReader createInstrinsic_max (const AST::TypeBase& argType, ValueReader arg0, ValueReader arg1)
    {
        if (argType.isPrimitiveFloat32())  return createBinary (wasm::BinaryOp::MaxFloat32, argType, arg0, arg1);
        if (argType.isPrimitiveFloat64())  return createBinary (wasm::BinaryOp::MaxFloat64, argType, arg0, arg1);

        return {}; // fall back to library implementation
    }

    ValueReader createIntrinsic_reinterpretFloatToInt (const AST::TypeBase& argType, ValueReader value)
    {
        if (argType.isPrimitiveFloat64())
            return createUnary (wasm::UnaryOp::ReinterpretFloat64, allocator.int64Type, value);

        CMAJ_ASSERT (argType.isPrimitiveFloat32());
        return createUnary (wasm::UnaryOp::ReinterpretFloat32, allocator.int32Type, value);
    }

    ValueReader createIntrinsic_reinterpretIntToFloat (const AST::TypeBase& argType, ValueReader value)
    {
        if (argType.isPrimitiveInt64())
            return createUnary (wasm::UnaryOp::ReinterpretInt64, allocator.float64Type, value);

        CMAJ_ASSERT (argType.isPrimitiveInt32());
        return createUnary (wasm::UnaryOp::ReinterpretInt32, allocator.float32Type, value);
    }

    static std::string makeSafeIdentifier (std::string_view name)
    {
        return makeSafeIdentifierName (std::string (name));
    }

    static bool isWASMPrimitive (const AST::TypeBase& type)
    {
        if (auto p = type.skipConstAndRefModifiers().getAsPrimitiveType())
            return p->isVoid() || p->isPrimitiveBool() || p->isPrimitiveInt()
                    || p->isPrimitiveFloat() || p->isPrimitiveString();

        return type.isEnum();
    }
};

//==============================================================================
WebAssemblyModule generateWebAssembly (const ProgramInterface& p, const BuildSettings& buildSettings)
{
    WebAssemblyGenerator gen (AST::getProgram (p));

    if (gen.generate (buildSettings))
        return gen.takeModuleInfo();

    return {};
}

std::string generateWAST (const ProgramInterface& p, const BuildSettings& buildSettings)
{
    WebAssemblyGenerator gen (AST::getProgram (p));

    if (gen.generate (buildSettings))
    {
        std::ostringstream out (std::ios::binary);
        out << *gen.currentModule;
        return out.str();
    }

    return {};
}

} // namespace cmaj::webassembly::binaryen

#endif // CMAJ_ENABLE_CODEGEN_BINARYEN
