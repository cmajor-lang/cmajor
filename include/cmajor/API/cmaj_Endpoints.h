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

#include "../../choc/containers/choc_SmallVector.h"
#include "../../choc/containers/choc_COM.h"
#include "../../choc/text/choc_JSON.h"
#include "../../choc/audio/choc_MIDI.h"


namespace cmaj
{

//==============================================================================
enum class EndpointType
{
    unknown  = 0,
    stream   = 1,
    value    = 2,
    event    = 3
};

std::string_view getEndpointTypeName (EndpointType);

//==============================================================================
/// This set of endpoint categories are used to hint to the host on what the
/// endpoint seems to be designed to do.
/// See EndpointDetails::getSuggestedPurpose()
enum class EndpointPurpose
{
    unknown,
    console,
    audioIn,
    audioOut,
    midiIn,
    midiOut,
    parameterControl,
    timeSignature,
    tempo,
    transportState,
    timelinePosition
};

std::string_view getEndpointPurposeName (EndpointPurpose);

constexpr std::string_view getConsoleEndpointID()       { return "console"; }

/// Helper functions to deal with getting MIDI in and out of endpoints.
namespace MIDIEvents
{
    int32_t midiMessageToPackedInt (choc::midi::ShortMessage);

    choc::midi::ShortMessage packedMIDIDataToMessage (int32_t packed);

    bool isMIDIMessageType (const choc::value::Type&);

    choc::value::Value createMIDIMessageObject (choc::midi::ShortMessage);
}

//==============================================================================
/// This class holds an endpoint ID, which is basically a string containing the
/// endpoint name that was declared in the program.
struct EndpointID
{
    static EndpointID create (std::string s)            { EndpointID i; i.ID = std::move (s); return i; }
    static EndpointID create (std::string_view s)       { return create (std::string (s)); }

    const std::string& toString() const                 { return ID; }
    operator const char*() const                        { return ID.c_str(); }

    operator bool() const                               { return ! ID.empty(); }
    bool isValid() const                                { return ! ID.empty(); }

    bool operator== (const EndpointID& other) const     { return other.ID == ID; }
    bool operator!= (const EndpointID& other) const     { return other.ID != ID; }

private:
    std::string ID;

    template <typename Type> operator Type() const = delete;
};

//==============================================================================
/// This class holds the set of things that are known about a given endpoint.
/// You'll get a set of these in the form of an EndpointDetailsList by calling
/// various methods on a cmaj::Engine object.
struct EndpointDetails
{
    EndpointID endpointID;
    EndpointType endpointType;
    bool isInput = false;

    /// The types of the frames or events that this endpoint uses.
    /// For an event endpoint, there may be multiple data types for the different
    /// event types it can handle. For streams and values, there should be exactly
    /// one type in this array.
    choc::SmallVector<choc::value::Type, 2> dataTypes;
    choc::value::Value annotation;

    /// If known, this will contain the location of the endpoint's declaration
    std::string sourceFileLocation;

    bool isOutput() const       { return ! isInput; }
    bool isStream() const       { return endpointType == EndpointType::stream; }
    bool isEvent() const        { return endpointType == EndpointType::event; }
    bool isValue() const        { return endpointType == EndpointType::value; }

    /// Attempts to make an informed guess about what kind of purpose the author
    /// intended this endpoint to be used for.
    EndpointPurpose getSuggestedPurpose() const;

    /// Checks whether this endpoint has the name of the standard console output
    bool isConsole() const;

    /// Looks at the type of data that this endpoint uses, and returns true if it
    /// seems to be passing MIDI event objects
    bool isMIDI() const;

    /// Looks at the type of data that this endpoint uses, and if it's a floating
    /// point or vector, returns the number of channels. If it doesn't seem to be
    /// audio data, then this will just return 0.
    uint32_t getNumAudioChannels() const;

    /// Uses some heuristics to make a guess at whether this endpoint appears
    /// to be a plugin parameter
    bool isParameter() const;

    /// Attempts to say whether this endpoint is passing some kind of timeline data
    bool isTimeline() const;

    /// Returns true if this endpoint seems to be dealing with time signature objects
    bool isTimelineTimeSignature() const;

    /// Returns true if this endpoint seems to be dealing with tempo objects
    bool isTimelineTempo() const;

    /// Returns true if this endpoint seems to be dealing with transport
    /// state change objects
    bool isTimelineTransportState() const;

    /// Returns true if this endpoint seems to be dealing with timeline positions
    bool isTimelinePosition() const;

