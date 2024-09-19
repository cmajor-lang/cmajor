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

        this.statusListener = status =>
        {
            this.status = status;
            this.createControlElements();
        };

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();

        this.titleElement      = this.shadowRoot.getElementById ("patch-title");
        this.parametersElement = this.shadowRoot.getElementById ("patch-parameters");
    }

    /** This is picked up by some of our wrapper code to know whether it makes
     *  sense to put a title bar/logo above the GUI.
     */
    hasOwnTitleBar()
    {
        return true;
    }

    //==============================================================================
    /** @private */
    connectedCallback()
    {
        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
    }

    /** @private */
    disconnectedCallback()
    {
        this.patchConnection.removeStatusListener (this.statusListener);
    }

    /** @private */
    createControlElements()
    {
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
    getHTML()
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

            .main {
                background: var(--background);
                height: 100%;
            }

            .header {
                width: 100%;
                height: var(--header-height);
                border-bottom: 0.1rem solid var(--foreground);
                display: flex;
                justify-content: space-between;
                align-items: center;
            }

            #patch-title {
                color: var(--foreground);
                text-overflow: ellipsis;
                white-space: nowrap;
                overflow: hidden;
                cursor: default;
                font-size: 140%;
            }

            .logo {
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

            .header-filler {
                flex: 1;
            }

            #patch-parameters {
                height: calc(100% - var(--header-height));
                overflow: auto;
                padding: 1rem;
                text-align: center;
            }

            ${Controls.getAllCSS()}

            </style>

            <div class="main">
              <div class="header">
                <span class="logo"></span>
                <h2 id="patch-title"></h2>
                <div class="header-filler"></div>
              </div>
              <div id="patch-parameters"></div>
            </div>`;
    }
}

window.customElements.define ("cmaj-generic-patch-view", GenericPatchView);

//==============================================================================
/** Creates a generic view element which can be used to control any patch.
 *  @param {PatchConnection} patchConnection - the connection to the target patch
 */
export default function createPatchView (patchConnection)
{
    return new GenericPatchView (patchConnection);
}
