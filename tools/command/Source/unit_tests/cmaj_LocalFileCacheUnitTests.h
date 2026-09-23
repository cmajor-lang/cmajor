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

#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"

#include "choc/platform/choc_UnitTest.h"
#include "choc/threading/choc_ThreadSafeFunctor.h"
#include "choc/memory/choc_Base64.h"
#include "choc/text/choc_JSON.h"

#include "../../../modules/server/src/cmaj_LocalFileCache.h"

#include <cstddef>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace cmaj::local_file_cache_tests
{

/// The block size that the cache splits files into - must match
/// LocalFileCache::chunkSize
static constexpr size_t testChunkSize = 32768;

//==============================================================================
/// Stands in for the server session that the cache normally talks to, recording
/// the messages that would have been sent to the client so tests can check them.
struct TestSession
{
    void sendMessageToClient (std::string_view type, const choc::value::ValueView& message)
    {
        messages.push_back ({ std::string (type), choc::value::Value (message) });
    }

    struct Message
    {
        std::string type;
        choc::value::Value content;
    };

    size_t countMessages (std::string_view type) const
    {
        size_t total = 0;

        for (auto& m : messages)
            if (m.type == type)
                ++total;

        return total;
    }

    void clearMessages()        { messages.clear(); }

    std::vector<Message> messages;
};

using Cache      = LocalFileCache<TestSession>;
using FileRegion = Cache::FileRegion;

//==============================================================================
/// A cache along with its mock session, and a stream which captures the status
/// dumps so that they don't get sprayed over the test report.
struct TestCache
{
    std::string getOutput() const       { return output.str(); }
    void clearOutput()                  { output.str ({}); }

    TestSession session;
    std::ostringstream output;
    Cache cache { session, output };
};

//==============================================================================
/// Records what a read request's callback was given, so that a test can tell
/// whether it was fulfilled, cancelled, or is still pending.
struct ReadResult
{
    std::function<void(const void*, size_t)> getCallback()
    {
        return [this] (const void* source, size_t size)
        {
            ++numCallbacks;

            if (source == nullptr)
            {
                wasCancelled = true;
            }
            else
            {
                auto s = static_cast<const char*> (source);
                data.assign (s, s + size);
            }
        };
    }

    int numCallbacks = 0;
    bool wasCancelled = false;
    std::vector<char> data;
};

//==============================================================================
static std::vector<char> createTestData (size_t size)
{
    std::vector<char> data;
    data.reserve (size);

    for (size_t i = 0; i < size; ++i)
        data.push_back (static_cast<char> ('a' + (i % 26)));

    return data;
}

static std::vector<char> slice (const std::vector<char>& data, size_t start, size_t end)
{
    return std::vector<char> (data.begin() + static_cast<std::ptrdiff_t> (start),
                              data.begin() + static_cast<std::ptrdiff_t> (end));
}

/// Feeds a block of file content to the cache, as the client would.
static void sendChunk (TestCache& t, const std::filesystem::path& file,
                       uint64_t start, const std::vector<char>& data)
{
    t.cache.handleMessageFromClient ("file_content",
                                     choc::json::create ("file", file.generic_string(),
                                                         "start", static_cast<int64_t> (start),
                                                         "data", choc::base64::encodeToString (data)));
}

static size_t countFiles (const TestCache& t)
{
    return static_cast<size_t> (std::distance (t.cache.begin(), t.cache.end()));
}

//==============================================================================
static void testRegisterAndRemoveFiles (choc::test::TestProgress& progress)
{
    CHOC_TEST (RegisterAndRemoveFiles)

    TestCache t;
    const std::filesystem::path fileA { "/patches/a.txt" }, fileB { "/patches/b.txt" };

    // an empty cache knows nothing about anything
    CHOC_EXPECT_FALSE (t.cache.fileExists (fileA));
    CHOC_EXPECT_EQ (t.cache.getFileSize (fileA), static_cast<uint64_t> (0));
    CHOC_EXPECT_TRUE (t.cache.getModificationTime (fileA) == std::filesystem::file_time_type());
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (0));

    t.cache.registerFile (fileA, 100);
    t.cache.registerFile (fileB, 40000);

    CHOC_EXPECT_TRUE (t.cache.fileExists (fileA));
    CHOC_EXPECT_TRUE (t.cache.fileExists (fileB));
    CHOC_EXPECT_FALSE (t.cache.fileExists ("/patches/missing.txt"));
    CHOC_EXPECT_EQ (t.cache.getFileSize (fileA), static_cast<uint64_t> (100));
    CHOC_EXPECT_EQ (t.cache.getFileSize (fileB), static_cast<uint64_t> (40000));
    CHOC_EXPECT_EQ (t.cache.getFileSize ("/patches/missing.txt"), static_cast<uint64_t> (0));
    CHOC_EXPECT_TRUE (t.cache.getModificationTime (fileA) != std::filesystem::file_time_type());
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (2));

    // re-registering updates a file in place rather than duplicating it
    t.cache.registerFile (fileA, 250);
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (2));
    CHOC_EXPECT_EQ (t.cache.getFileSize (fileA), static_cast<uint64_t> (250));

    // a zero-length file is still a file
    t.cache.registerFile ("/patches/empty.txt", 0);
    CHOC_EXPECT_TRUE (t.cache.fileExists ("/patches/empty.txt"));
    CHOC_EXPECT_EQ (t.cache.getFileSize ("/patches/empty.txt"), static_cast<uint64_t> (0));

    t.cache.removeFile (fileA);
    CHOC_EXPECT_FALSE (t.cache.fileExists (fileA));
    CHOC_EXPECT_TRUE (t.cache.fileExists (fileB));
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (2));

    // removing something that was never there is harmless
    t.cache.removeFile ("/patches/missing.txt");
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (2));
}

