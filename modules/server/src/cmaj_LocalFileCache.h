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

#include <unordered_map>
#include <condition_variable>
#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "../../../include/cmajor/helpers/cmaj_PatchManifest.h"

namespace cmaj
{

template <typename Session>
struct LocalFileCache
{
    LocalFileCache (Session& s) : session (s) {}

    void clear()
    {
        for (auto& f : files)
            removeFile (f.first);
    }

    void registerFile (const std::filesystem::path& filename, uint64_t size)
    {
        auto f = getFile (filename);

        if (f == nullptr)
        {
            files.push_back (std::make_shared<File> (*this));
            f = files.back().get();
        }

        f->filename = filename;
        f->setSize (size);
        f->cancelRequests();
        dumpStatus();
    }

    void removeFile (const std::filesystem::path& filename)
    {
        for (auto i = files.begin(), e = files.end(); i != e; ++i)
        {
            if ((*i)->filename == filename)
            {
                (*i)->cancelRequests();
                files.erase (i);
                dumpStatus();
                break;
            }
        }
    }

    auto begin() const       { return files.begin(); }
    auto end() const         { return files.end(); }

    bool fileExists (const std::filesystem::path& filename) const
    {
        return getFile (filename) != nullptr;
    }

    uint64_t getFileSize (const std::filesystem::path& filename) const
    {
        if (auto f = getFile (filename))
            return f->size;

        return 0;
    }

    std::filesystem::file_time_type getModificationTime (const std::filesystem::path& filename) const
    {
        if (auto f = getFile (filename))
            return f->lastModificationTime;

        return {};
    }

    std::shared_ptr<std::istream> createFileStream (const std::filesystem::path& filename) const
    {
        if (auto f = getFilePtr (filename))
            return std::make_shared<Stream> (f);

        return {};
    }

    struct FileRegion
    {
        uint64_t start = 0, end = 0;

        size_t size() const                         { return static_cast<size_t> (end - start); }
        bool contains (FileRegion other) const      { return other.start >= start && other.end <= end; }
    };

    bool requestRead (const std::filesystem::path& filename,
                      FileRegion region,
                      std::function<void(const void*, size_t)> callback)
    {
        CMAJ_ASSERT (region.end >= region.start);

        if (auto file = getFile (filename))
            return file->requestRead (region, std::move (callback));

        return false;
    }

    bool handleMessageFromClientConcurrently (const choc::value::ValueView& message)
    {
        if (auto typeMember = message["type"]; typeMember.isString())
        {
            if (typeMember.getString() == "file_content")
            {
                handleFileContent (message);
                return true;
            }
        }

        return false;
    }

    bool handleMessageFromClient (std::string_view type, const choc::value::ValueView& message)
    {
        if (type == "file_content")
        {
            handleFileContent (message);
            return true;
        }

        if (type == "register_file")
        {
            if (auto filename = message["filename"]; filename.isString())
                if (auto size = message["size"]; size.isInt())
                    registerFile (std::filesystem::path (filename.getString()),
                                  static_cast<uint64_t> (std::max (static_cast<int64_t> (0), size.getWithDefault<int64_t> (0))));

            return true;
        }

        if (type == "remove_file")
        {
            if (auto filename = message["filename"]; filename.isString())
                removeFile (std::filesystem::path (filename.getString()));

            return true;
        }

        return false;
    }

    void handleFileContent (const choc::value::ValueView& message)
    {
        if (auto fileMember = message["file"]; fileMember.isString())
        {
            if (auto file = getFile (std::filesystem::path (fileMember.getString())))
            {
                if (auto startMember = message["start"]; startMember.isInt())
                {
                    std::vector<char> data;

                    if (auto dataMember = message["data"]; dataMember.isString())
                        if (! choc::base64::decodeToContainer (data, dataMember.getString()))
                            data.clear();

                    auto start = startMember.getWithDefault<uint64_t> (0);
                    file->setChunk ({ start, start + data.size() }, data.data());
                }
            }
        }
    }

    bool initialiseManifest (cmaj::PatchManifest& manifest, const std::filesystem::path& file)
    {
        manifest.manifestFile = file.filename().string();
        manifest.name = file.filename().string();

        auto folder = file.parent_path();

        const auto getFullPath = [folder] (const std::string& f) -> std::filesystem::path
        {
            return folder / std::filesystem::path (f).relative_path();
        };

        manifest.createFileReader = [this, getFullPath] (const std::string& f) -> std::shared_ptr<std::istream>
        {
            return createFileStream (getFullPath (f));
        };

        manifest.getFullPathForFile = [getFullPath] (const std::string& f) -> std::string
        {
            return getFullPath (f).string();
        };

        manifest.getFileModificationTime = [this, getFullPath] (const std::string& f) -> std::filesystem::file_time_type
        {
            return getModificationTime (getFullPath (f));
        };

        manifest.fileExists = [this, getFullPath] (const std::string& f) { return fileExists (getFullPath (f)); };

        return true;
    }

