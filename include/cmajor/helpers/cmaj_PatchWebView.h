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

#include "../../choc/gui/choc_WebView.h"

#include "cmaj_PatchUtilities.h"
#include "cmaj_DefaultGUI.h"

namespace cmaj
{

//==============================================================================
/// A HTML patch GUI implementation.
struct PatchWebView  : public PatchView
{
    /// Map a file extension (".html", ".js") to a MIME type (i.e. "text/html", "text/javascript").
    /// A default implementation is provided, but it is non-exhaustive. If a custom mapping function is given,
    /// it will be called first, falling back to the default implementation if an empty result is returned.
    using MimeTypeMappingFn = std::function<std::string(std::string_view extension)>;

    PatchWebView (Patch&, uint32_t viewWidth, uint32_t viewHeight, MimeTypeMappingFn = {});

    choc::ui::WebView& getWebView()  { return webview; }

    void sendMessage (const choc::value::ValueView&) override;

private:
    MimeTypeMappingFn toMimeTypeCustomImpl;
    choc::ui::WebView webview { { true, [this] (const auto& path) { return onRequest (path); } } };
    std::filesystem::path indexFilename, rootFolder;

    void initialiseFromFirstHTMLView();
    void createBindings();

    using ResourcePath = choc::ui::WebView::Options::Path;
    using OptionalResource = std::optional<choc::ui::WebView::Options::Resource>;
    OptionalResource onRequest (const ResourcePath&);

