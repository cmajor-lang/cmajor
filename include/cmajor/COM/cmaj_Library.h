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

#include "../../choc/platform/choc_DynamicLibrary.h"
#include "../../choc/platform/choc_Platform.h"
#include "cmaj_EngineFactoryInterface.h"
#include <memory>

#ifndef CMAJ_ASSERT
 #define CMAJ_ASSERT(x) CHOC_ASSERT(x)
 #define CMAJ_ASSERT_FALSE CMAJ_ASSERT(false)
#endif

#ifndef CMAJOR_DLL
 /// This flag tells the helper functions below whether to load their functions
 /// from a DLL or whether they are being linked directly into the project
 #define CMAJOR_DLL 1
#endif

namespace cmaj
{

/** Manages the entry-point functions to the Cmajor compiler library.

    If loading the library as a DLL, you'll need to make sure you make a
    successful call to cmaj::Library::initialise ("path to your DLL") before
    using any of the functions in the Library class.

    Note that if you stick to using the helper classes cmaj::Program and
    cmaj::Performer, then you don't need to know or care about this Library
    stuff or the details of the COM API.
*/
struct Library
{
    /// Attempts to initialise the library for a DLL at the given file location.
    /// Returns true if it was successfully loaded.
    static bool initialise (std::string_view pathToDLL);

    /// Returns the Cmajor engine version number
    static const char* getVersion();

    /// Creates a new, empty Program
    static cmaj::ProgramPtr createProgram();

    /// Returns a space-separated list of available factory types
    static const char* getEngineTypes();

    /// Creates a factory with the given name, or nullptr if the name isn't found.
    /// EngineFactory is a COM class, so the caller needs to release it when no longer
    /// needed, or more sensibly capture it in a choc::com::Ptr
    static EngineFactoryPtr createEngineFactory (const char* engineType = nullptr);

    /// Returns the standard name of the Cmajor DLL for the current platform
    static constexpr const char* getDLLName();

    //==============================================================================
    /// (Used internally)
    struct EntryPoints
    {
        virtual ~EntryPoints() {}
        virtual const char* getVersion() = 0;
        virtual cmaj::ProgramInterface* createProgram() = 0;
        virtual const char* getEngineTypes() = 0;
        virtual EngineFactoryInterface* createEngineFactory (const char*) = 0;
    };

   #if CMAJOR_DLL
private:
    static EntryPoints& getEntryPoints();
    static std::unique_ptr<choc::file::DynamicLibrary>& getLibrary();
    static inline EntryPoints* entryPoints = nullptr;
   #endif
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

#if CMAJOR_DLL

/// This is the name of the single entry point function to the DLL - when
/// there's a breaking change to the API, this will be updated to prevent
/// accidental use of older (or newer) library versions.
static constexpr const char* entryPointFunction = "cmajor_getEntryPointsV4";

inline std::unique_ptr<choc::file::DynamicLibrary>& Library::getLibrary()
{
    static std::unique_ptr<choc::file::DynamicLibrary> library;
    return library;
}

inline Library::EntryPoints& Library::getEntryPoints()
{
    // Must call Library::initialise() to load the DLL before using any other functions!
    CMAJ_ASSERT (entryPoints != nullptr);
    return *entryPoints;
}

inline bool Library::initialise (std::string_view pathToDLL)
{
    auto& library = getLibrary();

    if (library != nullptr)
        return true;

    if (pathToDLL.empty())
        return false;

    library = std::make_unique<choc::file::DynamicLibrary> (pathToDLL);

    if (library->handle == nullptr)
    {
       #if CHOC_WINDOWS
        constexpr char separator = '\\';
       #else
        constexpr char separator = '/';
       #endif

        auto path = std::string (pathToDLL);

        if (path.back() != separator)
            path += separator;

        library = std::make_unique<choc::file::DynamicLibrary> (path + getDLLName());
    }

    if (library->handle != nullptr)
    {
        using GetEntryPointsFn = EntryPoints*(*)();

        if (auto fn = (GetEntryPointsFn) library->findFunction (entryPointFunction))
        {
            entryPoints = fn();

            if (entryPoints != nullptr)
                return true;
        }
    }

    library.reset();
    return false;
}

inline const char* Library::getVersion()                { return getEntryPoints().getVersion(); }
inline cmaj::ProgramPtr Library::createProgram()        { return cmaj::ProgramPtr (getEntryPoints().createProgram()); }
inline const char* Library::getEngineTypes()            { return getEntryPoints().getEngineTypes(); }

inline EngineFactoryPtr Library::createEngineFactory (const char* engineName)
{
    auto& library = getLibrary();

    if (library == nullptr)
        return {};

    return cmaj::EngineFactoryPtr (getEntryPoints().createEngineFactory (engineName));
}

#else

inline bool Library::initialise (std::string_view) { return true; }

#endif

constexpr const char* Library::getDLLName()
{
   #if CHOC_WINDOWS
    return "CmajPerformer.dll";
   #elif CHOC_OSX
    return "libCmajPerformer.dylib";
   #else
    return "libCmajPerformer.so";
   #endif
}


} // namespace cmaj
