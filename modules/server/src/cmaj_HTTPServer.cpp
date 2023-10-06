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

#undef CHOC_ASSERT

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <functional>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <thread>

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "choc/text/choc_StringUtilities.h"
#include "choc/platform/choc_Assert.h"
#include "choc/tests/choc_UnitTest.h"

#include "../include/cmaj_HTTPServer.h"

#include "choc/platform/choc_DisableAllWarnings.h"
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include "cmaj_HTTPSession.h"
#include "choc/platform/choc_ReenableAllWarnings.h"

namespace cmaj::server
{

namespace asio = boost::asio;

//==============================================================================
struct HTTPServer::Pimpl
{
    Pimpl (std::string_view ipAddress, uint16_t port, const CreateClientInstanceFn& createClient)
    {
        auto address = asio::ip::make_address (ipAddress);

        for (;;)
        {
            // Create and launch a listening port
            ioContextListener = std::make_shared<IOCListener> (ioc, createClient, asio::ip::tcp::endpoint { address, port });

            if (ioContextListener->failedWithAddressInUse())
            {
                if (++port < 65535)
                {
                    ioContextListener.reset();
                    continue;
                }
            }

            if (! ioContextListener->failed())
                ioContextListener->launch();

            break;
        }
    }

    ~Pimpl()
    {
        stop();
    }

    void start (uint32_t numThreads)
    {
        // Run the I/O service on the requested number of threads
        assert (connectionThreads.empty());
        connectionThreads.reserve (numThreads);

        for (uint32_t i = 0; i < numThreads; ++i)
        {
            connectionThreads.emplace_back ([this]
            {
                try
                {
                    ioc.run();
                }
                catch (std::exception& e)
                {
                    std::cerr << e.what() << std::endl;
                }
            });
        }
    }

    void stop()
    {
        ioc.stop();

        // Block until all the threads exit
        for (auto& t : connectionThreads)
            t.join();
    }

    std::string getHost() const        { return ioContextListener->host; }
    uint16_t getPort() const           { return ioContextListener->port; }
    std::string getAddress() const     { return ioContextListener->address; }

private:
    asio::io_context ioc;
    std::vector<std::thread> connectionThreads;

    //==============================================================================
    struct IOCListener  : public std::enable_shared_from_this<IOCListener>
    {
        IOCListener (asio::io_context& i,
                     const CreateClientInstanceFn& cc,
                     asio::ip::tcp::endpoint endpoint)
            : ioContext(i), acceptor(i), createClient (cc)
        {
            acceptor.open (endpoint.protocol(), errorCode);
            if (errorCode) { fail(errorCode, "open"); return; }

            acceptor.set_option (asio::socket_base::reuse_address(true), errorCode);
            if (errorCode) { fail(errorCode, "set_option"); return; }

            acceptor.bind (endpoint, errorCode);
            if (errorCode) { fail(errorCode, "bind"); return; }

            acceptor.listen (asio::socket_base::max_listen_connections, errorCode);
            if (errorCode) { fail(errorCode, "listen"); return; }

            auto ep = acceptor.local_endpoint();

            std::ostringstream oss;
            oss << ep;
            address = oss.str();

            port = ep.port();
            host = choc::text::splitString (address, ':', false).front();
        }

        void launch()
        {
            acceptor.async_accept (asio::make_strand (ioContext),
                                   beast::bind_front_handler (&IOCListener::on_accept, shared_from_this()));
        }

        void fail (beast::error_code ec, char const* what)
        {
            // Don't report on canceled operations
            if (ec != asio::error::operation_aborted)
                std::cerr << what << ": " << ec.message() << "\n";
        }

        bool failed() const                     { return errorCode.failed(); }
        bool failedWithAddressInUse() const     { return errorCode == asio::error::address_in_use; }

        void on_accept (beast::error_code ec, asio::ip::tcp::socket socket)
        {
            if (ec)
                return fail (ec, "accept");

            // Launch a new session for this connection
            std::make_shared<HTTPSession> (std::move(socket), createClient)
                ->run();

            // The new connection gets its own strand
            acceptor.async_accept (asio::make_strand (ioContext),
                                   beast::bind_front_handler (&IOCListener::on_accept, shared_from_this()));
        }

        asio::io_context& ioContext;
        asio::ip::tcp::acceptor acceptor;
        beast::error_code errorCode = {};
        CreateClientInstanceFn createClient;
        std::string host, address;
        uint16_t port = 0;
    };

