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


static constexpr size_t   maxIdentifierLength      = 256;
static constexpr size_t   maxInitialiserListLength = 1024 * 64;
static constexpr size_t   maxEndpointArraySize     = 256;
static constexpr size_t   maxProcessorArraySize    = 256;
static constexpr size_t   maxDelayLineLength       = 1024 * 256;
static constexpr int32_t  maxInternalLatency       = 20 * 48000;
static constexpr uint64_t maxArraySize             = std::numeric_limits<int32_t>::max();
static constexpr uint64_t maxVectorSize            = 256;

using ArraySize = uint32_t;

static constexpr std::string_view getSpecialisedFunctionSuffix()     { return "_specialised"; }
static constexpr std::string_view getBinaryProgramHeader()           { return "Cmaj0001"; }

static bool isSpecialFunctionName (const Strings& sp, PooledString name)
{
    return name == sp.mainFunctionName
        || name == sp.userInitFunctionName
        || name == sp.systemInitFunctionName
        || name == sp.consoleEndpointName;
}

static std::string getEventHandlerFunctionName (const EndpointDeclaration& endpoint, const AST::TypeBase& eventType, const std::string& prefix)
{
    auto name = prefix + endpoint.getEndpointID().toString();

    size_t index = 0;
    auto dataTypes = endpoint.getDataTypes();

    for (auto& type : dataTypes)
    {
        if (eventType.isSameType (type, TypeBase::ComparisonFlags::ignoreReferences
                                            | TypeBase::ComparisonFlags::ignoreConst))
        {
            if (dataTypes.size() > 1)
                name += "_" + std::to_string (index + 1);

            return name;
        }

        ++index;
    }

    CMAJ_ASSERT_FALSE;
    return name;
}

static std::string getEventHandlerFunctionName (const AST::Function& f, const std::string& prefix = "_sendEvent_")
{
    auto& endpoint = *f.getParentProcessor().findEndpointWithName (f.getName());

    auto& paramType = (f.getNumParameters() > 1) ? f.getParameterTypes()[1].get()
                                                    : f.context.allocator.createVoidType();

    return getEventHandlerFunctionName (endpoint, paramType, prefix);
}

static std::string getSetValueFunctionName (const EndpointDeclaration& inputEndpoint)
{
    return "_setValue_" + std::string (inputEndpoint.getName());
}

//==============================================================================
template <typename ObjectOrContext>
[[noreturn]] static void throwError (const ObjectOrContext& errorContext, DiagnosticMessage message, bool isStaticAssertion = false)
{
    ObjectRefVector<const Expression> genericFunctionCallChain;

    ref<const ObjectContext> context = getContext (errorContext);

    for (auto p = context->parentScope; p != nullptr && genericFunctionCallChain.size() < 10; p = p->getParentScope())
        if (auto f = p->getAsFunction())
            if (auto originalCall = castToSkippingReferences<Expression> (f->originalCallLeadingToSpecialisation))
                genericFunctionCallChain.push_back (*originalCall);

    DiagnosticMessageList messages;

    if (isStaticAssertion && context->isInSystemModule())
    {
        for (auto& genericCall : genericFunctionCallChain)
            if (context->isInSystemModule())
                context = getContext (genericCall);
    }
    else
    {
        for (auto& genericCall : genericFunctionCallChain)
        {
            auto callDescription = AST::printFunctionCallDescription (genericCall, AST::PrintOptionFlags::useShortNames);
            messages.prepend (Errors::cannotResolveGenericFunction (callDescription).withContext (genericCall->context));
        }
    }

    messages.add (context, message);
    cmaj::throwError (messages);
}

//==============================================================================
enum PrintOptionFlags
{
    useShortNames             = 0,
    useFullyQualifiedNames    = 1,
    skipAliases               = 2,
    defaultStyle              = useFullyQualifiedNames
};

static std::string print (const Program&);
static std::string print (const Program&, PrintOptionFlags);
static std::string print (const Object&);
static std::string print (const Object&, PrintOptionFlags);

static std::string printFunctionCallDescription (const Object&, PrintOptionFlags flags = PrintOptionFlags::defaultStyle);
static std::string printFunctionDeclaration (const Function&, PrintOptionFlags flags = PrintOptionFlags::defaultStyle);