    static std::string toMimeTypeDefaultImpl (std::string_view extension);
};


//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

inline PatchWebView::PatchWebView (Patch& p, uint32_t viewWidth, uint32_t viewHeight, MimeTypeMappingFn toMimeTypeFn)
    : PatchView (p, viewWidth, viewHeight), toMimeTypeCustomImpl (std::move (toMimeTypeFn))
{
    initialiseFromFirstHTMLView();
    createBindings();
}

inline void PatchWebView::sendMessage (const choc::value::ValueView& msg)
{
    webview.evaluateJavascript ("if (window.patchConnection) window.patchConnection.handleEventFromServer ("
                                  + choc::json::toString (msg, true) + ");");
}

inline void PatchWebView::initialiseFromFirstHTMLView()
{
    for (auto& view : loadedPatch.manifest.views)
    {
        if (loadedPatch.manifest.fileExists (view.html))
        {
            const auto index = std::filesystem::path (view.html);
            indexFilename = index.filename();
            rootFolder = index.parent_path();

            width  = view.width;
            height = view.height;
            resizable = view.resizable;
            return;
        }
    }
}

inline void PatchWebView::createBindings()
{
    webview.addInitScript (R"(
    function PatchConnection()
    {
        this.onPatchStatusChanged        = function (errorMessage, patchManifest, inputsList, outputsList) {};
        this.onSampleRateChanged         = function (newSampleRate) {};
        this.onParameterEndpointChanged  = function (endpointID, newValue) {};
        this.onOutputEvent               = function (endpointID, newValue) {};
        this.onStoredStateChanged        = function() {};

        this.requestStatusUpdate = function()
        {
            window.cmaj_clientRequest ({ type: "req_status" });
        };

        this.requestEndpointValue = function (endpointID)
        {
            window.cmaj_clientRequest ({ type: "req_endpoint", id: endpointID });
        };

        this.sendEventOrValue = function (endpointID, value, optionalNumFrames)
        {
            window.cmaj_clientRequest ({ type: "send_value", id: endpointID, value: value, rampFrames: optionalNumFrames });
        };

        this.sendParameterGestureStart = function (endpointID)
        {
            window.cmaj_clientRequest ({ type: "send_gesture_start", id: endpointID });
        };

        this.sendParameterGestureEnd = function (endpointID)
        {
            window.cmaj_clientRequest ({ type: "send_gesture_end", id: endpointID });
        };

        this.getStoredState = function()
        {
            return window.cmaj_clientRequest ({ type: "get_state" });
        }

        this.setStoredState = function (newStateString)
        {
            window.cmaj_clientRequest ({ type: "set_state", value: newStateString });
        }

        this.handleEventFromServer = function (msg)
        {
            if (msg.type == "output_event")         this.onOutputEvent (msg.ID, msg.value);
            else if (msg.type == "param_value")     this.onParameterEndpointChanged (msg.ID, msg.value);
            else if (msg.type == "status")          this.onPatchStatusChanged (msg.error, msg.manifest, msg.inputs, msg.outputs);
            else if (msg.type == "sample_rate")     this.onSampleRateChanged (msg.rate);
            else if (msg.type == "state_changed")   this.onStoredStateChanged();
        };

        window.patchConnection = this;
    }
    )");

    webview.bind ("cmaj_clientRequest", [this] (const choc::value::ValueView& args) -> choc::value::Value
    {
        if (args.isArray())
        {
            if (auto msg = args[0]; msg.isObject())
            {
                if (auto typeMember = msg["type"]; typeMember.isString())
                {
                    auto type = typeMember.getString();

                    if (type == "send_value")
                    {
                        if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                        {
                            auto endpointID = cmaj::EndpointID::create (endpointIDMember.getString());
                            loadedPatch.sendEventOrValueToPatch (endpointID, msg["value"], msg["rampFrames"].getWithDefault<int32_t> (-1));
                        }
                    }
                    else if (type == "send_gesture_start")
                    {
                        if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                            loadedPatch.sendGestureStart (cmaj::EndpointID::create (endpointIDMember.getString()));
                    }
                    else if (type == "send_gesture_end")
                    {
                        if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                            loadedPatch.sendGestureEnd (cmaj::EndpointID::create (endpointIDMember.getString()));
                    }
                    else if (type == "req_status")
                    {
                        patch.sendPatchStatusChangeToViews();
                    }
                    else if (type == "req_endpoint")
                    {
                        if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                        {
                            auto endpointID = cmaj::EndpointID::create (endpointIDMember.getString());

                            if (auto param = loadedPatch.findParameter (endpointID))
                                patch.sendParameterChangeToViews (endpointID, param->currentValue);
                        }
                    }
                    else if (type == "set_state")
                    {
                        if (auto value = msg["value"]; value.isString())
                            patch.setStoredState (value.toString(), true);
                    }
                    else if (type == "get_state")
                    {
                        return choc::value::Value (patch.getStoredState());
                    }
                }
            }
        }

        return {};
    });
}

inline PatchWebView::OptionalResource PatchWebView::onRequest (const ResourcePath& path)
{
    const auto toResource = [] (const std::string& content, const auto& mimeType) -> choc::ui::WebView::Options::Resource
    {
        return
        {
            std::vector<uint8_t> (reinterpret_cast<const uint8_t*> (content.c_str()),
                                  reinterpret_cast<const uint8_t*> (content.c_str()) + content.size()),
            mimeType
        };
    };

    const auto toMimeType = [this] (const auto& extension)
    {
        const auto mimeType = toMimeTypeCustomImpl ? toMimeTypeCustomImpl (extension) : std::string {};
        return mimeType.empty() ? toMimeTypeDefaultImpl (extension) : mimeType;
    };

    const auto navigateToIndex = path == "/";

    const auto shouldServeDefaultGUIResource = indexFilename.empty();
    if (shouldServeDefaultGUIResource)
    {
        const auto file = std::filesystem::path (path).relative_path();

        if (const auto content = DefaultGUI::findResource (file.string()); ! content.empty())
            return toResource (content, toMimeType (navigateToIndex ? ".html" : file.extension().string()));

        return {};
    }

    const auto relativePath = navigateToIndex ? indexFilename : std::filesystem::path (path).relative_path();
    const auto file = rootFolder / relativePath;

    if (const auto content = loadedPatch.manifest.readFileContent (file.string()); ! content.empty())
        return toResource (content, toMimeType (relativePath.extension().string()));

    return {};
}

inline std::string PatchWebView::toMimeTypeDefaultImpl (std::string_view extension)
{
    if (extension == ".css")  return "text/css";
    if (extension == ".html") return "text/html";
    if (extension == ".js")   return "text/javascript";
    if (extension == ".mjs")  return "text/javascript";
    if (extension == ".svg")  return "image/svg+xml";
    if (extension == ".wasm") return "application/wasm";

    return "application/octet-stream";
}

} // namespace cmaj
