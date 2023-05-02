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

#include "../../choc/text/choc_JSON.h"


namespace cmaj
{

//==============================================================================
struct BuildSettings
{
    BuildSettings() = default;

    double       getMaxFrequency() const                   { return getWithRangeCheck (maxFrequencyMember, 1.0, 1000000.0, defaultMaxFrequency); }
    double       getFrequency() const                      { return getWithRangeCheck (frequencyMember, 1.0, 1000000.0, 0.0); }
    uint32_t     getMaxBlockSize() const                   { return getWithRangeCheck (maxBlockSizeMember, 1u, 8192u, defaultMaxBlockSize); }
    uint64_t     getMaxStateSize() const                   { return getWithRangeCheck (maxStateSizeMember, static_cast<uint64_t> (8192), static_cast<uint64_t> (1024 * 1024 * 1024 + 1), defaultMaxStateSize); }
    uint64_t     getMaxStackSize() const                   { return getWithRangeCheck (maxStackSizeMember, static_cast<uint64_t> (1024), static_cast<uint64_t> (1024 * 1024 * 1024 + 1), defaultMaxStackSize); }
    uint32_t     getEventBufferSize() const                { return getWithRangeCheck (eventBufferSizeMember, 1u, 8192u, defaultEventBufferSize); }
    int          getOptimisationLevel() const              { return getWithRangeCheck (optimisationLevelMember, -1, 5, -1); }
    int32_t      getSessionID() const                      { return getWithDefault (sessionIDMember, 0); }
    bool         shouldIgnoreWarnings() const              { return getWithDefault (ignoreWarningsMember, false); }
    bool         shouldDumpDebugInfo() const               { return getWithDefault (debugMember, false); }
    bool         isDebugFlagSet() const                    { return getWithDefault (debugMember, false); }
    bool         shouldUseFastMaths() const                { return getOptimisationLevel() >= 4; }

    BuildSettings& setMaxFrequency (double f)              { setProperty (maxFrequencyMember, f); return *this; }
    BuildSettings& setFrequency (double f)                 { setProperty (frequencyMember, f); return *this; }
    BuildSettings& setMaxBlockSize (uint32_t size)         { setProperty (maxBlockSizeMember, static_cast<int32_t> (size)); return *this; }
    BuildSettings& setMaxStateSize (uint64_t size)         { setProperty (maxStateSizeMember, static_cast<int32_t> (size)); return *this; }
    BuildSettings& setMaxStackSize (uint64_t size)         { setProperty (maxStackSizeMember, static_cast<int32_t> (size)); return *this; }
    BuildSettings& setEventBufferSize (uint32_t size)      { setProperty (eventBufferSizeMember, static_cast<int32_t> (size)); return *this; }
    BuildSettings& setOptimisationLevel (int level)        { setProperty (optimisationLevelMember, level); return *this; }
    BuildSettings& setSessionID (int32_t id)               { setProperty (sessionIDMember, id); return *this; }
    BuildSettings& setDebugFlag (bool b)                   { setProperty (debugMember, b); return *this; }

    void reset()                                           { settings = choc::value::Value(); }

    static BuildSettings fromJSON (choc::value::Value v)
    {
        BuildSettings bs;
        bs.settings = std::move (v);
        return bs;
    }

    static BuildSettings fromJSON (std::string_view js)
    {
        try
        {
            return fromJSON (choc::json::parse (js));
        }
        catch (const std::exception&) {}

        return {};
    }

    std::string toJSON() const
    {
        if (settings.isObject())
            return choc::json::toString (settings, true);

        return {};
    }

    choc::value::Value getValue() const
    {
        return settings;
    }

    void mergeValues (const BuildSettings& other)
    {
        if (! settings.isObject())
        {
            settings = other.settings;
        }
        else if (other.settings.isObject())
        {
            for (uint32_t i = 0; i < other.settings.size(); ++i)
            {
                auto member = other.settings.getObjectMemberAt (i);
                settings.setMember (member.name, member.value);
            }
        }
    }

    static constexpr double   defaultMaxFrequency    = 192000.0;
    static constexpr uint64_t defaultMaxStateSize    = 20 * 1024 * 1024;
    static constexpr uint64_t defaultMaxStackSize    = 5 * 1024 * 1024;
    static constexpr uint32_t defaultEventBufferSize = 32;
    static constexpr uint32_t defaultMaxBlockSize    = 1024;

private:
    choc::value::Value settings;

    static constexpr auto maxFrequencyMember       = "maxFrequency";
    static constexpr auto frequencyMember          = "frequency";
    static constexpr auto maxBlockSizeMember       = "maxBlockSize";
    static constexpr auto maxStateSizeMember       = "maxStateSize";
    static constexpr auto maxStackSizeMember       = "maxStackSize";
    static constexpr auto eventBufferSizeMember    = "eventBufferSize";
    static constexpr auto optimisationLevelMember  = "optimisationLevel";
    static constexpr auto sessionIDMember          = "sessionID";
    static constexpr auto ignoreWarningsMember     = "ignoreWarnings";
    static constexpr auto debugMember              = "debug";
    static constexpr auto mainProcessorMember      = "mainProcessor";

    template <typename Type>
    Type getWithDefault (std::string_view name, Type defaultValue) const
    {
        if (settings.isObject() && settings.hasObjectMember (name))
            return settings[name].getWithDefault (defaultValue);

        return defaultValue;
    }

    template <typename Type>
    Type getWithRangeCheck (std::string_view name, Type minValue, Type maxValue, Type defaultValue) const
    {
        if (settings.isObject() && settings.hasObjectMember (name))
        {
            auto value = settings[name].getWithDefault (defaultValue);

            if (value < minValue)
                return minValue;

            if (value > maxValue)
                return maxValue;

            return value;
        }

        return defaultValue;
    }

    template <typename Type>
    void setProperty (std::string_view name, Type value)
    {
        if (! settings.isObject())
            settings = choc::value::createObject ({});

        settings.setMember (name, value);
    }
};


} // namespace cmaj