//==============================================================================
template <int granularity, typename SizeType>
static constexpr SizeType getAlignedSize (SizeType size)
{
    static_assert (choc::math::isPowerOf2 (granularity), "granularity must be a power of 2");
    return (size + (SizeType) (granularity - 1)) & ~(SizeType) (granularity - 1);
}

template <typename IntegerType>
static IntegerType wrap (IntegerType value, IntegerType limit)
{
    CMAJ_ASSERT (limit > 0);
    value %= limit;
    return value < 0 ? (value + limit) : value;
}

template <typename IntegerType>
static IntegerType clamp (IntegerType value, IntegerType limit)
{
    CMAJ_ASSERT (limit > 0);
    return value < IntegerType() ? IntegerType() : (value >= limit ? limit -1 : value);
}

//==============================================================================
static bool isSafeIdentifierName (const std::string& s)
{
    return s == makeSafeIdentifierName (s);
}

static std::string makeIdentifierRemovingColons (std::string_view s)
{
    while (choc::text::startsWith (s, ":"))
        s = s.substr (1);

    return makeSafeIdentifierName (choc::text::replace (s, "::", "_"));
}

//==============================================================================
/// Represents the range of integers which can be represented by a type or value.
/// The end value is non-inclusive, so a value of { 0, 0 } is invalid.
struct IntegerRange
{
    int64_t start = 0, end = 0;

    bool isValid() const                               { return end > start; }
    int64_t size() const                               { return end - start; }
    bool contains (int64_t value) const                { return value >= start && value < end; }
    bool contains (IntegerRange other) const           { return other.isValid() && other.start >= start && other.end <= end; }
    bool canBeBelow (IntegerRange other) const         { CMAJ_ASSERT (isValid() && other.isValid()); return start < other.end - 1; }
    bool canBeEqual (IntegerRange other) const         { CMAJ_ASSERT (isValid() && other.isValid()); return end > other.start && start < other.end; }

    template <typename IntegerType>
    static constexpr IntegerRange forType()   { return { std::numeric_limits<IntegerType>::min(),
                                                         std::numeric_limits<IntegerType>::max() }; }

    static IntegerRange forObject (const Expression& e)
    {
        if (auto v = e.getAsValueBase())          return v->getKnownIntegerRange();
        if (auto t = e.getAsTypeBase())           return t->getAddressableIntegerRange();

        return {};
    }
};

//==============================================================================
template <typename ObjectType, typename ParentType>
struct UniqueNameList
{
    UniqueNameList (std::string prefixToUse = {}) : prefix (std::move (prefixToUse)) {}
    UniqueNameList (const UniqueNameList&) = delete;

    std::string getName (const ObjectType& o)
    {
        auto& name = names[std::addressof (o)];

        if (name.empty())
        {
            auto root = static_cast<ParentType&> (*this).getRootName (o);

            if (root.empty())
                root = "_";

            if (! prefix.empty())
                root = std::string (prefix) + root;

            auto exists = [this] (const std::string& nameToCheck) -> bool
            {
                for (auto& n : names)
                    if (n.second == nameToCheck)
                        return true;

                return false;
            };

            auto uniqueName = root;
            auto& suffix = suffixes[root];

            if (suffix != 0)
                uniqueName = root + "_" + std::to_string (suffix++);

            while (exists (uniqueName))
                uniqueName = root + "_" + std::to_string (suffix++);

            name = uniqueName;
        }

        return name;
    }

    void clear()
    {
        names.clear();
        suffixes.clear();
    }

    std::unordered_map<const ObjectType*, std::string> names;
    std::unordered_map<std::string, uint32_t> suffixes;
    std::string prefix;
};

//==============================================================================
template <typename NameExistsFn>
static std::string findUniqueName (std::string root, NameExistsFn&& nameExists)
{
    auto name = root;

    for (size_t suffix = 1;;)
    {
        if (! nameExists (name))
            return name;

        name = root + "_" + std::to_string (++suffix);
    }
}

static std::string createUniqueName (std::string root, const ListProperty& existingItems)
{
    return findUniqueName (root,
                           [&] (std::string_view nameToTest)
                           {
                               auto name = existingItems.getStringPool().get (nameToTest);

                               for (auto& item : existingItems)
                                   if (item->hasName (name))
                                       return true;

                               return false;
                           });
}

