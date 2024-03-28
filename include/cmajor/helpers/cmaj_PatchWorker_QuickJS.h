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
#include "../../choc/javascript/choc_javascript_QuickJS.h"
#include "../../choc/javascript/choc_javascript_Timer.h"
#include "../../choc/javascript/choc_javascript_Console.h"


namespace cmaj
{

/// When you create a Patch object, you need to set its createContextForPatchWorker
/// property so that it knows what kind of javascript context to create to run
/// any patch workers that may be needed. This function sets up a QuickJS context
/// for that purpose, and gives it the appropriate library functions that it needs.
inline void enableQuickJSPatchWorker (Patch& p)
{
    struct Worker : Patch::WorkerContext
    {
        Worker (Patch& p) : patch (p) {}
        ~Worker() override {}

        void initialise (std::function<void(const choc::value::ValueView&)> sendMessageToPatch,
                         std::function<void(const std::string&)> reportError) override
        {
            context = choc::javascript::createQuickJSContext();

            registerTimerFunctions (context);
            registerConsoleFunctions (context);

            auto& p = patch;

            context.registerFunction ("_internalReadResource", [&p] (choc::javascript::ArgumentList args) -> choc::value::Value
            {
                try
                {
                    if (auto path = args.get<std::string>(0); ! path.empty())
                    {
                        if (auto manifest = p.getManifest())
                        {
                            if (auto content = manifest->readFileContent (path))
                            {
                                if (choc::text::findInvalidUTF8Data (content->data(), content->length()) == nullptr)
                                    return choc::value::Value (*content);

                                return choc::value::createArray (static_cast<uint32_t> (content->length()),
                                                                [&] (uint32_t i) { return static_cast<int32_t> ((*content)[i]); });
                            }
                        }
                    }
                }
                catch (...)
                {}

                return {};
            });

            context.registerFunction ("_internalReadResourceAsAudioData", [&p] (choc::javascript::ArgumentList args) -> choc::value::Value
            {
                try
                {
                    if (auto path = args.get<std::string>(0); ! path.empty())
                        if (auto manifest = p.getManifest())
                            return readManifestResourceAsAudioData (*manifest, path, args[1] != nullptr ? *args[1] : choc::value::Value());
                }
                catch (...)
                {}

                return {};
            });

            choc::javascript::Context::ReadModuleContentFn resolveModule = [&p] (std::string_view path) -> std::optional<std::string>
            {
                if (auto manifest = p.getManifest())
                    return readJavascriptResource (path, manifest);

                return {};
            };

            context.registerFunction ("cmaj_sendMessageToServer", [send = std::move (sendMessageToPatch)] (choc::javascript::ArgumentList args) -> choc::value::Value
            {
                if (auto message = args[0])
                    send (*message);

                return {};
            });

            if (auto manifest = p.getManifest())
            {
                context.runModule (getGlueCode (choc::json::toString (manifest->manifest),
                                                choc::json::getEscapedQuotedString (manifest->patchWorker)),
                                   resolveModule,
                                   [reportError = std::move (reportError)] (const std::string& error, const choc::value::ValueView&)
                                   {
                                       if (! error.empty())
                                           reportError (error);
                                   });
            }
        }

        void sendMessage (const std::string& msg, std::function<void(const std::string&)> reportError) override
        {
            context.run ("currentView?.deliverMessageFromServer(" + msg + ");",
                         [reportError = std::move (reportError)] (const std::string& error, const choc::value::ValueView&)
            {
                if (! error.empty())
                    reportError (error);
            });
        }

        static std::string getGlueCode (const std::string& manifestJSON, const std::string& worker)
        {
            return choc::text::replace (R"(
import { PatchConnection } from "./cmaj_api/cmaj-patch-connection.js"
import runWorker from WORKER_MODULE

class WorkerPatchConnection  extends PatchConnection
{
    constructor()
    {
        super();
        this.manifest = MANIFEST;
        globalThis.currentView = this;
    }

    getResourceAddress (path)
    {
        return path.startsWith ("/") ? path : ("/" + path);
    }

    sendMessageToServer (message)
    {
        cmaj_sendMessageToServer (message);
    }
}

const connection = new WorkerPatchConnection();

connection.readResource = (path) =>
{
    return {
        then (resolve, reject)
        {
            const data = _internalReadResource (path);

            if (data)
                resolve (data);
            else
                reject ("Failed to load resource");

            return this;
        },
        catch() {},
        finally() {}
    };
}

connection.readResourceAsAudioData = (path) =>
{
    return {
        then (resolve, reject)
        {
            const data = _internalReadResourceAsAudioData (path);

            if (data)
                resolve (data);
            else
                reject ("Failed to load resource");

            return this;
        },
        catch() {},
        finally() {}
    };
}

runWorker (connection);
)",
            "MANIFEST", manifestJSON,
            "WORKER_MODULE", worker);
        }

        Patch& patch;
        choc::javascript::Context context;
    };

    p.createContextForPatchWorker = [&p]
    {
        return std::make_unique<Worker> (p);
    };
}

} // namespace cmaj
