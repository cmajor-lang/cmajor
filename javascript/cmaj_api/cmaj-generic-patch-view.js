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

import * as Controls from "./cmaj-parameter-controls.js"
import { PatchConnection } from "./cmaj-patch-connection.js"


//==============================================================================
/** A simple, generic view which can control any type of patch */
class GenericPatchView extends HTMLElement
{
    /** Creates a view for a patch.
     *  @param {PatchConnection} patchConnection - the connection to the target patch
     */
    constructor (patchConnection)
    {
        super();

        this.patchConnection = patchConnection;

        this.statusListener = /** @param {StatusMessage} status */ (status) =>
        {
            this.status = status;
            this.createControlElements();
        };

        this.attachShadow ({ mode: "open" });
        const root = /** @type {ShadowRoot} */ (this.shadowRoot);
        root.innerHTML = this.createHTML();

        this.titleElement      = /** @type {HTMLElement} */ (root.querySelector ("cmaj-generic-patch-title"));
        this.parametersElement = /** @type {HTMLElement} */ (root.querySelector ("cmaj-generic-patch-parameters"));
    }

    /** This is picked up by some of our wrapper code to know whether it makes
     *  sense to put a title bar/logo above the GUI.
     *  @returns {boolean}
     */
    hasOwnTitleBar()
    {
        return true;
    }

    //==============================================================================
    connectedCallback()
    {
        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
    }

    disconnectedCallback()
    {
        this.patchConnection.removeStatusListener (this.statusListener);
    }

    /** @private */
    createControlElements()
    {
        if (! this.parametersElement || ! this.titleElement)
            return;

        this.parametersElement.innerHTML = "";
        this.titleElement.innerText = this.status?.manifest?.name ?? "Cmajor";

        if (this.status?.details?.inputs)
        {
            for (const endpointInfo of this.status.details.inputs)
            {
                if (! endpointInfo.annotation?.hidden)
                {
                    const control = Controls.createLabelledControl (this.patchConnection, endpointInfo);

                    if (control)
                        this.parametersElement.appendChild (control);
                }
            }
        }
    }

    /** @private */
    createHTML()
    {
        const baseUrl = import.meta.url;

        return `
            <style>
            * {
                box-sizing: border-box;
                user-select: none;
                -webkit-user-select: none;
                -moz-user-select: none;
                -ms-user-select: none;
                font-family: Avenir, 'Avenir Next LT Pro', Montserrat, Corbel, 'URW Gothic', source-sans-pro, sans-serif;
                font-size: 0.9rem;
            }

            :host {
                --header-height: 2.5rem;
                --foreground: #ffffff;
                --background: #1a1a1a;

                display: block;
                height: 100%;
                background-color: var(--background);
            }

            cmaj-generic-patch-main {
                background: var(--background);
                height: 100%;
            }

            cmaj-generic-patch-header {
                width: 100%;
                height: var(--header-height);
                border-bottom: 0.1rem solid var(--foreground);
                display: flex;
                justify-content: space-between;
                align-items: center;
            }

            cmaj-generic-patch-title {
                color: var(--foreground);
                text-overflow: ellipsis;
                white-space: nowrap;
                overflow: hidden;
                cursor: default;
                font-size: 140%;
                font-weight: bold;
            }

            cmaj-generic-patch-logo {
                flex: 1;
                height: 80%;
                margin-left: 0.3rem;
                margin-right: 0.3rem;
                background-color: var(--foreground);
                mask: url(${baseUrl}/../assets/cmajor-logo.svg);
                mask-repeat: no-repeat;
                -webkit-mask: url(${baseUrl}/../assets/cmajor-logo.svg);
                -webkit-mask-repeat: no-repeat;
                min-width: 6.25rem;
            }

            cmaj-generic-patch-padding {
                flex: 1;
            }

            cmaj-generic-patch-parameters {
                display: flex;
                flex-flow: row wrap;
                justify-content: center;
                height: calc(100% - var(--header-height));
                overflow: auto;
                padding: 1rem;
                text-align: center;
            }

            ${Controls.getAllCSS()}

            </style>

            <cmaj-generic-patch-main>
              <cmaj-generic-patch-header>
                <cmaj-generic-patch-logo></cmaj-generic-patch-logo>
                <cmaj-generic-patch-title></cmaj-generic-patch-title>
                <cmaj-generic-patch-padding></cmaj-generic-patch-padding>
              </cmaj-generic-patch-header>
              <cmaj-generic-patch-parameters></cmaj-generic-patch-parameters>
            </cmaj-generic-patch-main>`;
    }
}

//==============================================================================
/** Creates a generic view element which can be used to control any patch.
 *  @param {PatchConnection} patchConnection - the connection to the target patch
 *  @returns {HTMLElement}
 */
export default function createPatchView (patchConnection)
{
    const genericPatchViewName = "cmaj-generic-patch-view";

    if (! window.customElements.get (genericPatchViewName))
        window.customElements.define (genericPatchViewName, GenericPatchView);

    const ctor = /** @type {any} */ (window.customElements.get (genericPatchViewName));
    return /** @type {HTMLElement} */ (new ctor (patchConnection));
}
