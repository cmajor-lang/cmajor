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

import * as patchViewUtils from "../../cmaj_api/cmaj-patch-view.js";
import { el, Disposer } from "../core/dom.js";

/*
    Shows the patch's own GUI (its custom view, or the generic one). Each PatchGUIView makes its
    own instance with createPatchViewHolder(), so the GUI tab and the floating window each have
    an independent copy of the patch view on the same PatchConnection.

    The view is re-created when the connection changes, when the store's guiGeneration changes
    (after an asset or manifest change, or when the endpoints change), or when the user picks a
    different view type. A plain rebuild of the DSP code keeps the existing view.

    After the patch's files change, the new view's modules are loaded from new URLs (see
    addVersionFolder() in cmaj-patch-panel.js). If creating it then fails (e.g. a view module that
    defines a custom element without checking whether it's already defined), the page is
    reloaded, once, as the old panel always did.

    ctx: the panel context (store, settings, ...)
    options: { fill: true to stretch the view to the container (floating window) }
*/
export class PatchGUIView
{
    constructor (ctx, options = {})
    {
        this.ctx = ctx;
        this.options = options;
        this.disposer = new Disposer();
        this.viewType = undefined;
        this.creationNumber = 0;
        this.currentKey = null;
        this.isDisposed = false;

        this.holder = el ("div", { class: "cp-gui-holder" });
        this.message = el ("div", { class: "cp-gui-message" });
        this.banner = el ("div", { class: "cp-gui-banner", role: "status", hidden: true });
        this.element = el ("div", { class: ["cp-gui-view", options.fill && "cp-gui-fill"] }, this.holder, this.message, this.banner);

        this.disposer.add (ctx.store.subscribe (["connection", "guiGeneration", "loaded", "error", "statusText", "connected", "guiViewType"],
                                                () => this.update(), true));
    }

    dispose()
    {
        this.isDisposed = true;
        this.disposer.dispose();
        this.removeView();
        this.element.remove();
    }

    /// The view types this patch offers ("custom", "generic")
    getAvailableViewTypes()
    {
        return patchViewUtils.getAvailableViewTypes (this.ctx.store.get ("connection")?.patchConnection);
    }

    update()
    {
        const s = this.ctx.store.state;
        const patchConnection = s.connection?.patchConnection;
        const viewType = s.guiViewType || undefined;
        const key = patchConnection ? `${s.guiGeneration}|${viewType}` : null;

        if (! patchConnection)
            this.removeView();
        else if (key !== this.currentKey || patchConnection !== this.currentConnection)
            this.createView (patchConnection, viewType, key);

        // what to say when there's no view (or over one that's out of date)
        const hasView = !! this.holder.firstChild;

        if (! hasView)
            this.showMessage (! s.connected ? (s.statusText || "Connecting to the Cmajor server…")
                                : s.error ? "The patch failed to build: see Problems"
                                : s.statusText ? s.statusText
                                : patchConnection ? "" : "No patch loaded");
        else
            this.showMessage (null);

        this.banner.hidden = ! (hasView && (s.error || ! s.loaded));
        this.banner.textContent = s.error ? "Build failed: showing the last good build"
                                          : (s.statusText || "Rebuilding…");
        this.banner.classList.toggle ("cp-error", !! s.error);
    }

    showMessage (text)
    {
        this.message.textContent = text ?? "";
        this.message.hidden = ! text;
    }

    async createView (patchConnection, viewType, key)
    {
        const creationNumber = ++this.creationNumber;
        const isAfterFileChange = (this.ctx.getResourceVersion?.() ?? 0) > 0;
        this.currentKey = key;
        this.currentConnection = patchConnection;

        // (something newer was asked for while this one was loading, or the view was removed)
        const isOutOfDate = () => this.isDisposed || creationNumber !== this.creationNumber;

        try
        {
            const view = await patchViewUtils.createPatchViewHolder (patchConnection, viewType);

            if (isOutOfDate())
                return;

            this.clearHolder();

            if (view)
            {
                this.holder.append (view);
                this.view = view;
            }
        }
        catch (e)
        {
            if (isOutOfDate())
                return;

            this.clearHolder();
            this.ctx.log?.("error", "gui", "Couldn't create the patch's GUI: " + (e?.message ?? e));

            if (isAfterFileChange && reloadPageOnce())
                return;
        }

        this.update();
    }

    /// Removes the view, and abandons any that's still being created
    removeView()
    {
        ++this.creationNumber;
        this.clearHolder();
    }

    clearHolder()
    {
        // (removing the holder calls the patch view's disconnectedCallback, which is where views
        // detach their listeners)
        this.holder.replaceChildren();
        this.view = null;

        if (! this.ctx.store.get ("connection"))
        {
            this.currentKey = null;
            this.currentConnection = null;
        }
    }

    /// The natural size of the patch's view, from the manifest (or a default)
    getManifestSize()
    {
        const view = this.ctx.store.get ("manifest")?.view;
        return { width: view?.width > 10 ? view.width : 600, height: view?.height > 10 ? view.height : 400,
                 resizable: view?.resizable !== false };
    }
}

//==============================================================================
const reloadTimeKey = "cmaj-panel-gui-reload-time";

/// Reloads the page, unless it was already reloaded for this in the last 20 seconds (so a view
/// that's simply broken shows its error rather than reloading for ever). Returns true if it did.
function reloadPageOnce()
{
    try
    {
        const last = Number (sessionStorage.getItem (reloadTimeKey) ?? 0);

        if (Date.now() - last < 20000)
            return false;

        sessionStorage.setItem (reloadTimeKey, String (Date.now()));
    }
    catch (e)
    {
        return false;   // (no storage, so no way to tell whether it would loop)
    }

    location.reload();
    return true;
}