//==============================================================================
static void testClear (choc::test::TestProgress& progress)
{
    CHOC_TEST (Clear)

    ReadResult read;
    TestCache t;

    t.cache.registerFile ("/a.txt", 100);
    t.cache.registerFile ("/b.txt", 100);

    // nothing is loaded yet, so this request stays pending
    CHOC_EXPECT_TRUE (t.cache.requestRead ("/a.txt", { 0, 50 }, read.getCallback()));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);

    t.cache.clear();

    // clearing cancels any outstanding requests and empties the cache
    CHOC_EXPECT_EQ (read.numCallbacks, 1);
    CHOC_EXPECT_TRUE (read.wasCancelled);
    CHOC_EXPECT_TRUE (read.data.empty());
    CHOC_EXPECT_FALSE (t.cache.fileExists ("/a.txt"));
    CHOC_EXPECT_FALSE (t.cache.fileExists ("/b.txt"));
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (0));

    // and clearing an already-empty cache is a no-op
    t.cache.clear();
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (0));
}

//==============================================================================
static void testFileRegions (choc::test::TestProgress& progress)
{
    CHOC_TEST (FileRegions)

    FileRegion empty { 10, 10 }, small { 10, 20 }, large { 0, 100 };

    CHOC_EXPECT_EQ (empty.size(), static_cast<size_t> (0));
    CHOC_EXPECT_EQ (small.size(), static_cast<size_t> (10));
    CHOC_EXPECT_EQ (large.size(), static_cast<size_t> (100));

    CHOC_EXPECT_TRUE (large.contains (small));
    CHOC_EXPECT_TRUE (large.contains (large));
    CHOC_EXPECT_TRUE (small.contains (small));
    CHOC_EXPECT_TRUE (small.contains (FileRegion { 12, 18 }));
    CHOC_EXPECT_FALSE (small.contains (large));
    CHOC_EXPECT_FALSE (small.contains (FileRegion { 5, 15 }));
    CHOC_EXPECT_FALSE (small.contains (FileRegion { 15, 25 }));
}

