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
#include "../../choc/javascript/choc_javascript.h"
#include "../../choc/gui/choc_WebView.h"


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
        Worker (Patch& p) : patch (p) {}
        ~Worker() override {}

        void initialise (std::function<void(const choc::value::ValueView&)> sendMessageToPatch,
                         std::function<void(const std::string&)> reportError) override
        {
            choc::ui::WebView::Options opts;
            opts.enableDebugMode = true;
            opts.fetchResource = [this] (const std::string& path) { return fetchResource (path); };
            webview = std::make_unique<choc::ui::WebView> (opts);

            webview->bind ("cmaj_sendMessageToServer", [send = std::move (sendMessageToPatch)] (const choc::value::ValueView& args) -> choc::value::Value
            {
                if (args.isArray() && args.size() != 0)
                    send (args[0]);

                return {};
            });

            webview->bind ("cmaj_reportError", [reportError = std::move (reportError)] (const choc::value::ValueView& args) -> choc::value::Value
            {
                if (args.isArray() && args.size() != 0)
                    reportError (args[0].toString());

                return {};
            });

            webview->bind ("_internalReadResourceAsAudioData", [&p = patch] (const choc::value::ValueView& args) -> choc::value::Value
            {
                try
                {
                    if (args.isArray() && args.size() != 0)
                    {
                        if (auto path = args[0].toString(); ! path.empty())
                        {
                            choc::value::Value annotation;

                            if (args.size() > 1)
                                annotation = args[1];

                            if (auto manifest = p.getManifest())
                                return readManifestResourceAsAudioData (*manifest, path, annotation);
                        }
                    }
                }
                catch (...)
                {}

                return {};
            });

            webview->navigate ({});
        }

        void sendMessage (const std::string& msg, std::function<void(const std::string&)> reportError) override
        {
            webview->evaluateJavascript ("window.currentView?.deliverMessageFromServer(" + msg + ");",
                                         [reportError = std::move (reportError)] (const std::string& error, const choc::value::ValueView&)
            {
                if (! error.empty())
                    reportError (error);
            });
        }

        std::optional<choc::ui::WebView::Options::Resource> fetchResource (const std::string& path)
        {
            if (auto manifest = patch.getManifest())
            {
                choc::ui::WebView::Options::Resource result;
                std::string text;

                if (path == "/")
                {
                    text = getHTML (choc::json::toString (manifest->manifest),
                                    choc::json::getEscapedQuotedString (manifest->patchWorker));

                    result.mimeType = "text/html";
                }
                else
                {
                    if (auto moduleText = readJavascriptResource (path, manifest))
                    {
                        text = *moduleText;
                        result.mimeType = getMIMEType (path.substr (path.find_last_of (".")));
                    }
                }

                auto src = reinterpret_cast<const uint8_t*> (text.data());
                result.data.insert (result.data.end(), src, src + text.length());
                return result;
            }

            return {};
        }

        static std::string getMIMEType (std::string_view extension)
        {
            if (extension == ".css")  return "text/css";
            if (extension == ".html") return "text/html";
            if (extension == ".js")   return "text/javascript";
            if (extension == ".mjs")  return "text/javascript";
            if (extension == ".svg")  return "image/svg+xml";
            if (extension == ".wasm") return "application/wasm";
            if (extension == ".ogg")  return "audio/ogg";
            if (extension == ".wav")  return "audio/wav";
            if (extension == ".flac") return "audio/flac";

            return "application/octet-stream";
        }

        static std::string getHTML (const std::string& manifestJSON, const std::string& worker)
        {
            return choc::text::replace (R"(
<!DOCTYPE html>
<html></html>

<script type="module">

import { PatchConnection } from "./cmaj_api/cmaj-patch-connection.js"
import runWorker from WORKER_MODULE

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

try
{
    const connection = new WorkerPatchConnection();
    await runWorker (connection);
}
catch (e)
{
    window.cmaj_reportError (e.toString());
}

</script>
)",
            "MANIFEST", manifestJSON,
            "WORKER_MODULE", worker);
        }

        Patch& patch;
        std::unique_ptr<choc::ui::WebView> webview;
    };

    p.createContextForPatchWorker = [&p]
    {
        return std::make_unique<Worker> (p);
    };
}

} // namespace cmaj
