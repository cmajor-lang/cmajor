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
#include "cmaj_EmbeddedWebAssets.h"

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

    static std::unique_ptr<PatchWebView> create (Patch&, const PatchManifest::View*, MimeTypeMappingFn = {});
    ~PatchWebView() override;

    choc::ui::WebView& getWebView();

    void sendMessage (const choc::value::ValueView&) override;

private:
    struct Impl;

    PatchWebView (std::unique_ptr<Impl>, const PatchManifest::View*);

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
    Impl (Patch& p, const PatchManifest::View* v, MimeTypeMappingFn toMimeTypeFn)
        : patch (p), toMimeTypeCustomImpl (std::move (toMimeTypeFn))
    {
        if (v != nullptr)
            view = std::move (*v);

        createBindings();
    }

    void sendMessage (const choc::value::ValueView& msg)
    {
        webview.evaluateJavascript ("if (window.cmaj_handleMessageFromPatch) window.cmaj_handleMessageFromPatch ("
                                    + choc::json::toString (msg, true) + ");");
    }

    void createBindings()
    {
        webview.bind ("cmaj_sendMessageToPatch", [this] (const choc::value::ValueView& args) -> choc::value::Value
        {
            try
            {
                if (args.isArray() && args.size() != 0)
                    patch.handleCientMessage (args[0]);
            }
            catch (const std::exception& e)
            {
                std::cout << "Error processing message from client: " << e.what() << std::endl;
            }

            return {};
        });
    }

    Patch& patch;
    PatchManifest::View view;
    MimeTypeMappingFn toMimeTypeCustomImpl;

   #if defined (DEBUG) || defined (_DEBUG) || ! (defined (NDEBUG) || defined (_NDEBUG))
    static constexpr bool allowWebviewDevMode = true;
   #else
    static constexpr bool allowWebviewDevMode = false;
   #endif

    choc::ui::WebView webview { { allowWebviewDevMode, [this] (const auto& path) { return onRequest (path); } } };

    using ResourcePath = choc::ui::WebView::Options::Path;
    using OptionalResource = std::optional<choc::ui::WebView::Options::Resource>;
    OptionalResource onRequest (const ResourcePath&);

    static std::string toMimeTypeDefaultImpl (std::string_view extension);
};

inline std::unique_ptr<PatchWebView> PatchWebView::create (Patch& p, const PatchManifest::View* view, MimeTypeMappingFn toMimeType)
{
    auto impl = std::make_unique <PatchWebView::Impl> (p, view, std::move (toMimeType));
    return std::unique_ptr<PatchWebView> (new PatchWebView (std::move (impl), view));
}

inline PatchWebView::PatchWebView (std::unique_ptr<Impl> impl, const PatchManifest::View* view)
    : PatchView (impl->patch, view), pimpl (std::move (impl))
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
<html lang="en">
<head>
  <meta charset="utf-8" />
  <title>Cmajor Patch Controls</title>
</head>

<style>
  * { box-sizing: border-box; padding: 0; margin: 0; border: 0; }
  html { background: black; overflow: hidden; }
  body { display: block; position: absolute; width: 100%; height: 100%; }
  #cmaj-outer-container { display: block; position: relative; width: 100%; height: 100%; overflow: auto; }
  #cmaj-inner-container { display: block; position: relative; width: 100%; height: 100%; overflow: visible; transform-origin: 0% 0%; }
</style>

<body>
  <div id="cmaj-outer-container">
    <div id="cmaj-inner-container"></div>
  </div>
</body>

<script type="module">

import { PatchConnectionBase } from "/cmaj_api/cmaj-patch-connection-base.js"

//==============================================================================
class PatchConnection  extends PatchConnectionBase
{
    constructor()
    {
        super();
        this.manifest = MANIFEST;
        window.cmaj_handleMessageFromPatch = msg => this.deliverMessageToClient (msg);
    }

    getResourceAddress (path)
    {
        return path.startsWith ("/") ? path : ("/" + path);
    }

    sendMessageToServer (message)
    {
        window.cmaj_sendMessageToPatch (message);
    }
}

//==============================================================================
const outer = document.getElementById ("cmaj-outer-container");
const inner = document.getElementById ("cmaj-inner-container");

let currentView = null;
let defaultViewWidth = 0, defaultViewHeight = 0;

async function createView()
{
    const view = VIEW_TYPE;
    defaultViewWidth = view?.width || 600;
    defaultViewHeight = view?.height || 400;

    const patchConnection = new PatchConnection();
    currentView = await patchConnection.createView (view);

    if (currentView)
        inner.appendChild (currentView);
}

function getScaleToApplyToView (view, originalViewW, originalViewH, parentToFitTo)
{
    if (view && parentToFitTo)
    {
        const scaleLimits = view.getScaleFactorLimits?.();

        if (scaleLimits && (scaleLimits.minScale || scaleLimits.maxScale))
        {
            const minScale = scaleLimits.minScale || 0.25;
            const maxScale = scaleLimits.maxScale || 5.0;

            const parentStyle = getComputedStyle (parentToFitTo);
            const parentW = parentToFitTo.clientWidth - parseFloat (parentStyle.paddingLeft) - parseFloat (parentStyle.paddingRight);
            const parentH = parentToFitTo.clientHeight - parseFloat (parentStyle.paddingTop) - parseFloat (parentStyle.paddingBottom);

            const scaleW = parentW / originalViewW;
            const scaleH = parentH / originalViewH;

            return Math.min (maxScale, Math.max (minScale, Math.min (scaleW, scaleH)));
        }
    }

    return undefined;
}

function updateViewScale()
{
    const scale = getScaleToApplyToView (currentView, defaultViewWidth, defaultViewHeight, document.body);

    if (scale)
        inner.style.transform = `scale(${scale})`;
    else
        inner.style.transform = "none";
}

createView();

const resizeObserver = new ResizeObserver (() => updateViewScale());
resizeObserver.observe (document.body);

updateViewScale();

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

        return toResource (choc::text::replace (cmajor_patch_gui_html,
                                                "MANIFEST", choc::json::toString (manifestObject),
                                                "VIEW_TYPE", choc::json::toString (viewToUse.view)),
                           toMimeType (".html"));
    }

    auto pathToFind = relativePath.generic_string();
    auto mimeType = toMimeType (relativePath.extension().string());

    if (auto manifest = patch.getManifest())
        if (auto content = manifest->readFileContent (pathToFind); ! content.empty())
            return toResource (content, mimeType);

    if (choc::text::startsWith (pathToFind, "cmaj_api/"))
        if (auto content = EmbeddedWebAssets::findResource (pathToFind.substr (std::string_view ("cmaj_api/").length())); ! content.empty())
            return toResource (content, mimeType);

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