//==============================================================================
static void testReadRequests (choc::test::TestProgress& progress)
{
    CHOC_TEST (ReadRequests)

    ReadResult read;
    TestCache t;

    const std::filesystem::path file { "/patches/data.bin" };
    auto content = createTestData (100);

    // a request for a file that isn't registered is rejected
    CHOC_EXPECT_FALSE (t.cache.requestRead (file, { 0, 10 }, read.getCallback()));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);

    t.cache.registerFile (file, content.size());
    t.session.clearMessages();

    // nothing is loaded, so the request is queued and the data asked for
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 0, 100 }, read.getCallback()));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);
    CHOC_EXPECT_EQ (t.session.countMessages ("req_file_read"), static_cast<size_t> (1));
    CHOC_EXPECT_EQ (t.session.messages.front().content["file"].toString(), file.generic_string());
    CHOC_EXPECT_EQ (t.session.messages.front().content["offset"].getWithDefault<int64_t> (-1), static_cast<int64_t> (0));
    CHOC_EXPECT_EQ (t.session.messages.front().content["size"].getWithDefault<int64_t> (-1), static_cast<int64_t> (100));

    // when the content arrives, the pending request is fulfilled with it
    sendChunk (t, file, 0, content);
    CHOC_EXPECT_EQ (read.numCallbacks, 1);
    CHOC_EXPECT_FALSE (read.wasCancelled);
    CHOC_EXPECT_TRUE (read.data == content);

    // now it's cached, a request is fulfilled immediately without asking the client
    t.session.clearMessages();
    ReadResult cached;
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 20, 60 }, cached.getCallback()));
    CHOC_EXPECT_EQ (cached.numCallbacks, 1);
    CHOC_EXPECT_TRUE (cached.data == slice (content, 20, 60));
    CHOC_EXPECT_EQ (t.session.countMessages ("req_file_read"), static_cast<size_t> (0));

    // an empty request is trivially satisfied
    ReadResult nothing;
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 10, 10 }, nothing.getCallback()));
    CHOC_EXPECT_EQ (nothing.numCallbacks, 1);
    CHOC_EXPECT_TRUE (nothing.data.empty());
}

//==============================================================================
static void testMultiChunkReads (choc::test::TestProgress& progress)
{
    CHOC_TEST (MultiChunkReads)

    ReadResult read;
    TestCache t;

    const std::filesystem::path file { "/patches/big.bin" };
    auto content = createTestData (testChunkSize + 5000);

    t.cache.registerFile (file, content.size());
    t.session.clearMessages();

    // a request straddling the block boundary needs two blocks from the client
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { testChunkSize - 100, testChunkSize + 100 }, read.getCallback()));
    CHOC_EXPECT_EQ (t.session.countMessages ("req_file_read"), static_cast<size_t> (2));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);

    // the first block on its own isn't enough to satisfy it
    sendChunk (t, file, 0, slice (content, 0, testChunkSize));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);

    // but once the second arrives, the data is assembled across the boundary
    sendChunk (t, file, testChunkSize, slice (content, testChunkSize, content.size()));
    CHOC_EXPECT_EQ (read.numCallbacks, 1);
    CHOC_EXPECT_FALSE (read.wasCancelled);
    CHOC_EXPECT_TRUE (read.data == slice (content, testChunkSize - 100, testChunkSize + 100));

    // a subsequent spanning read is served straight from the cached blocks
    t.session.clearMessages();
    ReadResult spanning;
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 0, testChunkSize + 5000 }, spanning.getCallback()));
    CHOC_EXPECT_EQ (spanning.numCallbacks, 1);
    CHOC_EXPECT_TRUE (spanning.data == content);
    CHOC_EXPECT_EQ (t.session.countMessages ("req_file_read"), static_cast<size_t> (0));
}

