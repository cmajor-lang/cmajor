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

#include "../../choc/platform/choc_Platform.h"
#include "../../choc/text/choc_Files.h"
#include "../../choc/threading/choc_ThreadSafeFunctor.h"
#include "../../choc/threading/choc_TaskThread.h"
#include "../../choc/audio/choc_AudioFileFormat_WAV.h"
#include "../../choc/audio/choc_AudioFileFormat_Ogg.h"
#include "../../choc/audio/choc_AudioFileFormat_FLAC.h"
#include "../../choc/audio/choc_AudioFileFormat_MP3.h"
#include "../../choc/platform/choc_HighResolutionSteadyClock.h"

#if ! CHOC_EMSCRIPTEN
 #include "../../choc/gui/choc_MessageLoop.h"
 #define CMAJ_HAS_MESSAGE_LOOP 1
#else
 #define CMAJ_HAS_MESSAGE_LOOP 0
#endif

#include "../API/cmaj_Endpoints.h"
#include "../API/cmaj_Program.h"
#include "../API/cmaj_ExternalVariables.h"

#include "cmaj_EmbeddedWebAssets.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

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

    /// This is a copy of the whole manifest as a JSON object. Most of the values
    /// from this are parsed by this helper class to populate its other members, but
    /// if you have custom items in the manifest, this is where you can find them.
    choc::value::Value manifest;

    /// This is the patch's unique ID string.
    std::string ID;

    /// The patch's version number
    std::string version;

    /// The display name for the patch.
    std::string name;

    /// An optional longer description for the patch.
    std::string description;

    /// An optional free-form category to describe this patch.
    std::string category;

    /// This is the file path of the .cmajorpatch file from which this manifest was
    /// loaded (or blank if it didn't come from a file).
    std::string manifestFile;

    /// An optional manufacturer name
    std::string manufacturer;

    /// Optionally, this overrides the Cmajor processor that is chosen to be the
    /// main processor for the patch.
    std::string mainProcessor;

    /// True if the isInstrument flag was set
    bool isInstrument = false;

    /// This array is a list of the .cmajor source files that the manifest specifies.
    std::vector<std::string> sourceFiles;

    /// An optional path to a patch worker .js file
    std::string patchWorker;

    /// The "resources" field can be a string (or array of strings) which provides
    /// wildcards at which to find resource files in the patch bundle - this is used
    /// by exporters to know which resource files a patch is going to need
    std::vector<std::string> resources;

    /// This flag is set to false in special cases where a patch has a fixed, pre-built
    /// program, e.g in an exported C++ version of a patch.
    bool needsToBuildSource = true;

    // These functors are used for all file access, as the patch may be loaded from
    // all sorts of virtual filesystems

    /// Checks whether a resource file within the patch exists.
    std::function<bool(const std::string&)> fileExists;
    /// This function will attempt to create a stream to read the given patch file.
    /// Returns nullptr if the file can't be opened.
    std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReader;
    /// Attempts to open a stream to the given patch resource file and load the
    /// whole thing. Returns an empty optional on failure.
    std::optional<std::string> readFileContent (const std::string& name) const;
    /// This takes a relative path to a resource within the patch and converts it to
    /// an absolute path (if applicable).
    std::function<std::string(const std::string&)> getFullPathForFile;
    /// This attempts to find the last modification time of a file within the patch
    /// If that's not possible, it returns an empty time object.
    std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTime;

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

    /// A list of all the View objects that were specified in the manifest.
    std::vector<View> views;

    /// Returns a pointer to the first View object in the patch which seems
    /// to exist, or nullptr if the manifest has no working views.
    const View* findDefaultView() const;

    /// If the manifest has a generic view entry, this returns it, so that you
    /// can find out more about its preferred size etc.
    const View* findGenericView() const;

    /// Returns a copy of the manifest object that has large items like
    /// chunks of external data removed.
    choc::value::Value getStrippedManifest() const;

    /// Sets up the lambdas needed to read from a given file
    void createFileReaderFunctions (const std::filesystem::path&);

    /// Parses and returns a map of names to externals to their values, if any are
    /// specified in the manifest.
    std::unordered_map<std::string, choc::value::ValueView> getExternalsList() const;

    /// Returns a function that can auto-resolve externals for this manifest, using
    /// the replaceFilenameStringsWithAudioData() helper function. This function
    /// can be passed straight into the Engine::load() method.
    std::function<choc::value::Value(const cmaj::ExternalVariable&)> createExternalResolverFunction() const;

    /// Parses and adds all the source files from this patch to the given Program,
    /// returning true if no errors were encountered.
    bool addSourceFilesToProgram (Program&, DiagnosticMessageList&,
                                  const std::function<void()>& checkForStopSignal);