static std::string createUniqueName (std::string root, choc::span<AST::PooledString> names)
{
    return findUniqueName (root,
                           [&] (std::string_view nameToTest)
                           {
                               return std::find (names.begin(), names.end(), nameToTest) != names.end();
                           });
}

template<class ModuleType>
static std::string createUniqueModuleName (const ModuleType& original)
{
    size_t cloneIndex = 1;

    for (auto& module : original.getParentNamespace().getSubModules())
        if (module->originalName.get() == original.getName())
            ++cloneIndex;

    return "_" + std::string (original.getName()) + "_" + std::to_string (cloneIndex);
}

//==============================================================================
struct SignatureBuilder
{
    SignatureBuilder& operator<< (std::string_view s)
    {
        if (firstItem)
            firstItem = false;
        else
            sig << '_';

        sig << s;
        return *this;
    }

    SignatureBuilder& operator<< (size_t n)
    {
        return operator<< (std::to_string (n));
    }

    SignatureBuilder& operator<< (const Object& o)
    {
        if (auto v = o.getAsValueBase())
        {
            if (auto c = v->constantFold())
            {
                c->writeSignature (*this);
                return *this;
            }
        }

        o.writeSignature (*this);
        return *this;
    }

    SignatureBuilder& operator<< (const Property& p)
    {
        p.writeSignature (*this);
        return *this;
    }

    std::string toString (size_t maxLength) const
    {
        auto s = sig.str();
        auto stripped = removeDuplicateUnderscores (makeSafeIdentifierName (s.substr (0, maxLength)));

        if (stripped == s)
            return s;

        return stripped + "_" + makeHashString (getXXHash());
    }

    uint32_t getXXHash() const
    {
        choc::hash::xxHash32 hash;
        hash.addInput (sig.str());
        return hash.getHash();
    }

    static std::string makeHashString (uint32_t n)
    {
        constexpr const char encoding[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        constexpr size_t base = sizeof (encoding) - 1;

        char text[32];
        char* t = text;

        while (n > 0)
        {
            *t++ = encoding[n % base];
            n /= base;
        }

        *t = 0;
        return text;
    }

    static std::string removeDuplicateUnderscores (std::string_view s)
    {
        std::string result;
        result.reserve (s.length());
        char lastChar = 0;

        for (auto c : s)
        {
            if (c == '_' && lastChar == c)
                continue;

            result += c;
            lastChar = c;
        }

        return result;
    }

    std::ostringstream sig  { std::ios::binary };
    bool firstItem = true;
};

//==============================================================================
template<class ModuleType>
static ModuleType& createClonedSiblingModule (ModuleType& original, std::string newName)
{
    auto& copy = original.context.allocator.createDeepClone (original);

    if (! newName.empty())
    {
        copy.name = copy.getStringPool().get (AST::createUniqueName (std::move (newName), original.getParentNamespace().subModules));
        copy.originalName = original.getOriginalName();
    }

    original.getParentNamespace().subModules.addReference (copy);
    return copy;
}

template<class ModuleType>
static ModuleType& createClonedSiblingModule (ModuleType& original, bool keepOriginalName)
{
    return createClonedSiblingModule (original, keepOriginalName ? std::string()
                                                                 : createUniqueModuleName (original));
}

//==============================================================================
static bool isLogicalOperator (BinaryOpTypeEnum::Enum op) noexcept
{
    return op == BinaryOpTypeEnum::Enum::logicalAnd || op == BinaryOpTypeEnum::Enum::logicalOr;
}

static bool isEqualityOperator (BinaryOpTypeEnum::Enum op) noexcept
{
    return op == BinaryOpTypeEnum::Enum::equals || op == BinaryOpTypeEnum::Enum::notEquals;
}

static bool isComparisonOperator (BinaryOpTypeEnum::Enum op) noexcept
{
    return op == BinaryOpTypeEnum::Enum::lessThan    || op == BinaryOpTypeEnum::Enum::lessThanOrEqual
        || op == BinaryOpTypeEnum::Enum::greaterThan || op == BinaryOpTypeEnum::Enum::greaterThanOrEqual;
}

static bool isBitwiseOperator (BinaryOpTypeEnum::Enum op) noexcept
{
    return op == BinaryOpTypeEnum::Enum::bitwiseOr  || op == BinaryOpTypeEnum::Enum::bitwiseAnd || op == BinaryOpTypeEnum::Enum::bitwiseXor
        || op == BinaryOpTypeEnum::Enum::leftShift  || op == BinaryOpTypeEnum::Enum::rightShift || op == BinaryOpTypeEnum::Enum::rightShiftUnsigned;
}

static bool isArithmeticOperator (BinaryOpTypeEnum::Enum op) noexcept
{
    return op == BinaryOpTypeEnum::Enum::add       || op == BinaryOpTypeEnum::Enum::subtract
        || op == BinaryOpTypeEnum::Enum::multiply  || op == BinaryOpTypeEnum::Enum::divide
        || op == BinaryOpTypeEnum::Enum::modulo    || op == BinaryOpTypeEnum::Enum::exponent;
}

//==============================================================================
struct FunctionArgumentListInfo
{
    template <typename Iterator>
    bool populate (Iterator& args)
    {
        argInfo.reserve (args.size());

        for (auto& arg : args)
        {
            if (auto value = castToValue (arg))
            {
                if (auto type = value->getResultType())
                {
                    if (type->isResolved())
                    {
                        argInfo.push_back ({ arg, *value, *type, value->constantFold() });
                        continue;
                    }
                }
            }

            return false;
        }

        return true;
    }

