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

#include "../../choc/platform/choc_DynamicLibrary.h"
#include "../../choc/platform/choc_Platform.h"
#include "cmaj_EngineFactoryInterface.h"
#include <memory>

#ifndef CMAJOR_DLL
 /// This flag tells the helper functions below whether to load their functions
 /// from a DLL or whether they are being linked directly into the project
 #define CMAJOR_DLL 1
#endif

#ifdef __clang__
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#elif __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
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

    /// Frees the global instance of the library (although if there are engine objects
    /// still using it, the library that they're using will remain active until they
    /// are all destroyed).
    static void shutdown();

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
        // NB: no virtual destructor because this object is passed over a DLL boundary
        virtual const char* getVersion() = 0;
        virtual cmaj::ProgramInterface* createProgram() = 0;
        virtual const char* getEngineTypes() = 0;
        virtual EngineFactoryInterface* createEngineFactory (const char*) = 0;
    };

   #if CMAJOR_DLL
    using SharedLibraryPtr = std::shared_ptr<choc::file::DynamicLibrary>;
   #else
    struct SharedLibraryPtr {};
   #endif

    /// This returns an opaque ref-counted pointer that a client can use to make
    /// sure the library doesn't go out of scope while it's in use.
    static SharedLibraryPtr getSharedLibraryPtr();

private:
   #if CMAJOR_DLL
    static SharedLibraryPtr& getSharedLibraryPtrRef();
    static EntryPoints& getEntryPoints();
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
static constexpr const char* entryPointFunction = "cmajor_getEntryPointsV10";

inline Library::SharedLibraryPtr& Library::getSharedLibraryPtrRef()
{
    static SharedLibraryPtr library;
    return library;
}

inline Library::SharedLibraryPtr Library::getSharedLibraryPtr()
{
    return getSharedLibraryPtrRef();
}

inline Library::EntryPoints& Library::getEntryPoints()
{
    // Must call Library::initialise() to load the DLL before using any other functions!
    CHOC_ASSERT (entryPoints != nullptr);
    return *entryPoints;
}

inline bool Library::initialise (std::string_view pathToDLL)
{
    auto& library = getSharedLibraryPtrRef();

    if (library != nullptr)
        return true;

    if (pathToDLL.empty())
        return false;

    library = std::make_shared<choc::file::DynamicLibrary> (pathToDLL);

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

        library = std::make_shared<choc::file::DynamicLibrary> (path + getDLLName());
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

inline void Library::shutdown()
{
    entryPoints = {};
    getSharedLibraryPtrRef().reset();
}

inline const char* Library::getVersion()                { return getEntryPoints().getVersion(); }
inline cmaj::ProgramPtr Library::createProgram()        { return cmaj::ProgramPtr (getEntryPoints().createProgram()); }
inline const char* Library::getEngineTypes()            { return getEntryPoints().getEngineTypes(); }

inline EngineFactoryPtr Library::createEngineFactory (const char* engineName)
{
    if (getSharedLibraryPtr() == nullptr)
        return {};

    return cmaj::EngineFactoryPtr (getEntryPoints().createEngineFactory (engineName));
}

#else

inline bool Library::initialise (std::string_view) { return true; }
inline void Library::shutdown() {}
inline Library::SharedLibraryPtr Library::getSharedLibraryPtr() { return {}; }

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

#ifdef __clang__
 #pragma clang diagnostic pop
#elif __GNUC__
 #pragma GCC diagnostic pop
#endif
