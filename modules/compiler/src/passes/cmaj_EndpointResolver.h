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
struct EndpointResolver  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    EndpointResolver (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    AST::PooledString consoleEndpointName { program.allocator.strings.consoleEndpointName };

    //==============================================================================
    void visit (AST::WriteToEndpoint& w) override
    {
        super::visit (w);

        auto endpoint = AST::castToSkippingReferences<AST::EndpointDeclaration> (w.target);

        if (endpoint == nullptr)
        {
            if (auto bracket = AST::castToSkippingReferences<AST::BracketedSuffix> (w.target))
            {
                if (bracket->terms.size() == 1)
                {
                    auto& term = AST::castToRef<AST::BracketedSuffixTerm> (bracket->terms[0]);

                    if (term.startIndex != nullptr && term.endIndex == nullptr)
                    {
                        w.target.referTo (bracket->parent);
                        w.targetIndex.referTo (term.startIndex);
                        registerChange();
                    }
                }
            }

            if (auto i = AST::castTo<AST::Identifier> (w.target))
            {
                if (i->hasName (consoleEndpointName))
                {
                    auto parentProcessor = findTopVisitedItemOfType<AST::ProcessorBase>();

                    if (parentProcessor == nullptr)
                        throwError (w, Errors::unimplementedFeature ("Writing to the console from a free function"));

                    endpoint = getOrCreateConsoleEndpoint (*parentProcessor);
                    w.target.createReferenceTo (*endpoint);
                    registerChange();
                }
            }
        }

        if (endpoint != nullptr && endpoint->hasName (consoleEndpointName))
        {
            if (auto value = AST::castToValue (w.value))
            {
                if (auto type = value->getResultType())
                {
                    addConsoleTypeIfNotPresent (*endpoint, *type);

                    if (type->isBoundedType())
                    {
                        auto& cast = w.allocateChild<AST::Cast> ();
                        cast.targetType.referTo (endpoint->context.allocator.createInt32Type());
                        cast.arguments.addReference (w.value);
                        w.value.referTo (cast);
                    }

                    return;
                }
            }

            registerFailure();
        }
    }

    void visit (AST::GraphNode& n) override
    {
        super::visit (n);

        if (auto childProcessor = n.getProcessorType())
        {
            if (auto childEndpoint = childProcessor->findEndpointWithName (consoleEndpointName))
            {
                auto& processor = n.getParentProcessor();
                auto& outerEndpoint = getOrCreateConsoleEndpoint (processor);

                for (auto& type : childEndpoint->dataTypes)
                    if (auto t = AST::castToTypeBase (type))
                        addConsoleTypeIfNotPresent (outerEndpoint, *t);

                if (auto graph = processor.getAsGraph())
                {
                    if (! isConnected (*graph, n, *childEndpoint, outerEndpoint))
                    {
                        AST::addConnection (*graph, n, *childEndpoint, nullptr, outerEndpoint);
                        registerChange();
                    }
                }
                else
                {
                    throwError (n, Errors::unimplementedFeature ("Writing to console from a composite processor"));
                }
            }

            return;
        }

        registerFailure();
    }

    void visit (AST::Connection& c) override
    {
        super::visit (c);

        for (auto dest : c.dests.getAsObjectList())
        {
            if (auto i = dest->getAsIdentifier())
            {
                if (i->hasName (consoleEndpointName))
                {
                    auto parentProcessor = findTopVisitedItemOfType<AST::ProcessorBase>();

                    getOrCreateConsoleEndpoint (*parentProcessor);
                    registerChange();
                }
            }
            else if (auto endpoint = dest->getAsEndpointInstance())
            {
                if (auto declaration = endpoint->getEndpoint (true))
                {
                    if (declaration->hasName (consoleEndpointName))
                    {
                        auto types = getSourceEndpointTypes (c);

                        for (auto type : types)
                            addConsoleTypeIfNotPresent (const_cast<AST::EndpointDeclaration&> (*declaration), type);
                    }
                }
            }
        }
    }

    static void addIfNotPresent (AST::ObjectRefVector<const AST::TypeBase>& target, const AST::ObjectRefVector<const AST::TypeBase>& items)
    {
        for (auto t : items)
        {
            bool duplicate = false;

            for (auto n : target)
                if (t->isSameType (n, AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                    duplicate = true;

            if (! duplicate)
                target.push_back (t);
        }
    }

    static AST::ObjectRefVector<const AST::TypeBase> getSourceEndpointTypes (AST::Object& source)
    {
        if (auto sourceConnection = source.getAsConnection())
        {
            AST::ObjectRefVector<const AST::TypeBase> result;

            for (auto s : sourceConnection->sources.getAsObjectList())
                addIfNotPresent (result, getSourceEndpointTypes (s));

            return result;
        }
        else if (auto instance = source.getAsEndpointInstance())
        {
            if (auto declaration = instance->getEndpoint (true))
                return declaration->getDataTypes (false);
        }

        return {};
    }

    void visit (AST::BinaryOperator& op) override
    {
        super::visit (op);

        auto seemsToBeOldStyleWriteToConsole = [this] (const AST::BinaryOperator& b) -> bool
        {
            if (b.op == AST::BinaryOpTypeEnum::Enum::leftShift)
            {
                if (auto i = AST::castTo<AST::Identifier> (b.lhs))
                    return i->hasName (consoleEndpointName);

                return AST::castToSkippingReferences<AST::EndpointDeclaration> (b.lhs) != nullptr;
            }

            return false;
        };

        if (seemsToBeOldStyleWriteToConsole (op))
            throwError (op, Errors::seemsToBeOldStyleEndpointWrite());
    }

    static bool isConnected (AST::Graph& graph, AST::GraphNode& sourceNode, AST::EndpointDeclaration& sourceOutput, AST::EndpointDeclaration& destOutput)
    {
        for (auto& c : graph.connections)
            if (auto conn = AST::castToSkippingReferences<AST::Connection> (c))
                if (isConnectionFrom (*conn, sourceNode, sourceOutput) && isConnectionTo (*conn, destOutput))
                    return true;

        return false;
    }

    static bool isConnectionFrom (const AST::Connection& conn, const AST::GraphNode& sourceNode, const AST::EndpointDeclaration& sourceOutput)
    {
        for (auto& source : conn.sources)
        {
            if (AST::castToSkippingReferences<AST::Connection> (source) != nullptr)
                continue;

            ptr<AST::EndpointInstance> sourceEndpoint;

            if (auto element = AST::castToSkippingReferences<AST::GetElement> (source))
                sourceEndpoint = AST::castToSkippingReferences<AST::EndpointInstance> (element->parent);
            else
                sourceEndpoint = AST::castToSkippingReferences<AST::EndpointInstance> (source);

            if (sourceEndpoint != nullptr)
                if (AST::castToSkippingReferences<AST::GraphNode> (sourceEndpoint->node) == sourceNode)
                    if (AST::castToSkippingReferences<AST::EndpointDeclaration> (sourceEndpoint->endpoint) == sourceOutput)
                        return true;
        }

        return false;
    }

    static bool isConnectionTo (const AST::Connection& conn, const AST::EndpointDeclaration& destOutput)
    {
        for (auto& dest : conn.dests)
        {
            ptr<AST::EndpointInstance> destEndpoint;

            if (auto element = AST::castToSkippingReferences<AST::GetElement> (dest))
                destEndpoint = AST::castToSkippingReferences<AST::EndpointInstance> (element->parent);
            else
                destEndpoint = AST::castToSkippingReferences<AST::EndpointInstance> (dest);

            if (destEndpoint != nullptr)
                if (AST::castToSkippingReferences<AST::EndpointDeclaration> (destEndpoint->endpoint) == destOutput)
                    return true;
        }

        return false;
    }

    AST::EndpointDeclaration& getOrCreateConsoleEndpoint (AST::ProcessorBase& processor)
    {
        if (auto e = processor.findEndpointWithName (consoleEndpointName))
            return *e;

        auto& e = processor.allocateChild<AST::EndpointDeclaration>();
        e.name = consoleEndpointName;
        e.isInput = false;
        e.endpointType = AST::EndpointTypeEnum::Enum::event;
        processor.endpoints.addReference (e);
        registerChange();
        return e;
    }

    void addConsoleTypeIfNotPresent (AST::EndpointDeclaration& endpoint, const AST::TypeBase& type)
    {
        if (! type.isResolved())
        {
            registerFailure();
            return;
        }

        auto& typeToAdd = type.isBoundedType() ? endpoint.context.allocator.createInt32Type() : type;

        for (auto t : endpoint.getDataTypes())
            if (t->isSameType (typeToAdd, AST::TypeBase::ComparisonFlags::ignoreReferences
                                          | AST::TypeBase::ComparisonFlags::ignoreConst))
                return;

        endpoint.dataTypes.addReference (AST::createReference (endpoint, typeToAdd.skipConstAndRefModifiers()));
        registerChange();
    }
};

}
