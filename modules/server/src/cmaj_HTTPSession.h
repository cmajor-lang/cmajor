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


// This class is heavily derived from code that is copyright (c) 2016-2019 Vinnie Falco:
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
// Official repository: https://github.com/vinniefalco/CppCon2018

#pragma once

#include <optional>

namespace cmaj::server
{

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

//==============================================================================
/**
    Represents an established HTTP connection
*/
class HTTPSession  : public std::enable_shared_from_this<HTTPSession>
{
public:
    HTTPSession (asio::ip::tcp::socket&& socket,
                 const HTTPServer::CreateClientInstanceFn& createNewClient)
        : tcpStream (std::move (socket))
    {
        clientInstance = createNewClient();
        CHOC_ASSERT (clientInstance != nullptr);
    }

    ~HTTPSession()
    {
        clientInstance.reset();
    }

    void run()
    {
        do_read();
    }

    static constexpr uint64_t messageBodySizeLimit = 10000;
    static constexpr int timeoutSeconds = 30;

private:
    //==============================================================================
    beast::tcp_stream tcpStream;
    beast::flat_buffer flatBuffer;
    std::shared_ptr<HTTPServer::ClientInstance> clientInstance;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    using ParserType = http::request_parser<http::string_body>;
    using MessageType = ParserType::value_type;
    std::optional<ParserType> parser;

    void fail (beast::error_code ec, char const* what)
    {
        // Don't report on canceled operations
        if (ec == asio::error::operation_aborted)
            return;

        std::cerr << what << ": " << ec.message() << "\n";
    }

    void do_read()
    {
        parser.emplace(); // Construct a new parser for each message
        parser->body_limit (messageBodySizeLimit);
        tcpStream.expires_after (std::chrono::seconds (timeoutSeconds));

        http::async_read (tcpStream, flatBuffer, parser->get(),
                          beast::bind_front_handler (&HTTPSession::on_read, shared_from_this()));
    }

    void runWebsocketSession (asio::ip::tcp::socket&& socket, MessageType&& req)
    {
        auto target = std::string (req.target());

        std::make_shared<WebsocketSession> (clientInstance, std::move (socket))
           ->run (std::move (req));

        clientInstance->upgradedToWebsocket (target);
    }

    void on_read (beast::error_code ec, std::size_t)
    {
        // This means they closed the connection
        if (ec == http::error::end_of_stream)
        {
            tcpStream.socket().shutdown (asio::ip::tcp::socket::shutdown_send, ec);
            return;
        }

        if (ec)
            return fail (ec, "read");

        // Upgrade to a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        if (beast::websocket::is_upgrade (parser->get()))
            return runWebsocketSession (tcpStream.release_socket(), parser->release());

        // Send the response
        handle_request (parser->release());
    }

    template <typename ResponseType>
    void send_request_response (ResponseType&& response)
    {
        auto sp = std::make_shared<ResponseType> (std::forward<ResponseType> (response));

        http::async_write (tcpStream, *sp,
                           [self = shared_from_this(), sp] (beast::error_code e, std::size_t bytes)
                           {
                               self->on_write (e, bytes, sp->need_eof());
                           });
    }