//==============================================================================
static void testRequestCancellation (choc::test::TestProgress& progress)
{
    CHOC_TEST (RequestCancellation)

    ReadResult removed, reRegistered, pastEnd;
    TestCache t;

    t.cache.registerFile ("/a.txt", 100);
    t.cache.registerFile ("/b.txt", 100);
    t.cache.registerFile ("/c.txt", 100);

    // removing a file cancels its outstanding requests
    CHOC_EXPECT_TRUE (t.cache.requestRead ("/a.txt", { 0, 50 }, removed.getCallback()));
    CHOC_EXPECT_EQ (removed.numCallbacks, 0);
    t.cache.removeFile ("/a.txt");
    CHOC_EXPECT_EQ (removed.numCallbacks, 1);
    CHOC_EXPECT_TRUE (removed.wasCancelled);
    CHOC_EXPECT_TRUE (removed.data.empty());

    // and so does re-registering one, since its contents may have changed
    CHOC_EXPECT_TRUE (t.cache.requestRead ("/b.txt", { 0, 50 }, reRegistered.getCallback()));
    CHOC_EXPECT_EQ (reRegistered.numCallbacks, 0);
    t.cache.registerFile ("/b.txt", 200);
    CHOC_EXPECT_EQ (reRegistered.numCallbacks, 1);
    CHOC_EXPECT_TRUE (reRegistered.wasCancelled);

    // a request running past the end of the file can never be satisfied, so no
    // blocks are asked for, and it waits to be cancelled
    t.session.clearMessages();
    CHOC_EXPECT_TRUE (t.cache.requestRead ("/c.txt", { 50, 100000 }, pastEnd.getCallback()));
    CHOC_EXPECT_EQ (t.session.countMessages ("req_file_read"), static_cast<size_t> (0));
    CHOC_EXPECT_EQ (pastEnd.numCallbacks, 0);

    t.cache.removeFile ("/c.txt");
    CHOC_EXPECT_EQ (pastEnd.numCallbacks, 1);
    CHOC_EXPECT_TRUE (pastEnd.wasCancelled);
}

//==============================================================================
static void testCancelledRequestsAreForgotten (choc::test::TestProgress& progress)
{
    CHOC_TEST (CancelledRequestsAreForgotten)

    ReadResult read;
    TestCache t;

    const std::filesystem::path file { "/patches/reload.bin" };
    auto content = createTestData (100);

    t.cache.registerFile (file, content.size());
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 0, 100 }, read.getCallback()));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);

    // re-registering the file cancels the outstanding request...
    t.cache.registerFile (file, content.size());
    CHOC_EXPECT_EQ (read.numCallbacks, 1);
    CHOC_EXPECT_TRUE (read.wasCancelled);

    // ...and drops it, so cancelling again doesn't notify it a second time
    t.cache.registerFile (file, content.size());
    CHOC_EXPECT_EQ (read.numCallbacks, 1);

    // ...nor does the content turning up afterwards
    sendChunk (t, file, 0, content);
    CHOC_EXPECT_EQ (read.numCallbacks, 1);
    CHOC_EXPECT_TRUE (read.data.empty());

    // ...nor does removing the file
    t.cache.removeFile (file);
    CHOC_EXPECT_EQ (read.numCallbacks, 1);

    // and a fresh request against the reloaded file still behaves normally
    ReadResult reloaded;
    t.cache.registerFile (file, content.size());
    sendChunk (t, file, 0, content);
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 0, 100 }, reloaded.getCallback()));
    CHOC_EXPECT_EQ (reloaded.numCallbacks, 1);
    CHOC_EXPECT_FALSE (reloaded.wasCancelled);
    CHOC_EXPECT_TRUE (reloaded.data == content);
}

