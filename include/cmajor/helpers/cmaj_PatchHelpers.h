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

#include "../../choc/text/choc_Files.h"
#include "../../choc/audio/choc_AudioFileFormat_WAV.h"
#include "../../choc/audio/choc_AudioFileFormat_Ogg.h"
#include "../../choc/audio/choc_AudioFileFormat_FLAC.h"
#include "../../choc/audio/choc_AudioFileFormat_MP3.h"

#include "../API/cmaj_Endpoints.h"
#include "../API/cmaj_ExternalVariables.h"

#include <algorithm>

namespace cmaj
{

static constexpr int32_t currentPatchCompatibilityVersion = 1;

//==============================================================================
/// Parses and represents a .cmajorpatch file
struct PatchManifest
{
    /// Initialises this manifest object by reading a given patch from the
    /// filesystem.
    /// This will throw an exception if there are errors parsing the file.
    void initialiseWithFile (std::filesystem::path manifestFile);

    /// Initialises this manifest object by reading a given patch using a set
    /// of custom file-reading functors.
    /// This will throw an exception if there are errors parsing the file.
    void initialiseWithVirtualFile (std::string patchFileLocation,
                                    std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReader,
                                    std::function<std::string(const std::string&)> getFullPathForFile,
                                    std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTime,
                                    std::function<bool(const std::string&)> fileExists);

    /// Refreshes the content by re-reading from the original source
    bool reload();

    choc::value::Value manifest;
    std::string manifestFile, ID, name, description, category, manufacturer, version;
    bool isInstrument = false;
    std::vector<std::string> sourceFiles;
    choc::value::Value externals;
    bool needsToBuildSource = true;

    // These functors are used for all file access, as the patch may be loaded from
    // all sorts of virtual filesystems
    std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReader;
    std::function<std::string(const std::string&)> getFullPathForFile;
    std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTime;
    std::function<bool(const std::string&)> fileExists;
    std::string readFileContent (const std::string& name) const;

    /// Represents one of the GUI views in the patch
    struct View
    {
        /// A (possibly relative) URL for the view content
        std::string getSource() const;
        uint32_t getWidth() const;
        uint32_t getHeight() const;
        bool isResizable() const;

        choc::value::Value view = choc::value::createObject ({});
    };

    std::vector<View> views;

    const View* findDefaultView() const;
    const View* findGenericView() const;

    /// Returns a copy of the manifest object that has large items like
    /// chunks of external data removed.
    choc::value::Value getStrippedManifest() const;

    /// Sets up the lambdas needed to read from a given file
    void createFileReaderFunctions (const std::filesystem::path&);

private:
    void addSource (const choc::value::ValueView&);
    void addView (const choc::value::ValueView&);
};



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
    uint64_t numDiscreteOptions = 0;
    uint32_t rampFrames = 0;

    float snapAndConstrainValue (float newValue) const;

    float getStringAsValue (std::string_view text) const;
    std::string getValueAsString (float value) const;

    float convertTo0to1 (float) const;
    float convertFrom0to1 (float) const;

    static std::string parseFormatString (choc::text::UTF8Pointer, float value);

private:
    bool hasFormatString() const;
    bool hasDiscreteTextOptions() const;
    size_t toDiscreteOptionIndex (float newValue) const;
    float toValueFromDiscreteOptionIndex (size_t) const;

    uint64_t stepCount = 0;
};


//==============================================================================
/// This helper class contains a set of pre-prepared Value objects for timeline
/// events, so that it can provide one when needed without needing to allocate.
struct TimelineEventGenerator
{
    choc::value::Value& getTimeSigEvent (int numerator, int denominator);
    choc::value::Value& getBPMEvent (float bpm);
    choc::value::Value& getTransportStateEvent (bool isRecording, bool isPlaying);
    choc::value::Value& getPositionEvent (int64_t currentFrame, double ppq, double ppqBar);

private:
    choc::value::Value timeSigEvent     { choc::value::createObject ("TimeSignature",
                                                                     "numerator", 0,
                                                                     "denominator", 0) };
    choc::value::Value tempoEvent       { choc::value::createObject ("Tempo",
                                                                     "bpm", 0.0f) };
    choc::value::Value transportState   { choc::value::createObject ("TransportState",
                                                                     "flags", 0) };
    choc::value::Value positionEvent    { choc::value::createObject ("Position",
                                                                     "frameIndex", (int64_t) 0,
                                                                     "quarterNote", 0.0,
                                                                     "barStartQuarterNote", 0.0) };
};