    struct ArgInfo
    {
        const Object& object;
        const ValueBase& value;
        const TypeBase& type;
        ptr<const ConstantValueBase> constant;
    };

    choc::SmallVector<ArgInfo, 8> argInfo;
};

//==============================================================================
static std::string getFunctionCallDescription (std::string_view name, choc::span<ref<Object>> argList)
{
    FunctionArgumentListInfo argInfo;
    argInfo.populate (argList);

    choc::SmallVector<std::string, 8> types;

    for (auto& arg : argInfo.argInfo)
        types.push_back (AST::print (arg.type, AST::PrintOptionFlags::useShortNames));

    return std::string (name) + "(" + choc::text::joinStrings (types, ", ") + ")";
}

static std::string getFunctionCallDescription (std::string_view name, const Expression& call)
{
    if (auto fc = call.getAsFunctionCall()) return getFunctionCallDescription (name, fc->arguments.getAsObjectList());
    if (auto cc = call.getAsCallOrCast())   return getFunctionCallDescription (name, cc->arguments.getAsObjectList());

    CMAJ_ASSERT_FALSE;
    return {};
}

static ptr<Function> findEventHandlerFunction (const EndpointDeclaration& endpoint, const TypeBase& eventType)
{
    for (auto& f : endpoint.getParentProcessor().functions)
    {
        auto& fn = castToFunctionRef (f);

        if (fn.isEventHandler && endpoint.hasName (fn.name.get()))
        {
            auto paramType = fn.getNumParameters() == 1 ? endpoint.context.allocator.createVoidType()
                                                        : castToRef<VariableDeclaration> (fn.parameters.back()).getType();

            if (paramType->isSameType (eventType,
                                       TypeBase::ComparisonFlags::ignoreReferences
                                         | TypeBase::ComparisonFlags::ignoreConst))
                return fn;
        }
    }

    return {};
}

//==============================================================================
/// Builds and holds a set of FunctionInfo objects for all the functions, and
/// calculates the stack size while it's at it.
struct FunctionInfoGenerator
{
    FunctionInfoGenerator() = default;

    template <typename Program>
    void generate (const Program& program)
    {
        functionInfoHolder.clear();
        program.visitAllFunctions (true, [this] (Function& f) { f.tempStorage = createInfoHolder(); });
        program.visitAllFunctions (true, [this] (Function& f) { iterateCallSequences (f, nullptr, 0, {}); });
    }

    //==============================================================================
    struct FunctionInfo
    {
        uint64_t localStackSize = 0;

        bool calledFromMain   = false;
        bool calledFromEvent  = false;
        bool calledFromInit   = false;

        ptr<const Statement> advanceCall, readStreamCall, writeStreamCall, writeEventCall, readValueCall, writeValueCall;

