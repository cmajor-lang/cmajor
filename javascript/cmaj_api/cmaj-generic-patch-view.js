//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"

import * as Controls from "/cmaj_api/cmaj-parameter-controls.js"

//==============================================================================
class GenericPatchView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();

        this.patchConnection = patchConnection;

        this.statusListener = status =>
        {
            this.status = status;
            this.#createControlElements();
        };

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.#getHTML();

        this.titleElement      = this.shadowRoot.getElementById ("patch-title");
        this.parametersElement = this.shadowRoot.getElementById ("patch-parameters");
    }

    connectedCallback()
    {
        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
    }

    disconnectedCallback()
    {
        this.patchConnection.removeStatusListener (this.statusListener);
    }

    #createControlElements()
    {
        this.parametersElement.innerHTML = "";
        this.titleElement.innerText = this.status?.manifest?.name ?? "Cmajor";

        for (const endpointInfo of this.status?.details?.inputs)
        {
            if (! endpointInfo.annotation?.hidden)
            {
                const control = Controls.createLabelledControl (this.patchConnection, endpointInfo);

                if (control)
                    this.parametersElement.appendChild (control);
            }
        }
    }

    #getHTML()
    {
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
                height: 100%;
                background-color: var(--foreground);
                mask: url(cmaj_api/assets/sound-stacks-logo.svg);
                mask-repeat: no-repeat;
                -webkit-mask: url(cmaj_api/assets/sound-stacks-logo.svg);
                -webkit-mask-repeat: no-repeat;
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

            ${Controls.Options.getCSS()}
            ${Controls.Knob.getCSS()}
            ${Controls.Switch.getCSS()}
            ${Controls.LabelledControlHolder.getCSS()}

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
export default function createPatchView (patchConnection)
{
    return new GenericPatchView (patchConnection);
}
