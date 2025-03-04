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

namespace cmaj::passes
{

//==============================================================================
struct NameResolver  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    NameResolver (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::Identifier& i) override
    {
        if (i.name == i.getStrings().wrap || i.name == i.getStrings().clamp)
        {
            if (auto cs = getPreviousObjectOnVisitStack().getAsChevronedSuffix())
            {
                if (cs->terms.size() > 1)
                    throwError (cs->terms[1], Errors::wrapOrClampSizeHasOneArgument());

                auto& t = replaceWithNewObject<AST::BoundedType> (*cs);
                t.isClamp = (i.name == i.getStrings().clamp);
                t.limit.setChildObject (cs->terms[0].getObjectRef());
                return;
            }
        }

        if (! performNameSearch (i, i.name, false) && throwOnErrors)
        {
            AST::NameSearch search;
            findAllMatches (search, i, i.name, false);

            if (search.itemsFound.empty())
                throwError (i, Errors::unresolvedSymbol (i.name.get()));
        }
    }

    void visit (AST::NamespaceSeparator& separator) override
    {
        if (auto name = AST::castTo<AST::Identifier> (separator.lhs))
        {
            if (! performNameSearch (*name, name->name, true))
            {
                if (throwOnErrors)
                    throwError (AST::getContextOfStartOfExpression (separator),
                                Errors::unresolvedSymbol (AST::print (separator)));

                return visitObject (separator.lhs);
            }
        }
        else if (auto parentModule = AST::castToSkippingReferences<AST::ModuleBase> (separator.lhs))
        {
            if (! parentModule->isGenericOrParameterised())
                resolveChildOfModule (separator, *parentModule);
        }
        else if (auto e = AST::castToSkippingReferences<AST::EnumType> (separator.lhs))
        {
            resolveEnumMember (separator, *e);
        }
        else
        {
            visitObject (separator.lhs);
        }
    }

    static ptr<AST::GraphNode> findNode (AST::Object& o)
    {
        if (auto node = AST::castToSkippingReferences<AST::GraphNode> (o))
            return node;

        if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (o))
            if (auto node = AST::castToSkippingReferences<AST::GraphNode> (getElement->parent))
                return node;