        void setAdvanceCall     (const Statement& s)   { if (advanceCall     == nullptr)  advanceCall = s; }
        void setWriteStreamCall (const Statement& s)   { if (writeStreamCall == nullptr)  writeStreamCall = s; }
        void setReadStreamCall  (const Statement& s)   { if (readStreamCall  == nullptr)  readStreamCall = s; }
        void setWriteEventCall  (const Statement& s)   { if (writeEventCall  == nullptr)  writeEventCall = s; }
        void setWriteValueCall  (const Statement& s)   { if (writeValueCall  == nullptr)  writeValueCall = s; }
        void setReadValueCall   (const Statement& s)   { if (readValueCall   == nullptr)  readValueCall = s; }

        bool isOnlyCalledFromMain() const { return calledFromMain && ! (calledFromInit || calledFromEvent); }

        ptr<const Statement> getStreamAccessStatement() const
        {
            if (writeStreamCall != nullptr)  return writeStreamCall;
            if (readStreamCall != nullptr)   return readStreamCall;
            return {};
        }

        ptr<const Statement> getValueReadAccessStatement() const
        {
            if (readValueCall != nullptr)    return readValueCall;
            return {};
        }

        ptr<const Statement> getValueAccessStatement() const
        {
            if (writeValueCall != nullptr)   return writeValueCall;
            if (readValueCall != nullptr)    return readValueCall;
            return {};
        }

        ptr<const Statement> getEventAccessStatement() const
        {
            if (writeEventCall != nullptr)  return writeEventCall;
            return {};
        }

        ptr<const Statement> getStreamOrValueAccessStatement() const
        {
            if (auto a = getStreamAccessStatement()) return a;
            if (auto a = getValueAccessStatement())  return a;
            return {};
        }
    };

    static FunctionInfo& getInfo (const Function& f)
    {
        auto info = static_cast<FunctionInfo*> (f.tempStorage);
        CMAJ_ASSERT (info != nullptr);
        return *info;
    }

    //==============================================================================
    void throwErrorIfRecursive()
    {
        if (! recursiveCallSequence.empty())
        {
            std::vector<std::string> functionNames;

            for (auto& f : recursiveCallSequence)
                functionNames.push_back (choc::text::addSingleQuotes (std::string (f->getOriginalName())));

            auto& context = recursiveCallSequence.front();

            if (functionNames.size() == 1)  throwError (context, Errors::functionCallsItselfRecursively (functionNames.front()));
            if (functionNames.size() == 2)  throwError (context, Errors::functionsCallEachOtherRecursively (functionNames[0], functionNames[1]));
            if (functionNames.size() >  2)  throwError (context, Errors::recursiveFunctionCallSequence (choc::text::joinStrings (functionNames, ", ")));
        }
    }

    ObjectRefVector<const Function> recursiveCallSequence;
    uint64_t maximumStackSize = 0;

private:
    //==============================================================================
    static constexpr uint64_t perCallStackOverhead = 16;
    static constexpr uint64_t stackItemAlignment = 8;

    std::vector<std::unique_ptr<FunctionInfo>> functionInfoHolder;

    FunctionInfo* createInfoHolder()
    {
        functionInfoHolder.push_back (std::make_unique<FunctionInfo>());
        return functionInfoHolder.back().get();
    }

    struct CallStack
    {
        CallStack* previous;
        ptr<const FunctionCall> call;
        const Function& function;

        bool contains (Function& f) const
        {
            return std::addressof (f) == std::addressof (function)
                    || (previous != nullptr && previous->contains (f));
        }

        void buildCallSequence (const Function& lastFunction, ObjectRefVector<const Function>& sequence) const
        {
            sequence.insert (sequence.begin(), function);

            if (previous != nullptr && std::addressof (lastFunction) != std::addressof (function))
                previous->buildCallSequence (lastFunction, sequence);
        }
    };