    template <typename RequestType>
    void send_failure_response (const RequestType& req, http::status status, std::string text)
    {
        http::response<http::string_body> res { status, req.version() };
        res.set (http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set (http::field::content_type, "text/html");
        res.keep_alive (req.keep_alive());
        res.body() = std::move (text);
        res.prepare_payload();

        send_request_response (std::move (res));
    }

    void on_write (beast::error_code ec, std::size_t, bool close)
    {
        if (ec)
            return fail (ec, "write");

        if (close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            tcpStream.socket().shutdown (asio::ip::tcp::socket::shutdown_send, ec);
            return;
        }

        // Read another request
        do_read();
    }

    // Return a reasonable mime type based on the extension of a file.
    static std::string_view mime_type (std::string_view path)
    {
        if (auto question = path.rfind ("?"); question != std::string_view::npos)
            path = path.substr (0, question);

        auto const ext = [&path]
        {
            auto pos = path.rfind(".");

            if (pos == std::string_view::npos)
                return std::string_view{};

            return path.substr(pos);
        }();

        if (beast::iequals (ext, ".htm"))   return "text/html";
        if (beast::iequals (ext, ".html"))  return "text/html";
        if (beast::iequals (ext, ".php"))   return "text/html";
        if (beast::iequals (ext, ".css"))   return "text/css";
        if (beast::iequals (ext, ".txt"))   return "text/plain";
        if (beast::iequals (ext, ".js"))    return "application/javascript";
        if (beast::iequals (ext, ".json"))  return "application/json";
        if (beast::iequals (ext, ".xml"))   return "application/xml";
        if (beast::iequals (ext, ".swf"))   return "application/x-shockwave-flash";
        if (beast::iequals (ext, ".flv"))   return "video/x-flv";
        if (beast::iequals (ext, ".png"))   return "image/png";
        if (beast::iequals (ext, ".jpe"))   return "image/jpeg";
        if (beast::iequals (ext, ".jpeg"))  return "image/jpeg";
        if (beast::iequals (ext, ".jpg"))   return "image/jpeg";
        if (beast::iequals (ext, ".gif"))   return "image/gif";
        if (beast::iequals (ext, ".bmp"))   return "image/bmp";
        if (beast::iequals (ext, ".ico"))   return "image/vnd.microsoft.icon";
        if (beast::iequals (ext, ".tiff"))  return "image/tiff";
        if (beast::iequals (ext, ".tif"))   return "image/tiff";
        if (beast::iequals (ext, ".svg"))   return "image/svg+xml";
        if (beast::iequals (ext, ".svgz"))  return "image/svg+xml";
        if (beast::iequals (ext, ".woff2")) return "font/woff2";

        return "application/text";
    }

    // This function produces an HTTP response for the given
    // request. The type of the response object depends on the
    // contents of the request, so the interface requires the
    // caller to pass a generic lambda for receiving the response.
    void handle_request (MessageType&& req)
    {
        // Make sure we can handle the method
        if (req.method() != http::verb::get
             && req.method() != http::verb::head)
            return send_failure_response (req, http::status::bad_request, "Unknown HTTP-method");

        auto subPath = std::string (req.target());

        // Request path must be absolute and not contain "..".
        if (subPath.empty()
             || subPath[0] != '/'
             || subPath.find("..") != std::string_view::npos)
            return send_failure_response (req, http::status::bad_request, "Illegal request-target");

        if (subPath.back() == '/')
            subPath.append ("index.html");

        auto content = clientInstance->getHTTPContent (subPath);

        if (auto directContent = std::get_if<std::string> (&content))
        {
            if (req.method() == http::verb::get)
            {
                auto size = http::string_body::size (*directContent);

                http::response<http::string_body> res
                {
                    std::piecewise_construct,
                    std::make_tuple(std::move (*directContent)),
                    std::make_tuple(http::status::ok, req.version())
                };

                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::content_type, mime_type (subPath));
                res.content_length(size);
                res.keep_alive(req.keep_alive());

                return send_request_response (std::move(res));
            }

            return send_failure_response (req, http::status::bad_request, "POST not supported");
        }

        auto path = std::get<std::filesystem::path> (content).string();

        // Attempt to open the file
        beast::error_code ec;
        http::file_body::value_type body;
        body.open (path.c_str(), beast::file_mode::scan, ec);

        // Handle the case where the file doesn't exist
        if (ec == boost::system::errc::no_such_file_or_directory)
            return send_failure_response (req, http::status::not_found,
                                          "The resource '" + std::string (req.target()) + "' was not found.");

        // Handle an unknown error
        if (ec)
            return send_failure_response (req, http::status::internal_server_error,
                                          "An error occurred: '" + ec.message() + "'");

        // Cache the size since we need it after the move
        auto const size = body.size();

        // Respond to HEAD request
        if (req.method() == http::verb::head)
        {
            http::response<http::empty_body> res {http::status::ok, req.version()};
            res.set (http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set (http::field::content_type, mime_type(path));
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return send_request_response (std::move(res));
        }

        // Respond to GET request
        http::response<http::file_body> res
        {
            std::piecewise_construct,
            std::make_tuple (std::move(body)),
            std::make_tuple (http::status::ok, req.version())
        };

        res.set (http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set (http::field::content_type, mime_type(path));
        res.content_length (size);
        res.keep_alive (req.keep_alive());
        return send_request_response (std::move(res));
    }

    //==============================================================================
    struct WebsocketSession   : public std::enable_shared_from_this<WebsocketSession>
    {
        WebsocketSession (std::shared_ptr<HTTPServer::ClientInstance> c, asio::ip::tcp::socket&& socket)
            : clientInstance (std::move (c)), websocketStream (std::move(socket))
        {
        }

        ~WebsocketSession()
        {
            clientInstance->sendFn = {};
            clientInstance.reset();
        }

        void send (std::shared_ptr<std::string const> const& ss)
        {
            asio::post (websocketStream.get_executor(),
                        beast::bind_front_handler (&WebsocketSession::on_send, shared_from_this(), ss));
        }

        void run (MessageType&& req)
        {
            // Set suggested timeout settings for the websocket
            websocketStream.set_option (beast::websocket::stream_base::timeout::suggested (beast::role_type::server));

            // Set a decorator to change the Server of the handshake
            websocketStream.set_option (beast::websocket::stream_base::decorator ([] (beast::websocket::response_type& res)
            {
                res.set (http::field::server, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-cmaj-multi");
            }));

            // Accept the websocket handshake
            websocketStream.async_accept (req, beast::bind_front_handler (&WebsocketSession::on_accept, shared_from_this()));
        }

    private:
        std::shared_ptr<HTTPServer::ClientInstance> clientInstance;
        beast::flat_buffer buffer;
        beast::websocket::stream<beast::tcp_stream> websocketStream;
        std::vector<std::shared_ptr<std::string const>> queue;

        void fail (beast::error_code ec, char const* what)
        {
            // Don't report these
            if (ec == asio::error::operation_aborted || ec == beast::websocket::error::closed)
                return;

            std::cerr << what << ": " << ec.message() << "\n";
        }

        void on_accept (beast::error_code ec)
        {
            if (ec)
                return fail(ec, "accept");

            websocketStream.async_read (buffer, beast::bind_front_handler (&WebsocketSession::on_read, shared_from_this()));

            clientInstance->sendFn = [this] (std::string m)
            {
                this->send (std::make_shared<const std::string> (std::move (m)));
            };
        }

        void on_read (beast::error_code ec, std::size_t)
        {
            if (ec)
                return fail(ec, "read");

            auto bufferDataAsString = beast::buffers_to_string (buffer.data());

            clientInstance->handleMessage (bufferDataAsString);

            // Clear the buffer
            buffer.consume (buffer.size());

            // Read another message
            websocketStream.async_read (buffer, beast::bind_front_handler (&WebsocketSession::on_read, shared_from_this()));
        }

        void on_send (std::shared_ptr<std::string const> const& ss)
        {
            // Always add to queue
            queue.push_back (ss);

            // If not currently writing, so send this immediately
            if (queue.size() <= 1)
                websocketStream.async_write (asio::buffer (*queue.front()),
                                             beast::bind_front_handler (&WebsocketSession::on_write, shared_from_this()));
        }

        void on_write (beast::error_code ec, std::size_t)
        {
            // Handle the error, if any
            if (ec)
                return fail(ec, "write");

            // Remove the string from the queue
            queue.erase (queue.begin());

            // Send the next message if any
            if (! queue.empty())
                websocketStream.async_write (asio::buffer (*queue.front()),
                                             beast::bind_front_handler (&WebsocketSession::on_write, shared_from_this()));
        }
    };
};

}