//==============================================================================
choc::value::Value replaceFilenameStringsWithAudioData (PatchManifest& manifest,
                                                        const choc::value::ValueView& sourceObject,
                                                        const choc::value::ValueView& annotation);


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

inline void PatchManifest::initialiseWithVirtualFile (std::string patchFileLocation,
                                                      std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReaderFn,
                                                      std::function<std::string(const std::string&)> getFullPathForFileFn,
                                                      std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTimeFn,
                                                      std::function<bool(const std::string&)> fileExistsFn)
{
    createFileReader = std::move (createFileReaderFn);
    getFullPathForFile = std::move (getFullPathForFileFn);
    getFileModificationTime = std::move (getFileModificationTimeFn);
    fileExists = std::move (fileExistsFn);
    CHOC_ASSERT (createFileReader && getFullPathForFile && getFileModificationTime && fileExists);

    manifestFile = std::move (patchFileLocation);
    name = std::filesystem::path (manifestFile).filename().string();
    reload();
}

inline void PatchManifest::createFileReaderFunctions (const std::filesystem::path& file)
{
    manifestFile = file.filename().string();
    name = file.filename().string();

    auto folder = file.parent_path();

    const auto getFullPath = [folder] (const std::string& f) -> std::filesystem::path
    {
        return folder / std::filesystem::path (f).relative_path();
    };

    createFileReader = [getFullPath] (const std::string& f) -> std::shared_ptr<std::istream>
    {
        try
        {
            return std::make_shared<std::ifstream> (getFullPath (f), std::ios::binary | std::ios::in);
        }
        catch (...) {}

        return {};
    };

    getFullPathForFile = [getFullPath] (const std::string& f) -> std::string
    {
        return getFullPath (f).string();
    };

    getFileModificationTime = [getFullPath] (const std::string& f) -> std::filesystem::file_time_type
    {
        try
        {
            return last_write_time (getFullPath (f));
        }
        catch (...) {}

        return {};
    };

    fileExists = [getFullPath] (const std::string& f) { return exists (getFullPath (f)); };
}

inline void PatchManifest::initialiseWithFile (std::filesystem::path file)
{
    createFileReaderFunctions (std::move (file));
    reload();
}

inline bool PatchManifest::reload()
{
    if (createFileReader == nullptr || manifestFile.empty())
        return false;

    manifest = choc::json::parse (readFileContent (manifestFile));

    ID = {};
    name = {};
    description = {};
    category = {};
    manufacturer = {};
    version = {};
    isInstrument = false;
    sourceFiles.clear();
    externals = choc::value::Value();
    views.clear();

    if (manifest.isObject())
    {
        if (! manifest.hasObjectMember ("CmajorVersion"))
            throw std::runtime_error ("The manifest must contain a property \"CmajorVersion\"");

        if (auto cmajVersion = manifest["CmajorVersion"].getWithDefault<int64_t> (0);
            cmajVersion < 1 || cmajVersion > currentPatchCompatibilityVersion)
            throw std::runtime_error ("Incompatible value for CmajorVersion");

        ID             = manifest["ID"].toString();
        name           = manifest["name"].toString();
        description    = manifest["description"].toString();
        category       = manifest["category"].toString();
        manufacturer   = manifest["manufacturer"].toString();
        version        = manifest["version"].toString();
        isInstrument   = manifest["isInstrument"].getWithDefault<bool> (false);
        externals      = manifest["externals"];

        if (ID.length() < 4)
            throw std::runtime_error ("The manifest must contain a valid and globally unique \"ID\" property");

        if (name.length() > 128 || name.empty())
            throw std::runtime_error ("The manifest must contain a valid \"name\" property");

        if (version.length() > 24 || version.empty())
            throw std::runtime_error ("The manifest must contain a valid \"version\" property");

        addSource (manifest["source"]);
        addView (manifest["view"]);

        return true;
    }

    throw std::runtime_error ("The patch file did not contain a valid JSON object");
}