        return {};
    }

    void visit (AST::DotOperator& dot) override
    {
        visitObject (dot.lhs); // skip the RHS

        auto name = AST::castToRef<AST::Identifier> (dot.rhs).name.get();

        if (auto node = findNode (dot.lhs))
        {
            AST::NameSearch search;
            search.nameToFind                   = name;
            search.stopAtFirstScopeWithResults  = true;
            search.findVariables                = false;
            search.findTypes                    = false;
            search.findFunctions                = false;
            search.findNamespaces               = false;
            search.findProcessors               = false;
            search.findNodes                    = true;
            search.findEndpoints                = true;
            search.onlyFindLocalVariables       = false;

            if (auto processorType = node->getProcessorType())
                if (! processorType->isGenericOrParameterised())
                    search.performSearch (*processorType, {});

            if (! search.itemsFound.empty())
            {
                if (search.itemsFound.size() == 1)
                {
                    if (auto endpoint = AST::castTo<AST::EndpointDeclaration> (search.itemsFound.front()))
                    {
                        if (endpoint->isHoistedEndpoint())
                        {
                            registerFailure();
                            return;
                        }

                        if (endpoint->isOutput() && ! endpoint->isEvent())
                        {
                            auto& read = replaceWithNewObject<AST::ReadFromEndpoint> (dot);
                            auto& endpointInstance = read.allocateChild<AST::EndpointInstance>();
                            endpointInstance.endpoint.referTo (endpoint);
                            endpointInstance.node.referTo (dot.lhs);
                            read.endpointInstance.referTo (endpointInstance);
                            return;
                        }

                        auto& endpointInstance = replaceWithNewObject<AST::EndpointInstance> (dot);
                        endpointInstance.node.referTo (dot.lhs);
                        endpointInstance.endpoint.referTo (*endpoint);
                        return;
                    }

                    if (auto n = AST::castTo<AST::GraphNode> (search.itemsFound.front()))
                        throwError (dot.rhs, Errors::nestedGraphNodesAreNotSupported());
                }
            }

            registerFailure();
        }

        auto lhsValue = AST::castToValue (dot.lhs);
        ptr<const AST::TypeBase> lhsType;

        if (lhsValue != nullptr)
            lhsType = lhsValue->getResultType();
        else
            lhsType = AST::castToTypeBase (dot.lhs);

        if (lhsType != nullptr && ! lhsType->isResolved())
            lhsType = {};

        auto lhsEndpoint = AST::castToSkippingReferences<AST::EndpointDeclaration> (dot.lhs);

        if (lhsType == nullptr && lhsEndpoint == nullptr)
        {
            registerFailure();
            return;
        }

        if (lhsValue != nullptr)
        {
            auto shouldConvertToStructMember = [&]
            {
                auto& type = lhsType->skipConstAndRefModifiers();

                if (auto s = type.getAsStructType())
                    return s->indexOfMember (name) >= 0;

                if (name == "real" || name == "imag")
                    return type.isComplexOrVectorOfComplex();

                return false;
            };

            if (shouldConvertToStructMember())
            {
                auto& getMember = dot.context.allocate<AST::GetStructMember>();
                getMember.object.setChildObject (dot.lhs.get());
                getMember.member = name;
                replaceObject (dot, getMember);
                return;
            }
        }

        if (resolveMetaFunction<AST::ValueMetaFunction, AST::ValueMetaFunctionTypeEnum, true> (dot, name))
            return;

        if (resolveMetaFunction<AST::TypeMetaFunction, AST::TypeMetaFunctionTypeEnum, false> (dot, name))
            return;

        registerFailure();
    }

    int insideFunction = 0, insideProcessor = 0, insideConnection = 0, insideConnectionIf = 0, couldBeFunctionName = 0;

    void visit (AST::Function& f) override
    {
        ++insideFunction;
        super::visit (f);
        --insideFunction;
    }

    void visit (AST::Connection& c) override
    {
        ++insideConnection;
        super::visit (c);
        --insideConnection;
    }

    void visit (AST::ConnectionIf& c) override
    {
        ++insideConnectionIf;
        super::visit (c);
        --insideConnectionIf;
    }

    void visit (AST::Processor& p) override
    {
        ++insideProcessor;
        super::visit (p);
        --insideProcessor;
    }

    void visit (AST::Graph& g) override
    {
        ++insideProcessor;
        super::visit (g);
        --insideProcessor;
    }

    void visit (AST::FunctionCall& fc) override
    {
        if (shouldVisitObject (fc))
        {
            ++couldBeFunctionName;
            visitProperty (fc.targetFunction);
           --couldBeFunctionName;
            visitProperty (fc.arguments);
        }
    }

    void visit (AST::CallOrCast& cc) override
    {
        if (shouldVisitObject (cc))
        {
            visitProperty (cc.arguments);
            ++couldBeFunctionName;
            visitProperty (cc.functionOrType);
            --couldBeFunctionName;
        }
    }

    void visit (AST::EnumType& e) override
    {
        super::visit (e);

        if (e.items.front().getAsObjectProperty() != nullptr)
        {
            validation::DuplicateNameChecker nameChecker;
            nameChecker.checkList (e.items);

            for (size_t i = 0; i < e.items.size(); ++i)
                e.items.setString (e.items[i].getObjectRef().getName(), i);

            registerChange();
        }
    }

    bool performNameSearch (AST::Expression& nameObject, AST::PooledString name, bool onlyFindNamespaces)
    {
        AST::NameSearch search;
        findAllMatches (search, nameObject, name, onlyFindNamespaces);

        if (handleAmbiguousOrUnknownResult (nameObject, search))
            return false;

        auto& itemFound = search.itemsFound.front().get();

        if (insideProcessor != 0 && (insideFunction != 0 || insideConnection != 0 || insideConnectionIf != 0))
        {
            if (auto endpoint = itemFound.getAsEndpointDeclaration())
            {
                if (endpoint->isInput && ! endpoint->isEvent())
                {
                    auto& read = replaceWithNewObject<AST::ReadFromEndpoint> (nameObject);
                    auto& endpointInstance = read.allocateChild<AST::EndpointInstance>();
                    endpointInstance.endpoint.createReferenceTo (endpoint);
                    read.endpointInstance.createReferenceTo (endpointInstance);
                    return true;
                }
            }
        }

        if (auto v = itemFound.getAsVariableDeclaration())
        {
            replaceObject (nameObject, AST::createVariableReference (nameObject.context, *v));
            validation::checkVariableInitialiserForRecursion (*v);
            return true;
        }

        // this pass searches for functions so we can catch ambiguous names, but leaves it up to
        // the FunctionResolver to actually resolve them
        if (itemFound.getAsFunction() != nullptr)
        {
            registerFailure();
            return false;
        }

        replaceObject (nameObject, AST::createReference (nameObject, itemFound));
        validation::checkExpressionForRecursion (itemFound);
        return true;
    }

    void resolveChildOfModule (AST::NamespaceSeparator& separator, AST::ModuleBase& parentModule)
    {
        AST::NameSearch search;
        search.nameToFind                   = separator.rhs.toString();
        search.stopAtFirstScopeWithResults  = true;
        search.findVariables                = true;
        search.findTypes                    = true;
        search.findFunctions                = false;
        search.findNamespaces               = true;
        search.findProcessors               = true;
        search.findNodes                    = true;
        search.findEndpoints                = true;
        search.onlyFindLocalVariables       = false;

        parentModule.performLocalNameSearch (search, {});

        if (handleAmbiguousOrUnknownResult (separator, search))
            return;

        auto& itemFound = search.itemsFound.front().get();

        if (auto v = itemFound.getAsVariableDeclaration())
        {
            replaceObject (separator, AST::createVariableReference (separator.rhs, *v));
            validation::checkVariableInitialiserForRecursion (*v);
        }
        else
        {
            replaceObject (separator, AST::createReference (separator.rhs, itemFound));
            validation::checkExpressionForRecursion (itemFound);
        }
    }

    void resolveEnumMember (AST::NamespaceSeparator& separator, AST::EnumType& parentEnum)
    {
        auto memberName = AST::castTo<AST::Identifier> (separator.rhs);

        if (memberName == nullptr)
            throwError (separator.rhs, Errors::expectedEnumMember());

        auto nameToFind = memberName->getName();

        for (uint32_t i = 0; i < parentEnum.items.size(); ++i)
        {
            if (parentEnum.items[i].hasName (nameToFind))
            {
                auto& c = separator.context.allocate<AST::ConstantEnum>();
                c.type.referTo (parentEnum);
                c.index = i;
                replaceObject (separator, c);
                return;
            }
        }

        throwError (separator.rhs,
                    Errors::unknownEnumMember (parentEnum.getFullyQualifiedReadableName(), nameToFind));
    }

    void findAllMatches (AST::NameSearch& search, AST::Expression& nameObject,
                         AST::PooledString name, bool onlyFindNamespaces)
    {
        if (name == allocator.strings.rootNamespaceName && onlyFindNamespaces)
        {
            search.addResult (nameObject.getRootNamespace());
            return;
        }
        
        search.nameToFind = name;

        if (auto parentScope = nameObject.getParentScope())
        {
            search.stopAtFirstScopeWithResults  = true;
            search.findVariables                = ! onlyFindNamespaces;
            search.findTypes                    = ! onlyFindNamespaces;
            search.findFunctions                = couldBeFunctionName && ! onlyFindNamespaces;
            search.findNamespaces               = true;
            search.findProcessors               = true;
            search.findNodes                    = ! onlyFindNamespaces;
            search.findEndpoints                = ! onlyFindNamespaces;
            search.onlyFindLocalVariables       = false;

            if (auto parentStatement = parentScope->getAsStatement())
                search.performSearch (*parentStatement, nameObject);
            else
                search.performSearch (nameObject, {});
        }
    }

    bool handleAmbiguousOrUnknownResult (AST::Object& name, AST::NameSearch& search)
    {
        if (search.itemsFound.size() > 1)  { handleAmbiguousName (name, search); return true; }
        if (search.itemsFound.empty())     { handleUnknownName (name, search); return true; }

        return false;
    }

    void handleAmbiguousName (AST::Object& nameObject, AST::NameSearch& search)
    {
        if (registerFailure())
            validation::throwAmbiguousNameError (nameObject.getLocationOfStartOfExpression(),
                                                 search.nameToFind, search.itemsFound);
    }

    void handleUnknownName (AST::Object& nameObject, AST::NameSearch& search)
    {
        if (resolveBuiltInConstant (nameObject, search.nameToFind))
            return;

        //  if this identifier is inside a CallOrCast then leave it for the validator to give a better error message
        if (registerFailure() && ! (visitStackContains<AST::CallOrCast>() || visitStackContains<AST::FunctionCall>()))
        {
            if (! search.findFunctions)
            {
                search.findFunctions = true;
                search.performSearch (nameObject, {});

                if (! search.itemsFound.empty())
                    throwError (nameObject, Errors::didNotExpectFunctionName());
            }

            throwError (nameObject, Errors::unresolvedSymbol (search.nameToFind));
        }
    }

    bool resolveBuiltInConstant (AST::Object& nameObject, std::string_view name)
    {
        if (name == "pi")     return replaceBuiltInConstant<AST::ConstantFloat64> (nameObject, pi);
        if (name == "twoPi")  return replaceBuiltInConstant<AST::ConstantFloat64> (nameObject, twoPi);
        if (name == "nan")    return replaceBuiltInConstant<AST::ConstantFloat32> (nameObject, std::numeric_limits<double>::quiet_NaN());
        if (name == "inf")    return replaceBuiltInConstant<AST::ConstantFloat32> (nameObject, std::numeric_limits<double>::infinity());

        return false;
    }

    template <typename ReplacementType>
    bool replaceBuiltInConstant (AST::Object& target, double value)
    {
        auto& c = replaceWithNewObject<ReplacementType> (target);
        c.value = value;
        return true;
    }

    template <typename MetaFunction, typename MetaFunctionEnum, bool isValueMetaFunction>
    bool resolveMetaFunction (AST::DotOperator& dot, std::string_view name)
    {
        auto index = MetaFunctionEnum::getEnums().getID (name);

        if (index < 0)
            return false;

        auto& mf = dot.context.allocate<MetaFunction>();
        mf.op.setID (index);

        if constexpr (isValueMetaFunction)
            mf.arguments.addChildObject (dot.lhs);
        else
            mf.source.setChildObject (dot.lhs);

        replaceObject (dot, mf);
        return true;
    }

    //==============================================================================
    static ptr<AST::Statement> findBreakOrContinueTarget (AST::Object& breakOrContinue, AST::PooledString requiredName)
    {
        for (auto scope = breakOrContinue.getParentScope(); scope != nullptr; scope = scope->getParentScope())
        {
            if (! requiredName.empty())
                if (auto block = AST::castTo<AST::ScopeBlock> (*scope))
                    if (block->hasName (requiredName))
                        return *block;

            if (auto loop = AST::castTo<AST::LoopStatement> (*scope))
                if (requiredName.empty() || loop->hasName (requiredName))
                    return *loop;
        }

        return {};
    }

    static ptr<AST::Statement> findForwardBranchTarget (ptr<AST::ScopeBlock> scope, AST::PooledString requiredName)
    {
        ptr<AST::Statement> result;

        if (scope)
        {
            scope->visitObjectsInScope ([&] (AST::Object& o)
            {
                if (auto block = AST::castTo<AST::ScopeBlock> (o))
                    if (block->hasName (requiredName))
                        result = block;
            });
        }

        return result;
    }

    template <typename Type>
    void resolveBreakOrContinueTargetName (Type& b, bool isBreak)
    {
        if (b.targetBlock != nullptr)
        {
            if (AST::castTo<AST::ScopeBlock> (b.targetBlock) != nullptr
                 || AST::castTo<AST::LoopStatement> (b.targetBlock) != nullptr)
                return;

            if (auto i = AST::castTo<AST::Identifier> (b.targetBlock))
            {
                auto name = i->name.get();

                if (auto target = findBreakOrContinueTarget (b, name))
                {
                    if (! isBreak && target->getAsLoopStatement() == nullptr)
                        throwError (b, Errors::continueMustBeInsideALoop());

                    b.targetBlock.referTo (*target);
                    registerChange();
                    return;
                }

                throwError (b.targetBlock, Errors::cannotFindBlockLabel (name));
            }

            throwError (b.targetBlock, Errors::expectedBlockLabel());
        }
        else
        {
            if (auto target = findBreakOrContinueTarget (b, {}))
            {
                b.targetBlock.referTo (*target);
                registerChange();
                return;
            }

            throwError (b, isBreak ? Errors::breakMustBeInsideALoop()
                                   : Errors::continueMustBeInsideALoop());
        }
    }

    void visit (AST::BreakStatement& b) override
    {
        resolveBreakOrContinueTargetName (b, true);
    }

    void visit (AST::ContinueStatement& c) override
    {
        resolveBreakOrContinueTargetName (c, false);
    }

    void visit (AST::ForwardBranch& b) override
    {
        super::visit (b);

        for (auto& targetBlock : b.targetBlocks)
        {
            if (auto identifier = AST::castTo<AST::Identifier> (targetBlock))
            {
                auto blockName = identifier->name.get();

                if (auto target = findForwardBranchTarget (AST::castTo<AST::ScopeBlock> (b.getParentScope()), blockName))
                    targetBlock->getAsObjectProperty()->referTo (*target);
                else
                    throwError (targetBlock, Errors::cannotFindForwardJumpTarget (blockName));
            }
            else if (auto block = AST::castTo<AST::ScopeBlock> (targetBlock))
            {
            }
            else
            {
                throwError (targetBlock, Errors::expectedBlockLabel());
            }
        }
    }

    void visit (AST::HoistedEndpointPath&) override
    {
        // the names in the list inside this would be resolved wrongly by this class
    }
};


}
