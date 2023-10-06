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

import { createPatchView } from "/cmaj_api/cmaj-patch-view.js"


export default class PatchViewHolder extends HTMLElement
{
    constructor()
    {
        super();
        this.innerHTML = this.getHTML();
        this.container = this.querySelector ("#cmaj-view-container");
        this.viewSizeAdjustment = 2;
    }

    connectedCallback()
    {
        this.resizeObserver = new ResizeObserver (() => this.updateViewScale());

        this.resizeObserver.observe (this.parentElement);
        this.updateViewScale();
    }

    disconnectedCallback()
    {
        this.resizeObserver?.disconnect();
        this.resizeObserver = undefined;
        this.clear();
    }

    updateViewScale()
    {
        if (this.currentView && this.parentElement)
        {
            const viewStyle = getComputedStyle (this.currentView);
            const viewW = this.currentView.scrollWidth - parseFloat (viewStyle.paddingLeft) - parseFloat (viewStyle.paddingRight) + this.viewSizeAdjustment;
            const viewH = this.currentView.scrollHeight - parseFloat (viewStyle.paddingTop) - parseFloat (viewStyle.paddingBottom) + this.viewSizeAdjustment;

            const scale = this.getScaleToApplyToView (this.currentView, viewW, viewH, this.parentElement);

            if (scale)
                this.container.style.transform = `scale(${scale})`;
            else
                this.container.style.transform = "none";
        }
    }

    getScaleToApplyToView (view, originalViewW, originalViewH, parentToFitTo)
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

    async refreshView (session, viewType)
    {
        this.clear();

        this.viewPatchConnection = session.createPatchConnection();

        this.currentView = await createPatchView (this.viewPatchConnection, viewType);

        if (this.currentView)
        {
            this.container.appendChild (this.currentView);
            this.updateViewScale();
        }
        else
        {
            this.clear();
        }
    }

    clear()
    {
        this.viewPatchConnection?.dispose?.();
        this.viewPatchConnection = undefined;

        if (this.currentView)
        {
            this.container.removeChild (this.currentView);
            this.currentView = undefined;
        }

        this.container.style.transform = "none";
    }

    getHTML()
    {
        return `
<style>
    * {
        box-sizing: border-box;
    }

    :host {
        position: relative;
        display: block;
    }

    #cmaj-view-container {
        position: absolute;
        overflow: visible;
        transform-origin: 0% 0%;
        width: 100%;
        height: 100%;
    }
</style>
<div id="cmaj-view-container"></div>
`;
    }
}
