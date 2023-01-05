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

inline constexpr std::string_view getConsoleEndpointID()   { return "console"; }

/// Helper functions to deal with getting MIDI in and out of endpoints.
namespace MIDIEvents
{
    inline int32_t midiMessageToPackedInt (choc::midi::ShortMessage m)
    {
        return static_cast<int32_t> (m.data[0]) << 16
             | static_cast<int32_t> (m.data[1]) << 8
             | static_cast<int32_t> (m.data[2]);
    }

    inline choc::midi::ShortMessage packedMIDIDataToMessage (int32_t packed)
    {
        return choc::midi::ShortMessage (static_cast<uint8_t> (packed >> 16),
                                         static_cast<uint8_t> (packed >> 8),
                                         static_cast<uint8_t> (packed));
    }

    inline bool isMIDIMessageType (const choc::value::Type& type)
    {
        return type.isObject()
                && choc::text::contains (type.getObjectClassName(), "Message")
                && type.getNumElements() == 1
                && type.getObjectMember (0).type.isInt32();
    }

    inline choc::value::Value createMIDIMessageObject (choc::midi::ShortMessage m)
    {
        return choc::value::createObject ("Message", "message", midiMessageToPackedInt (m));
    }
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

    bool isOutput() const       { return ! isInput; }
    bool isStream() const       { return endpointType == EndpointType::stream; }
    bool isEvent() const        { return endpointType == EndpointType::event; }
    bool isValue() const        { return endpointType == EndpointType::value; }

    /// Attempts to make an informed guess about what kind of purpose the author
    /// intended this endpoint to be used for.
    EndpointPurpose getSuggestedPurpose() const
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

    /// Checks whether this endpoint has the name of the standard console output
    bool isConsole() const
    {
        return ! isInput && endpointID.toString() == "console";
    }

    /// Looks at the type of data that this endpoint uses, and returns true if it
    /// seems to be passing MIDI event objects
    bool isMIDI() const
    {
        return isEvent()
                && dataTypes.size() == 1
                && MIDIEvents::isMIDIMessageType (dataTypes.front());
    }

    /// Looks at the type of data that this endpoint uses, and if it's a floating
    /// point or vector, returns the number of channels. If it doesn't seem to be
    /// audio data, then this will just return 0.
    uint32_t getNumAudioChannels() const
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

    /// Uses some heuristics to make a guess at whether this endpoint appears
    /// to be a plugin parameter
    bool isParameter() const
    {
        return isInput
                && annotation.isObject()
                && annotation.hasObjectMember ("name")
                && dataTypes.size() == 1
                && (dataTypes.front().isFloat()
                     || dataTypes.front().isInt()
                     || dataTypes.front().isBool());
    }

    /// Attempts to say whether this endpoint is passing some kind of timeline data
    bool isTimeline() const
    {
        return isTimelineTimeSignature()
            || isTimelineTempo()
            || isTimelineTransportState()
            || isTimelinePosition();
    }

    /// Returns true if this endpoint seems to be dealing with time signature objects
    bool isTimelineTimeSignature() const
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

    /// Returns true if this endpoint seems to be dealing with tempo objects
    bool isTimelineTempo() const
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

    /// Returns true if this endpoint seems to be dealing with transport
    /// state change objects
    bool isTimelineTransportState() const
    {
        if (dataTypes.size() != 1)
            return false;

        const auto& type = dataTypes.front();

        return type.isObject()
                && choc::text::contains (type.getObjectClassName(), "TransportState")
                && type.getNumElements() == 1
                && type.getObjectMember (0).name == "state"
                && type.getObjectMember (0).type.isInt();
    }

    /// Returns true if this endpoint seems to be dealing with timeline positions
    bool isTimelinePosition() const
    {
        if (dataTypes.size() != 1)
            return false;

        const auto& type = dataTypes.front();

        return type.isObject()
                && choc::text::contains (type.getObjectClassName(), "Position")
                && type.getNumElements() == 3
                && type.getObjectMember (0).name == "currentFrame"
                && type.getObjectMember (0).type.isInt64()
                && type.getObjectMember (1).name == "currentQuarterNote"
                && type.getObjectMember (1).type.isFloat64()
                && type.getObjectMember (2).name == "lastBarStartQuarterNote"
                && type.getObjectMember (2).type.isFloat64();
    }

    //==============================================================================
    /// Creates a JSON reporesentation of the endpoint's properties
    choc::value::Value toJSON() const
    {
        auto o = choc::value::createObject ({},
                                            "endpointID",   endpointID.toString(),
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

        return o;
    }

    /// Creates an EndpointDetails object from a JSON representation that was
    /// creates with EndpointDetails::toJSON(). This may throw an exception if
    /// the JSON format isn't correct.
    static EndpointDetails fromJSON (const choc::value::ValueView& v, bool isIn)
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

        return d;
    }
};

//==============================================================================
/// A set of EndpointDetails objects for each endpoint that a program exposes.
/// To get an EndpointDetailsList, see the methods in cmaj::Engine.
struct EndpointDetailsList
{
    /// Returns a JSON string containing the items in this list.
    std::string getDescription() const
    {
        return choc::json::toString (toJSON(), true);
    }

    /// Serialises the list into a JSON object
    choc::value::Value toJSON() const
    {
        auto list = choc::value::createEmptyArray();

        for (auto& e : endpoints)
            list.addArrayElement (e.toJSON());

        return list;
    }

    /// Serialises the list into a JSON string in the form of a COM object
    choc::com::String* toJSONCOMString() const
    {
        return choc::com::createRawString (choc::json::toString (toJSON(), true));
    }

    /// Creates a list from some JSON that was created by the toJSON() method,
    /// returning an empty list if the JSON was invalid.
    static EndpointDetailsList fromJSON (std::string_view json, bool isInput)
    {
        try
        {
            EndpointDetailsList result;
            auto list = choc::json::parse (json);
            result.endpoints.reserve (list.size());

            for (uint32_t i = 0; i < list.size(); ++i)
                result.endpoints.push_back (EndpointDetails::fromJSON (list[i], isInput));

            return result;
        }
        catch (const std::exception&)
        {}

        return {};
    }

    // Thsee let you iterate each EndpointDetails object in the list
    auto begin() const      { return endpoints.begin(); }
    auto end() const        { return endpoints.end(); }

    size_t size() const     { return endpoints.size(); }

    std::vector<EndpointDetails> endpoints;
};

//==============================================================================
inline std::string convertConsoleMessageToString (const choc::value::ValueView& v)
{
    if (v.isString())   return std::string (v.getString());
    if (v.isInt())      return std::to_string (v.get<int64_t>());
    if (v.isFloat())    return std::to_string (v.get<double>());
    if (v.isBool())     return (v.get<bool>() ? "true" : "false");

    return choc::json::toString (v);
}


} // namespace cmaj
