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
#include "../../choc/text/choc_MIMETypes.h"

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

    static std::unique_ptr<PatchWebView> create (Patch&, const PatchManifest::View&, MimeTypeMappingFn = {});
    ~PatchWebView() override;

    choc::ui::WebView& getWebView();
    void sendMessage (const choc::value::ValueView&) override;
    void reload();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;

    PatchWebView (std::unique_ptr<Impl>, const PatchManifest::View&);
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
    Impl (Patch& p, MimeTypeMappingFn toMimeTypeFn)
        : patch (p), toMimeTypeCustomImpl (std::move (toMimeTypeFn))
    {
        createBindings();
    }

    void sendMessage (const choc::value::ValueView& msg)
    {
        if (! webview.evaluateJavascript ("window.cmaj_deliverMessageFromServer?.(" + choc::json::toString (msg, true) + ");"))
            CMAJ_ASSERT_FALSE;
    }

    void createBindings()
    {
        bool boundOK = webview.bind ("cmaj_sendMessageToServer", [this] (const choc::value::ValueView& args) -> choc::value::Value
        {
            try
            {
                if (args.isArray() && args.size() != 0)
                    patch.handleClientMessage (*ownerView, args[0]);
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

    choc::ui::WebView::Options getWebviewOptions()
    {
        choc::ui::WebView::Options options;
        options.enableDebugMode = allowWebviewDevMode;
        options.acceptsFirstMouseClick = true;
        options.fetchResource = [this] (const auto& path) { return onRequest (path); };
        return options;
    }

    std::optional<choc::ui::WebView::Options::Resource> onRequest (const std::string&);

    PatchWebView* ownerView = nullptr;
    Patch& patch;
    MimeTypeMappingFn toMimeTypeCustomImpl;

   #if CMAJ_ENABLE_WEBVIEW_DEV_TOOLS
    static constexpr bool allowWebviewDevMode = true;
   #else
    static constexpr bool allowWebviewDevMode = false;
   #endif

    choc::ui::WebView webview { getWebviewOptions() };
};

inline std::unique_ptr<PatchWebView> PatchWebView::create (Patch& p, const PatchManifest::View& view, MimeTypeMappingFn toMimeType)
{
    auto impl = std::make_unique <PatchWebView::Impl> (p, std::move (toMimeType));
    return std::unique_ptr<PatchWebView> (new PatchWebView (std::move (impl), view));
}

inline PatchWebView::PatchWebView (std::unique_ptr<Impl> impl, const PatchManifest::View& view)
    : PatchView (impl->patch, view), pimpl (std::move (impl))
{
    pimpl->ownerView = this;
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

inline void PatchWebView::reload()
{
    getWebView().evaluateJavascript ("document.location.reload()");
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
  body { display: block; position: absolute; width: 100%; height: 100%; }
  #cmaj-view-container { display: block; position: relative; width: 100%; height: 100%; overflow: auto; }
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
async function createView()
{
    const patchConnection = new EmbeddedPatchConnection();

    const view = await createPatchViewHolder (patchConnection, viewInfo);

    document.getElementById ("cmaj-view-container").appendChild (view);
}

createView();

</script>
</html>
)";

inline std::optional<choc::ui::WebView::Options::Resource> PatchWebView::Impl::onRequest (const std::string& path)
{
    const auto toMimeType = [this] (const auto& extension)
    {
        if (toMimeTypeCustomImpl)
            if (auto m = toMimeTypeCustomImpl (extension); ! m.empty())
                return m;

        return choc::web::getMIMETypeFromFilename (extension, "application/octet-stream");
    };

    auto relativePath = std::filesystem::path (path).relative_path();
    bool wantsRootHTMLPage = relativePath.empty();

    if (wantsRootHTMLPage)
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
                                                        "$VIEW_TO_USE$", choc::json::toString (viewToUse.view, true)),
                                                     "text/html");
    }

    if (auto content = readJavascriptResource (path, patch.getManifest()))
        if (! content->empty())
            return choc::ui::WebView::Options::Resource (*content, toMimeType (relativePath.extension().string()));

    return {};
}


} // namespace cmaj
