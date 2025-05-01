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
struct ExternalVariableManager
{
    void setExternalRequestor (EngineInterface::RequestExternalVariableFn fn, void* c)
    {
        context = c;
        requestExternalVariable = fn;
        externals.clear();
    }

    bool addExternalIfNotPresent (VariableDeclaration& v)
    {
        CMAJ_ASSERT (v.getType() != nullptr && v.getType()->isResolved());

        auto type = v.getType();
        auto name = v.getFullyQualifiedReadableName();

        if (externals.find (name) == externals.end())
        {
            externals[name] = {};

            if (requestExternalVariable != nullptr)
            {
                ExternalVariable e { name, type->toChocType(), getAnnotationAsChocValue (v.annotation) };
                requestExternalVariable (context, choc::json::toString (e.toJSON()).c_str());
            }
        }

        if (externals[name].has_value())
            return applyValueToVariableInitialiser (v, externals[name].value());

        return initialiseExternal (v);
    }

    bool setValue (std::string name, choc::value::ValueView value)
    {
        if (externals.find (name) != externals.end())
        {
            externals[name] = value;
            return true;
        }

        return false;
    }

private:
    std::unordered_map<std::string, std::optional<choc::value::Value>> externals;

    EngineInterface::RequestExternalVariableFn requestExternalVariable = nullptr;
    void* context = nullptr;

    static bool initialiseExternal (VariableDeclaration& variable)
    {
        if (variable.initialValue == nullptr)
        {
            if (auto annotation = castTo<Annotation> (variable.annotation))
            {
                auto value = createFromAnnotation (variable.name.toString(), *annotation);

                if (! value.isVoid())
                {
                    if (value.isObject())
                        value = coerceAudioDataToType (castToTypeBaseRef (variable.declaredType).toChocType(), value);

                    return applyValueToVariableInitialiser (variable, value);
                }
            }
        }

        return false;
    }

    static choc::value::Value createFromAnnotation (std::string_view variableName, const Annotation& annotation)
    {
        if (annotation.getBoolFlag ("sinewave") || annotation.getBoolFlag ("sine"))         return renderWave<choc::oscillator::Sine<double>>     (variableName, 1, annotation);
        if (annotation.getBoolFlag ("square")   || annotation.getBoolFlag ("squarewave"))   return renderWave<choc::oscillator::Square<double>>   (variableName, 2, annotation);
        if (annotation.getBoolFlag ("sawtooth") || annotation.getBoolFlag ("saw"))          return renderWave<choc::oscillator::Saw<double>>      (variableName, 2, annotation);
        if (annotation.getBoolFlag ("triangle"))                                            return renderWave<choc::oscillator::Triangle<double>> (variableName, 2, annotation);

        if (auto defaultProp = getAnnotationProperty (annotation, "default", false))
            return defaultProp->toValue (nullptr);

        return {};
    }

    static bool applyValueToVariableInitialiser (VariableDeclaration& variable, choc::value::ValueView value)
    {
        auto& variableType = castToTypeBaseRef (variable.declaredType);
        auto coerced = coerceAudioDataToType (variableType.toChocType(), value);

        auto& constValue = variableType.allocateConstantValue (variable.context);

        if (! constValue.setFromValue (coerced))
            throwError (variable, Errors::cannotApplyExternalVariableValue (value.getType().getDescription(), variable.getName()));

        variable.initialValue.referTo (constValue);
        variable.isExternal = false;
        variable.isConstant = true;

        auto& makeConst = variable.context.allocate<AST::MakeConstOrRef>();
        makeConst.source.referTo (variable.declaredType);
        makeConst.makeConst = true;
        variable.declaredType.referTo (makeConst);

        return true;
    }

    static constexpr int64_t maxNumFrames = 100000000;
    static constexpr double maxFrequency = 10000000.0;
    static constexpr double maxRate = 10000000.0;

    //==============================================================================
    static bool isSampleRateName (std::string_view name)         { return name == "frequency" || name == "rate" || name == "sampleRate"; }
    static bool isSampleRateType (const choc::value::Type& type) { return type.isFloat() || type.isInt(); }
    static bool isFrameArray (const choc::value::Type& type)     { return type.isUniformArray() && isAudioSampleType (type.getElementType()); }

    static bool isAudioSampleType (const choc::value::Type& type)
    {
        if (type.isPrimitive())
            return type.isFloat32() || type.isFloat64() || type.isInt32();

        return (type.isVector() || type.isArray()) && isAudioSampleType (type.getElementType());
    }

