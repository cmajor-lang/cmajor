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

#include "cmaj_PerformerInterface.h"


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