inline std::string PatchManifest::readFileContent (const std::string& file) const
{
    if (auto stream = createFileReader (file))
    {
        try
        {
            stream->seekg (0, std::ios_base::end);
            auto fileSize = stream->tellg();

            if (fileSize > 0)
            {
                std::string result;
                result.resize (static_cast<std::string::size_type> (fileSize));
                stream->seekg (0);

                if (stream->read (reinterpret_cast<std::ifstream::char_type*> (result.data()), static_cast<std::streamsize> (fileSize)))
                    return result;
            }
        }
        catch (...) {}
    }

    return {};
}

inline void PatchManifest::addSource (const choc::value::ValueView& source)
{
    if (source.isString())
    {
        sourceFiles.push_back (source.get<std::string>());
    }
    else if (source.isArray())
    {
        for (auto f : source)
            addSource (f);
    }
}

inline void PatchManifest::addView (const choc::value::ValueView& view)
{
    if (view.isArray())
    {
        for (auto e : view)
            if (e.isObject())
                addView (e);
    }
    else if (view.isObject())
    {
        views.push_back (View { choc::value::Value (view) });
    }
}

inline const PatchManifest::View* PatchManifest::findDefaultView() const
{
    for (auto& view : views)
        if (view.getSource().empty() || fileExists (view.getSource()))
            return std::addressof (view);

    return {};
}

inline const PatchManifest::View* PatchManifest::findGenericView() const
{
    for (auto& view : views)
        if (view.getSource().empty())
            return std::addressof (view);

    return {};
}

inline choc::value::Value PatchManifest::getStrippedManifest() const
{
    if (! (manifest.isObject() && manifest.hasObjectMember ("externals")))
        return manifest;

    auto stripped = choc::value::createObject ({});

    for (uint32_t i = 0; i < manifest.size(); ++i)
    {
        auto m = manifest.getObjectMemberAt (i);

        if (std::string_view (m.name) != "externals")
            stripped.addMember (m.name, m.value);
    }

    return stripped;
}

inline std::string PatchManifest::View::getSource() const  { return view["src"].toString(); }
inline uint32_t PatchManifest::View::getWidth() const      { return view["width"].getWithDefault<uint32_t> (0); }
inline uint32_t PatchManifest::View::getHeight() const     { return view["height"].getWithDefault<uint32_t> (0); }
inline bool PatchManifest::View::isResizable() const       { return view["resizable"].getWithDefault<bool> (true); }

//==============================================================================
inline choc::value::Value& TimelineEventGenerator::getTimeSigEvent (int numerator, int denominator)
{
    timeSigEvent.setMember ("numerator", numerator);
    timeSigEvent.setMember ("denominator", denominator);
    return timeSigEvent;
}

inline choc::value::Value& TimelineEventGenerator::getBPMEvent (float bpm)
{
    tempoEvent.setMember ("bpm", bpm);
    return tempoEvent;
}

inline choc::value::Value& TimelineEventGenerator::getTransportStateEvent (bool isRecording, bool isPlaying)
{
    transportState.setMember ("flags", isRecording ? 2 : isPlaying ? 1 : 0);
    return transportState;
}

inline choc::value::Value& TimelineEventGenerator::getPositionEvent (int64_t currentFrame, double quarterNote, double barStartQuarterNote)
{
    positionEvent.setMember ("frameIndex", currentFrame);
    positionEvent.setMember ("quarterNote", quarterNote);
    positionEvent.setMember ("barStartQuarterNote", barStartQuarterNote);
    return positionEvent;
}

