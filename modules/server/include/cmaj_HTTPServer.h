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

#include <string>
#include <memory>
#include <functional>
#include <filesystem>
#include <variant>
#include "choc/tests/choc_UnitTest.h"

namespace cmaj::server
{

/// A Server object opens a server on a specific port and waits for incoming connections.
class HTTPServer
{
public:
    struct ClientInstance
    {
        ClientInstance() = default;
        virtual ~ClientInstance() = default;

        virtual void handleMessage (std::string_view) = 0;
        void sendMessage (std::string);

        // provides the target path that was given for the web socket
        virtual void upgradedToWebsocket (std::string_view) = 0;

        /// Must return either a string with the file content or the location
        /// of a file to serve
        virtual std::variant<std::string, std::filesystem::path> getHTTPContent (std::string_view path) = 0;

    private:
        friend class HTTPSession;
        std::function<void(std::string)> sendFn;
    };

    using CreateClientInstanceFn = std::function<std::shared_ptr<ClientInstance>()>;

    //==============================================================================
    HTTPServer (std::string ipAddress,
                uint16_t port,
                const CreateClientInstanceFn&);

    /// Cancels the connection and waits for it to close before returning.
    ~HTTPServer();

    /// Returns true if the server connected ok and is now running.
    bool isConnected() const;

    /// Returns the host address of the server
    std::string getHost() const;

    /// If you specify 0 as the desired port, a free one will be automatically
    /// assigned so you can use this method to retrieve it
    uint16_t getPort() const;

    /// Returns the address (host + port) this server is curently running on
    std::string getAddress() const;

private:
    struct Pimpl;
    std::unique_ptr<Pimpl> pimpl;
};

void runUnitTests (choc::test::TestProgress&);

}
