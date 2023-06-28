//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include <unordered_map>

#include "../API/cmaj_Engine.h"

namespace cmaj
{

//==============================================================================
/// Used to help with the task of coercing random JSON/ValueView objects into
/// the correct data-type to send to endpoints, without allocating.
struct EndpointTypeCoercionHelperList
{
    void clear()
    {
        inputs.clear();
        outputs.clear();
        inputMappings.clear();
        outputMappings.clear();
        dictionary.reset();
    }

    void initialise (const Engine& engine, uint32_t maxFramesPerBlock, bool addAllInputMappings, bool addAllOutputMappings)
    {
        CMAJ_ASSERT (engine.isLoaded());
        clear();
        dictionary = std::make_unique<Dictionary>();
        initialiseInputs (engine, maxFramesPerBlock, addAllInputMappings);
        initialiseOutputs (engine, maxFramesPerBlock, addAllOutputMappings);
        setScratchPointers (scratchData.data());
    }

    void initialiseDictionary (const Performer& performer)
    {
        getDictionary().owner = performer;
    }

    void initialiseInputs (const Engine& engine, uint32_t maxFramesPerBlock, bool addAllMappings)
    {
        auto inputDetails = engine.getInputEndpoints();
        inputs.resize (inputDetails.endpoints.size());
        size_t index = 0;

        for (auto& input : inputDetails)
        {
            uint32_t maxNumArrayElements = input.isStream() ? std::max (2u, maxFramesPerBlock) : 0;

            ensureScratchSize (inputs[index++].initialise (input, getDictionary(), maxNumArrayElements));

            if (addAllMappings)
                addMapping (input.endpointID.toString(),
                            engine.getEndpointHandle (input.endpointID.toString().c_str()));
        }
    }

    void initialiseOutputs (const Engine& engine, uint32_t maxFramesPerBlock, bool addAllMappings)
    {
        auto outputDetails = engine.getOutputEndpoints();
        outputs.resize (outputDetails.endpoints.size());
        size_t index = 0;

        for (auto& output : outputDetails)
        {
            uint32_t maxNumArrayElements = output.isStream() ? std::max (2u, maxFramesPerBlock) : 0;
            ensureScratchSize (outputs[index++].initialise (output, getDictionary(), maxNumArrayElements));

            if (addAllMappings)
                addMapping (output.endpointID.toString(),
                            engine.getEndpointHandle (output.endpointID.toString().c_str()));
        }
    }

    void setScratchPointers (void* data)
    {
        for (auto& input : inputs)
            input.setScratchPointers (data);

        for (auto& output : outputs)
            output.setScratchPointers (data);
    }

    void addMapping (std::string_view endpointID, EndpointHandle handle)
    {
        for (auto& e : inputs)
        {
            if (e.endpointID.toString() == endpointID)
            {
                inputMappings[handle] = std::addressof (e);
                return;
            }
        }

        for (auto& e : outputs)
        {
            if (e.endpointID.toString() == endpointID)
            {
                outputMappings[handle] = std::addressof (e);
                return;
            }
        }

        CMAJ_ASSERT_FALSE;
    }

    EndpointType getInputEndpointType (EndpointHandle handle)
    {
        if (auto e = getInput (handle))
            return e->endpointType;

        return EndpointType::unknown;
    }

    struct CoercedData
    {
        CoercedData (const void* d, uint32_t s) : data (d), size (s), valid (true) {}
        CoercedData() {}

        const void* data = nullptr;
        uint32_t size = 0;
        bool valid = false;

        operator bool() const       { return valid; }
    };

    struct CoercedDataWithIndex
    {
        CoercedData data;
        uint32_t typeIndex = 0;

        operator bool() const       { return data; }
    };

    CoercedData coerceValue (EndpointHandle handle, const choc::value::ValueView& source)
    {
        if (auto e = getInput (handle))
            if (e->endpointType == EndpointType::value)
                return e->scratchSpaces.front().getCoercedValue (source);

        return {};
    }

