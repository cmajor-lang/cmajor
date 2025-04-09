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


//==============================================================================
struct Intrinsic
{
    #define CMAJ_INTRINSICS(X) \
        X(abs,                     1,   true,   true  ) \
        X(min,                     2,   true,   true  ) \
        X(max,                     2,   true,   true  ) \
        X(clamp,                   3,   true,   true  ) \
        X(select,                  3,   true,   true  ) \
        X(wrap,                    2,   true,   true  ) \
        X(fmod,                    2,   false,  true  ) \
        X(remainder,               2,   false,  true  ) \
        X(floor,                   1,   false,  true  ) \
        X(ceil,                    1,   false,  true  ) \
        X(rint,                    1,   false,  true  ) \
        X(addModulo2Pi,            2,   false,  true  ) \
        X(sqrt,                    1,   false,  true  ) \
        X(pow,                     2,   false,  true  ) \
        X(exp,                     1,   false,  true  ) \
        X(log,                     1,   false,  true  ) \
        X(log10,                   1,   false,  true  ) \
        X(sin,                     1,   false,  true  ) \
        X(cos,                     1,   false,  true  ) \
        X(tan,                     1,   false,  true  ) \
        X(sinh,                    1,   false,  true  ) \
        X(cosh,                    1,   false,  true  ) \
        X(tanh,                    1,   false,  true  ) \
        X(asinh,                   1,   false,  true  ) \
        X(acosh,                   1,   false,  true  ) \
        X(atanh,                   1,   false,  true  ) \
        X(asin,                    1,   false,  true  ) \
        X(acos,                    1,   false,  true  ) \
        X(atan,                    1,   false,  true  ) \
        X(atan2,                   2,   false,  true  ) \
        X(isnan,                   1,   false,  true  ) \
        X(isinf,                   1,   false,  true  ) \
        X(reinterpretFloatToInt,   1,   false,  true  ) \
        X(reinterpretIntToFloat,   1,   true,   false ) \

    enum class Type
    {
        #define CMAJ_HANDLE_INTRINSIC(name, numArgs, canDoInt, canDoFloat)   name,
        CMAJ_INTRINSICS (CMAJ_HANDLE_INTRINSIC)
        #undef CMAJ_HANDLE_INTRINSIC
        unknown
    };

    static Type getIntrinsicForName (std::string_view nameToFind)
    {
        #define CMAJ_HANDLE_INTRINSIC(name, numArgs, canDoInt, canDoFloat)   if (nameToFind == #name) return Type::name;
        CMAJ_INTRINSICS (CMAJ_HANDLE_INTRINSIC)
        #undef CMAJ_HANDLE_INTRINSIC

        return Type::unknown;
    }

    static std::string_view getIntrinsicName (Type type)
    {
        #define CMAJ_HANDLE_INTRINSIC(name, numArgs, canDoInt, canDoFloat)   if (type == Type::name) return #name;
        CMAJ_INTRINSICS (CMAJ_HANDLE_INTRINSIC)
        #undef CMAJ_HANDLE_INTRINSIC

        CMAJ_ASSERT_FALSE;
        return {};
    }

    using ArgList = choc::span<ref<const ConstantValueBase>>;

    static ptr<ConstantValueBase> constantFoldIntrinsicCall (Type intrinsic, ArgList args)
    {
        CMAJ_ASSERT (intrinsic != Type::unknown);

        if (auto argType = getArgType (args))
        {
            if (argType->isPrimitiveFloat64())   return perform<double, true> (intrinsic, args);
            if (argType->isPrimitiveFloat32())   return perform<float,  true> (intrinsic, args);
            if (argType->isPrimitiveInt64())     return perform<int64_t, false> (intrinsic, args);
            if (argType->isPrimitiveInt32())     return perform<int32_t, false> (intrinsic, args);

            if (auto v = argType->getAsVectorType())
                return performVectorOp (*v, intrinsic, args);
        }

        return {};
    }

private:
    static ptr<const TypeBase> getArgType (ArgList args)
    {
        auto argType = args.front()->getResultType();

        for (size_t i = 1; i < args.size(); ++i)
        {
            if (auto t = args[i]->getResultType())
            {
                if (t->isPrimitiveInt() || t->isPrimitiveFloat())
                {
                    if (TypeRules::getArgumentSuitability (*argType, *t, nullptr) != TypeRules::ArgumentSuitability::impossible)
                        argType = t;

                    continue;
                }
            }

            return {};
        }

        return argType;
    }

