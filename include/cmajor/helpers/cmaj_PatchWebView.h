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

#include <memory>

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

    static std::unique_ptr<PatchWebView> create (Patch&, uint32_t viewWidth, uint32_t viewHeight, MimeTypeMappingFn = {});
    ~PatchWebView() override;

    choc::ui::WebView& getWebView();

    void sendMessage (const choc::value::ValueView&) override;

private:
    struct Impl;

    PatchWebView (std::unique_ptr<Impl>);

    std::unique_ptr<Impl> pimpl;
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

struct PatchWebView::Impl
{
    Impl (Patch&, uint32_t viewWidth, uint32_t viewHeight, MimeTypeMappingFn = {});

    void sendMessage (const choc::value::ValueView&);

    Patch& patch;
    uint32_t width = 0, height = 0;
    bool resizable = true;

    MimeTypeMappingFn toMimeTypeCustomImpl;
    choc::ui::WebView webview { { true, [this] (const auto& path) { return onRequest (path); } } };
    std::filesystem::path jsModuleFilename;

    void initialiseFromFirstHTMLView();
    void createBindings();

    using ResourcePath = choc::ui::WebView::Options::Path;
    using OptionalResource = std::optional<choc::ui::WebView::Options::Resource>;
    OptionalResource onRequest (const ResourcePath&);

    static std::string toMimeTypeDefaultImpl (std::string_view extension);
};

inline PatchWebView::Impl::Impl (Patch& p, uint32_t viewWidth, uint32_t viewHeight, MimeTypeMappingFn toMimeTypeFn)
    : patch (p), width (viewWidth), height (viewHeight), toMimeTypeCustomImpl (std::move (toMimeTypeFn))
{
    initialiseFromFirstHTMLView();
    createBindings();
}

inline void PatchWebView::Impl::sendMessage (const choc::value::ValueView& msg)
{
    webview.evaluateJavascript ("if (window.cmaj_handleMessageFromPatch) window.cmaj_handleMessageFromPatch ("
                                  + choc::json::toString (msg, true) + ");");
}

inline void PatchWebView::Impl::initialiseFromFirstHTMLView()
{
    if (auto manifest = patch.getManifest())
    {
        for (auto& view : manifest->views)
        {
            if (manifest->fileExists (view.source))
            {
                auto sourceFile = std::filesystem::path (view.source);

                if (sourceFile.extension() == ".js")
                {
                    jsModuleFilename = sourceFile;
                    width  = view.width;
                    height = view.height;
                    resizable = view.resizable;
                    return;
                }
            }
        }
    }
}

inline void PatchWebView::Impl::createBindings()
{
    webview.bind ("cmaj_sendMessageToPatch", [this] (const choc::value::ValueView& args) -> choc::value::Value
    {
        if (args.isArray() && args.size() != 0)
            patch.handleCientMessage (args[0]);

        return {};
    });
}

inline std::unique_ptr<PatchWebView> PatchWebView::create (Patch& p, uint32_t viewWidth, uint32_t viewHeight, MimeTypeMappingFn toMimeType)
{
    auto impl = std::make_unique <PatchWebView::Impl> (p, viewWidth, viewHeight, std::move (toMimeType));
    return std::unique_ptr<PatchWebView> (new PatchWebView (std::move (impl)));
}

inline PatchWebView::PatchWebView (std::unique_ptr<Impl> impl)
    : PatchView (impl->patch, impl->width, impl->height), pimpl (std::move (impl))
{
}

inline PatchWebView::~PatchWebView() = default;

inline choc::ui::WebView& PatchWebView::getWebView()
{
    return pimpl->webview;
}

inline void PatchWebView::sendMessage (const choc::value::ValueView& msg)
{
    return pimpl->sendMessage (msg);
}

static constexpr auto cmajor_patch_gui_html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Cmajor Patch Controls</title>
</head>

<style>
  body { overflow: hidden; margin: 0; padding: 0; }
</style>

<body></body>

<script type="module">

IMPORT_VIEW