    CoercedDataWithIndex coerceValueToMatchingType (EndpointHandle handle, const choc::value::ValueView& source, EndpointType requiredType)
    {
        if (auto e = getInput (handle))
        {
            if (e->endpointType == requiredType)
            {
                if (doesObjectHaveTypeAsProperty (source))
                    return e->coerceValueToMatchingType (convertTypePropertyToObjectType (source));

                return e->coerceValueToMatchingType (source);
            }
        }

        return {};
    }

    CoercedData coerceArray (EndpointHandle handle, const choc::value::ValueView& source, EndpointType requiredType)
    {
        if (auto e = getInput (handle))
        {
            if (e->endpointType == requiredType)
            {
                CMAJ_ASSERT (e->scratchSpaces.size() == 1);
                return e->scratchSpaces.front().getCoercedArray (source);
            }
        }

        return {};
    }

    choc::value::ValueView getViewForOutputArray (EndpointHandle handle, EndpointType requiredType)
    {
        if (auto e = getOutput (handle))
        {
            if (e->endpointType == requiredType)
            {
                return e->scratchValueViews.front();
            }
        }

        return {};
    }

    choc::value::Value getOutputEvents (EndpointHandle handle, Performer& performer)
    {
        if (auto e = getOutput (handle))
        {
            if (e->endpointType == EndpointType::event)
            {
                auto result = choc::value::createEmptyArray();

                performer.iterateOutputEvents (handle, [&] (EndpointHandle h, uint32_t dataTypeIndex,
                                                            uint32_t frameOffset, const void* valueData, uint32_t valueDataSize)
                {
                    auto d = static_cast<const uint8_t*> (valueData);
                    const auto& event = getViewForOutputData (h, dataTypeIndex, { d, d + valueDataSize });

                    result.addArrayElement (choc::value::createObject ("event",
                                                                       "frameOffset", static_cast<int32_t> (frameOffset),
                                                                       "event", event));
                    return true;
                });

                return result;
            }
        }

        return {};
    }

    const choc::value::ValueView& getViewForOutputData (EndpointHandle handle, uint32_t dataTypeIndex, choc::span<const uint8_t> data)
    {
        auto e = getOutput (handle);
        CMAJ_ASSERT (e != nullptr && dataTypeIndex < e->scratchValueViews.size());
        auto& view = e->scratchValueViews[dataTypeIndex];
        CMAJ_ASSERT (data.size() == view.getType().getValueDataSize());
        view.setRawData (const_cast<uint8_t*> (data.data()));
        return view;
    }

private:
    //==============================================================================
    struct ScratchSpace
    {
        void initialise (const choc::value::Type& frameType, const choc::value::Type& viewType,
                         choc::value::StringDictionary& d, uint32_t maxNumArrayElements)
        {
            maxArraySize = maxNumArrayElements;
            type = frameType;
            typeSize = static_cast<uint32_t> (type.getValueDataSize());

            scratchView = choc::value::ValueView (viewType, nullptr,
                                                  type.usesStrings() ? std::addressof (d) : nullptr);
        }

        CoercedData getCoercedValue (const choc::value::ValueView& source)
        {
            if (source.getType() == type)
                return { source.getRawData(), typeSize };

            if (coerceChocValue (scratchView, source))
                return { scratchView.getRawData(), typeSize };

            return {};
        }

        CoercedData getCoercedArray (const choc::value::ValueView& source)
        {
            if (source.getType().getElementType() == type)
                return { source.getRawData(), static_cast<uint32_t> (source.getType().getValueDataSize()) };

            auto sourceSize = source.getType().getNumElements();

            if (sourceSize <= maxArraySize)
                if (coerceChocValue (scratchView, source))
                    return { scratchView.getRawData(), typeSize * sourceSize };

            return {};
        }

        choc::value::Type type;
        choc::value::ValueView scratchView;
        uint32_t typeSize = 0;
        uint32_t maxArraySize = 0;