    static bool isFrameTypeFloat32 (const choc::value::Type& type)
    {
        return type.isFloat32() || ((type.isVector() || type.isArray()) && type.getElementType().isFloat32());
    }

    template <typename OscillatorType>
    static choc::value::Value renderWave (std::string_view variableName, uint32_t oversamplingFactor,
                                          const Annotation& annotation)
    {
        return renderWave<OscillatorType> (oversamplingFactor,
                                           getValue<double> (variableName, annotation, "frequency", maxFrequency),
                                           getValue<double> (variableName, annotation, "rate", maxRate),
                                           getNumFrames (variableName, annotation));
    }

    static ptr<ConstantValueBase> getAnnotationProperty (const Annotation& annotation,
                                                         std::string_view name, bool throwErrorIfMissing)
    {
        if (auto prop = annotation.findProperty (name))
        {
            if (auto c = prop->constantFold())
                return *c;

            throwError (*prop, Errors::expectedConstant());
        }

        if (throwErrorIfMissing)
            throwError (annotation, Errors::missingExternalGeneratorProperty (name));

        return {};
    }

    template <typename Type>
    static Type getValue (std::string_view variableName, const Annotation& annotation,
                          std::string_view propertyName, Type maxValue)
    {
        auto prop = getAnnotationProperty (annotation, propertyName, true);
        auto asPrimitive = prop->castToPrimitive<Type>();

        if (! asPrimitive.has_value())
            throwError (*prop, Errors::expectedValue());

        auto n = *asPrimitive;

        if (n <= 0 || n > maxValue)
            throwError (*prop, Errors::illegalExternalGeneratorProperty (propertyName, variableName));

        return n;
    }

    static int64_t getNumFrames (std::string_view variableName, const Annotation& annotation)
    {
        if (annotation.findProperty ("frames") != nullptr)
            return getValue<int64_t> (variableName, annotation, "frames", maxNumFrames);

        if (annotation.findProperty ("numFrames") != nullptr)
            return getValue<int64_t> (variableName, annotation, "numFrames", maxNumFrames);

        throwError (annotation, Errors::missingExternalGeneratorProperty ("numFrames"));
        return 0;
    }

    template <typename OscillatorType>
    static choc::value::Value renderWave (uint32_t oversamplingFactor, double frequency, double sampleRate, int64_t numFrames)
    {
        auto data = choc::oscillator::createChannelArray<OscillatorType, float> ({ 1u, (uint32_t) (numFrames * oversamplingFactor) },
                                                                                 frequency, sampleRate * oversamplingFactor);

        if (oversamplingFactor == 1)
            return convertAudioDataToObject (data, sampleRate);

        // Resample to the right size
        choc::buffer::ChannelArrayBuffer<float> resampledData (1, (uint32_t) numFrames, false);
        choc::interpolation::sincInterpolate (resampledData, data);
        return convertAudioDataToObject (resampledData, sampleRate);
    }


    template <typename SampleType>
    static choc::buffer::InterleavedBuffer<SampleType> convertArrayToFrameBuffer (const choc::value::ValueView& array)
    {
        choc::buffer::InterleavedBuffer<SampleType> dest;

        CMAJ_ASSERT (array.getType().isUniformArray());
        auto frameType = array.getType().getElementType();
        auto numChannels = frameType.getNumElements();
        auto numFrames = array.size();

        dest.resize ({ numChannels, numFrames });

        if (isFrameTypeFloat32 (frameType))
            copy (dest, choc::buffer::createInterleavedViewFromValue<float> (array));
        else
            copy (dest, choc::buffer::createInterleavedViewFromValue<double> (array));

        return dest;
    }

    template <typename TargetSampleType>
    static choc::value::Value coerceAudioFrameArray (const choc::value::ValueView& sourceArray, uint32_t numDestChans)
    {
        auto buffer = convertArrayToFrameBuffer<TargetSampleType> (sourceArray);
        auto numSourceChans = buffer.getNumChannels();

        if (numSourceChans != numDestChans)
        {
            buffer.resize ({ numDestChans, buffer.getNumFrames() });

            for (uint32_t i = numSourceChans; i < numDestChans; ++i)
                copy (buffer.getChannel (i),
                      buffer.getChannel (numSourceChans - 1));
        }

        return choc::value::Value (choc::buffer::createValueViewFromBuffer (buffer.getView()));
    }

