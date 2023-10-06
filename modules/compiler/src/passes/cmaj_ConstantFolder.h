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
struct ConstantFolder  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    ConstantFolder (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void fold (AST::ValueBase& v)
    {
        if (auto folded = v.constantFold())
        {
            if (folded != v)
            {
                if (folded->context.location != nullptr && folded->context.location != v.context.location)
                {
                    auto& clone = folded->createDeepClone (folded->context.allocator);
                    clone.context.location = v.context.location;
                    replaceObject (v, clone);
                    return;
                }

                folded->context.location = v.context.location;
                replaceObject (v, *folded);
            }
        }
    }

    void fold (AST::ObjectProperty& p)
    {
        if (auto v = AST::castToValue (p))
            fold (*v);
    }

    void fold (AST::ListProperty& list)
    {
        for (auto& i : list)
            if (auto v = AST::castToValue (i))
                fold (*v);
    }

    void visit (AST::ValueMetaFunction& m) override
    {
        super::visit (m);
        fold (m);
        convertMetafunctionValueToType (*m.arguments.front().getAsObjectProperty(), m.isGetSizeOp());
    }

    void visit (AST::TypeMetaFunction& m) override
    {
        super::visit (m);
        convertMetafunctionValueToType (m.source, false);
    }

    void convertMetafunctionValueToType (AST::ObjectProperty& source, bool dontFoldSlices)
    {
        if (auto asValue = AST::castToValue (source))
        {
            auto type = asValue->getResultType();

            if (type != nullptr && type->isResolved() && ! (dontFoldSlices && type->isSlice()))
            {
                // do not use replaceObject here, as we don't want to replace all instances of a value with its type
                source.referTo (*type);
                registerChange();
                return;
            }

            registerFailure();
        }
    }

    void visit (AST::Cast& c) override
    {
        super::visit (c);

        if (auto targetType = AST::castToTypeBase (c.targetType))
        {
            if (targetType->isResolved() && targetType->isVoid())
                throwError (c, Errors::targetCannotBeVoid());

            fold (c);
        }
    }

    void visit (AST::UnaryOperator& o) override      { super::visit (o); fold (o); }
    void visit (AST::BinaryOperator& o) override     { super::visit (o); fold (o); }
    void visit (AST::TernaryOperator& o) override    { super::visit (o); fold (o); }
    void visit (AST::GetElement& o) override         { super::visit (o); fold (o.indexes); fold (o); }
    void visit (AST::GetArraySlice& o) override      { super::visit (o); fold (o.start); fold (o.end); fold (o); }
    void visit (AST::GetStructMember& o) override    { super::visit (o); fold (o); }
    void visit (AST::FunctionCall& o) override       { super::visit (o); fold (o); }
    void visit (AST::VectorType& o) override         { super::visit (o); fold (o.numElements); }
    void visit (AST::ArrayType& o) override          { super::visit (o); fold (o.dimensionList); o.resetCachedElementType(); }
    void visit (AST::GraphNode& n) override          { super::visit (n); fold (n.arraySize); }

    void visit (AST::IfStatement& i) override
    {
        super::visit (i);

        if (i.isConst)
        {
            if (auto c = AST::getAsFoldedConstant (i.condition))
            {
                if (auto value = c->getAsBool())
                {
                    if (*value)
                    {
                        replaceObject (i, i.trueBranch);
                    }
                    else
                    {
                        if (i.falseBranch != nullptr)
                            replaceObject (i, i.falseBranch);
                        else
                            replaceWithNewObject<AST::NoopStatement> (i);
                    }
                }
            }
        }
    }

    void visit (AST::ConnectionIf& connection) override
    {
        super::visit (connection);

        if (auto c = AST::getAsFoldedConstant (connection.condition))
        {
            if (auto value = c->getAsBool())
            {
                if (*value)
                {
                    replaceObject (connection, connection.trueConnections);
                }
                else
                {
                    if (connection.falseConnections != nullptr)
                    {
                        replaceObject (connection, connection.falseConnections);
                    }
                    else
                    {
                        replaceWithNewObject<AST::NoopStatement> (connection);
                    }
                }
            }
        }
    }
};

}