private:
    static void addStrings (std::vector<std::string>&, const choc::value::ValueView&);
    void addView (const choc::value::ValueView&);
    void addWorker (const choc::value::ValueView&);
};

//==============================================================================
/// This helper class implements the logic for interpreting an EndpointDetails
/// object as a set of more parameter-focued values.
struct PatchParameterProperties
{
    /// Initialises all the properties in this class from the details provided.
    PatchParameterProperties (const EndpointDetails&);

    std::string endpointID, name, unit, group;
    float minValue = 0, maxValue = 0, step = 0, defaultValue = 0;
    std::vector<std::string> valueStrings;
    bool isEvent = false, boolean = false, automatable = false, hidden = false, discrete = false;
    uint32_t rampFrames = 0;

    /// Takes a full-range value, clamps it to lie between minValue and maxValue,
    /// and also applies any snapping that may be required according to the `step`
    /// or `valueStrings` members.
    float snapAndConstrainValue (float newValue) const;

    /// Attempts to parse a text string (possibly entered by a user) to a value.
    /// If the string doesn't contain anything numeric, this will return a null optional.
    /// If the string is an out of range value, it'll be returned so that the caller can
    /// decide whether to clip it or reject it.
    std::optional<float> getStringAsValue (std::string_view text) const;

    /// Converts a (full-range) numeric value to a text string, using rules based
    /// on the contents and format of `valueStrings` and other hints.
    std::string getValueAsString (float value) const;

    /// Attempts to turn a ValueView into a parameter value, by either parsing it as
    /// a string, or converting it to a float. If no valid value can be extracted, this
    /// just returns `defaultValue`.
    float parseValue (const choc::value::ValueView&) const;

    /// Maps a value from the range (minValue, maxValue) to the range (0, 1.0).
    /// This will also clamp any out-of-range values so that the value returned
    /// is always between 0 and 1.0.
    float convertTo0to1 (float) const;

    /// Maps a value from the range (0, 1.0) to the range (minValue, maxValue).
    /// No clamping is applied here, as the input is expected to be valid.
    float convertFrom0to1 (float) const;

    /// If the parameter is quantised into steps or options, this returns how
    /// many there are, or returns 0 if the value is continuous.
    uint64_t getNumDiscreteOptions() const;

private:
    //==============================================================================
    bool hasFormatString() const;
    bool hasDiscreteTextOptions() const;
    size_t toDiscreteOptionIndex (float newValue) const;
    std::optional<float> toValueFromDiscreteOptionIndex (size_t) const;
    static std::string parseFormatString (choc::text::UTF8Pointer, float value);
};


//==============================================================================
/// This helper class contains a set of pre-prepared Value objects for timeline
/// events, so that it can provide one when needed without needing to allocate.
struct TimelineEventGenerator
{
    choc::value::Value& getTimeSigEvent (int numerator, int denominator);
    choc::value::Value& getBPMEvent (float bpm);
    choc::value::Value& getTransportStateEvent (bool isRecording, bool isPlaying, bool isLooping);
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
choc::value::Value readManifestResourceAsAudioData (const PatchManifest& manifest,
                                                    const std::string& path,
                                                    const choc::value::ValueView& annotation);

choc::value::Value replaceFilenameStringsWithAudioData (const PatchManifest& manifest,
                                                        const choc::value::ValueView& sourceObject,
                                                        const choc::value::ValueView& annotation);

std::optional<std::string> readJavascriptResource (std::string_view resourcePath, const PatchManifest*);

//==============================================================================
struct PatchFileChangeChecker
{
    struct ChangeType
    {
        bool cmajorFilesChanged = false,
             assetFilesChanged = false,
             manifestChanged = false;
    };

    PatchFileChangeChecker (const PatchManifest&, std::function<void(ChangeType)>&& onChange);
    ~PatchFileChangeChecker();

    ChangeType checkAndReset();

private:
   #if CMAJ_HAS_MESSAGE_LOOP
    struct SourceFilesWithTimes
    {
        SourceFilesWithTimes() = default;
        SourceFilesWithTimes (SourceFilesWithTimes&&) = default;
        SourceFilesWithTimes& operator= (SourceFilesWithTimes&&) = default;

        struct File
        {
            std::string file;
            std::filesystem::file_time_type lastWriteTime;

