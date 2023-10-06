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

#include "cmaj_PerformerInterface.h"

#ifdef __clang__
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#elif __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#endif

namespace cmaj
{

//==============================================================================
/**
    A COM base class for implementing a database of cached binary objects that the
    runtime can use to re-load previously compiled programs, avoiding the need to
    re-link them.
*/
struct CacheDatabaseInterface   : public choc::com::Object
{
    /// Asks the cache to store a block of data for a given key.
    /// The hash is a string, and the cache should overwrite an existing entry with the
    /// same name if one exists.
    virtual void store (const char* key, const void* dataToSave, uint64_t dataSize) = 0;

    /// Looks for, and copies out, an existing entry in the cache.
    /// If the key is found, the cache object must copy the stored data into the given buffer.
    /// If the buffer is nullptr, nothing will be copied.
    /// The method returns the size of the object that was found, or 0 if no such key exists
    /// in the database.
    virtual uint64_t reload (const char* key, void* destAddress, uint64_t destSize) = 0;

    /// handy smart-pointer type for handling these objects
    using Ptr = choc::com::Ptr<CacheDatabaseInterface>;
};


} // namespace cmaj

#ifdef __clang__
 #pragma clang diagnostic pop
#elif __GNUC__
 #pragma GCC diagnostic pop
#endif
