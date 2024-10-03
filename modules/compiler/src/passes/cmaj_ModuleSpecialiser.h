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

struct ModuleSpecialiser  : public Pass
{
    using super = Pass;
    using super::visit;

    ModuleSpecialiser (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    struct SpecialisationArgs;

    // The maximum number of instances of a module that can be cloned to avoid runaway recursion
    static constexpr size_t maxCloneCount = 100;

    ptr<AST::GraphNode> graphNode;

    //==============================================================================
    void visit (AST::IfStatement& o) override
    {
        if (! o.isConst)
        {
            super::visit (o);
        }
    }

    void visit (AST::CallOrCast& cc) override
    {
        super::visit (cc);

        auto args = cc.arguments.getAsObjectList();

        if (! args.empty())
            if (auto specialised = getSpecialisedModuleIfNeeded (cc.functionOrType, args))
                replaceObject (cc, AST::createReference (cc.context, specialised));
    }

    void visit (AST::NamespaceSeparator& ns) override
    {
        super::visit (ns);

        if (auto parentModule = AST::castToSkippingReferences<AST::ModuleBase> (ns.lhs))
            if (parentModule->isAnyParentParameterised())
                if (auto specialised = getSpecialisedModuleIfNeeded (ns.lhs, {}))
                    replaceObject (ns.lhs, AST::createReference (ns.lhs, specialised));
    }

    void visit (AST::GraphNode& node) override
    {
        auto oldNode = graphNode;
        graphNode = node;
        super::visit (node);
        graphNode = oldNode;

        if (auto specialised = getSpecialisedModuleIfNeeded (node.processorType, {}))
        {
            if (auto p = AST::castTo<AST::ProcessorBase> (specialised))
                node.processorType.createReferenceTo (p);
            else
                node.processorType.referTo (*specialised);
        }
    }

    ptr<AST::ModuleBase> getSpecialisedModuleIfNeeded (const AST::ObjectProperty& moduleProperty,
                                                       choc::span<ref<AST::Object>> args)
    {
        if (auto originalModule = AST::castToSkippingReferences<AST::ModuleBase> (moduleProperty))
        {
            if (auto module = specialiseParentsIfNeeded (AST::getContext (moduleProperty), *originalModule, throwOnErrors))
            {
                SpecialisationArgs specialisationArgs (*module, moduleProperty->getName());

                if (specialisationArgs.setArgs (AST::getContext (moduleProperty), args, throwOnErrors))
                {
                    module = getOrCreateSpecialisedModule (*module, specialisationArgs);

                    if (module != nullptr)
                    {
                        if (module != originalModule)
                            registerChange();

                        return module;
                    }
                }
            }

            return {};
        }

        registerFailure();
        return {};
    }

    ptr<AST::ModuleBase> specialiseIfNeeded (const AST::ObjectContext& errorContext,
                                             ptr<AST::ModuleBase> module, bool shouldThrowOnErrors)
    {
        if (module != nullptr)
        {
            module = specialiseParentsIfNeeded (errorContext, *module, shouldThrowOnErrors);

            if (module->isGenericOrParameterised())
            {
                SpecialisationArgs specialisationArgs (*module, module->getName());

                if (specialisationArgs.setArgs (errorContext, {}, shouldThrowOnErrors))
                    return getOrCreateSpecialisedModule (*module, specialisationArgs);
            }

            return module;
        }

        return {};
    }

    ptr<AST::ModuleBase> specialiseParentsIfNeeded (const AST::ObjectContext& errorContext,
                                                    AST::ModuleBase& module, bool shouldThrowOnErrors)
    {
        if (auto parentModule = module.findParentModule())
            if (auto newParent = specialiseIfNeeded (errorContext, parentModule, shouldThrowOnErrors))
                return newParent->findChildModule (module.name);

        return module;
    }

    ptr<AST::ModuleBase> getOrCreateSpecialisedModule (AST::ModuleBase& target, const SpecialisationArgs& args)
    {
        if (! args.areAllArgTypesResolved())
            return {};

        auto specialisedName = args.getSignature();

        if (graphNode && graphNode->getClockMultiplier() != 1.0)
            specialisedName = specialisedName + "_" + std::to_string (graphNode->getClockMultiplier());

        auto specialisedNamePooled = target.getStringPool().get (specialisedName);

        if (auto existingInstance = target.getParentNamespace().findChildModule (specialisedNamePooled))
            return existingInstance;

        checkNumberOfClones (target);

        auto& newInstance = AST::createClonedSiblingModule (target, specialisedName);
        args.applyToTarget (newInstance);
        newInstance.specialisationParams.reset();

        CMAJ_ASSERT (target.getParentNamespace().findChildModule (specialisedNamePooled) == newInstance);
        return newInstance;
    }

    static AST::Function& getFunctionWithSpecialisedParentModules (AST::Program& p, const AST::ObjectContext& errorContext, AST::Function& fn, bool shouldThrowOnErrors)
    {
        if (auto parentModule = fn.findParentModule())
            if (parentModule->isAnyParentParameterised())
                if (auto newParent = ModuleSpecialiser (p).specialiseIfNeeded (errorContext, *parentModule, shouldThrowOnErrors))
                    return *newParent->findFunction (fn.getName(), fn.getNumParameters());

        return fn;
    }

    static void checkNumberOfClones (AST::ModuleBase& target)
    {
        size_t cloneCount = 0;

        for (auto& module : target.getParentNamespace().getSubModules())
            if (module->originalName.get() == target.getName())
                if (++cloneCount > maxCloneCount)
                    throwError (target.context, Errors::tooManyNamespaceInstances (std::to_string (maxCloneCount)));
    }

    //==============================================================================
    void visit (AST::Namespace& n) override     { super::visit (n); validation::checkSpecialisationParams (n); }
    void visit (AST::Processor& p) override     { super::visit (p); validation::checkSpecialisationParams (p); }
    void visit (AST::Graph& g) override         { super::visit (g); validation::checkSpecialisationParams (g); }

    //==============================================================================
    struct SpecialisationArgs
    {
        SpecialisationArgs (AST::ModuleBase& m, std::string_view name)
            : module (m), moduleName (name), params (m.specialisationParams.getAsObjectList())
        {
            for (auto& param : params)
            {
                if (auto defaultValue = validation::getSpecialisationParamDefault (param))
                    defaultArgs.push_back (*defaultValue);
                else
                    ++numRequiredArgs;
            }
        }

        bool setArgs (const AST::ObjectContext& errorContext, choc::span<ref<AST::Object>> args, bool throwOnError)
        {
            if (params.empty() && args.empty())
                return false;

            suppliedArgs = args;
            auto numArgsProvided = args.size();

            if (numArgsProvided > params.size())
                throwWrongNumberOfArgsError (suppliedArgs[params.size()]->context, true);

            if (numArgsProvided < numRequiredArgs)
                throwWrongNumberOfArgsError (errorContext, numArgsProvided != 0);

            for (size_t i = 0; i < params.size(); ++i)
            {
                auto& param = params[i].get();

                if (i >= numArgsProvided)
                {
                    CMAJ_ASSERT (i >= numRequiredArgs);
                    completeArgs.push_back (defaultArgs[i - numRequiredArgs]);
                    continue;
                }

                auto& arg = suppliedArgs[i].get();

                if (auto alias = param.getAsAlias())
                {
                    if (alias->aliasType == AST::AliasTypeEnum::Enum::typeAlias)
                    {
                        if (auto type = AST::castToTypeBase (arg))
                        {
                            completeArgs.push_back (*type);
                            continue;
                        }

                        if (throwOnError && ! arg.isSyntacticObject()) // leave syntax errors for the validator
                            throwError (arg, Errors::expectedType());
                    }
                    else if (alias->aliasType == AST::AliasTypeEnum::Enum::processorAlias)
                    {
                        if (auto processor = AST::castToSkippingReferences<AST::ProcessorBase> (arg))
                        {
                            completeArgs.push_back (*processor);
                            continue;
                        }

                        if (throwOnError && ! arg.isSyntacticObject()) // leave syntax errors for the validator
                            throwError (arg, Errors::expectedProcessorName());
                    }
                    else
                    {
                        CMAJ_ASSERT (alias->aliasType == AST::AliasTypeEnum::Enum::namespaceAlias);

                        if (auto ns = AST::castToSkippingReferences<AST::Namespace> (arg))
                        {
                            completeArgs.push_back (*ns);
                            continue;
                        }

                        if (throwOnError && ! arg.isSyntacticObject()) // leave syntax errors for the validator
                            throwError (arg, Errors::expectedNamespaceName());
                    }

                    return false;
                }

                if (param.isVariableDeclaration())
                {
                    if (auto value = AST::castToValue (arg))
                    {
                        if (auto constant = value->constantFold())
                        {
                            completeArgs.push_back (*constant);
                            continue;
                        }

                        return false;
                    }

                    if (throwOnError)
                        throwError (arg, Errors::expectedValue());

                    return false;
                }

                CMAJ_ASSERT_FALSE;
            }

            return true;
        }

        bool areAllArgTypesResolved() const
        {
            for (auto& arg : suppliedArgs)
                if (auto t = AST::castToTypeBase (arg))
                    if (! t->isResolved())
                        return false;

            return true;
        }

        std::string getSignature() const
        {
            AST::SignatureBuilder sig;
            sig << module.getFullyQualifiedNameWithoutRoot() << "specialised";

            for (auto& arg : completeArgs)
                sig << arg;

            return sig.toString (40);
        }

        void updateTarget (AST::ChildObject& p, AST::Object& o) const
        {
            if (o.isSyntacticObject())
                p.referTo (o);
            else
                p.createReferenceTo (o);
        }

        void applyToTarget (AST::ModuleBase& targetModule) const
        {
            auto& targetParams = targetModule.specialisationParams;
            CMAJ_ASSERT (targetParams.size() == completeArgs.size());

            for (size_t i = 0; i < targetParams.size(); ++i)
            {
                auto& sourceArg = completeArgs[i].get();

                if (auto alias = AST::castTo<AST::Alias> (targetParams[i]))
                {
                    updateTarget (alias->target, sourceArg);
                    targetModule.aliases.addChildObject (*alias);
                    validation::checkAliasTargetType (*alias, true);
                    continue;
                }

                if (auto variable = AST::castToVariableDeclaration (targetParams[i]))
                {
                    auto& cast = sourceArg.context.allocate<AST::Cast>();
                    updateTarget (cast.targetType, variable->declaredType);
                    cast.arguments.addReference (sourceArg);
                    cast.onlySilentCastsAllowed = true;

                    variable->initialValue.setChildObject (cast);
                    variable->isConstant = true;

                    if (auto ns = targetModule.getAsNamespace())
                        ns->constants.addChildObject (*variable);
                    else if (auto p = targetModule.getAsProcessorBase())
                        p->stateVariables.addChildObject (*variable);
                    else
                        CMAJ_ASSERT_FALSE;

                    continue;
                }

                CMAJ_ASSERT_FALSE;
            }
        }

        void throwWrongNumberOfArgsError (const AST::ObjectContext& context, bool anyProvided)
        {
            if (module.isProcessorBase())
                throwError (context, anyProvided ? Errors::wrongNumArgsForProcessor (moduleName)
                                                 : Errors::cannotUseProcessorWithoutArgs (moduleName));

            if (module.isNamespace())
                throwError (context, anyProvided ? Errors::wrongNumArgsForNamespace (moduleName)
                                                 : Errors::cannotUseNamespaceWithoutArgs (moduleName));

            CMAJ_ASSERT_FALSE;
        }

        AST::Object& module;
        std::string moduleName;
        choc::SmallVector<ref<AST::Object>, 8> params, defaultArgs, suppliedArgs, completeArgs;
        uint32_t numRequiredArgs = 0;
    };
};

}