            bool operator== (const File& other) const   { return file == other.file && lastWriteTime == other.lastWriteTime; }
            bool operator!= (const File& other) const   { return ! operator== (other); }
        };

        void add (const PatchManifest& m, const std::string& file)
        {
            files.push_back ({ file, m.getFileModificationTime (file) });
        }

        bool operator== (const SourceFilesWithTimes& other) const { return files == other.files; }
        bool operator!= (const SourceFilesWithTimes& other) const { return ! (files == other.files); }

        std::vector<File> files;
    };

    PatchManifest manifest;
    SourceFilesWithTimes manifestFiles, cmajorFiles, assetFiles;
    choc::threading::ThreadSafeFunctor<std::function<void(ChangeType)>> callback;
    choc::threading::TaskThread fileChangeCheckThread;
   #endif
};

//==============================================================================
struct CPUMonitor
{
    void reset (double sampleRate);

    /// Starts timing a process block
    void startProcess();
    /// Ends timing the current block, and possibly calls handleCPULevel
    /// with a new level, if available.
    void endProcess (uint32_t numFrames);

    /// This will be called to deliver new CPU levels when it either changes
    /// significantly, or just periodically if it remains constant.
    std::function<void(float)> handleCPULevel;

    /// Sets the minimum number of frames to sample between each call to
    /// the handleCPULevel callback. If this is 0, no callbacks are made
    std::atomic<uint32_t> framesPerCallback { 0 };

private:
    using Clock = choc::HighResolutionSteadyClock;
    using TimePoint = Clock::time_point;
    using Seconds = std::chrono::duration<double>;

    double inverseRate = 0;
    TimePoint processStartTime;
    double average = 0;
    uint32_t frameCount = 0, minFramesPerCallback = 0;
    float lastLevelSent = 0;
    uint32_t lastLevelConstantCounter = 0;
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

    if (auto content = readFileContent (manifestFile))
    {
        manifest = choc::json::parse (*content);

        ID = {};
        name = {};
        description = {};
        category = {};
        manufacturer = {};
        version = {};
        mainProcessor = {};
        isInstrument = false;
        sourceFiles.clear();
        views.clear();
        resources.clear();

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
            mainProcessor  = manifest["mainProcessor"].toString();
            version        = manifest["version"].toString();
            isInstrument   = manifest["isInstrument"].getWithDefault<bool> (false);

            if (ID.length() < 4)
                throw std::runtime_error ("The manifest must contain a valid and globally unique \"ID\" property");

            if (name.length() > 128 || name.empty())
                throw std::runtime_error ("The manifest must contain a valid \"name\" property");

            if (version.length() > 24 || version.empty())
                throw std::runtime_error ("The manifest must contain a valid \"version\" property");

            addStrings (sourceFiles, manifest["source"]);
            addView (manifest["view"]);
            addWorker (manifest["worker"]);
            addStrings (resources, manifest["resources"]);

            return true;
        }
    }

    throw std::runtime_error ("The patch file did not contain a valid JSON object");
}

inline std::optional<std::string> PatchManifest::readFileContent (const std::string& file) const
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

inline void PatchManifest::addStrings (std::vector<std::string>& strings, const choc::value::ValueView& source)
{
    if (source.isString())
    {
        strings.push_back (source.get<std::string>());
    }
    else if (source.isArray())
    {
        for (auto f : source)
            addStrings (strings, f);
    }
}

inline void PatchManifest::addView (const choc::value::ValueView& view)
{
    if (view.isArray())
    {
        for (auto e : view)
            addView (e);
    }
    else if (view.isObject())
    {
        views.push_back (View { choc::value::Value (view) });
    }
}