//==============================================================================
inline choc::value::Value replaceFilenameStringsWithAudioData (PatchManifest& manifest,
                                                               const choc::value::ValueView& v,
                                                               const choc::value::ValueView& annotation)
{
    if (v.isVoid())
        return {};

    if (v.isString())
    {
        try
        {
            auto s = v.get<std::string>();

            if (auto reader = manifest.createFileReader (s))
            {
                choc::value::Value audioFileContent;

                choc::audio::AudioFileFormatList formats;
                formats.addFormat<choc::audio::OggAudioFileFormat<false>>();
                formats.addFormat<choc::audio::MP3AudioFileFormat>();
                formats.addFormat<choc::audio::FLACAudioFileFormat<false>>();
                formats.addFormat<choc::audio::WAVAudioFileFormat<true>>();

                auto error = cmaj::readAudioFileAsValue (audioFileContent, formats, reader, annotation);

                if (error.empty())
                    return audioFileContent;
            }
        }
        catch (...)
        {}
    }

    if (v.isArray())
    {
        auto copy = choc::value::createEmptyArray();

        for (auto element : v)
            copy.addArrayElement (replaceFilenameStringsWithAudioData (manifest, element, annotation));

        return copy;
    }

    if (v.isObject())
    {
        auto copy = choc::value::createObject ({});

        for (uint32_t i = 0; i < v.size(); ++i)
        {
            auto m = v.getObjectMemberAt (i);
            copy.setMember (m.name, replaceFilenameStringsWithAudioData (manifest, m.value, annotation));
        }

        return copy;
    }

    return choc::value::Value (v);
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

    if (auto text = details.annotation["text"].toString(); ! text.empty())
    {
        valueStrings = choc::text::splitString (choc::text::removeDoubleQuotes (std::string (text)), '|', false);

        if (hasDiscreteTextOptions())
        {
            const auto hasUserDefinedRange = [] (const auto& annotation)
            {
                return annotation.hasObjectMember ("min") && annotation.hasObjectMember ("max");
            };

            if (! hasUserDefinedRange (details.annotation))
            {
                minValue = 0.0f;
                maxValue = static_cast<float> (valueStrings.size() - 1u);
            }
        }
    }

    defaultValue  = details.annotation["init"].getWithDefault<float> (minValue);
    automatable   = details.annotation["automatable"].getWithDefault<bool> (true);
    boolean       = details.annotation["boolean"].getWithDefault<bool> (false);
    hidden        = details.annotation["hidden"].getWithDefault<bool> (false);
    rampFrames    = details.annotation["rampFrames"].getWithDefault<uint32_t> (0);

    const auto calculateNumDiscreteOptions = [&details, this]() -> uint64_t
    {
        if (this->boolean)
            return 2;

        if (hasDiscreteTextOptions())
            return valueStrings.size();

        if (details.annotation["discrete"].getWithDefault<bool> (false))
            if (step > 0)
                return static_cast<uint64_t> ((maxValue - minValue) / step) + 1u;

        return 0;
    };
    numDiscreteOptions = calculateNumDiscreteOptions();
    stepCount = numDiscreteOptions > 0 ? numDiscreteOptions - 1 : 0;
}

inline float PatchParameterProperties::snapAndConstrainValue (float newValue) const
{
    if (numDiscreteOptions > 0)
        return toValueFromDiscreteOptionIndex (toDiscreteOptionIndex (newValue));

    if (step > 0)
        newValue = std::round (newValue / step) * step;

    return std::clamp (newValue, minValue, maxValue);
}

inline std::string PatchParameterProperties::getValueAsString (float value) const
{
    value = snapAndConstrainValue (value);

    if (hasFormatString())
        return parseFormatString (choc::text::UTF8Pointer (valueStrings.front().c_str()), value);

    if (hasDiscreteTextOptions())
        return valueStrings[toDiscreteOptionIndex (value)];

    return choc::text::floatToString (value);
}

inline float PatchParameterProperties::getStringAsValue (std::string_view text) const
{
    auto target = std::string (choc::text::trim (text));

    if (hasDiscreteTextOptions())
        for (size_t i = 0; i < static_cast<size_t> (numDiscreteOptions); ++i)
            if (choc::text::toLowerCase (choc::text::trim (valueStrings[i])) == choc::text::toLowerCase (target))
                return toValueFromDiscreteOptionIndex (i);

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
    return std::clamp (v, 0.0f, 1.0f);
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

inline bool PatchParameterProperties::hasFormatString() const
{
    return valueStrings.size() == 1;
}

inline bool PatchParameterProperties::hasDiscreteTextOptions() const
{
    return valueStrings.size() > 1;
}

inline size_t PatchParameterProperties::toDiscreteOptionIndex (float value) const
{
    return std::min (static_cast<size_t> (convertTo0to1 (value) * numDiscreteOptions), static_cast<size_t> (stepCount));
}

inline float PatchParameterProperties::toValueFromDiscreteOptionIndex (size_t i) const
{
    auto index0to1 = static_cast<double> (i) / static_cast<double> (stepCount);
    return convertFrom0to1 (static_cast<float> (index0to1));
}

} // namespace cmaj
