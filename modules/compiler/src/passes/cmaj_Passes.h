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

#pragma once

#include "../validation/cmaj_ValidationUtilities.h"

namespace cmaj::passes
{
    //==============================================================================
    struct Pass  : public AST::Visitor
    {
        Pass (AST::Program& p) : AST::Visitor (p.allocator), program (p) {}

        AST::Program& program;
        size_t numReplaced = 0, numFailures = 0;
        bool throwOnErrors = false;

        /// Replaces an object and also registers a change
        void replaceObject (AST::Object& old, AST::Object& replacement)
        {
            if (replacement.isChildOf (old))
                replacement.setParentScope (*old.getParentScope());

            if (std::addressof (old) != std::addressof (replacement))
                if (old.replaceWith (replacement))
                    registerChange();
        }

        template <typename ObjectType>
        ObjectType& replaceWithNewObject (AST::Object& old)
        {
            auto& newObject = old.context.allocate<ObjectType>();
            replaceObject (old, newObject);
            return newObject;
        }

        /// Adds a failure and returns true if an error should be thrown
        bool registerFailure()
        {
            ++numFailures;
            return throwOnErrors;
        }

        void registerChange()
        {
            ++numReplaced;
        }
    };

    //==============================================================================
    struct PassAvoidingGenericFunctionsAndModules  : public Pass
    {
        using Pass::visit;

        PassAvoidingGenericFunctionsAndModules (AST::Program& p) : Pass (p) {}

        bool shouldVisitObject (AST::Object& o) override
        {
            return ! o.isGenericOrParameterised();
        }

        // We want to find failing static assertions as soon as possible, so having this check
        // running in all passes helps to achieve that
        void visit (AST::StaticAssertion& sa) override
        {
            Pass::visit (sa);

            if (insideIfConst > 0)
                return;

            if (auto cond = AST::getAsFoldedConstant (sa.condition))
            {
                if (cond->getAsBool().has_value())
                {
                    validation::throwIfStaticAssertionFails (sa);
                    replaceWithNewObject<AST::NoopStatement> (sa);
                }
            }
        }

        void visit (AST::IfStatement& i) override
        {
            if (i.isConst)
            {
                auto wasThrowing = throwOnErrors;
                throwOnErrors = false;
                ++insideIfConst;
                Pass::visit (i);
                --insideIfConst;
                throwOnErrors = wasThrowing;
            }
            else
            {
                Pass::visit (i);
            }
        }

        int insideIfConst = 0;
    };

    //==============================================================================
    struct PassResult
    {
        size_t numChanges = 0, numFailures = 0;

        PassResult& operator+= (PassResult other)           { numChanges += other.numChanges; numFailures += other.numFailures; return *this; }
        PassResult operator+ (PassResult other) const       { auto p = *this; p += other; return p; }
    };

    template <typename PassType>
    PassResult runPass (AST::Program& program, bool throwOnErrors)
    {
        PassType pass (program);
        pass.throwOnErrors = throwOnErrors;
        pass.visitObject (program.rootNamespace);

        return { pass.numReplaced, pass.numFailures };
    }
}

#include "cmaj_DuplicateNameChecker.h"
#include "cmaj_NameResolver.h"
#include "cmaj_ModuleSpecialiser.h"
#include "cmaj_ConstantFolder.h"
#include "cmaj_FunctionResolver.h"
#include "cmaj_TypeResolver.h"
#include "cmaj_ProcessorResolver.h"
#include "cmaj_EndpointResolver.h"
#include "cmaj_StrengthReduction.h"
#include "cmaj_ExternalResolver.h"