    template <typename ArgType, bool isFloat>
    static ptr<ConstantValueBase> perform (Type intrinsic, ArgList args)
    {
        ArgType argArray[4] = {};
        size_t i = 0;

        for (auto& arg : args)
            argArray[i++] = getAsPrimitive<ArgType> (arg);

        auto& allocator = args.front()->context.allocator;

        #define CMAJ_HANDLE_INTRINSIC(name, numArgs, canDoInt, canDoFloat) \
            if constexpr (isFloat ? canDoFloat : canDoInt) \
                if (intrinsic == Type::name && numArgs == args.size()) \
                    return allocator.createConstant (perform_ ## name (argArray));

        CMAJ_INTRINSICS (CMAJ_HANDLE_INTRINSIC)
        #undef CMAJ_HANDLE_INTRINSIC

        return {};
    }

    static ptr<ConstantValueBase> performVectorOp (const VectorType& vectorType, Type intrinsic, ArgList args)
    {
        if (auto size = vectorType.getSize().constantFold())
        {
            if (auto sizeInt64 = size->getAsInt64())
            {
                auto numElements = static_cast<size_t> (*sizeInt64);
                auto& result = vectorType.context.allocator.allocate<ConstantAggregate> (args.front()->context);
                result.type.createReferenceTo (vectorType);
                ObjectRefVector<const ConstantValueBase> elementArgs;

                for (size_t i = 0; i < numElements; ++i)
                {
                    elementArgs.clear();

                    for (auto& arg : args)
                        elementArgs.push_back (castToRef<ConstantAggregate> (arg).getElementValueRef (i));

                    if (auto elementResult = constantFoldIntrinsicCall (intrinsic, elementArgs))
                        result.values.addReference (*elementResult);
                    else
                        return {};
                }

                 return result;
            }
        }

        return {};
    }

    template <typename T> static T modulo (T a, T b)
    {
        if constexpr (std::is_floating_point<T>::value)
            return std::fmod (a, b);
        else
            return a % b;
    }

    template <typename T> static T perform_abs           (const T* args)  { return std::abs (args[0]); }
    template <typename T> static T perform_min           (const T* args)  { return std::min (args[0], args[1]); }
    template <typename T> static T perform_max           (const T* args)  { return std::max (args[0], args[1]); }
    template <typename T> static T perform_select        (const T* args)  { return static_cast<bool> (args[0]) ? args[1] : args[2]; }
    template <typename T> static T perform_clamp         (const T* args)  { return args[0] < args[1] ? args[1] : (args[0] > args[2] ? args[2] : args[0]); }
    template <typename T> static T perform_wrap          (const T* args)  { if (args[1] == 0) return 0; auto n = modulo (args[0], args[1]); if (n < 0) n += args[1]; return n; }
    template <typename T> static T perform_fmod          (const T* args)  { return args[1] != 0 ? std::fmod (args[0], args[1]) : 0; }
    template <typename T> static T perform_remainder     (const T* args)  { return args[1] != 0 ? std::remainder (args[0], args[1]) : 0; }
    template <typename T> static T perform_floor         (const T* args)  { return std::floor (args[0]); }
    template <typename T> static T perform_ceil          (const T* args)  { return std::ceil (args[0]); }
    template <typename T> static T perform_rint          (const T* args)  { return std::rint (args[0]); }
    template <typename T> static T perform_addModulo2Pi  (const T* args)  { auto n = args[0] + args[1]; if (n >= (T) twoPi) n = std::remainder (n, (T) twoPi); return n; }
    template <typename T> static T perform_sqrt          (const T* args)  { return std::sqrt (args[0]); }
    template <typename T> static T perform_pow           (const T* args)  { return std::pow (args[0], args[1]); }
    template <typename T> static T perform_exp           (const T* args)  { return std::exp (args[0]); }
    template <typename T> static T perform_log           (const T* args)  { return std::log (args[0]); }
    template <typename T> static T perform_log10         (const T* args)  { return std::log10 (args[0]); }
    template <typename T> static T perform_sin           (const T* args)  { return std::sin (args[0]); }
    template <typename T> static T perform_cos           (const T* args)  { return std::cos (args[0]); }
    template <typename T> static T perform_tan           (const T* args)  { return std::tan (args[0]); }
    template <typename T> static T perform_sinh          (const T* args)  { return std::sinh (args[0]); }
    template <typename T> static T perform_cosh          (const T* args)  { return std::cosh (args[0]); }
    template <typename T> static T perform_tanh          (const T* args)  { return std::tanh (args[0]); }
    template <typename T> static T perform_asinh         (const T* args)  { return std::asinh (args[0]); }
    template <typename T> static T perform_acosh         (const T* args)  { return std::acosh (args[0]); }
    template <typename T> static T perform_atanh         (const T* args)  { return std::atanh (args[0]); }
    template <typename T> static T perform_asin          (const T* args)  { return std::asin (args[0]); }
    template <typename T> static T perform_acos          (const T* args)  { return std::acos (args[0]); }
    template <typename T> static T perform_atan          (const T* args)  { return std::atan (args[0]); }
    template <typename T> static T perform_atan2         (const T* args)  { return std::atan2 (args[0], args[1]); }

    template <typename T> static bool perform_isnan (const T* args)       { return std::isnan (args[0]) ? 1.0f : 0.0f; }
    template <typename T> static bool perform_isinf (const T* args)       { return std::isinf (args[0]) ? 1.0f : 0.0f; }

    static int32_t perform_reinterpretFloatToInt (const float* args)      { return choc::memory::bit_cast<int32_t> (args[0]); }
    static int64_t perform_reinterpretFloatToInt (const double* args)     { return choc::memory::bit_cast<int64_t> (args[0]); }
    static float   perform_reinterpretIntToFloat (const int32_t* args)    { return choc::memory::bit_cast<float>   (args[0]); }
    static double  perform_reinterpretIntToFloat (const int64_t* args)    { return choc::memory::bit_cast<double>  (args[0]); }
};