//==============================================================================
static void testMessageHandling (choc::test::TestProgress& progress)
{
    CHOC_TEST (MessageHandling)

    TestCache t;
    const std::filesystem::path file { "/patches/msg.bin" };
    auto content = createTestData (64);

    // register_file
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("register_file",
                                                       choc::json::create ("filename", file.generic_string(),
                                                                           "size", static_cast<int64_t> (content.size()))));
    CHOC_EXPECT_TRUE (t.cache.fileExists (file));
    CHOC_EXPECT_EQ (t.cache.getFileSize (file), static_cast<uint64_t> (64));

    // a negative size gets clamped rather than wrapping around
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("register_file",
                                                       choc::json::create ("filename", "/patches/negative.bin",
                                                                           "size", static_cast<int64_t> (-10))));
    CHOC_EXPECT_TRUE (t.cache.fileExists ("/patches/negative.bin"));
    CHOC_EXPECT_EQ (t.cache.getFileSize ("/patches/negative.bin"), static_cast<uint64_t> (0));

    // malformed messages are claimed, but ignored
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("register_file", choc::json::create ("nonsense", 123)));
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("remove_file", choc::json::create ("nonsense", 123)));
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("file_content", choc::json::create ("nonsense", 123)));
    CHOC_EXPECT_EQ (countFiles (t), static_cast<size_t> (2));

    // file_content delivers data to a waiting request
    ReadResult read;
    CHOC_EXPECT_TRUE (t.cache.requestRead (file, { 0, 64 }, read.getCallback()));
    CHOC_EXPECT_EQ (read.numCallbacks, 0);
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("file_content",
                                                       choc::json::create ("file", file.generic_string(),
                                                                           "start", static_cast<int64_t> (0),
                                                                           "data", choc::base64::encodeToString (content))));
    CHOC_EXPECT_EQ (read.numCallbacks, 1);
    CHOC_EXPECT_TRUE (read.data == content);

    // content for an unknown file is ignored rather than blowing up
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("file_content",
                                                       choc::json::create ("file", "/patches/unknown.bin",
                                                                           "start", static_cast<int64_t> (0),
                                                                           "data", choc::base64::encodeToString (content))));

    // unrecognised message types are declined
    CHOC_EXPECT_FALSE (t.cache.handleMessageFromClient ("something_else",
                                                        choc::json::create ("filename", file.generic_string())));

    // the concurrent handler only claims file_content messages
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClientConcurrently (
                          choc::json::create ("type", "file_content",
                                              "file", file.generic_string(),
                                              "start", static_cast<int64_t> (0),
                                              "data", choc::base64::encodeToString (content))));
    CHOC_EXPECT_FALSE (t.cache.handleMessageFromClientConcurrently (choc::json::create ("type", "something_else")));
    CHOC_EXPECT_FALSE (t.cache.handleMessageFromClientConcurrently (choc::json::create ("no_type_member", 1)));

    // remove_file
    CHOC_EXPECT_TRUE (t.cache.handleMessageFromClient ("remove_file",
                                                       choc::json::create ("filename", file.generic_string())));
    CHOC_EXPECT_FALSE (t.cache.fileExists (file));
}

//==============================================================================
static void testFileStreams (choc::test::TestProgress& progress)
{
    CHOC_TEST (FileStreams)

    TestCache t;
    const std::filesystem::path file { "/patches/stream.bin" };
    auto content = createTestData (200);

    // there's no stream for a file the cache doesn't have
    CHOC_EXPECT_TRUE (t.cache.createFileStream (file) == nullptr);

    t.cache.registerFile (file, content.size());
    sendChunk (t, file, 0, content);

    auto stream = t.cache.createFileStream (file);
    CHOC_EXPECT_TRUE (stream != nullptr);

    // the whole file reads back through the stream
    std::vector<char> buffer (content.size());
    stream->read (buffer.data(), static_cast<std::streamsize> (buffer.size()));
    CHOC_EXPECT_EQ (static_cast<int64_t> (stream->gcount()), static_cast<int64_t> (content.size()));
    CHOC_EXPECT_TRUE (buffer == content);

    // seeking to an absolute position
    stream->clear();
    stream->seekg (50);
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (50));

    // ...and then relative to it
    stream->seekg (10, std::ios_base::cur);
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (60));

    std::vector<char> part (20);
    stream->read (part.data(), static_cast<std::streamsize> (part.size()));
    CHOC_EXPECT_EQ (static_cast<int64_t> (stream->gcount()), static_cast<int64_t> (20));
    CHOC_EXPECT_TRUE (part == slice (content, 60, 80));

    // seeking to the end reports the file's size, which is how callers measure it
    stream->clear();
    stream->seekg (0, std::ios_base::end);
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (content.size()));

    // the stream keeps its file alive even after it's dropped from the cache
    t.cache.removeFile (file);
    CHOC_EXPECT_FALSE (t.cache.fileExists (file));
    stream->clear();
    stream->seekg (0);
    stream->read (buffer.data(), static_cast<std::streamsize> (buffer.size()));
    CHOC_EXPECT_EQ (static_cast<int64_t> (stream->gcount()), static_cast<int64_t> (content.size()));
    CHOC_EXPECT_TRUE (buffer == content);
}

