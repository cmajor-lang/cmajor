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

#include "../../choc/threading/choc_ThreadSafeFunctor.h"
#include "../COM/cmaj_CacheDatabaseInterface.h"

namespace cmaj
{

//==============================================================================
/// A simple implementation of CacheDatabaseInterface that saves the data as
/// files in a given folder, and deletes the oldest files when a maximum
/// number exist
struct FileBasedCacheDatabase   : public choc::com::ObjectWithAtomicRefCount<CacheDatabaseInterface, FileBasedCacheDatabase>
{
    FileBasedCacheDatabase (std::filesystem::path parentFolder, size_t maxNumFilesAllowed)
       : folder (std::move (parentFolder)), maxNumFiles (maxNumFilesAllowed)
    {
        purgeThread.start (0, [this] { removeOldFiles(); });
    }

    virtual ~FileBasedCacheDatabase() = default;

    void store (const char* key, const void* dataToSave, uint64_t dataSize) override
    {
        try
        {
            std::lock_guard<decltype(lock)> l (lock);
            choc::file::replaceFileWithContent (getCacheFile (key).string(),
                                                std::string_view (static_cast<const char*> (dataToSave),
                                                                  static_cast<std::string_view::size_type> (dataSize)));
        }
        catch (...) {}

        purgeThread.trigger();
    }

    uint64_t reload (const char* key, void* destAddress, uint64_t destSize) override
    {
        std::lock_guard<decltype(lock)> l (lock);

        try
        {
            auto file = getCacheFile (key);
            auto size = file_size (file);

            if (size == 0)
                return 0;

            if (destAddress == nullptr || destSize < size)
                return size;

            std::fstream stream (file);
            stream.read (static_cast<char*> (destAddress), static_cast<std::streamsize> (size));

            if (stream.gcount() != static_cast<std::streamsize> (size))
                return 0;

            // write a byte at the end and then erase it to update the file's modification time
            stream.put (0);
            stream.sync();
            resize_file (file, size);

            return size;
        }
        catch (...) {}

        return 0;
    }

private:
    std::filesystem::path folder;
    size_t maxNumFiles = 0;
    std::mutex lock;
    choc::threading::TaskThread purgeThread;

    static std::string getFileNamePrefix()   { return "cmajor_cache_"; }

    std::filesystem::path getCacheFile (const std::string& key)    { return folder / (getFileNamePrefix() + key); }

    void removeOldFiles()
    {
        std::lock_guard<decltype(lock)> l (lock);

        struct File
        {
            std::filesystem::path file;
            std::filesystem::file_time_type time;

            bool operator< (const File& other) const     { return time < other.time; }
        };

        std::vector<File> files;

        for (auto& f : std::filesystem::directory_iterator { folder })
        {
            if (choc::text::startsWith (f.path().stem().string(), getFileNamePrefix()))
            {
                try
                {
                    auto time = last_write_time (f.path());
                    files.push_back ({ f.path(), time });
                }
                catch (...) {}
            }
        }

        std::sort (files.begin(), files.end());

        if (files.size() > maxNumFiles)
        {
            for (size_t i = 0; i < files.size() - maxNumFiles; ++i)
            {
                try
                {
                    remove (files[i].file);
                }
                catch (...) {}
            }
        }
    }
};

} // namespace cmaj