    void dumpStatus()
    {
        std::cout << "================================" << std::endl
                    << "Files: " << files.size() << std::endl
                    << std::endl;

        for (auto& f : files)
            std::cout << f->filename.generic_string() << ", " << choc::text::getByteSizeDescription (f->size) << std::endl;

        std::cout << std::endl;
    }

private:
    //==============================================================================
    static constexpr size_t chunkSize = 32768;

    static size_t getBlockIndex (uint64_t frame)        { return static_cast<size_t> (frame / chunkSize); }
    static uint64_t getBlockStart (uint64_t frame)      { return getBlockIndex (frame) * chunkSize; }
    static size_t getNumBlocksNeeded (uint64_t frames)  { return static_cast<size_t> ((frames + chunkSize - 1) / chunkSize); }

    //==============================================================================
    struct Chunk
    {
        FileRegion region;
        std::vector<char> data;

        FileRegion getAvailableRegion() const        { return { region.start, region.start + data.size() }; }
        bool isLoaded() const                        { return data.size() == region.size(); }

        size_t read (void* dest, FileRegion regionWanted) const
        {
            auto availableEnd = region.start + data.size();

            if (regionWanted.start > availableEnd || regionWanted.end <= region.start)
                return false;

            auto available = static_cast<size_t> (availableEnd - regionWanted.start);
            auto sizeToDo = std::min (regionWanted.size(), available);
            auto startOffset = static_cast<size_t> (regionWanted.start - region.start);

            if (dest != nullptr)
                std::memcpy (dest, data.data() + startOffset, sizeToDo);

            return sizeToDo;
        }
    };

    //==============================================================================
    struct ReadRequest
    {
        ReadRequest (FileRegion r, std::function<void(const void*, size_t)>&& c)
            : region (r), callback (std::move (c))
        {}

        FileRegion region;
        std::function<void(const void*, size_t)> callback;
        std::chrono::steady_clock::time_point creationTime { std::chrono::steady_clock::now() };
    };

    //==============================================================================
    struct File  : public std::enable_shared_from_this<File>
    {
        File (LocalFileCache& c) : owner (c) {}

        void setSize (size_t newSize)
        {
            lastModificationTime = std::filesystem::file_time_type::clock::now();
            size = newSize;
            chunks.clear();
            chunks.resize (getNumBlocksNeeded (size));

            for (size_t i = 0; i < chunks.size(); ++i)
                chunks[i].region = { i * chunkSize, std::min ((i + 1) * chunkSize, newSize) };
        }

        Chunk* getChunk (uint64_t position)
        {
            auto i = getBlockIndex (position);
            return i < chunks.size() ? std::addressof (chunks[i]) : nullptr;
        }

        const Chunk* getChunk (uint64_t position) const
        {
            auto i = getBlockIndex (position);
            return i < chunks.size() ? std::addressof (chunks[i]) : nullptr;
        }

        bool requestRead (FileRegion region, std::function<void(const void*, size_t)> callback)
        {
            CMAJ_ASSERT (region.end >= region.start);

            if (fulfilRequest (region, callback))
                return true;

            pendingRequests.push_back (std::make_unique<ReadRequest> (region, std::move (callback)));

            for (auto chunk : getBlocksNeededForRequest (region))
                owner.session.sendMessageToClient ("req_file_read", choc::json::create (
                                                                      "file", filename.generic_string(),
                                                                      "offset", static_cast<int64_t> (chunk.start),
                                                                      "size", static_cast<int64_t> (chunk.size())));
            return true;
        }

        std::vector<FileRegion> getBlocksNeededForRequest (FileRegion region)
        {
            std::vector<FileRegion> result;

            if (region.size() != 0)
                for (auto i = getBlockIndex (region.start); i <= getBlockIndex (region.end - 1); ++i)
                    if (! chunks[i].isLoaded())
                        result.push_back (chunks[i].region);

            return result;
        }

        bool fulfilRequest (FileRegion region, const std::function<void(const void*, size_t)>& callback)
        {
            if (auto c = getChunk (region.start))
            {
                if (c->getAvailableRegion().contains (region))
                {
                    callback (c->data.data() + static_cast<size_t> (region.start - c->region.start), region.size());
                    return true;
                }

                if (read (nullptr, region) == region.size())
                {
                    std::vector<char> assembledData;
                    assembledData.resize (region.size());
                    read (assembledData.data(), region);
                    callback (assembledData.data(), region.size());
                    return true;
                }
            }

            return false;
        }

        void dispatchFulfillableRequests()
        {
            for (size_t i = 0; i < pendingRequests.size(); ++i)
            {
                if (fulfilRequest (pendingRequests[i]->region, pendingRequests[i]->callback))
                {
                    pendingRequests.erase (pendingRequests.begin() + static_cast<int> (i));
                    --i;
                }
            }
        }