function PatchConnection()
{
    this.onPatchStatusChanged        = function (errorMessage, patchManifest, inputsList, outputsList, details) {};
    this.onSampleRateChanged         = function (newSampleRate) {};
    this.onParameterEndpointChanged  = function (endpointID, newValue) {};
    this.onOutputEvent               = function (endpointID, newValue) {};
    this.onStoredStateValueChanged   = function (key, value) {};
    this.onFullStateValue            = function (state) {};

    this.getGenericGUIResourceAddress = function (path)                        { return path; }
    this.getResourceAddress           = function (path)                        { return path; }

    this.requestStatusUpdate        = function()                               { window.cmaj_sendMessageToPatch ({ type: "req_status" }); };
    this.resetToInitialState        = function()                               { window.cmaj_sendMessageToPatch ({ type: "req_reset" }); }
    this.requestEndpointValue       = function (endpointID)                    { window.cmaj_sendMessageToPatch ({ type: "req_endpoint", id: endpointID }); };
    this.requestStoredStateValue    = function (key)                           { window.cmaj_sendMessageToPatch ({ type: "req_state_value", key: key }); }
    this.sendEventOrValue           = function (endpointID, value, numFrames)  { window.cmaj_sendMessageToPatch ({ type: "send_value", id: endpointID, value: value, rampFrames: numFrames }); };
    this.sendParameterGestureStart  = function (endpointID)                    { window.cmaj_sendMessageToPatch ({ type: "send_gesture_start", id: endpointID }); };
    this.sendParameterGestureEnd    = function (endpointID)                    { window.cmaj_sendMessageToPatch ({ type: "send_gesture_end", id: endpointID }); };
    this.sendMIDIInputEvent         = function (shortMIDICode)                 { window.cmaj_sendMessageToPatch ({ type: "midi_input", midiEvent: shortMIDICode }); };
    this.sendStoredStateValue       = function (key, newValue)                 { window.cmaj_sendMessageToPatch ({ type: "send_state_value", key : key, value: newValue }); };
    this.sendFullStoredState        = function (fullState)                     { window.cmaj_sendMessageToPatch ({ type: "send_full_state", value: fullState }); }

    const self = this;

    window.cmaj_handleMessageFromPatch = function (msg)
    {
        switch (msg.type)
        {
            case "output_event":    self.onOutputEvent?. (msg.ID, msg.value); break;
            case "param_value":     self.onParameterEndpointChanged?. (msg.ID, msg.value); break;
            case "status":          self.onPatchStatusChanged?. (msg.error, msg.manifest, msg.details?.inputs, msg.details?.outputs, msg.details); break;
            case "sample_rate":     self.onSampleRateChanged?. (msg.rate); break;
            case "state_key_value": self.onStoredStateValueChanged?. (msg.key, msg.value); break;
            case "full_state":      this.onFullStateValue?. (msg.value); break;
            default: break;
        }
    };
}

function createConnection() { return new PatchConnection(); }

async function appendPatchView (parent)
{
    const patchView = await createPatchView (createConnection());
    patchView.style.display = "block";
    patchView.style.height = "100vh";

    parent.appendChild (patchView);
}

appendPatchView (document.body);

</script>
</html>
)";

inline PatchWebView::Impl::OptionalResource PatchWebView::Impl::onRequest (const ResourcePath& path)
{
    const auto toResource = [] (std::string_view content, const auto& mimeType) -> choc::ui::WebView::Options::Resource
    {
        return
        {
            std::vector<uint8_t> (reinterpret_cast<const uint8_t*> (content.data()),
                                  reinterpret_cast<const uint8_t*> (content.data()) + content.size()),
            mimeType
        };
    };

    const auto toMimeType = [this] (const auto& extension)
    {
        const auto mimeType = toMimeTypeCustomImpl ? toMimeTypeCustomImpl (extension) : std::string {};
        return mimeType.empty() ? toMimeTypeDefaultImpl (extension) : mimeType;
    };

    auto relativePath = std::filesystem::path (path).relative_path();
    bool usingDefaultView = jsModuleFilename.empty();

    if (relativePath.empty())
    {
        auto viewLocation = usingDefaultView ? "/index.js"
                                             : "/" + jsModuleFilename.filename().string();

        return toResource (choc::text::replace (cmajor_patch_gui_html,
                                                "IMPORT_VIEW",
                                                "import createPatchView from "
                                                   + choc::json::getEscapedQuotedString (viewLocation) + ";"),
                           toMimeType (".html"));
    }

    if (usingDefaultView)
    {
        if (auto content = DefaultGUI::findResource (relativePath.string()); ! content.empty())
            return toResource (content, toMimeType (relativePath.extension().string()));
    }
    else
    {
        if (auto manifest = patch.getManifest())
            if (auto content = manifest->readFileContent ((jsModuleFilename.parent_path() / relativePath).generic_string()); ! content.empty())
                return toResource (content, toMimeType (relativePath.extension().string()));
    }

    return {};
}

inline std::string PatchWebView::Impl::toMimeTypeDefaultImpl (std::string_view extension)
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