    void iterateCallSequences (Function& f, CallStack* previous, uint64_t stackSize, const FunctionInfo& callerInfo)
    {
        if (f.isGenericOrParameterised())
            return;

        if (previous != nullptr && previous->contains (f))
        {
            if (recursiveCallSequence.empty())
                previous->buildCallSequence (f, recursiveCallSequence);

            return;
        }

        stackSize += perCallStackOverhead + getLocalVariableStackSize (f);
        maximumStackSize = std::max (maximumStackSize, stackSize);

        auto& info = getInfo (f);

        info.calledFromEvent = info.calledFromEvent || callerInfo.calledFromEvent  || f.isEventHandler;
        info.calledFromMain   = info.calledFromMain   || callerInfo.calledFromMain    || f.isMainFunction();
        info.calledFromInit  = info.calledFromInit  || callerInfo.calledFromInit   || f.isUserInitFunction();

        CallStack newStack { previous, nullptr, f };
        auto newStackAddr = std::addressof (newStack);

        f.visitObjectsInScope ([this, previous, newStackAddr, stackSize, &info] (const Object& s)
        {
            if (auto fc = s.getAsFunctionCall())
            {
                if (auto targetFn = fc->getTargetFunction())
                {
                    newStackAddr->call = *fc;
                    iterateCallSequences (*targetFn, newStackAddr, stackSize, info);
                }
            }
            else if (auto a = s.getAsAdvance())
            {
                if (! a->hasNode())
                {
                    info.setAdvanceCall (*a);

                    for (auto prev = previous; prev != nullptr; prev = prev->previous)
                        getInfo (prev->function).setAdvanceCall (*prev->call);
                }
            }
            else if (auto w = s.getAsWriteToEndpoint())
            {
                if (! w->getGraphNode())
                {
                    auto endpoint = w->getEndpoint();

                    if (endpoint->isEvent())
                    {
                        info.setWriteEventCall (*w);

                        for (auto prev = previous; prev != nullptr; prev = prev->previous)
                            getInfo (prev->function).setWriteEventCall (*prev->call);
                    }
                    else if (endpoint->isStream())
                    {
                        info.setWriteStreamCall (*w);

                        for (auto prev = previous; prev != nullptr; prev = prev->previous)
                            getInfo (prev->function).setWriteStreamCall (*prev->call);
                    }
                    else
                    {
                        info.setWriteValueCall (*w);

                        for (auto prev = previous; prev != nullptr; prev = prev->previous)
                            getInfo (prev->function).setWriteValueCall (*prev->call);
                    }
                }
            }
            else if (auto r = s.getAsReadFromEndpoint())
            {
                if (! r->getGraphNode())
                {
                    auto& endpoint = *r->getEndpointDeclaration();

                    if (endpoint.isEvent())
                    {
                        CMAJ_ASSERT_FALSE;  // Can't read from events
                    }
                    else if (endpoint.isStream())
                    {
                        info.setReadStreamCall (*r);

                        for (auto prev = previous; prev != nullptr; prev = prev->previous)
                            getInfo (prev->function).setReadStreamCall (*prev->call);
                    }
                    else
                    {
                        info.setReadValueCall (*r);

                        for (auto prev = previous; prev != nullptr; prev = prev->previous)
                            getInfo (prev->function).setReadValueCall (*prev->call);
                    }
                }
            }
        });
    }

    static uint64_t getLocalVariableStackSize (Function& f)
    {
        auto& localStackSize = getInfo (f).localStackSize;

        if (localStackSize == 0)
        {
            uint64_t total = 0;

            f.visitAllLocalVariables ([&total] (const VariableDeclaration& v)
            {
                if (auto type = v.getType())
                    if (type->isResolved() && ! type->isVoid())
                        total += getAlignedSize<stackItemAlignment> (type->getPackedStorageSize());
            });

            localStackSize = total;
        }

        return localStackSize;
    }
};

//==============================================================================
struct Dependencies
{
    std::vector<const Function*> functions;
    std::vector<const StructType*> structs;
    std::vector<const VariableDeclaration*> stateVariables;
    std::vector<const Alias*> aliases;
    std::unordered_set<const Object*> visited;

    void addDependencies (const ProcessorBase& p)
    {
        for (auto& f : p.functions.iterateAs<AST::Function>())
            if (f.isExportedFunction())
                addDependencies (f);
    }