//==============================================================================
static void testStreamSequentialReads (choc::test::TestProgress& progress)
{
    CHOC_TEST (StreamSequentialReads)

    TestCache t;
    const std::filesystem::path file { "/patches/seq.bin" };
    auto content = createTestData (200);

    t.cache.registerFile (file, content.size());
    sendChunk (t, file, 0, content);

    auto stream = t.cache.createFileStream (file);
    CHOC_EXPECT_TRUE (stream != nullptr);

    // consecutive reads advance the position, returning consecutive data
    std::vector<char> first (50), second (50), rest (100);

    stream->read (first.data(), static_cast<std::streamsize> (first.size()));
    CHOC_EXPECT_EQ (static_cast<int64_t> (stream->gcount()), static_cast<int64_t> (50));
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (50));
    CHOC_EXPECT_TRUE (first == slice (content, 0, 50));

    stream->read (second.data(), static_cast<std::streamsize> (second.size()));
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (100));
    CHOC_EXPECT_TRUE (second == slice (content, 50, 100));

    stream->read (rest.data(), static_cast<std::streamsize> (rest.size()));
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (200));
    CHOC_EXPECT_TRUE (rest == slice (content, 100, 200));

    // a relative seek is measured from where the reads left off
    stream->seekg (-30, std::ios_base::cur);
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (170));

    std::vector<char> part (30);
    stream->read (part.data(), static_cast<std::streamsize> (part.size()));
    CHOC_EXPECT_TRUE (part == slice (content, 170, 200));
}

//==============================================================================
static void testStreamEndRelativeSeeks (choc::test::TestProgress& progress)
{
    CHOC_TEST (StreamEndRelativeSeeks)

    TestCache t;
    const std::filesystem::path file { "/patches/end.bin" };
    auto content = createTestData (200);

    t.cache.registerFile (file, content.size());
    sendChunk (t, file, 0, content);

    auto stream = t.cache.createFileStream (file);
    CHOC_EXPECT_TRUE (stream != nullptr);

    // seeking to the end reports the file's size
    stream->seekg (0, std::ios_base::end);
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (200));

    // and a negative offset counts backwards from the end
    stream->seekg (-50, std::ios_base::end);
    CHOC_EXPECT_EQ (static_cast<int64_t> (std::streamoff (stream->tellg())), static_cast<int64_t> (150));

    std::vector<char> tail (50);
    stream->read (tail.data(), static_cast<std::streamsize> (tail.size()));
    CHOC_EXPECT_TRUE (tail == slice (content, 150, 200));

    // the seek-to-end / tellg / rewind / read idiom that PatchManifest uses to
    // slurp a whole file
    stream->clear();
    stream->seekg (0, std::ios_base::end);
    auto size = static_cast<size_t> (std::streamoff (stream->tellg()));
    CHOC_EXPECT_EQ (size, content.size());

    std::vector<char> whole (size);
    stream->seekg (0);
    stream->read (whole.data(), static_cast<std::streamsize> (size));
    CHOC_EXPECT_TRUE (whole == content);
}

