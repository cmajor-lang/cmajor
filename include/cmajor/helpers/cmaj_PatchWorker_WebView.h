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

#include "cmaj_Patch.h"
#include "../../choc/choc/javascript/choc_javascript.h"
#include "../../choc/choc/javascript/choc_javascript_Console.h"
#include "../../choc/choc/gui/choc_WebView.h"
#include "../../choc/choc/network/choc_MIMETypes.h"
#include <mutex>
#include <memory>


namespace cmaj
{

/// When you create a Patch object, you need to set its createContextForPatchWorker
/// property so that it knows what kind of javascript context to create to run
/// any patch workers that may be needed. This function sets up a hidden WebView
/// for that purpose, and gives it the appropriate library functions that it needs.
inline void enableWebViewPatchWorker (Patch& p)
{
    struct Worker : Patch::WorkerContext
    {
        Worker (Patch& p, const std::string& wt) : patch (p), workerType (wt) {}

        ~Worker() override
        {
            // The webview's callbacks can still be called until it has been deleted, so
            // they must no longer reach this worker (or the patch that owns it).
            {
                std::lock_guard<std::recursive_mutex> lock (connection->lock);
                connection->worker = nullptr;
            }

            // Delete the webivew on the msg thread
            choc::messageloop::postMessage ([p = webview.release()]() { delete p; });
        }

        /// The webview's callbacks reach the worker via this, as the webview is deleted
        /// asynchronously and may outlive it.
        struct Connection
        {
            // Recursive, because a callback can cause the worker to be deleted
            std::recursive_mutex lock;
            Worker* worker = nullptr;
        };

        std::shared_ptr<Connection> connection = std::make_shared<Connection>();

        /// Calls fn (worker) if the worker still exists, keeping it alive while doing so
        static void callIfAlive (Connection& c, const std::function<void(Worker&)>& fn)
        {
            std::lock_guard<std::recursive_mutex> lock (c.lock);

            if (c.worker != nullptr)
                fn (*c.worker);
        }

        void initialise (std::function<void(const choc::value::ValueView&)> sendMessageToPatch,
                         std::function<void(const std::string&)> reportError) override
        {
            connection->worker = this;

            choc::ui::WebView::Options opts;
            opts.enableDebugMode = true;

            opts.fetchResource = [c = connection] (const std::string& path)
            {
                std::optional<choc::ui::WebView::Options::Resource> result;
                callIfAlive (*c, [&] (Worker& w) { result = w.fetchResource (path); });
                return result;
            };

            opts.webviewIsReady = [c = connection,
                                   send2 = std::move (sendMessageToPatch),
                                   reportError2 = std::move (reportError)] (choc::ui::WebView& w)
            {
                w.bind ("_cmaj_console_log", [] (const choc::value::ValueView& args) mutable -> choc::value::Value
                {
                    if (args.isArray() && args.size() != 0)
                    {
                        auto level = choc::javascript::LoggingLevel::log;

                        if (args.size() > 1)
                        {
                            switch (args[1].getWithDefault<int> (1))
                            {
                                case 0: level = choc::javascript::LoggingLevel::log; break;
                                case 1: level = choc::javascript::LoggingLevel::info; break;
                                case 2: level = choc::javascript::LoggingLevel::warn; break;
                                case 3: level = choc::javascript::LoggingLevel::error; break;
                                case 4: level = choc::javascript::LoggingLevel::debug; break;
                            }
                        }

                        consoleLog (args[0], level);
                    }

                    return {};
                });

                w.bind ("cmaj_sendMessageToServer", [c, send = std::move (send2)] (const choc::value::ValueView& args) -> choc::value::Value
                {
                    if (args.isArray() && args.size() != 0)
                        callIfAlive (*c, [&] (Worker&) { send (args[0]); });

                    return {};
                });

                w.bind ("cmaj_reportError", [c, reportErrorFn = std::move (reportError2)] (const choc::value::ValueView& args) -> choc::value::Value
                {
                    if (args.isArray() && args.size() != 0)
                        callIfAlive (*c, [&] (Worker&) { reportErrorFn (args[0].toString()); });

                    return {};
                });

                w.bind ("_internalReadResourceAsAudioData", [c] (const choc::value::ValueView& args) -> choc::value::Value
                {
                    choc::value::Value result;

                    try
                    {
                        if (args.isArray() && args.size() != 0)
                        {
                            if (auto path = args[0].toString(); ! path.empty())
                            {
                                choc::value::Value annotation;

                                if (args.size() > 1)
                                    annotation = args[1];

                                callIfAlive (*c, [&] (Worker& worker)
                                {
                                    if (auto manifest = worker.patch.getManifest())
                                        result = readManifestResourceAsAudioData (*manifest, path, annotation);
                                });
                            }
                        }
                    }
                    catch (...)
                    {}

                    return result;
                });

                w.navigate ({});
            };

            webview = std::make_unique<choc::ui::WebView> (opts);
        }

        static void consoleLog (const choc::value::ValueView& content, choc::javascript::LoggingLevel level)
        {
            auto message = content.isString() ? content.toString() : choc::json::toString (content);

            if (level == choc::javascript::LoggingLevel::debug)
                std::cerr << message << std::endl;
            else
                std::cout << message << std::endl;
        }

        void sendMessage (const std::string& msg, std::function<void(const std::string&)> reportError) override
        {
            webview->evaluateJavascript ("window.currentView?.deliverMessageFromServer (" + msg + ");",
                                         [c = connection, reportErrorFn = std::move (reportError)] (const std::string& error, const choc::value::ValueView&)
            {
                if (! error.empty())
                    callIfAlive (*c, [&] (Worker&) { reportErrorFn (error); });
            });
        }

        std::optional<choc::ui::WebView::Options::Resource> fetchResource (const std::string& path)
        {
            if (auto manifest = patch.getManifest())
            {
                if (path == "/")
                    return choc::ui::WebView::Options::Resource (getHTML (*manifest), "text/html");

                if (auto moduleText = readJavascriptResource (path, manifest))
                    return choc::ui::WebView::Options::Resource (*moduleText, choc::network::getMIMETypeFromFilename (path, "application/octet-stream"));
            }

            return {};
        }

        std::string getHTML (const PatchManifest& manifest)
        {
            if (workerType == "sourceTransformer")
                return getSourceTransformerHTML (manifest);

            return getPatchWorkerHTML (manifest);
        }

        std::string getSourceTransformerHTML (const PatchManifest& manifest)
        {
            auto patchWorkerPath = manifest.sourceTransformer;

            if (! choc::text::startsWith (patchWorkerPath, "/"))
                patchWorkerPath = "/" + patchWorkerPath;

            return choc::text::replace (R"(
<!DOCTYPE html>
<html></html>

<script type="module">

window.console.log   =  function() { for (let a of arguments) _cmaj_console_log (a, 0); };
window.console.info  =  function() { for (let a of arguments) _cmaj_console_log (a, 1); };
window.console.warn  =  function() { for (let a of arguments) _cmaj_console_log (a, 2); };
window.console.error =  function() { for (let a of arguments) _cmaj_console_log (a, 3); };
window.console.debug =  function() { for (let a of arguments) _cmaj_console_log (a, 4); };

try
{
    const workerModule = await import (WORKER_MODULE);
    await workerModule.default();
}
catch (e)
{
    window.cmaj_reportError (e.toString());
}

</script>
)",
            "WORKER_MODULE", choc::json::getEscapedQuotedString (patchWorkerPath));
        }

        std::string getPatchWorkerHTML (const PatchManifest& manifest)
        {
            auto patchWorkerPath = manifest.patchWorker;

            if (! choc::text::startsWith (patchWorkerPath, "/"))
                patchWorkerPath = "/" + patchWorkerPath;

            return choc::text::replace (R"(
<!DOCTYPE html>
<html></html>

<script type="module">

import { PatchConnection } from "./cmaj_api/cmaj-patch-connection.js"

class WorkerPatchConnection  extends PatchConnection
{
    constructor()
    {
        super();
        this.manifest = MANIFEST;
        window.currentView = this;
    }

    getResourceAddress (path)
    {
        return path.startsWith ("/") ? path : ("/" + path);
    }

    sendMessageToServer (message)
    {
        cmaj_sendMessageToServer (message);
    }

    async readResource (path)
    {
        return fetch (path);
    }

    async readResourceAsAudioData (path)
    {
        return _internalReadResourceAsAudioData (path);
    }
}

window.console.log   =  function() { for (let a of arguments) _cmaj_console_log (a, 0); };
window.console.info  =  function() { for (let a of arguments) _cmaj_console_log (a, 1); };
window.console.warn  =  function() { for (let a of arguments) _cmaj_console_log (a, 2); };
window.console.error =  function() { for (let a of arguments) _cmaj_console_log (a, 3); };
window.console.debug =  function() { for (let a of arguments) _cmaj_console_log (a, 4); };

try
{
    const connection = new WorkerPatchConnection();

    const workerModule = await import (WORKER_MODULE);
    await workerModule.default (connection);
}
catch (e)
{
    window.cmaj_reportError (e.toString());
}

</script>
)",
            "MANIFEST", choc::json::toString (manifest.manifest),
            "WORKER_MODULE", choc::json::getEscapedQuotedString (patchWorkerPath));
        }

        Patch& patch;
        std::string workerType;
        std::unique_ptr<choc::ui::WebView> webview;
    };


    p.createContextForPatchWorker = [&p] (const std::string& workerType) -> std::unique_ptr<Patch::WorkerContext>
    {
        return std::make_unique<Worker> (p, workerType);
    };
}

} // namespace cmaj
