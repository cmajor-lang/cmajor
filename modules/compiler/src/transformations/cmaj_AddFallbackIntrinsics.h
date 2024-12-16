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

namespace cmaj::transformations
{

//==============================================================================
inline void addFallbackIntrinsics (AST::Program& program,
                                   const std::function<bool(AST::Intrinsic::Type)>& engineSupportsIntrinsic)
{
    struct ReplaceIntrinsicsWithFallbacks  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ReplaceIntrinsicsWithFallbacks (AST::Allocator& a, const std::function<bool(AST::Intrinsic::Type)>& f)
            : super (a), engineSupportsIntrinsic (f) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::FunctionCall& fc) override
        {
            super::visit (fc);

            auto intrin = fc.getIntrinsicType();

            if (intrin != AST::Intrinsic::Type::unknown)
            {
                if (! engineSupportsIntrinsic (intrin))
                {
                    if (auto originalFunction = fc.getTargetFunction())
                    {
                        for (auto& paramType : originalFunction->getParameterTypes())
                            if (! paramType->isPrimitive())
                                return;

                        auto replaceFn = [&] (std::string_view name, uint32_t numArgs)
                        {
                            fc.intrinsicType.reset();
                            auto intrinsicsNamespace = findIntrinsicsNamespace (fc.getRootNamespace());
                            auto ns = intrinsicsNamespace
                                         ->findChildModule (intrinsicsNamespace->getStringPool().get ("internal"))
                                         ->findChildModule (intrinsicsNamespace->getStringPool().get ("math_implementations"));

                            auto fallbackFn = ns->findFunction ([name, numArgs, originalFunction] (const AST::Function& f)
                            {
                                return f.getNumParameters() == numArgs && f.getName().get() == name && f.hasSameParameterTypes (*originalFunction);
                            });

                            fc.targetFunction.referTo (fallbackFn);
                        };

                        switch (intrin)
                        {
                            case AST::Intrinsic::Type::sin:     replaceFn ("sin",   1); break;
                            case AST::Intrinsic::Type::cos:     replaceFn ("cos",   1); break;
                            case AST::Intrinsic::Type::tan:     replaceFn ("tan",   1); break;
                            case AST::Intrinsic::Type::log:     replaceFn ("log",   1); break;
                            case AST::Intrinsic::Type::log10:   replaceFn ("log10", 1); break;
                            case AST::Intrinsic::Type::exp:     replaceFn ("exp",   1); break;
                            case AST::Intrinsic::Type::pow:     replaceFn ("pow",   2); break;
                            case AST::Intrinsic::Type::fmod:    replaceFn ("fmod",  2); break;

                            case AST::Intrinsic::Type::isnan:
                            case AST::Intrinsic::Type::isinf:
                            case AST::Intrinsic::Type::sqrt:
                            case AST::Intrinsic::Type::abs:
                            case AST::Intrinsic::Type::min:
                            case AST::Intrinsic::Type::max:
                            case AST::Intrinsic::Type::floor:
                            case AST::Intrinsic::Type::ceil:
                            case AST::Intrinsic::Type::rint:
                            case AST::Intrinsic::Type::reinterpretFloatToInt:
                            case AST::Intrinsic::Type::reinterpretIntToFloat:
                            case AST::Intrinsic::Type::select:
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
                            case AST::Intrinsic::Type::unknown:
                            default:
                                break;
                        }
                    }
                }
            }
        }

        const std::function<bool(AST::Intrinsic::Type)>& engineSupportsIntrinsic;
    };

    ReplaceIntrinsicsWithFallbacks r (program.allocator, engineSupportsIntrinsic);
    r.visit (program.rootNamespace);
}

}