//==============================================================================
static void testManifestInitialisation (choc::test::TestProgress& progress)
{
    CHOC_TEST (ManifestInitialisation)

    TestCache t;
    const std::filesystem::path folder { "/patches/demo" };
    auto manifestFile = folder / "demo.cmajorpatch";
    auto sourceFile = folder / "demo.cmajor";
    auto content = createTestData (32);

    t.cache.registerFile (manifestFile, 10);
    t.cache.registerFile (sourceFile, content.size());
    sendChunk (t, sourceFile, 0, content);

    cmaj::PatchManifest manifest;
    CHOC_EXPECT_TRUE (t.cache.initialiseManifest (manifest, manifestFile));

    CHOC_EXPECT_EQ (manifest.manifestFile, std::string ("demo.cmajorpatch"));
    CHOC_EXPECT_EQ (manifest.name, std::string ("demo.cmajorpatch"));

    // referenced files resolve relative to the manifest's folder
    CHOC_EXPECT_EQ (manifest.getFullPathForFile ("demo.cmajor"), sourceFile.string());
    CHOC_EXPECT_TRUE (manifest.fileExists ("demo.cmajor"));
    CHOC_EXPECT_FALSE (manifest.fileExists ("missing.cmajor"));
    CHOC_EXPECT_TRUE (manifest.getFileModificationTime ("demo.cmajor") != std::filesystem::file_time_type());
    CHOC_EXPECT_TRUE (manifest.getFileModificationTime ("missing.cmajor") == std::filesystem::file_time_type());

    // and the reader pulls their content out of the cache
    CHOC_EXPECT_TRUE (manifest.createFileReader ("missing.cmajor") == nullptr);
    auto reader = manifest.createFileReader ("demo.cmajor");
    CHOC_EXPECT_TRUE (reader != nullptr);

    std::vector<char> buffer (content.size());
    reader->read (buffer.data(), static_cast<std::streamsize> (buffer.size()));
    CHOC_EXPECT_TRUE (buffer == content);

    // and reading a whole file through the manifest works end-to-end
    auto slurped = manifest.readFileContent ("demo.cmajor");
    CHOC_EXPECT_TRUE (slurped.has_value());
    CHOC_EXPECT_TRUE (slurped.has_value() && *slurped == std::string (content.begin(), content.end()));
}

//==============================================================================
static void testStatusOutput (choc::test::TestProgress& progress)
{
    CHOC_TEST (StatusOutput)

    TestCache t;

    // registering a file dumps the new state of the cache
    t.clearOutput();
    t.cache.registerFile ("/patches/one.txt", 2048);
    auto afterRegister = t.getOutput();
    CHOC_EXPECT_TRUE (afterRegister.find ("Files: 1") != std::string::npos);
    CHOC_EXPECT_TRUE (afterRegister.find ("/patches/one.txt") != std::string::npos);

    // as does asking for it directly
    t.clearOutput();
    t.cache.dumpStatus();
    auto dumped = t.getOutput();
    CHOC_EXPECT_TRUE (dumped.find ("Files: 1") != std::string::npos);
    CHOC_EXPECT_TRUE (dumped.find ("/patches/one.txt") != std::string::npos);
    CHOC_EXPECT_TRUE (dumped.find (choc::text::getByteSizeDescription (2048)) != std::string::npos);

    // removing and clearing report the emptied cache
    t.cache.registerFile ("/patches/two.txt", 4096);
    t.clearOutput();
    t.cache.removeFile ("/patches/two.txt");
    CHOC_EXPECT_TRUE (t.getOutput().find ("Files: 1") != std::string::npos);

    t.clearOutput();
    t.cache.clear();
    CHOC_EXPECT_TRUE (t.getOutput().find ("Files: 0") != std::string::npos);
}

//==============================================================================
inline void runUnitTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (LocalFileCache);

    testRegisterAndRemoveFiles (progress);
    testClear (progress);
    testFileRegions (progress);
    testReadRequests (progress);
    testMultiChunkReads (progress);
    testRequestCancellation (progress);
    testCancelledRequestsAreForgotten (progress);
    testMessageHandling (progress);
    testFileStreams (progress);
    testStreamSequentialReads (progress);
    testStreamEndRelativeSeeks (progress);
    testManifestInitialisation (progress);
    testStatusOutput (progress);
}

} // namespace cmaj::local_file_cache_tests