    //==============================================================================
    /// Creates a JSON reporesentation of the endpoint's properties
    choc::value::Value toJSON (bool includeSourceLocation) const;

    /// Creates an EndpointDetails object from a JSON representation that was
    /// creates with EndpointDetails::toJSON(). This may throw an exception if
    /// the JSON format isn't correct.
    static EndpointDetails fromJSON (const choc::value::ValueView&, bool isInput);
};

//==============================================================================
/// A set of EndpointDetails objects for each endpoint that a program exposes.
/// To get an EndpointDetailsList, see the methods in cmaj::Engine.
struct EndpointDetailsList
{
    /// Returns a JSON string containing the items in this list.
    std::string getDescription() const;

    /// Serialises the list into a JSON object
    choc::value::Value toJSON (bool includeSourceLocations) const;

    /// Creates a list from some JSON that was created by the toJSON() method,
    /// returning an empty list if the JSON was invalid.
    static EndpointDetailsList fromJSON (const choc::value::ValueView& json, bool isInput);

    // Thsee let you iterate each EndpointDetails object in the list
    auto begin() const      { return endpoints.begin(); }
    auto end() const        { return endpoints.end(); }

    size_t size() const     { return endpoints.size(); }

    std::vector<EndpointDetails> endpoints;
};

//==============================================================================
/// Helper function to interpret a console message value as a readable string.
std::string convertConsoleMessageToString (const choc::value::ValueView&);

//==============================================================================
/// Adds a special "_type" member to an object with a representation of its type
choc::value::Value addTypeToValueAsProperty (const choc::value::ValueView&);

/// Checks for our special "_type" property
bool doesObjectHaveTypeAsProperty (const choc::value::ValueView&);

/// Applies a special "_type" property from an ojbect to its own type
choc::value::Value convertTypePropertyToObjectType (const choc::value::ValueView&);

//==============================================================================
/// This helper class implements the logic for interpreting an EndpointDetails
/// object as a set of more parameter-focued values.
struct PatchParameterProperties
{
    PatchParameterProperties (const EndpointDetails&);

    float minValue = 0, maxValue = 0, step = 0, defaultValue = 0;
    std::string name, unit, group;
    std::vector<std::string> valueStrings;
    bool isEvent = false, boolean = false, automatable = false, hidden = false;
    uint64_t numSteps = 0;
    uint32_t rampFrames = 0;

    float snapAndConstrainValue (float newValue) const;

    float getStringAsValue (std::string_view text) const;
    std::string getValueAsString (float value) const;

    float convertTo0to1 (float) const;
    float convertFrom0to1 (float) const;