        void setChunk (FileRegion region, const void* data)
        {
            CMAJ_ASSERT (region.start == getBlockStart (region.start) && region.size() <= chunkSize);

            if (auto c = getChunk (region.start))
            {
                c->data.resize (region.size());
                std::memcpy (c->data.data(), data, region.size());
            }

            dispatchFulfillableRequests();
            cancelOldRequests();
        }

        size_t read (void* dest, FileRegion region) const
        {
            size_t total = 0;
            auto chunk = getChunk (region.start);

            while (region.size() > 0 && chunk != nullptr)
            {
                auto done = chunk->read (dest, region);

                if (done == 0)
                    break;

                total += done;
                region.start += done;

                if (dest != nullptr)
                    dest = static_cast<char*> (dest) + done;

                auto nextChunk = getChunk (region.start);

                if (nextChunk == chunk)
                    break;

                chunk = nextChunk;
            }

            return total;
        }

        void cancelRequests()
        {
            for (auto& r : pendingRequests)
                r->callback (nullptr, 0);
        }

        void cancelOldRequests()
        {
            auto oldestAllowedTime = std::chrono::steady_clock::now() - std::chrono::milliseconds (5000);
            std::vector<std::unique_ptr<ReadRequest>> cancelled;

            pendingRequests.erase (std::remove_if (pendingRequests.begin(), pendingRequests.end(),
                                   [&] (auto& r)
                                   {
                                       if (r->creationTime < oldestAllowedTime)
                                       {
                                           cancelled.push_back (std::move (r));
                                           return true;
                                       }

                                       return false;
                                   }),
                                   pendingRequests.end());

            for (auto& r : cancelled)
                r->callback (nullptr, 0);
        }

        LocalFileCache& owner;
        std::filesystem::path filename;
        uint64_t size = 0;
        std::vector<Chunk> chunks;
        std::vector<std::unique_ptr<ReadRequest>> pendingRequests;
        std::filesystem::file_time_type lastModificationTime;
    };

    //==============================================================================
    class Stream : public std::istream
    {
    public:
        Stream (std::shared_ptr<File> file)
            : std::istream (std::addressof (streamBuf)),
              streamBuf (file)
        {
        }

    private:
        struct StreamBuf  : public std::streambuf
        {
            StreamBuf (std::shared_ptr<File> f) : file (f)
            {
            }

            pos_type seekoff (off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
            {
                if (dir == std::ios_base::beg)
                    position = static_cast<pos_type> (off);
                else if (dir == std::ios_base::cur)
                    position += static_cast<pos_type> (off);
                else if (dir == std::ios_base::end)
                    position = static_cast<pos_type> (static_cast<off_type> (file->size) - off);

                return position;
            }

            pos_type seekpos (pos_type pos, std::ios_base::openmode) override
            {
                position = pos;
                return pos;
            }

            struct Condition
            {
                void trigger()
                {
                    std::unique_lock<std::mutex> lock (mutex);
                    triggered = true;
                    condition.notify_all();
                }

                bool wait (uint32_t milliseconds)
                {
                    std::unique_lock<std::mutex> lock (mutex);

                    if (! triggered)
                        condition.wait_for (lock, std::chrono::milliseconds (milliseconds),
                                            [this] { return triggered; });

                    return triggered;
                }

                std::mutex mutex;
                std::condition_variable condition;
                bool triggered = false;
            };

            std::streamsize xsgetn (char_type* dest, std::streamsize size) override
            {
                Condition condition;
                std::streamsize result = 0;

                choc::threading::ThreadSafeFunctor<std::function<void(const void*, size_t)>> callback;

                callback = [&condition, &result, dest, size] (const void* source, size_t sourceSize)
                {
                    if (source != nullptr)
                    {
                        CMAJ_ASSERT (sourceSize >= static_cast<size_t> (size));
                        std::memcpy (dest, source, static_cast<size_t> (size));
                        result = static_cast<std::streamsize> (size);
                    }

                    condition.trigger();
                };

                file->requestRead ({ static_cast<uint64_t> (position),
                                     static_cast<uint64_t> (position + static_cast<pos_type> (size)) },
                                   callback);

                if (condition.wait (5000))
                    return result;

                callback.reset();
                return 0;
            }

            std::shared_ptr<File> file;
            pos_type position = 0;
        };

        StreamBuf streamBuf { *this };
    };

    //==============================================================================
    Session& session;
    std::vector<std::shared_ptr<File>> files;

    File* getFile (const std::filesystem::path& filename) const
    {
        for (auto& f : files)
            if (f->filename == filename)
                return f.get();

        return {};
    }

    std::shared_ptr<File> getFilePtr (const std::filesystem::path& filename) const
    {
        for (auto& f : files)
            if (f->filename == filename)
                return f;

        return {};
    }
};

} // namespace cmaj