    std::shared_ptr<IOCListener> ioContextListener;
};

//==============================================================================
HTTPServer::HTTPServer (std::string ipAddress, uint16_t port,
                        const CreateClientInstanceFn& createClient)
{
    pimpl = std::make_unique<Pimpl> (ipAddress, port, createClient);
    pimpl->start (4);
}

HTTPServer::~HTTPServer()
{
    pimpl.reset();
}

bool HTTPServer::isConnected() const
{
    return pimpl != nullptr;
}

std::string HTTPServer::getHost() const
{
    return pimpl->getHost();
}

uint16_t HTTPServer::getPort() const
{
    return pimpl->getPort();
}

std::string HTTPServer::getAddress() const
{
    return pimpl->getAddress();
}

void HTTPServer::ClientInstance::sendMessage (std::string m)
{
    if (sendFn != nullptr)
        sendFn (std::move (m));
}


//==============================================================================
struct TestClient
{
    TestClient (std::string host, uint16_t port,
                std::function<void (std::string_view)> onMessageRead_)
        : onMessageRead (std::move (onMessageRead_))
    {
        // Look up the domain name
        auto const results = resolver.resolve (host, std::to_string (port));

        // Make the connection on the IP address we get from a lookup
        asio::connect (ws.next_layer(), results.begin(), results.end());

        // Set a decorator to change the User-Agent of the handshake
        ws.set_option (beast::websocket::stream_base::decorator(
            [](beast::websocket::request_type& req)
            {
                req.set (http::field::user_agent,
                    std::string (BOOST_BEAST_VERSION_STRING) +
                        " websocket-client-coro");
            }));

        // Perform the websocket handshake
        ws.handshake (host, "/");
        ws.async_read (destBuffer, [this] (auto code, auto bytes) { readMessage (code, bytes); });

        std::atomic<bool> threadStarted { false };

        connection = std::thread ([this, &threadStarted]
        {
            threadStarted = true;

            for (;;)
            {
                if (threadShouldExit)
                    return;

                try
                {
                    ioc.run();
                }
                catch (std::exception const&)
                {
                    return;
                }
            }
        });

        // Wait for thread to be waiting for events
        while (! threadStarted)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    ~TestClient()
    {
        try
        {
            threadShouldExit = true;
            ioc.stop();
            ws.close (beast::websocket::close_code::normal);
        }
        catch (std::exception const& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }

        connection.join();
    }

    void send (std::string text)
    {
        ws.write (asio::buffer (text));
    }

private:
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver { ioc };
    beast::websocket::stream<asio::ip::tcp::socket> ws { ioc };
    beast::flat_buffer destBuffer;

    std::function<void (std::string_view)> onMessageRead;
    std::thread connection;
    std::atomic<bool> threadShouldExit { false };

    void readMessage (boost::beast::error_code ec, std::size_t numBytes)
    {
        if (numBytes > 0 && ! ec)
        {
            onMessageRead (beast::buffers_to_string (destBuffer.data()));
            destBuffer.clear();

            // Prepare another read callback to be processed by the thread
            ws.async_read (destBuffer,
                           [this] (auto code, auto bytes) { readMessage (code, bytes); });
        }
    }
};

void runUnitTests (choc::test::TestProgress& progress)
{
    using namespace std::chrono_literals;

    struct Test
    {
        std::string serverString, clientString;
        std::atomic<bool> clientConnected { false };
        std::atomic<int> messagecount { 0 };
    };

    Test testStatus;

    {
        struct TestClientInstance  : public cmaj::server::HTTPServer::ClientInstance
        {
            TestClientInstance (Test& t) : test (t)
            {
                test.clientConnected = true;
            }

            void handleMessage (std::string_view m) override
            {
                test.serverString += m;
                sendMessage (std::string (m));
            }

            void upgradedToWebsocket (std::string_view) override  {}

            std::variant<std::string, std::filesystem::path> getHTTPContent (std::string_view) override  { return {}; }

            Test& test;
        };

        HTTPServer server ("127.0.0.1", 8080,
                           [&] { return std::make_unique<TestClientInstance> (testStatus); });

        TestClient client (server.getHost(),
                           server.getPort(),
                           [&] (std::string_view m) mutable
                           {
                               testStatus.clientString += m;
                               ++testStatus.messagecount;
                           });

        {
            const auto start = std::chrono::system_clock::now();

            while (! testStatus.clientConnected)
            {
                if ((std::chrono::system_clock::now() - start) >= 30s)
                    break;

                std::this_thread::sleep_for (1ms);
            }
        }

        client.send ("Hello ");
        client.send ("world!");

        {
            const auto start = std::chrono::system_clock::now();

            while (testStatus.messagecount < 2)
            {
                if ((std::chrono::system_clock::now() - start) >= 30s)
                    break;

                std::this_thread::sleep_for (1ms);
            }
        }
    }

    {
        CHOC_CATEGORY (Websockets)
        CHOC_TEST (Communication)
        CHOC_EXPECT_TRUE (testStatus.clientConnected)
        CHOC_EXPECT_EQ (testStatus.clientString, "Hello world!")
        CHOC_EXPECT_EQ (testStatus.serverString, "Hello world!")
    }
}

} // namespace cmaj::server