        template <typename FloatType>
        static FloatType getFloat (const choc::value::ValueView& source)
        {
            if (source.getType().isFloat() || source.getType().isInt())
                return source.get<FloatType>();

            if (source.getType().isString())
            {
                if constexpr (sizeof (FloatType) == 8)
                    return std::strtod (source.get<const char*>(), nullptr);
                else
                    return std::strtof (source.get<const char*>(), nullptr);
            }

            if (source.getType().isBool())
                return source.get<bool>() ? static_cast<FloatType> (1) : FloatType();

            return {};
        }

        template <typename IntType>
        static IntType getInt (const choc::value::ValueView& source)
        {
            if (source.getType().isFloat() || source.getType().isInt())
                return source.get<IntType>();

            if (source.getType().isString())
            {
                if constexpr (sizeof (IntType) == 8)
                    return static_cast<IntType> (std::strtoll (source.get<const char*>(), nullptr, 10));
                else
                    return static_cast<IntType> (std::strtol (source.get<const char*>(), nullptr, 10));
            }

            if (source.getType().isBool())
                return source.get<bool>() ? 1 : 0;

            return {};
        }

        static bool coerceChocValue (const choc::value::ValueView& dest,
                                     const choc::value::ValueView& source)
        {
            auto& destType   = dest.getType();
            auto& sourceType = source.getType();
            auto destData    = const_cast<void*> (dest.getRawData());

            if (sourceType == destType)
            {
                memcpy (destData, source.getRawData(), destType.getValueDataSize());
                return true;
            }

            if (sourceType.isVoid())
                return false;

            if (destType.isFloat32())  { auto v = getFloat<float>  (source);   memcpy (destData, std::addressof (v), sizeof (v)); return true; }
            if (destType.isFloat64())  { auto v = getFloat<double> (source);   memcpy (destData, std::addressof (v), sizeof (v)); return true; }
            if (destType.isInt32())    { auto v = getInt<int32_t>  (source);   memcpy (destData, std::addressof (v), sizeof (v)); return true; }
            if (destType.isInt64())    { auto v = getInt<int64_t>  (source);   memcpy (destData, std::addressof (v), sizeof (v)); return true; }
            if (destType.isBool())     { auto v = getInt<int32_t>  (source);   memcpy (destData, std::addressof (v), sizeof (v)); return true; }
            if (destType.isVoid())     { return true; }

            if (destType.isVector() || destType.isArray())
            {
                auto destNumElements = destType.getNumElements();

                if (sourceType.isArray() || sourceType.isVector())
                {
                    auto sourceNumElements = sourceType.getNumElements();

                    for (uint32_t i = 0; i < destNumElements; ++i)
                    {
                        if (i >= sourceNumElements)
                            dest[i].setToZero();
                        else if (! coerceChocValue (dest[i], source[i]))
                            return false;
                    }

                    return true;
                }

                if (destType.isVectorSize1())
                    return coerceChocValue (dest[0], source);
            }

            if (destType.isObject() && sourceType.isObject())
            {
                for (uint32_t i = 0; i < destType.getNumElements(); ++i)
                {
                    auto& member = destType.getObjectMember (i);

                    if (! coerceChocValue (dest[i], source[member.name]))
                        return false;
                }

                return true;
            }

            return false;
        }
    };

    static choc::value::Type getViewType (const choc::value::Type& frameType, uint32_t maxNumArrayElements)
    {
        if (maxNumArrayElements > 1)
            return choc::value::Type::createArray (frameType, maxNumArrayElements);

        return frameType;
    }

