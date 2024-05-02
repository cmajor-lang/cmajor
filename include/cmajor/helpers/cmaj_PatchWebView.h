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

#include <memory>
#include "cmaj_Patch.h"
#include "../../choc/gui/choc_WebView.h"
#include "../../choc/network/choc_MIMETypes.h"

namespace cmaj
{

//==============================================================================
/// A HTML patch GUI implementation.
struct PatchWebView  : public PatchView
{
    PatchWebView (Patch&, const PatchManifest::View&);
    ~PatchWebView() override;

    void sendMessage (const choc::value::ValueView&) override;
    void reload();

    choc::ui::WebView& getWebView();

    void setStatusMessage (const std::string& newMessage);

    /// Provides a chunk of javascript that goes in a function which is run before the
    /// view element is added to its parent element.
    std::string extraSetupCode;

    /// Map a file extension (".html", ".js") to a MIME type (i.e. "text/html", "text/javascript").
    /// A default implementation is provided, but it is non-exhaustive. If a custom mapping function is given,
    /// it will be called first, falling back to the default implementation if an empty result is returned.
    std::function<std::string(std::string_view extension)> getMIMETypeForExtension;

private:
    void createBindings();
    choc::ui::WebView::Options getWebviewOptions();
    std::optional<choc::ui::WebView::Options::Resource> onRequest (const std::string&);

    choc::ui::WebView webview { getWebviewOptions() };
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

inline PatchWebView::PatchWebView (Patch& p, const PatchManifest::View& view)
    : PatchView (p, view)
{
    createBindings();
}

inline PatchWebView::~PatchWebView() = default;

inline void PatchWebView::sendMessage (const choc::value::ValueView& msg)
{
    webview.evaluateJavascript ("window.cmaj_deliverMessageFromServer?.(" + choc::json::toString (msg, true) + ");");
}

inline void PatchWebView::createBindings()
{
    bool boundOK = webview.bind ("cmaj_sendMessageToServer", [this] (const choc::value::ValueView& args) -> choc::value::Value
    {
        try
        {
            if (args.isArray() && args.size() != 0)
                patch.handleClientMessage (*this, args[0]);
        }
        catch (const std::exception& e)
        {
            std::cout << "Error processing message from client: " << e.what() << std::endl;
        }

        return {};
    });

    (void) boundOK;
    CMAJ_ASSERT (boundOK);
}

inline choc::ui::WebView::Options PatchWebView::getWebviewOptions()
{
    choc::ui::WebView::Options options;

   #if CMAJ_ENABLE_WEBVIEW_DEV_TOOLS
    options.enableDebugMode = true;
   #else
    options.enableDebugMode = false;
   #endif

    options.acceptsFirstMouseClick = true;
    options.fetchResource = [this] (const auto& path) { return onRequest (path); };
    return options;
}

inline choc::ui::WebView& PatchWebView::getWebView()
{
    return webview;
}

inline void PatchWebView::setStatusMessage (const std::string& newMessage)
{
    webview.evaluateJavascript ("window.setStatusMessage (" + choc::json::getEscapedQuotedString (newMessage) + ")");
}

inline void PatchWebView::reload()
{
    webview.evaluateJavascript ("document.location.reload()");
}

static constexpr auto cmajor_patch_gui_html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <title>Cmajor Patch Controls</title>
</head>

<style>
  * { box-sizing: border-box; padding: 0; margin: 0; border: 0; }
  html { background: black; overflow: hidden; }
  body { display: block; position: absolute; width: 100%; height: 100%; color: white; font-family: Monaco, Consolas, monospace; }
  #cmaj-view-container { display: block; position: relative; width: 100%; height: 100%; overflow: auto; }
  #cmaj-error-text { display: block; position: relative; width: 100%; height: 100%; padding: 1rem; text-wrap: wrap; }
</style>

<body>
  <div id="cmaj-view-container"></div>
</body>

<script type="module">

import { PatchConnection } from "../cmaj_api/cmaj-patch-connection.js"
import { createPatchViewHolder } from "./cmaj_api/cmaj-patch-view.js"

//==============================================================================
const patchManifest = $MANIFEST$;

const viewInfo = $VIEW_TO_USE$;

//==============================================================================
class EmbeddedPatchConnection  extends PatchConnection
{
    constructor()
    {
        super();
        this.manifest = patchManifest;
        window.cmaj_deliverMessageFromServer = msg => this.deliverMessageFromServer (msg);
    }

    getResourceAddress (path)
    {
        return path.startsWith ("/") ? path : ("/" + path);
    }

    sendMessageToServer (message)
    {
        window.cmaj_sendMessageToServer (message);
    }
}

//==============================================================================
const container = document.getElementById ("cmaj-view-container");
let isViewActive = false;

async function initialiseContainer()
{
$EXTRA_SETUP_CODE$
}

window.setStatusMessage = (newMessage) =>
{
    isViewActive = false;
    container.innerHTML = `<pre id="cmaj-error-text">${newMessage}</pre>`;
};

async function createViewIfNeeded (patchConnection)
{
    if (isViewActive)
        return;

    container.innerHTML = "";

    await initialiseContainer();

    const view = await createPatchViewHolder (patchConnection, viewInfo);

    if (view)
    {
        container.appendChild (view);
        isViewActive = true;
    }
    else
    {
        window.setStatusMessage ("No view available");
    }
}

async function initialisePatch()
{
    const patchConnection = new EmbeddedPatchConnection();

    const statusListener = async status =>
    {
        const getDescription = () =>
        {
            if (status.manifest?.name)
                return `Error building '${status.manifest.name}':`;

            return `Error:`;
        }

        if (status.error)
            window.setStatusMessage (getDescription() + "\n\n" + status.error.toString());
        else
            await createViewIfNeeded (patchConnection);
    };

    patchConnection.addStatusListener (statusListener);
    patchConnection.requestStatusUpdate();
}

initialisePatch();


</script>
</html>
)";

inline std::optional<choc::ui::WebView::Options::Resource> PatchWebView::onRequest (const std::string& path)
{
    const auto toMimeType = [this] (const auto& extension)
    {
        if (getMIMETypeForExtension)
            if (auto m = getMIMETypeForExtension (extension); ! m.empty())
                return m;

        return choc::network::getMIMETypeFromFilename (extension, "application/octet-stream");
    };

    auto relativePath = std::filesystem::path (path).relative_path();

    if (relativePath.empty())
    {
        choc::value::Value manifestObject;
        cmaj::PatchManifest::View viewToUse;

        if (auto manifest = patch.getManifest())
        {
            manifestObject = manifest->manifest;

            if (auto v = manifest->findDefaultView())
                viewToUse = *v;
        }

        return choc::ui::WebView::Options::Resource (choc::text::replace (cmajor_patch_gui_html,
                                                        "$MANIFEST$", choc::json::toString (manifestObject, true),
                                                        "$VIEW_TO_USE$", choc::json::toString (viewToUse.view, true),
                                                        "$EXTRA_SETUP_CODE$", extraSetupCode),
                                                     "text/html");
    }

    if (auto content = readJavascriptResource (path, patch.getManifest()))
        if (! content->empty())
            return choc::ui::WebView::Options::Resource (*content, toMimeType (relativePath.extension().string()));

    return {};
}


} // namespace cmaj
