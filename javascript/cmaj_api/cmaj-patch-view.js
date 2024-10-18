//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { PatchConnection } from "./cmaj-patch-connection.js"

//==============================================================================
/** Returns a list of types of view that can be created for this patch.
 */
export function getAvailableViewTypes (patchConnection)
{
    if (! patchConnection)
        return [];

    if (patchConnection.manifest?.view?.src)
        return ["custom", "generic"];

    return ["generic"];
}

//==============================================================================
/** Creates and returns a HTMLElement view which can be shown to control this patch.
 *
 *  If no preferredType argument is supplied, this will return either a custom patch-specific
 *  view (if the manifest specifies one), or a generic view if not. The preferredType argument
 *  can be used to choose one of the types of view returned by getAvailableViewTypes().
 *
 *  @param {PatchConnection} patchConnection - the connection to use
 *  @param {string} preferredType - the name of the type of view to open, e.g. "generic"
 *                                  or the name of one of the views in the manifest
 *  @returns {HTMLElement} a HTMLElement that can be displayed as the patch GUI
 */
export async function createPatchView (patchConnection, preferredType)
{
    if (patchConnection?.manifest)
    {
        let view = patchConnection.manifest.view;

        if (view && preferredType === "generic")
            if (view.src)
                view = undefined;

        const viewModuleURL = view?.src ? patchConnection.getResourceAddress (view.src) : "./cmaj-generic-patch-view.js";
        const viewModule = await import (viewModuleURL);
        const patchView = await viewModule?.default (patchConnection);

        if (patchView)
        {
            patchView.style.display = "block";

            if (view?.width > 10)
                patchView.style.width = view.width + "px";
            else
                patchView.style.width = undefined;

            if (view?.height > 10)
                patchView.style.height = view.height + "px";
            else
                patchView.style.height = undefined;

            return patchView;
        }
    }

    return undefined;
}

//==============================================================================
/** If a patch view declares itself to be scalable, this will attempt to scale it to fit
 *  into a given parent element.
 *
 *  @param {HTMLElement} view - the patch view
 *  @param {HTMLElement} parentToScale - the patch view's direct parent element, to which
 *                                       the scale factor will be applied
 *  @param {HTMLElement} parentContainerToFitTo - an outer parent of the view, whose bounds
 *                                                the view will be made to fit
 */
export function scalePatchViewToFit (view, parentToScale, parentContainerToFitTo)
{
    function getClientSize (view)
    {
        const clientStyle = getComputedStyle (view);

        return {
            width:  view.clientHeight - parseFloat (clientStyle.paddingTop)  - parseFloat (clientStyle.paddingBottom),
            height: view.clientWidth  - parseFloat (clientStyle.paddingLeft) - parseFloat (clientStyle.paddingRight)
        };
    }

    const scaleLimits = view.getScaleFactorLimits?.();

    if (scaleLimits && (scaleLimits.minScale || scaleLimits.maxScale) && parentContainerToFitTo)
    {
        const minScale = scaleLimits.minScale || 0.25;
        const maxScale = scaleLimits.maxScale || 5.0;

        const targetSize = getClientSize (parentContainerToFitTo);
        const clientSize = getClientSize (view);

        const scaleW = targetSize.width / clientSize.width;
        const scaleH = targetSize.height / clientSize.height;

        const scale = Math.min (maxScale, Math.max (minScale, Math.min (scaleW, scaleH)));

        parentToScale.style.transform = `scale(${scale})`;
    }
    else
    {
        parentToScale.style.transform = "none";
    }
}

//==============================================================================
class PatchViewHolder extends HTMLElement
{
    constructor (view)
    {
        super();
        this.view = view;
        this.style = `display: block; position: relative; width: 100%; height: 100%; overflow: visible; transform-origin: 0% 0%;`;
    }

    connectedCallback()
    {
        this.appendChild (this.view);
        this.resizeObserver = new ResizeObserver (() => scalePatchViewToFit (this.view, this, this.parentElement));
        this.resizeObserver.observe (this.parentElement);
        scalePatchViewToFit (this.view, this, this.parentElement);
    }

    disconnectedCallback()
    {
        this.resizeObserver = undefined;
        this.innerHTML = "";
    }
}

//==============================================================================
/** Creates and returns a HTMLElement view which can be shown to control this patch.
 *
 *  Unlike createPatchView(), this will return a holder element that handles scaling
 *  and resizing, and which follows changes to the size of the parent that you
 *  append it to.
 *
 *  If no preferredType argument is supplied, this will return either a custom patch-specific
 *  view (if the manifest specifies one), or a generic view if not. The preferredType argument
 *  can be used to choose one of the types of view returned by getAvailableViewTypes().
 *
 *  @param {PatchConnection} patchConnection - the connection to use
 *  @param {string} preferredType - the name of the type of view to open, e.g. "generic"
 *                                  or the name of one of the views in the manifest
 *  @returns {HTMLElement} a HTMLElement that can be displayed as the patch GUI
 */
export async function createPatchViewHolder (patchConnection, preferredType)
{
    const view = await createPatchView (patchConnection, preferredType);

    if (view)
    {
        const patchViewHolderName = "cmaj-patch-view-holder";

        if (! window.customElements.get (patchViewHolderName)) window.customElements.define (patchViewHolderName, PatchViewHolder);

        return new (window.customElements.get (patchViewHolderName)) (view);
    }
}