    //==============================================================================
    struct InputEndpoint
    {
        size_t initialise (const EndpointDetails& input,
                           choc::value::StringDictionary& dic,
                           uint32_t maxNumArrayElements)
        {
            endpointID = input.endpointID;
            endpointType = input.endpointType;

            size_t maxDataSize = 0;

            for (auto& type : input.dataTypes)
                maxDataSize = std::max (maxDataSize, getViewType (type, maxNumArrayElements).getValueDataSize());

            scratchSpaces.resize (input.dataTypes.size());

            for (size_t i = 0; i < input.dataTypes.size(); ++i)
                scratchSpaces[i].initialise (input.dataTypes[i],
                                             getViewType (input.dataTypes[i], maxNumArrayElements),
                                             dic, maxNumArrayElements);

            return maxDataSize;
        }

        void setScratchPointers (void* data)
        {
            for (auto& s : scratchSpaces)
                s.scratchView.setRawData (data);
        }

        CoercedDataWithIndex coerceValueToMatchingType (const choc::value::ValueView& source)
        {
            auto numTypes = static_cast<uint32_t> (scratchSpaces.size());

            if (numTypes == 1)
                return { scratchSpaces.front().getCoercedValue (source), 0 };

            for (uint32_t i = 0; i < numTypes; ++i)
                if (scratchSpaces[i].type == source.getType())
                    return { scratchSpaces[i].getCoercedValue (source), i };

            for (uint32_t i = 0; i < numTypes; ++i)
                if (auto coerced = scratchSpaces[i].getCoercedValue (source))
                    return { coerced, i };

            return {};
        }

        EndpointID endpointID;
        EndpointType endpointType;
        std::vector<ScratchSpace> scratchSpaces;
    };

    std::vector<InputEndpoint> inputs;
    std::unordered_map<EndpointHandle, InputEndpoint*> inputMappings;

    InputEndpoint* getInput (EndpointHandle handle) const
    {
        auto e = inputMappings.find (handle);
        return e != inputMappings.end() ? e->second : nullptr;
    }

    //==============================================================================
    struct OutputEndpoint
    {
        size_t initialise (const EndpointDetails& output,
                           choc::value::StringDictionary& dic,
                           uint32_t maxNumArrayElements)
        {
            endpointID = output.endpointID;
            endpointType = output.endpointType;
            scratchValueViews.resize (output.dataTypes.size());
            size_t maxDataSize = 0;

            for (size_t i = 0; i < output.dataTypes.size(); ++i)
            {
                auto& type = output.dataTypes[i];
                frameDataSize = static_cast<uint32_t> (type.getValueDataSize());
                auto arrayType = getViewType (type, maxNumArrayElements);
                maxDataSize = std::max (maxDataSize, arrayType.getValueDataSize());

                scratchValueViews[i] = choc::value::ValueView (arrayType, nullptr,
                                                               type.usesStrings() ? std::addressof (dic) : nullptr);
            }

            return maxDataSize;
        }

        void setScratchPointers (void* data)
        {
            for (auto& v : scratchValueViews)
                v.setRawData (data);
        }

        EndpointID endpointID;
        EndpointType endpointType;
        std::vector<choc::value::ValueView> scratchValueViews;
        uint32_t frameDataSize = 0;
    };

    std::vector<OutputEndpoint> outputs;
    std::unordered_map<EndpointHandle, OutputEndpoint*> outputMappings;
    std::vector<uint8_t> scratchData;

    void ensureScratchSize (size_t size)
    {
        if (scratchData.size() < size)
            scratchData.resize (size);
    }

    OutputEndpoint* getOutput (EndpointHandle handle) const
    {
        auto e = outputMappings.find (handle);
        return e != outputMappings.end() ? e->second : nullptr;
    }

    //==============================================================================
    struct Dictionary  : public choc::value::StringDictionary
    {
        Handle getHandleForString(std::string_view) override { CMAJ_ASSERT_FALSE; return {}; }

        std::string_view getStringForHandle (Handle handle) const override
        {
            return owner.getStringForHandle (handle.handle);
        }

        Performer owner;
    };

    Dictionary& getDictionary()
    {
        CMAJ_ASSERT (dictionary != nullptr);
        return *dictionary;
    }

    std::unique_ptr<Dictionary> dictionary;
};

} // namespace cmaj