    static choc::value::Value coerceAudioFrameArray (const choc::value::Type& targetFrameType, choc::value::Value&& sourceArray)
    {
        auto sourceFrameType = sourceArray.getType().getElementType();

        if (targetFrameType != sourceFrameType)
        {
            auto targetSampleType = (targetFrameType.isVector() || targetFrameType.isArray()) ? targetFrameType.getElementType() : targetFrameType;
            auto targetNumChans = targetFrameType.getNumElements();

            if (targetSampleType.isFloat32())  return coerceAudioFrameArray<float> (sourceArray, targetNumChans);
            if (targetSampleType.isFloat64())  return coerceAudioFrameArray<double> (sourceArray, targetNumChans);
        }

        return std::move (sourceArray);
    }

    static choc::value::Value coerceAudioDataToType (const choc::value::Type& targetType, const choc::value::ValueView& sourceValue)
    {
        if (sourceValue.isObject())
        {
            choc::value::Value frames, rate;

            for (uint32_t i = 0; i < sourceValue.size(); ++i)
            {
                auto member = sourceValue.getObjectMemberAt (i);

                if (isSampleRateName (member.name) && isSampleRateType (member.value.getType()))
                    rate = member.value;
                else if (isFrameArray (member.value.getType()))
                    frames = member.value;
            }

            if (! (frames.isVoid() && rate.isVoid()))
            {
                if (targetType.isArray())
                    return frames;

                if (targetType.isObject())
                {
                    auto o = choc::value::createObject ("AudioSample");

                    for (uint32_t i = 0; i < targetType.getNumElements(); ++i)
                    {
                        auto member = targetType.getObjectMember(i);

                        if (isSampleRateName (member.name) && isSampleRateType (member.type))
                            o.setMember (member.name, rate);
                        else if (isFrameArray (member.type))
                            o.setMember (member.name, coerceAudioFrameArray (member.type.getElementType(), std::move (frames)));
                    }

                    return o;
                }
            }

            if (targetType.isObject())
            {
                choc::value::Value coerced (targetType);

                for (uint32_t i = 0; i < coerced.size(); ++i)
                {
                    auto member = coerced.getObjectMemberAt (i);

                    if (sourceValue.hasObjectMember (member.name))
                        coerced.setMember (member.name, coerceAudioDataToType (member.value.getType(), sourceValue[member.name]));
                }

                return coerced;
            }
        }

        if (sourceValue.isArray() && targetType.isArray() && targetType.getNumElements() != 0)
        {
            return choc::value::createArray (sourceValue.size(), [&] (uint32_t i)
            {
                return coerceAudioDataToType (targetType.getArrayElementType (std::min (i, targetType.getNumElements() - 1)), sourceValue[i]);
            });
        }

        return choc::value::Value (sourceValue);
    }
};

//==============================================================================
struct ExternalFunctionManager
{
    void setExternalRequestor (EngineInterface::RequestExternalFunctionFn fn, void* c)
    {
        requestExternalFunction = fn;
        context = c;
        functionPointers.clear();
    }

    void addFunctionIfNotPresent (const Function& f)
    {
        if (functionPointers.find (std::addressof (f)) != functionPointers.end())
            return;

        auto& fn = functionPointers[std::addressof (f)];

        if (requestExternalFunction != nullptr)
        {
            auto paramTypes = choc::value::createEmptyArray();

            for (auto& p : f.getParameterTypes())
                paramTypes.addArrayElement (p->toChocType().toValue());

            if (auto resolved = requestExternalFunction (context,
                                                         f.getFullyQualifiedReadableName().c_str(),
                                                         choc::json::toString (paramTypes, false).c_str()))
                fn = resolved;
        }
    }

    void addFunctionWithImplementation (const Function& f, void* nativeFunction)
    {
        auto& fn = functionPointers[std::addressof (f)];
        CMAJ_ASSERT (fn == nullptr || fn == nativeFunction);
        fn = nativeFunction;
    }

    void* findResolvedFunction (const Function& f) const
    {
        if (auto fn = functionPointers.find (std::addressof (f)); fn != functionPointers.end())
            return fn->second;

        return {};
    }

private:
    std::unordered_map<const Function*, void*> functionPointers;
    EngineInterface::RequestExternalFunctionFn requestExternalFunction = nullptr;
    void* context = nullptr;
};