    static std::string parseFormatString (choc::text::UTF8Pointer, float value);
};



//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

inline std::string_view getEndpointTypeName (EndpointType type)
{
    switch (type)
    {
        case EndpointType::stream:   return "stream";
        case EndpointType::value:    return "value";
        case EndpointType::event:    return "event";

        case EndpointType::unknown:
        default:                     return {};
    }
}

inline std::string_view getEndpointPurposeName (EndpointPurpose p)
{
    switch (p)
    {
        case EndpointPurpose::unknown:              return {};
        case EndpointPurpose::console:              return "console";
        case EndpointPurpose::audioIn:              return "audio in";
        case EndpointPurpose::audioOut:             return "audio out";
        case EndpointPurpose::midiIn:               return "midi in";
        case EndpointPurpose::midiOut:              return "midi out";
        case EndpointPurpose::parameterControl:     return "parameter";
        case EndpointPurpose::timeSignature:        return "time signature";
        case EndpointPurpose::tempo:                return "tempo";
        case EndpointPurpose::transportState:       return "transport state";
        case EndpointPurpose::timelinePosition:     return "timeline position";
        default:                                    return {};
    }
}

inline int32_t MIDIEvents::midiMessageToPackedInt (choc::midi::ShortMessage m)
{
    return static_cast<int32_t> (m.data[0]) << 16
            | static_cast<int32_t> (m.data[1]) << 8
            | static_cast<int32_t> (m.data[2]);
}

inline choc::midi::ShortMessage MIDIEvents::packedMIDIDataToMessage (int32_t packed)
{
    return choc::midi::ShortMessage (static_cast<uint8_t> (packed >> 16),
                                        static_cast<uint8_t> (packed >> 8),
                                        static_cast<uint8_t> (packed));
}

inline bool MIDIEvents::isMIDIMessageType (const choc::value::Type& type)
{
    return type.isObject()
            && choc::text::contains (type.getObjectClassName(), "Message")
            && type.getNumElements() == 1
            && type.getObjectMember (0).type.isInt32();
}

inline choc::value::Value MIDIEvents::createMIDIMessageObject (choc::midi::ShortMessage m)
{
    return choc::value::createObject ("Message", "message", midiMessageToPackedInt (m));
}

inline EndpointPurpose EndpointDetails::getSuggestedPurpose() const
{
    if (isConsole())
        return EndpointPurpose::console;

    if (isMIDI())
        return isInput ? EndpointPurpose::midiIn
                        : EndpointPurpose::midiOut;

    if (isParameter())
        return EndpointPurpose::parameterControl;

    if (getNumAudioChannels() > 0)
        return isInput ? EndpointPurpose::audioIn
                        : EndpointPurpose::audioOut;

    if (isTimelineTimeSignature())  return EndpointPurpose::timeSignature;
    if (isTimelineTempo())          return EndpointPurpose::tempo;
    if (isTimelineTransportState()) return EndpointPurpose::transportState;
    if (isTimelinePosition())       return EndpointPurpose::timelinePosition;

    return EndpointPurpose::unknown;
}

inline bool EndpointDetails::isConsole() const
{
    return ! isInput && endpointID.toString() == "console";
}

inline bool EndpointDetails::isMIDI() const
{
    return isEvent()
            && dataTypes.size() == 1
            && MIDIEvents::isMIDIMessageType (dataTypes.front());
}

inline uint32_t EndpointDetails::getNumAudioChannels() const
{
    if (isStream())
    {
        auto& frameType = dataTypes.front();

        if (frameType.isFloat())
            return 1;

        if (frameType.isVector() && frameType.getElementType().isFloat())
            return frameType.getNumElements();
    }

    return 0;
}

inline bool EndpointDetails::isParameter() const
{
    return isInput
            && ! isStream()
            && annotation.isObject()
            && annotation.hasObjectMember ("name")
            && dataTypes.size() == 1
            && (dataTypes.front().isFloat()
                    || dataTypes.front().isInt()
                    || dataTypes.front().isBool());
}

inline bool EndpointDetails::isTimeline() const
{
    return isTimelineTimeSignature()
        || isTimelineTempo()
        || isTimelineTransportState()
        || isTimelinePosition();
}

inline bool EndpointDetails::isTimelineTimeSignature() const
{
    if (dataTypes.size() != 1)
        return false;

    const auto& type = dataTypes.front();

    return type.isObject()
            && choc::text::contains (type.getObjectClassName(), "TimeSignature")
            && type.getNumElements() == 2
            && type.getObjectMember (0).name == "numerator"
            && type.getObjectMember (0).type.isInt()
            && type.getObjectMember (1).name == "denominator"
            && type.getObjectMember (1).type.isInt();
}

inline bool EndpointDetails::isTimelineTempo() const
{
    if (dataTypes.size() != 1)
        return false;

    const auto& type = dataTypes.front();

    return type.isObject()
            && choc::text::contains (type.getObjectClassName(), "Tempo")
            && type.getNumElements() == 1
            && type.getObjectMember (0).name == "bpm"
            && type.getObjectMember (0).type.isFloat32();
}

inline bool EndpointDetails::isTimelineTransportState() const
{
    if (dataTypes.size() != 1)
        return false;

    const auto& type = dataTypes.front();

    return type.isObject()
            && choc::text::contains (type.getObjectClassName(), "TransportState")
            && type.getNumElements() == 1
            && type.getObjectMember (0).name == "flags"
            && type.getObjectMember (0).type.isInt();
}

inline bool EndpointDetails::isTimelinePosition() const
{
    if (dataTypes.size() != 1)
        return false;

    const auto& type = dataTypes.front();

    return type.isObject()
            && choc::text::contains (type.getObjectClassName(), "Position")
            && type.getNumElements() == 3
            && type.getObjectMember (0).name == "frameIndex"
            && type.getObjectMember (0).type.isInt64()
            && type.getObjectMember (1).name == "quarterNote"
            && type.getObjectMember (1).type.isFloat64()
            && type.getObjectMember (2).name == "barStartQuarterNote"
            && type.getObjectMember (2).type.isFloat64();
}

inline choc::value::Value EndpointDetails::toJSON (bool includeSourceLocation) const
{
    auto o = choc::json::create ("endpointID",   endpointID.toString(),
                                 "endpointType", getEndpointTypeName (endpointType));


    if (dataTypes.size() == 1)
    {
        o.addMember ("dataType", dataTypes.front().toValue());
    }
    else
    {
        auto types = choc::value::createEmptyArray();

        for (auto& d : dataTypes)
            types.addArrayElement (d.toValue());

        o.addMember ("dataTypes", types);
    }

    if (! annotation.isVoid())
        o.addMember ("annotation", annotation);

    if (auto purpose = getSuggestedPurpose(); purpose != EndpointPurpose::unknown)
        o.addMember ("purpose", getEndpointPurposeName (purpose));

    if (auto numAudioChans = getNumAudioChannels())
        o.addMember ("numAudioChannels", static_cast<int32_t> (numAudioChans));

    if (includeSourceLocation && ! sourceFileLocation.empty())
        o.addMember ("source", sourceFileLocation);

    return o;
}

inline EndpointDetails EndpointDetails::fromJSON (const choc::value::ValueView& v, bool isIn)
{
    EndpointDetails d;
    d.endpointID = EndpointID::create (v["endpointID"].getString());
    d.isInput = isIn;

    auto type = v["endpointType"].getString();

    if (type == "stream")       d.endpointType = EndpointType::stream;
    else if (type == "value")   d.endpointType = EndpointType::value;
    else if (type == "event")   d.endpointType = EndpointType::event;
    else throw std::runtime_error ("Unknown endpoint type");

    auto dataTypes = v["dataTypes"];

    if (dataTypes.isArray())
    {
        for (uint32_t i = 0; i < dataTypes.size(); ++i)
            d.dataTypes.push_back (choc::value::Type::fromValue (dataTypes[i]));
    }
    else
    {
        d.dataTypes.push_back (choc::value::Type::fromValue (v["dataType"]));
    }

    if (v.hasObjectMember ("annotation"))
        d.annotation = v["annotation"];

    if (v.hasObjectMember ("source"))
        d.sourceFileLocation = v["source"].toString();

    return d;
}

//==============================================================================
inline std::string EndpointDetailsList::getDescription() const
{
    return choc::json::toString (toJSON (true), true);
}

inline choc::value::Value EndpointDetailsList::toJSON (bool includeSourceLocations) const
{
    auto list = choc::value::createEmptyArray();

    for (auto& e : endpoints)
        list.addArrayElement (e.toJSON (includeSourceLocations));

    return list;
}

inline EndpointDetailsList EndpointDetailsList::fromJSON (const choc::value::ValueView& json, bool isInput)
{
    try
    {
        EndpointDetailsList result;
        result.endpoints.reserve (json.size());

        for (uint32_t i = 0; i < json.size(); ++i)
            result.endpoints.push_back (EndpointDetails::fromJSON (json[i], isInput));

        return result;
    }
    catch (const std::exception&)
    {}

    return {};
}

inline choc::value::Value addTypeToValueAsProperty (const choc::value::ValueView& v)
{
    choc::value::Value value (v);

    if (v.isObject() && ! v.getType().getObjectClassName().empty())
        value.setMember ("_type", v.getType().getObjectClassName());

    return value;
}

inline bool doesObjectHaveTypeAsProperty (const choc::value::ValueView& source)
{
    return source.isObject()
            && source.getObjectClassName().empty()
            && source["_type"].isString();
}

inline choc::value::Value convertTypePropertyToObjectType (const choc::value::ValueView& source)
{
    auto v = choc::value::createObject (source["_type"].toString());
    auto numMembers = source.getType().getNumElements();

    for (uint32_t i = 0; i < numMembers; ++i)
    {
        const auto& m = source.getType().getObjectMember (i);

        if (m.name != "_type")
            v.addMember (m.name, source[m.name]);
    }

    return v;
}

inline std::string convertConsoleMessageToString (const choc::value::ValueView& v)
{
    if (v.isString())   return std::string (v.getString());
    if (v.isInt())      return std::to_string (v.get<int64_t>());
    if (v.isFloat())    return std::to_string (v.get<double>());
    if (v.isBool())     return (v.get<bool>() ? "true" : "false");

    return choc::json::toString (v);
}

//==============================================================================
inline PatchParameterProperties::PatchParameterProperties (const EndpointDetails& details)
{
    isEvent = details.isEvent();

    name     = details.annotation["name"].getWithDefault<std::string> (details.endpointID.toString());
    unit     = details.annotation["unit"].toString();
    group    = details.annotation["group"].toString();
    minValue = details.annotation["min"].getWithDefault<float> (0);
    maxValue = details.annotation["max"].getWithDefault<float> (1.0f);
    step     = details.annotation["step"].getWithDefault<float> (0);
    numSteps = 1000;

    if (step > 0)
        numSteps = static_cast<uint64_t> ((maxValue - minValue) / step) + 1u;
    else
        step = (maxValue - minValue) / (float) numSteps;

    if (auto text = details.annotation["text"].toString(); ! text.empty())
    {
        valueStrings = choc::text::splitString (choc::text::removeDoubleQuotes (std::string (text)), '|', false);
        auto numStrings = valueStrings.size();

        if (numStrings > 1)
        {
            numSteps = static_cast<uint64_t> (numStrings);

            const auto hasUserDefinedRange = [] (const auto& annotation)
            {
                return annotation.hasObjectMember ("min") && annotation.hasObjectMember ("max");
            };

            if (! hasUserDefinedRange (details.annotation))
            {
                minValue = 0.0f;
                maxValue = static_cast<float> (numStrings - 1u);
            }
        }
    }

    defaultValue  = details.annotation["init"].getWithDefault<float> (minValue);
    automatable   = details.annotation["automatable"].getWithDefault<bool> (true);
    boolean       = details.annotation["boolean"].getWithDefault<bool> (false);
    hidden        = details.annotation["hidden"].getWithDefault<bool> (false);
    rampFrames    = details.annotation["rampFrames"].getWithDefault<uint32_t> (0);
}

inline float PatchParameterProperties::snapAndConstrainValue (float newValue) const
{
    if (step > 0)
        newValue = std::round (newValue / step) * step;

    return std::max (minValue, std::min (maxValue, newValue));
}

inline std::string PatchParameterProperties::getValueAsString (float value) const
{
    if (valueStrings.empty())
        return choc::text::floatToString (value);

    auto numStrings = static_cast<int> (valueStrings.size());
    int index = 0;

    if (numStrings > 1)
    {
        auto value0to1 = (value - minValue) / (maxValue - minValue);
        const auto stepCount = static_cast<float> (numStrings - 1);
        index = static_cast<int> (std::min (stepCount, value0to1 * static_cast<float> (numStrings)));
        index = std::max (0, std::min (numStrings - 1, index));
    }

    return parseFormatString (choc::text::UTF8Pointer (valueStrings[static_cast<size_t> (index)].c_str()), value);
}

inline float PatchParameterProperties::getStringAsValue (std::string_view text) const
{
    auto target = std::string (choc::text::trim (text));

    if (auto numStrings = valueStrings.size())
    {
        for (size_t i = 0; i < numStrings; ++i)
        {
            if (choc::text::toLowerCase (choc::text::trim (valueStrings[i])) == choc::text::toLowerCase (target))
            {
                auto index0to1 = static_cast<double> (i) / static_cast<double> (numStrings - 1);
                return static_cast<float> (minValue + index0to1 * (maxValue - minValue));
            }
        }
    }

    try
    {
        return static_cast<float> (std::stod (target));
    }
    catch (...) {}

    return 0;
}

inline float PatchParameterProperties::convertTo0to1 (float v) const
{
    v = (v - minValue) / (maxValue - minValue);
    return v < 0 ? 0 : (v > 1.0f ? 1.0f : v);
}

inline float PatchParameterProperties::convertFrom0to1 (float v) const
{
    return minValue + (maxValue - minValue) * v;
}

inline std::string PatchParameterProperties::parseFormatString (choc::text::UTF8Pointer text, float value)
{
    std::string result;

    for (;;)
    {
        auto c = text.popFirstChar();

        if (c == 0)
            return result;

        if (c == '%')
        {
            auto t = text;
            char sign = 0;

            if (value < 0)
                sign = '-';

            if (*t == '+')
            {
                ++t;

                if (value >= 0)
                    sign = '+';
            }

            value = std::abs (value);

            uint32_t numDigits = 0;
            bool isPadded = (*t == '0');

            for (;;)
            {
                auto digit = static_cast<uint32_t> (*t) - (uint32_t) '0';

                if (digit > 9)
                    break;

                numDigits = 10 * numDigits + digit;
                ++t;
            }

            bool isInt   = (*t == 'd');
            bool isFloat = (*t == 'f');

            if (isInt || isFloat)
            {
                if (sign != 0)
                    result += sign;

                if (isInt)
                {
                    auto n = std::to_string (static_cast<int64_t> (value + 0.5f));

                    if (isPadded && n.length() < numDigits)
                        result += std::string (numDigits - n.length(), '0');

                    result += n;
                }
                else
                {
                    result += choc::text::floatToString (value, numDigits != 0 ? (int) numDigits : -1, numDigits == 0);
                }

                text = ++t;
                continue;
            }
        }

        choc::text::appendUTF8 (result, c);
    }

    return result;
}

} // namespace cmaj
