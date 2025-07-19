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

#include <algorithm>
#include <optional>
#include <unordered_map>
#include "../../choc/choc/platform/choc_Platform.h"
#include "../../choc/choc/text/choc_Files.h"
#include "../../choc/choc/audio/choc_AudioFileFormat_WAV.h"
#include "../../choc/choc/audio/choc_AudioFileFormat_Ogg.h"
#include "../../choc/choc/audio/choc_AudioFileFormat_FLAC.h"
#include "../../choc/choc/audio/choc_AudioFileFormat_MP3.h"
#include "../API/cmaj_Program.h"
#include "../API/cmaj_ExternalVariables.h"

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

    /// An optional path to a source transformer .js file
    std::string sourceTransformer;

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
    bool addSourceFilesToProgram (Program&,
                                  DiagnosticMessageList&,
                                  const std::function<std::string(DiagnosticMessageList&, const std::string&, const std::string&)>& transformSource,
                                  const std::function<void()>& checkForStopSignal);

private:
    static void addStrings (std::vector<std::string>&, const choc::value::ValueView&);
    void addView (const choc::value::ValueView&);
    void addWorker (const choc::value::ValueView&);
    void addSourceTransformer (const choc::value::ValueView&);
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
            addSourceTransformer (manifest["sourceTransformer"]);
            addStrings (resources, manifest["resources"]);

            return true;
        }
    }

    throw std::runtime_error ("The patch file did not contain a valid JSON object");
}

inline std::optional<std::string> PatchManifest::readFileContent (const std::string& file) const
{
    try
    {
        if (auto stream = createFileReader (file))
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
    }
    catch (...) {}

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

inline void PatchManifest::addSourceTransformer (const choc::value::ValueView& transformer)
{
    if (transformer.isString())
        sourceTransformer = transformer.toString();
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

    if (manifest.isObject())
    {
        if (auto externals = manifest["externals"]; externals.isObject())
        {
            for (uint32_t i = 0; i < externals.size(); i++)
            {
                auto member = externals.getObjectMemberAt (i);
                result[member.name] = member.value;
            }
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
                                                    const std::function<std::string(DiagnosticMessageList&, const std::string&, const std::string&)>& transformSource,
                                                    const std::function<void()>& checkForStopSignal)
{
    if (needsToBuildSource)
    {
        for (auto& file : sourceFiles)
        {
            checkForStopSignal();

            if (auto content = readFileContent (file))
            {
                if (! program.parse (errors, getFullPathForFile (file), std::move (transformSource (errors, getFullPathForFile (file), *content))))
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


} // namespace cmaj