    void addDependencies (const Object& o)
    {
        if (visited.find (std::addressof (o)) != visited.end())
            return;

        visited.insert (std::addressof (o));

        if (auto f = o.getAsFunction())
        {
            addIfNotAlreadyThere (functions, *f);
        }
        else if (auto fc = o.getAsFunctionCall())
        {
            if (auto targetFn = fc->getTargetFunction())
                addDependencies (*targetFn);
        }
        else if (auto structType = o.getAsStructType())
        {
            addIfNotAlreadyThere (structs, *structType);
        }
        else if (auto vd = o.getAsVariableDeclaration())
        {
            if (vd->isStateVariable())
                addIfNotAlreadyThere (stateVariables, *vd);

            if (auto t = vd->getType())
                addDependencies (*t);
        }
        else if (auto vr = o.getAsVariableReference())
        {
            auto& v = vr->getVariable();

            if (v.isStateVariable())
                addIfNotAlreadyThere (stateVariables, v);

            if (auto t = v.getType())
                addDependencies (*t);
        }
        else if (auto alias = o.getAsAlias())
        {
            if (alias->aliasType == AliasTypeEnum::Enum::typeAlias)
            {
                addIfNotAlreadyThere (aliases, *alias);
                addDependencies (*alias->getTargetSkippingReferences());
            }
        }
        else if (auto ref = o.getAsNamedReference())
        {
            addDependencies (ref->getTarget());
        }

        const_cast<AST::Object&> (o).visitObjectsInScope ([this] (Object& s)
        {
            addDependencies (s);
        });
    }

    template <typename Object>
    void addIfNotAlreadyThere (std::vector<const Object*>& v, const Object& o)
    {
        if (std::find (v.begin(), v.end(), std::addressof (o)) != v.end())
            return;

        v.push_back (std::addressof (o));
    }
};

//==============================================================================
struct SideEffects
{
    bool modifiesStateVariables = false;
    bool modifiesLocalVariables = false;

    void add (const AST::Object& o)
    {
        if (! (modifiesStateVariables && modifiesStateVariables))
            if (auto s = AST::castTo<AST::Statement> (o))
                s->addSideEffects (*this);
    }

    void add (ptr<AST::Object> o)
    {
        if (o != nullptr)
            add (*o);
    }

    void add (const AST::Property& p)           { add (p.getObject()); }
    void addWritten (const AST::Property& p)    { addWritten (p.getObject()); }

    void addWritten (ptr<AST::Object> o)
    {
        add (o);

        if (! (modifiesStateVariables && modifiesStateVariables))
        {
            if (auto targetVariable = castToValueRef (o).getSourceVariable())
            {
                if (targetVariable->mayReferenceGlobalState())
                    modifiesStateVariables = true;

                if (targetVariable->isLocal() || targetVariable->isParameter())
                    modifiesLocalVariables = true;
            }
        }
    }
};

//==============================================================================
static choc::value::Value getAnnotationAsChocValue (const Property& p)
{
    if (auto a = castTo<Annotation> (p))
        return a->convertToValue();

    return {};
}

static EndpointDetails createEndpointDetails (const EndpointDeclaration& decl)
{
    CMAJ_ASSERT (! decl.isHoistedEndpoint());

    EndpointDetails d;
    d.endpointID = EndpointID::create (decl.getName());
    d.endpointType = decl.getEndpointType();
    d.isInput = decl.isInput;
    d.annotation = getAnnotationAsChocValue (decl.annotation);

    auto location = decl.context.getFullLocation();
    if (! location.filename.empty())
        d.sourceFileLocation = choc::text::trim (location.getLocationDescription());

    for (auto& t : decl.dataTypes)
        d.dataTypes.push_back (castToTypeBaseRef (t.get()).toChocType());

    return d;
}

/// Manages a list of endpoint details
struct EndpointList
{
    void clear()
    {
        endpoints.clear();
        outputEndpointDetails.endpoints.clear();
    }

    void initialise (const AST::ProcessorBase& processor)
    {
        for (auto& e : processor.endpoints.iterateAs<AST::EndpointDeclaration>())
            endpoints.push_back ({ e, createEndpointDetails (e) });

        inputEndpointDetails = getEndpointDetails (true);
        outputEndpointDetails = getEndpointDetails (false);
    }

    EndpointDetailsList inputEndpointDetails, outputEndpointDetails;

    struct EndpointInfo
    {
        AST::EndpointDeclaration& endpoint;
        EndpointDetails details;
    };

    std::vector<EndpointInfo> endpoints;

private:
    EndpointDetailsList getEndpointDetails (bool inputs) const
    {
        EndpointDetailsList result;

        for (auto& e : endpoints)
            if (e.details.isInput == inputs)
                result.endpoints.push_back (e.details);

        return result;
    }

