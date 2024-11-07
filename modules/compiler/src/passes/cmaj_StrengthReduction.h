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
struct StrengthReduction  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    StrengthReduction (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::GetArrayOrVectorSlice& o) override
    {
        super::visit (o);

        // If the slice is the entire range, then use the object directly
        if (auto parentSize = o.getParentSize())
            if (auto range = o.getResultRange (*parentSize))
                if (range->start == 0 && range->end == *parentSize)
                    replaceObject (o, o.parent);
    }

    void visit (AST::IfStatement& o) override
    {
        super::visit (o);

        if (auto v = o.condition->getAsConstantBool())
        {
            if (v->getAsBool() == true)
            {
                replaceObject (o, o.trueBranch);
            }
            else if (v->getAsBool() == false)
            {
                if (! o.falseBranch.hasDefaultValue())
                    replaceObject (o, o.falseBranch);
            }
        }
    }
};

}