inline void PatchManifest::addWorker (const choc::value::ValueView& worker)
{
    if (worker.isString())
        patchWorker = worker.toString();
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

inline std::unordered_map<std::string, choc::value::ValueView> PatchManifest::getExternalsList() const
{
    std::unordered_map<std::string, choc::value::ValueView> result;

    if (auto externals = manifest["externals"]; externals.isObject())
    {
        for (uint32_t i = 0; i < externals.size(); i++)
        {
            auto member = externals.getObjectMemberAt (i);
            result[member.name] = member.value;
        }
    }

    return result;
}

inline std::function<choc::value::Value(const cmaj::ExternalVariable&)> PatchManifest::createExternalResolverFunction() const
{
    return [this, list = getExternalsList()] (const cmaj::ExternalVariable& v) -> choc::value::Value
    {
        auto external = list.find (v.name);

        if (external != list.end())
            return replaceFilenameStringsWithAudioData (*this, external->second, v.annotation);

        return {};
    };
}

inline bool PatchManifest::addSourceFilesToProgram (Program& program,
                                                    DiagnosticMessageList& errors,
                                                    const std::function<void()>& checkForStopSignal)
{
    if (needsToBuildSource)
    {
        for (auto& file : sourceFiles)
        {
            checkForStopSignal();

            if (auto content = readFileContent (file))
            {
                if (! program.parse (errors, getFullPathForFile (file), std::move (*content)))
                    return false;
            }
            else
            {
                errors.add (cmaj::DiagnosticMessage::createError ("Could not open source file: " + file, {}));
                return false;
            }
        }
    }

    return true;
}

//==============================================================================
inline std::optional<std::string> readJavascriptResource (std::string_view path, const PatchManifest* manifest)
{
    auto pathToFind = std::filesystem::path (path).relative_path().generic_string();

    if (manifest != nullptr)
        if (auto content = manifest->readFileContent (pathToFind))
            return content;

    if (choc::text::startsWith (pathToFind, "cmaj_api/"))
    {
        auto subPath = pathToFind.substr (std::string_view ("cmaj_api/").length());

        if (subPath == "cmaj-version.js")
            return std::string ("export function getCmajorVersion() { return \"") + cmaj::Library::getVersion() + "\"; }";

        if (auto content = EmbeddedWebAssets::findResource (subPath); ! content.empty())
            return std::string (content);
    }

    return {};
}

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

inline choc::value::Value& TimelineEventGenerator::getTransportStateEvent (bool isRecording, bool isPlaying, bool isLooping)
{
    transportState.setMember ("flags", (isPlaying ? 1 : 0) | (isRecording ? 2 : 0) | (isLooping ? 4 : 0));
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
inline choc::value::Value readManifestResourceAsAudioData (const PatchManifest& manifest,
                                                           const std::string& path,
                                                           const choc::value::ValueView& annotation)
{
    choc::value::Value audioFileContent;

    if (auto reader = manifest.createFileReader (path))
    {
        choc::audio::AudioFileFormatList formats;
        formats.addFormat<choc::audio::OggAudioFileFormat<false>>();
        formats.addFormat<choc::audio::MP3AudioFileFormat>();
        formats.addFormat<choc::audio::FLACAudioFileFormat<false>>();
        formats.addFormat<choc::audio::WAVAudioFileFormat<true>>();

        auto error = cmaj::readAudioFileAsValue (audioFileContent, formats, reader, annotation);

        if (! error.empty())
            return {};
    }

    return audioFileContent;
}

inline choc::value::Value replaceFilenameStringsWithAudioData (const PatchManifest& manifest,
                                                               const choc::value::ValueView& v,
                                                               const choc::value::ValueView& annotation)
{
    if (v.isVoid())
        return {};

    if (v.isString())
    {
        try
        {
            auto audio = readManifestResourceAsAudioData (manifest, v.get<std::string>(), annotation);

            if (! audio.isVoid())
                return audio;
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

    endpointID = details.endpointID.toString();
    name       = details.annotation["name"].getWithDefault<std::string> (endpointID);
    unit       = details.annotation["unit"].toString();
    group      = details.annotation["group"].toString();
    minValue   = details.annotation["min"].getWithDefault<float> (0);
    maxValue   = details.annotation["max"].getWithDefault<float> (1.0f);
    step       = details.annotation["step"].getWithDefault<float> (0);

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
    discrete      = details.annotation["discrete"].getWithDefault<bool> (false);
    rampFrames    = details.annotation["rampFrames"].getWithDefault<uint32_t> (0);
}

inline float PatchParameterProperties::snapAndConstrainValue (float newValue) const
{
    if (getNumDiscreteOptions() > 1)
        return *toValueFromDiscreteOptionIndex (toDiscreteOptionIndex (newValue));

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

inline std::optional<float> PatchParameterProperties::getStringAsValue (std::string_view text) const
{
    auto target = std::string (choc::text::trim (text));

    if (valueStrings.size() > 1)
        for (size_t i = 0; i < valueStrings.size(); ++i)
            if (choc::text::toLowerCase (choc::text::trim (valueStrings[i])) == choc::text::toLowerCase (target))
                return toValueFromDiscreteOptionIndex (i);

    try
    {
        return static_cast<float> (std::stod (target));
    }
    catch (...) {}

    return {};
}

inline float PatchParameterProperties::parseValue (const choc::value::ValueView& v) const
{
    if (v.isString())
        if (auto val = getStringAsValue (v.getString()))
            return *val;

    return v.getWithDefault<float> (defaultValue);
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

inline uint64_t PatchParameterProperties::getNumDiscreteOptions() const
{
    if (valueStrings.size() > 1)
        return valueStrings.size();

    if (boolean)
        return 2;

    if (discrete && step > 0)
        return static_cast<uint64_t> ((maxValue - minValue) / step) + 1u;

    return 0;
}

inline size_t PatchParameterProperties::toDiscreteOptionIndex (float value) const
{
    auto numDiscreteSteps = getNumDiscreteOptions();
    return std::min (static_cast<size_t> (convertTo0to1 (value) * (float) numDiscreteSteps),
                     static_cast<size_t> (numDiscreteSteps > 0 ? numDiscreteSteps - 1 : 0));
}

inline std::optional<float> PatchParameterProperties::toValueFromDiscreteOptionIndex (size_t i) const
{
    if (auto numDiscreteSteps = getNumDiscreteOptions(); numDiscreteSteps > 1)
    {
        auto index0to1 = static_cast<double> (i) / static_cast<double> (numDiscreteSteps - 1);
        return convertFrom0to1 (static_cast<float> (index0to1));
    }

    return {};
}

//==============================================================================
inline void CPUMonitor::reset (double sampleRate)
{
    inverseRate = 1.0 / sampleRate;
    frameCount = 0;
    average = 0;
}

inline void CPUMonitor::startProcess()
{
    minFramesPerCallback = framesPerCallback;

    if (minFramesPerCallback != 0)
    {
        processStartTime = Clock::now();
    }
    else
    {
        frameCount = 0;
        average = 0;
    }
}

inline void CPUMonitor::endProcess (uint32_t numFrames)
{
    if (minFramesPerCallback != 0)
    {
        Seconds secondsInBlock = Clock::now() - processStartTime;
        auto blockLengthSeconds = Seconds (numFrames * inverseRate);
        auto proportionInBlock = secondsInBlock / blockLengthSeconds;

        average += (proportionInBlock - average) * 0.1;

        if (average < 0.001)
            average = 0;

        frameCount += numFrames;

        if (frameCount >= minFramesPerCallback)
        {
            frameCount = 0;
            auto newLevel = static_cast<float> (average);

            if (++lastLevelConstantCounter > 10
                || std::fabs (lastLevelSent - newLevel) > 0.002)
            {
                lastLevelConstantCounter = 0;
                lastLevelSent = newLevel;

                if (handleCPULevel)
                    handleCPULevel (newLevel);
            }
        }
    }
}

//==============================================================================
#if CMAJ_HAS_MESSAGE_LOOP
inline PatchFileChangeChecker::PatchFileChangeChecker (const PatchManifest& m, std::function<void(ChangeType)>&& onChange)
    : manifest (m), callback (std::move (onChange))
{
    checkAndReset();

    fileChangeCheckThread.start (1500, [this]
    {
        auto change = checkAndReset();

        if (change.cmajorFilesChanged || change.assetFilesChanged || change.manifestChanged)
            choc::messageloop::postMessage ([cb = callback, change] { cb (change); });
    });
}

inline PatchFileChangeChecker::~PatchFileChangeChecker()
{
    fileChangeCheckThread.stop();
    callback.reset();
}

inline PatchFileChangeChecker::ChangeType PatchFileChangeChecker::checkAndReset()
{
    SourceFilesWithTimes newManifests, newSources, newAssets;

    newManifests.add (manifest, manifest.manifestFile);

    for (auto& f : manifest.sourceFiles)
        newSources.add (manifest, f);

    for (auto& v : manifest.views)
        newAssets.add (manifest, v.getSource());

    if (! manifest.patchWorker.empty())
        newSources.add (manifest, manifest.patchWorker);

    ChangeType changes;

    if (manifestFiles != newManifests) { changes.manifestChanged    = true;  manifestFiles = std::move (newManifests); }
    if (cmajorFiles != newSources)     { changes.cmajorFilesChanged = true;  cmajorFiles = std::move (newSources); }
    if (assetFiles != newAssets)       { changes.assetFilesChanged  = true;  assetFiles = std::move (newAssets); }

    return changes;
}
#else
inline PatchFileChangeChecker::PatchFileChangeChecker (const PatchManifest&, std::function<void(ChangeType)>&&) {}
inline PatchFileChangeChecker::~PatchFileChangeChecker() {}
inline PatchFileChangeChecker::ChangeType PatchFileChangeChecker::checkAndReset() { return {}; }
#endif

} // namespace cmaj