    const EndpointDetails* findEndpointDetails (const EndpointID& endpointID) const
    {
        for (auto& d : inputEndpointDetails)
            if (d.endpointID == endpointID)
                return std::addressof (d);

        for (auto& d : outputEndpointDetails)
            if (d.endpointID == endpointID)
                return std::addressof (d);

        return {};
    }
};

//==============================================================================
template <typename Type>
static ptr<Type> applyLocationAndScopeTo (ptr<Type> object, const ObjectContext& context)
{
    if (auto o = object.get())
    {
        o->context.location = context.location;
        o->context.parentScope = context.parentScope;
    }

    return object;
}

static bool applySizeIfSlice (AST::ChildObject& possibleSlice, size_t newSize)
{
    if (auto type = AST::castToTypeBase (possibleSlice))
    {
        if (type->isResolved() && type->isSlice())
        {
            auto& array = AST::castToRefSkippingReferences<AST::ArrayType> (type->skipConstAndRefModifiers());

            possibleSlice.setChildObject (createArrayOfType (array.context,
                                                             array.getInnermostElementTypeObject(),
                                                             static_cast<int32_t> (newSize)));
            return true;
        }
    }

    return false;
}

static bool shouldSingleArgumentCastToSlice (const AST::Object& argument)
{
    if (auto value = AST::castToValue (argument))
        if (auto type = value->getResultType())
            if (type->isResolved())
                return ! type->isArray();

    return false;
}

static bool updateCastTypeSizeIfPossible (AST::Cast& c)
{
    if (auto targetType = AST::castToTypeBase (c.targetType))
        if (targetType->isResolved() && targetType->isSlice() && ! c.arguments.empty())
            if (c.arguments.size() != 1 || shouldSingleArgumentCastToSlice (c.arguments.front().getObjectRef()))
                return applySizeIfSlice (c.targetType, c.arguments.size());

    return false;
}

static bool areFixedSizeArraysCopiedToSlices (const TypeBase& globalType, const TypeBase& sourceType)
{
    if (globalType.isSlice())
        return sourceType.isFixedSizeArray();

    if (auto gs = castTo<StructType> (globalType.skipConstAndRefModifiers()))
    {
        if (gs->memberTypes.empty())
            return false;

        if (auto ss = castTo<StructType> (sourceType.skipConstAndRefModifiers()))
        {
            CMAJ_ASSERT (gs->memberTypes.size() == ss->memberTypes.size());

            for (size_t i = 0; i < gs->memberTypes.size(); ++i)
                if (areFixedSizeArraysCopiedToSlices (gs->getMemberType(i), ss->getMemberType(i)))
                    return true;

            return false;
        }
    }

    if (auto ga = castTo<ArrayType> (globalType.skipConstAndRefModifiers()))
        if (auto sa = castTo<ArrayType> (sourceType.skipConstAndRefModifiers()))
            return areFixedSizeArraysCopiedToSlices (*ga->getArrayOrVectorElementType(),
                                                        *sa->getArrayOrVectorElementType());

    return false;
}

static std::vector<size_t> getPathToChildOfStruct (const AST::StructType& parent, const AST::StructType& child)
{
    std::vector<size_t> result;

    for (size_t i = 0; i < parent.memberNames.size(); ++i)
    {
        ref<const AST::TypeBase> memberType = AST::castToTypeBaseRef (parent.memberTypes[i]);

        if (memberType->isArray())
            memberType = *memberType->getArrayOrVectorElementType();

        if (auto memberStruct = memberType->skipConstAndRefModifiers().getAsStructType())
        {
            if (memberStruct == std::addressof (child))
            {
                result.push_back (i);
                break;
            }

            auto subPath = getPathToChildOfStruct (*memberStruct, child);

            if (! subPath.empty())
            {
                result.push_back (i);
                result.insert (result.end(), subPath.begin(), subPath.end());
                break;
            }
        }
    }

    return result;
}

//==============================================================================
template <typename Function>
static choc::com::String* catchAllErrorsAsJSON (bool ignoreWarnings, Function&& fn)
{
    DiagnosticMessageList messageList;
    catchAllErrors (messageList, std::move (fn));

    if (messageList.empty() || (ignoreWarnings && ! messageList.hasErrors()))
        return {};

    return choc::com::createString (messageList.toJSONString (true)).getWithIncrementedRefCount();
}
