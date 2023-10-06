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


struct Statement  : public Object
{
    Statement (const ObjectContext& c) : Object (c) {}
    ~Statement() override = default;

    Statement* getAsStatement() override               { return this; }
    const Statement* getAsStatement() const override   { return this; }
    bool isStatement() const override                  { return true; }

    virtual void addSideEffects (SideEffects&) const = 0;

    virtual bool containsStatement (const Statement& other) const
    {
        return this == std::addressof (other);
    }
};

struct Expression   : public Statement
{
    Expression (const ObjectContext& c) : Statement (c) {}
    ~Expression() override = default;

    Expression* getAsExpression() override                 { return this; }
    const Expression* getAsExpression() const override     { return this; }
    bool isExpression() const override                     { return true; }
};

//==============================================================================
struct StaticAssertion  : public Statement
{
    StaticAssertion (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(StaticAssertion, 48)

    void addSideEffects (SideEffects&) const override  {}
    bool isDummyStatement() const override  { return true; }

    void initialiseFromArgs (choc::span<ref<Object>> args)
    {
        auto numArgs = args.size();

        if (numArgs != 1 && numArgs != 2)
            throwError (context, Errors::expected1or2Args());

        condition.setChildObject (args.front());

        if (numArgs == 2)
        {
            if (auto errorString = args[1]->getAsConstantString())
                error = errorString->value;
            else
                throwError (args[1], Errors::expectedStringLiteralAsArg2());
        }
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (condition, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, condition) \
        X (2, StringProperty, error)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct NamedReference  : public Expression
{
    NamedReference (const ObjectContext& c)  : Expression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(NamedReference, 49)

    Object& getTarget() const                                       { return target.getObjectRef(); }
    ptr<Object> getTargetSkippingReferences() const override        { return target.getObject(); }
    void addSideEffects (SideEffects&) const override               {}
    void writeSignature (SignatureBuilder& sig) const override      { sig << target; }

    bool hasName (PooledString nametoMatch) const override          { return target.hasName (nametoMatch); }
    PooledString getName() const override                           { return getTarget().getName(); }

    PooledString getOriginalName() const override
    {
        if (target.getObject() == *this)
            return {};

        return getTarget().getOriginalName();
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference, target)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Alias  : public Statement
{
    Alias (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Alias, 50)
    void writeSignature (SignatureBuilder& sb) const override       { sb << aliasType << target;}

    bool hasName (PooledString nametoMatch) const override          { return name.hasName (nametoMatch); }
    PooledString getName() const override                           { return name; }
    void setName (PooledString newName) override                    { name.set (newName); }
    ptr<Object> getTargetSkippingReferences() const override        { return target.getObject(); }

    TypeBase* getAsTypeBase() override                              { return target.getRawPointer() ? target->getAsTypeBase() : nullptr; } // NB: don't use castToTypeBase here, as it can recurse
    const TypeBase* getAsTypeBase() const override                  { return target.getRawPointer() ? target->getAsTypeBase() : nullptr; }
    bool isTypeBase() const override                                { return target.getRawPointer() && target->isTypeBase(); }
    void addSideEffects (SideEffects&) const override               {}
    bool isDummyStatement() const override                          { return true; }
    ptr<const Comment> getComment() const override                  { return castTo<const Comment> (comment); }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (aliasType, visit);
        visitObjectIfPossible (target, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, name) \
        X (2, AliasTypeEnum,  aliasType) \
        X (3, ChildObject,    target) \
        X (4, ChildObject,    comment)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Function  : public Object
{
    Function (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Function, 51)
    ptr<const Comment> getComment() const override                  { return castTo<const Comment> (comment); }
    bool hasName (PooledString nametoMatch) const override          { return name.hasName (nametoMatch); }
    PooledString getName() const override                           { return name; }
    void setName (PooledString newName) override                    { name.set (newName); }

    PooledString getOriginalName() const override
    {
        if (auto f = castToFunction (originalGenericFunction))
            return f->name;

        return name;
    }

    void performLocalNameSearch (NameSearch& search, ptr<const Statement>) override
    {
        if (search.findVariables)
            search.addFirstMatching (parameters);
    }

    size_t getNumParameters() const                 { return parameters.size(); }
    auto iterateParameters() const                  { return parameters.iterateAs<VariableDeclaration>(); }
    bool isMainFunction() const                     { return name == getStrings().mainFunctionName; }
    bool isSystemInitFunction() const               { return name == getStrings().systemInitFunctionName; }
    bool isSystemAdvanceFunction() const            { return name == getStrings().systemAdvanceFunctionName; }
    bool isUserInitFunction() const                 { return name == getStrings().userInitFunctionName; }
    bool isResetFunction() const                    { return name == getStrings().resetFunctionName && getNumNonInternalParameters() == 0; }
    bool isExportedFunction() const                 { return isExported || isEventHandler || isSystemInitFunction() || isSystemAdvanceFunction() || isMainFunction() || isUserInitFunction(); }
    bool isGenericOrParameterised() const override  { return ! genericWildcards.empty(); }
    bool isSpecialisedGeneric() const               { return originalGenericFunction != nullptr; }
    ptr<ScopeBlock> getMainBlock() const            { return castTo<ScopeBlock> (mainBlock); }

    size_t getNumNonInternalParameters() const
    {
        size_t result = 0;

        for (auto& i : iterateParameters())
            if (! i.isInternalVariable())
                result++;

        return result;
    }

    ObjectRefVector<TypeBase> getParameterTypes() const
    {
        ObjectRefVector<TypeBase> types;
        types.reserve (parameters.size());

        for (auto& p : parameters)
            types.push_back (castToTypeBaseRef (castToRef<VariableDeclaration> (p).declaredType));

        return types;
    }

    bool hasParameterTypes (choc::span<ref<const TypeBase>> types) const
    {
        if (types.size() != parameters.size())
            return false;

        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto& type = castToTypeBaseRef (getParameter (i).declaredType);

            if (! type.isSameType (types[i], AST::TypeBase::ComparisonFlags::ignoreConst
                                              | AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                return false;
        }

        return true;
    }

    bool hasSameParameterTypes (const Function& other) const
    {
        if (parameters.size() != other.parameters.size())
            return false;

        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto& type1 = castToTypeBaseRef (getParameter (i).declaredType);
            auto& type2 = castToTypeBaseRef (other.getParameter (i).declaredType);

            if (! type1.isSameType (type2, AST::TypeBase::ComparisonFlags::ignoreConst
                                            | AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                return false;
        }

        return true;
    }

    VariableDeclaration& getParameter (size_t index) const
    {
        return AST::castToRef<VariableDeclaration> (parameters[index]);
    }

    VariableDeclaration& getParameter (PooledString paramName) const
    {
        return AST::castToRef<VariableDeclaration> (parameters.findObjectWithName (paramName));
    }

    bool isIntrinsic() const
    {
        if (auto ns = findParentNamespace())
            if (ns->hasName (getStrings().intrinsicsNamespaceName))
                if (auto parent = ns->findParentNamespace())
                    return parent->isSystemModule();

        return false;
    }

    Intrinsic::Type getIntrinsic() const
    {
        if (isIntrinsic())
            return Intrinsic::getIntrinsicForName (getOriginalName());

        return Intrinsic::Type::unknown;
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << name;

        for (auto& param : iterateParameters())
            sig << param.declaredType;
    }

    template <typename Visitor>
    void visitAllLocalVariables (Visitor&& visitor) const
    {
        for (auto& param : parameters)
            visitor (castToVariableDeclarationRef (param));

        if (auto block = getMainBlock())
            block->visitAllLocalVariables (visitor);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (returnType, visit);
        visitObjectIfPossible (parameters, visit);
        visitObjectIfPossible (mainBlock, visit);
        visitObjectIfPossible (genericWildcards, visit);
    }

    void addSideEffects (SideEffects& effects) const
    {
        if (mainBlock != nullptr)
            getMainBlock()->addSideEffects (effects);
    }

    bool shouldPropertyAppearInSyntaxTree (const SyntaxTreeOptions& options, uint8_t propertyID) override
    {
        if (propertyID == 5)
            return options.options.includeFunctionContents;

        return true;
    }

    bool canConstantFoldProperty (const Property& p) override
    {
        return std::addressof (p) != std::addressof (originalCallLeadingToSpecialisation);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1,  StringProperty,   name) \
        X (2,  ChildObject,      annotation) \
        X (3,  ChildObject,      returnType) \
        X (4,  ListProperty,     parameters) \
        X (5,  ChildObject,      mainBlock) \
        X (6,  ListProperty,     genericWildcards) \
        X (7,  ObjectReference,  originalGenericFunction) \
        X (8,  ObjectReference,  originalCallLeadingToSpecialisation) \
        X (9,  BoolProperty,     isEventHandler) \
        X (10, BoolProperty,     isExported) \
        X (11, ChildObject,      comment) \
        X (12, BoolProperty,     isExternal) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES

    // non-persistent scratch variables to help with some algorithms
    void* tempStorage = nullptr;
};

//==============================================================================
struct VariableDeclaration  : public Statement
{
    VariableDeclaration (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(VariableDeclaration, 52)
    bool hasName (PooledString nametoMatch) const override        { return name.hasName (nametoMatch); }
    PooledString getName() const override                         { return name; }
    void setName (PooledString newName) override                  { name.set (newName); }
    void writeSignature (SignatureBuilder& sig) const override    { sig << getFullyQualifiedNameWithoutRoot(); }
    ptr<const Comment> getComment() const override                { return castTo<const Comment> (comment); }

    bool isLocal() const                        { return variableType == VariableTypeEnum::Enum::local; }
    bool isParameter() const                    { return variableType == VariableTypeEnum::Enum::parameter; }
    bool isStateVariable() const                { return variableType == VariableTypeEnum::Enum::state; }
    bool isGlobal() const                       { return isExternal || isStateVariable(); }

    bool isInternalVariable() const
    {
        // Internal variable names start with _
        return *getName().get().begin() == '_';
    }

    ptr<const TypeBase> getType() const
    {
        if (auto declared = declaredType.getRawPointer())
            return castToTypeBase (*declared);

        if (auto v = castToValue (initialValue))
            if (auto resultType = v->getResultType())
                return resultType->skipConstAndRefModifiers();

        return {};
    }

    bool isCompileTimeConstant() const
    {
        if (isConstant)
            return true;

        if (auto t = castToTypeBase (declaredType))
            return t->isConst();

        return false;
    }

    bool hasConstInitialiser() const
    {
        if (initialValue == nullptr && ! isInitialisedInInit)
            return true;

        return AST::getAsFoldedConstant (initialValue) != nullptr;
    }

    bool hasStaticConstantValue() const
    {
        return isExternal || (isCompileTimeConstant() && hasConstInitialiser());
    }

    bool canBeMadeConst() const
    {
        return (isStateVariable() || isLocal())
                && ! (isConstant || isExternal)
                && hasConstInitialiser()
                && ! isCompileTimeConstant();
    }

    IntegerRange getKnownIntegerRange() const
    {
        if (knownRange.isValid())
            return knownRange;

        if (auto t = getType())
            return t->getAddressableIntegerRange();

        return {};
    }

    void addSideEffects (SideEffects& effects) const override
    {
        if (initialValue != nullptr)
            effects.add (initialValue);
    }

    bool mayReferenceGlobalState() const
    {
        if (isParameter())
            return ! castToTypeBaseRef (declaredType).isNonConstReference();

        return ! isGlobal();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (declaredType, visit);
        visitObjectIfPossible (initialValue, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || initialValue.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1,  StringProperty,    name) \
        X (2,  ChildObject,       annotation) \
        X (3,  ChildObject,       declaredType) \
        X (4,  ChildObject,       initialValue) \
        X (5,  VariableTypeEnum,  variableType) \
        X (6,  BoolProperty,      isConstant) \
        X (7,  BoolProperty,      isExternal) \
        X (8,  BoolProperty,      isInitialisedInInit) \
        X (9,  ChildObject,       comment) \
        X (10, BoolProperty,      isAliasFree) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES

    ObjectRefVector<const AST::Object> sourcesOfNonGlobalData;
    IntegerRange knownRange;
};

//==============================================================================
struct ScopeBlock  : public Statement
{
    ScopeBlock (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ScopeBlock, 53)

    PooledString getName() const override                       { return label; }
    bool hasName (PooledString nametoMatch) const override      { return label.hasName (nametoMatch); }
    void setName (PooledString newName) override                { label.set (newName); }

    void addSideEffects (SideEffects& effects) const override
    {
        for (auto& s : statements)
            effects.add (s);
    }

    void performLocalNameSearch (NameSearch& search, ptr<const Statement> statementToSearchUpTo) override
    {
        if (search.findVariables || search.findTypes)
        {
            ptr<Object> lastMatch;
            auto statementToStopAt = findTopLevelStatementToStopAt (statementToSearchUpTo.get());

            for (auto& s : statements)
            {
                auto& statement = *s->getObject().get();

                if (statementToStopAt == std::addressof (statement))
                    break;

                if (statement.hasName (search.nameToFind))
                    if ((search.findVariables && statement.isVariableDeclaration())
                         || (search.findTypes && statement.isAlias()))
                        lastMatch = statement;
            }

            if (auto l = lastMatch.get())
                search.addResult (*l);
        }
    }

    const AST::Object* findTopLevelStatementToStopAt (const AST::Object* s) const
    {
        if (s == nullptr)
            return {};

        for (;;)
        {
            auto parent = s->context.parentScope.get();

            if (parent == this)
                return s;

            if (parent == nullptr)
                return {};

            s = parent;
        }
    }

    template <typename Visitor>
    void visitAllLocalVariables (Visitor&& visitor) const
    {
        for (auto& s : statements)
        {
            auto& statement = s->getObjectRef();

            if (auto v = statement.getAsVariableDeclaration())
            {
                if (v->isLocal())
                    visitor (*v);
            }
            else if (auto sub = statement.getAsScopeBlock())
            {
                sub->visitAllLocalVariables (visitor);
            }
        }
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);

        for (auto& s : statements)
            castToRef<Statement> (s).visitObjectsInScope (visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || statements.containsStatement (other);
    }

    int findIndexOfStatementContaining (const Statement& s)
    {
        for (size_t i = 0; i < statements.size(); ++i)
            if (statements[i].containsStatement (s))
                return static_cast<int> (i);

        return -1;
    }

    void addStatement (Statement& s, int index = -1)
    {
        statements.addChildObject (s, index);
    }

    void setStatement (size_t index, Statement& s)
    {
        statements.setChildObject (s, index);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1,  StringProperty, label) \
        X (2,  ListProperty,   statements) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ForwardBranch  : public Statement
{
    ForwardBranch (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ForwardBranch, 54)

    void addSideEffects (SideEffects& effects) const override       { effects.add (condition); }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (condition, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || condition.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,   condition) \
        X (2, ListProperty,  targetBlocks) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};


//==============================================================================
struct IfStatement  : public Statement
{
    IfStatement (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(IfStatement, 55)

    void addSideEffects (SideEffects& effects) const override
    {
        effects.add (condition);
        effects.add (trueBranch);
        effects.add (falseBranch);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (condition, visit);
        castToRef<Statement> (trueBranch).visitObjectsInScope (visit);
        visitObjectIfPossible (falseBranch, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || condition.containsStatement (other)
                || trueBranch.containsStatement (other)
                || falseBranch.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  condition) \
        X (2, ChildObject,  trueBranch) \
        X (3, ChildObject,  falseBranch) \
        X (4, BoolProperty, isConst) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct InPlaceOperator  : public Expression
{
    InPlaceOperator (const ObjectContext& c) : Expression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(InPlaceOperator, 56)

    std::string_view getSymbol() const      { return BinaryOperator::getSymbolForOperator (op); }

    void addSideEffects (SideEffects& effects) const override
    {
        effects.addWritten (target);
        effects.add (source);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (target, visit);
        visitObjectIfPossible (source, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || target.containsStatement (other)
                || source.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, BinaryOpTypeEnum,  op) \
        X (2, ChildObject,       target) \
        X (3, ChildObject,       source) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Assignment  : public Expression
{
    Assignment (const ObjectContext& c) : Expression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Assignment, 57)

    void addSideEffects (SideEffects& effects) const override
    {
        effects.addWritten (target);
        effects.add (source);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (target, visit);
        visitObjectIfPossible (source, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || target.containsStatement (other)
                || source.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,       target) \
        X (2, ChildObject,       source) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ReturnStatement  : public Statement
{
    ReturnStatement (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ReturnStatement, 58)

    void addSideEffects (SideEffects& effects) const override
    {
        effects.add (value);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (value, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || value.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, value) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct BreakStatement  : public Statement
{
    BreakStatement (const ObjectContext& c) : Statement (c) {}
    BreakStatement (const ObjectContext& c, Object& block) : Statement (c) { targetBlock.referTo (block); }

    CMAJ_AST_DECLARE_STANDARD_METHODS(BreakStatement, 59)

    void addSideEffects (SideEffects&) const override      {}

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference, targetBlock) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ContinueStatement  : public Statement
{
    ContinueStatement (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ContinueStatement, 60)

    void addSideEffects (SideEffects&) const override   {}

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference, targetBlock) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct LoopStatement  : public Statement
{
    LoopStatement (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(LoopStatement, 61)

    PooledString getName() const override                       { return label; }
    bool hasName (PooledString nametoMatch) const override      { return label.hasName (nametoMatch); }
    void setName (PooledString newName) override                { label.set (newName); }

    void performLocalNameSearch (NameSearch& search, ptr<const Statement>) override
    {
        if (search.findVariables)
        {
            if (auto i = initialisers.findObjectWithName (search.nameToFind))
                if (auto v = i->getAsVariableDeclaration())
                    search.addResult (*v);

            if (auto v = castToVariableDeclaration (numIterations))
                if (v->hasName (search.nameToFind))
                    search.addResult (*v);
        }
    }

    bool isInfinite() const
    {
        return condition == nullptr && numIterations == nullptr;
    }

    void addSideEffects (SideEffects& effects) const override
    {
        for (auto& i : initialisers)
            effects.add (i);

        effects.add (condition);
        effects.add (numIterations);
        effects.add (body);
        effects.add (iterator);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);

        for (auto& i : initialisers)
            visitObjectIfPossible (i, visit);

        visitObjectIfPossible (condition, visit);
        visitObjectIfPossible (numIterations, visit);
        visitObjectIfPossible (body, visit);
        visitObjectIfPossible (iterator, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                 || condition.containsStatement (other)
                 || numIterations.containsStatement (other)
                 || body.containsStatement (other)
                 || iterator.containsStatement (other)
                 || initialisers.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ListProperty, initialisers) \
        X (2, ChildObject, condition) \
        X (3, ChildObject, iterator) \
        X (4, ChildObject, numIterations) /* may be a variable with a bounded type, or just an integer constant */ \
        X (5, ChildObject, body) \
        X (6, StringProperty, label) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Advance  : public Statement
{
    Advance (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Advance, 62)

    bool hasNode() const
    {
        return node != nullptr;
    }

    ptr<AST::GraphNode> getNode() const
    {
        if (auto n = AST::castToSkippingReferences<AST::GraphNode> (node))
            return n;

        if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (node))
            return AST::castToSkippingReferences<AST::GraphNode> (getElement->parent);

        return {};
    }

    void addSideEffects (SideEffects& effects) const override
    {
        effects.modifiesStateVariables = true;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (node, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, node) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct WriteToEndpoint  : public Statement
{
    WriteToEndpoint (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(WriteToEndpoint, 63)

    ptr<EndpointDeclaration> getEndpoint() const
    {
        if (auto endpoint = castToSkippingReferences<EndpointDeclaration> (target))
            return endpoint;

        if (auto instance = castToSkippingReferences<EndpointInstance> (target))
            return castToSkippingReferences<EndpointDeclaration> (instance->endpoint);

        return {};
    }

    ptr<const GraphNode> getGraphNode() const
    {
        if (auto instance = castToSkippingReferences<EndpointInstance> (target))
            return castTo<GraphNode> (instance->node);

        return {};
    }

    ptr<EndpointInstance> getEndpointInstance() const
    {
        if (auto instance = castToSkippingReferences<EndpointInstance> (target))
            return instance;

        return {};
    }

    void addSideEffects (SideEffects& effects) const override
    {
        effects.modifiesStateVariables = true;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (target, visit);
        visitObjectIfPossible (targetIndex, visit);
        visitObjectIfPossible (value, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || target.containsStatement (other)
                || targetIndex.containsStatement (other)
                || value.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, target) \
        X (2, ChildObject, targetIndex) \
        X (3, ChildObject, value) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct NoopStatement  : public Statement
{
    NoopStatement (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS_NO_PROPS(NoopStatement, 64)

    bool isDummyStatement() const override  { return true; }
    void addSideEffects (SideEffects&) const override  {}

    template <typename VisitorType> void visitObjects (VisitorType&) {}
    bool isIdentical (const Object& other) const override { return classID == other.getObjectClassID(); }
};

//==============================================================================
struct Annotation  : public Object
{
    Annotation (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Annotation, 65)

    ptr<ValueBase> findProperty (std::string_view name) const
    {
        for (size_t i = 0; i < names.size(); ++i)
            if (auto s = names[i].getAsStringProperty())
                if (s->get() == name)
                    return castToValue (values[i]);

        return {};
    }

    ptr<ConstantValueBase> findConstantProperty (std::string_view name) const
    {
        if (auto v = findProperty (name))
            return v->constantFold();

        return {};
    }

    template <typename PrimitiveType>
    PrimitiveType getPropertyAs (std::string_view name, PrimitiveType defaultValue = {}) const
    {
        if (auto v = findConstantProperty (name))
            if (auto prim = getAsOptionalPrimitive<PrimitiveType> (*v))
                return *prim;

        return defaultValue;
    }

    bool getBoolFlag (std::string_view name) const
    {
        return getPropertyAs<bool> (name);
    }

    choc::value::Value convertToValue() const
    {
        auto object = choc::value::createObject ("annotation");

        for (size_t i = 0; i < names.size(); ++i)
        {
            if (auto value = castToValue (values[i]))
            {
                if (auto constant = value->constantFold())
                {
                    object.addMember (names[i].getAsStringProperty()->get(), constant->toValue (nullptr));
                }
            }
        }

        return object;
    }

    void setValue (PooledString name, AST::ValueBase& value)
    {
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (names[i].hasName (name))
            {
                values.remove (i);
                values.addChildObject (value, static_cast<int> (i));
                return;
            }
        }

        names.addString (name);
        values.addChildObject (value);
    }

    void mergeFrom (const AST::Annotation& source, bool overwriteExistingValues)
    {
        for (size_t i = 0; i < source.names.size(); ++i)
        {
            auto name = source.names[i].getAsStringProperty()->get();

            if (overwriteExistingValues || findProperty (name) == nullptr)
                setValue (name, AST::castToValueRef (source.values[i]));
        }
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (values, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ListProperty, names) \
        X (2, ListProperty, values) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Reset  : public Statement
{
    Reset (const ObjectContext& c) : Statement (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Reset, 75)

    bool hasNode() const
    {
        return node != nullptr;
    }

    ptr<AST::GraphNode> getNode() const
    {
        if (auto n = AST::castToSkippingReferences<AST::GraphNode> (node))
            return n;

        if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (node))
            return AST::castToSkippingReferences<AST::GraphNode> (getElement->parent);

        return {};
    }

    void addSideEffects (SideEffects&) const override  {}

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (node, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, node) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};
