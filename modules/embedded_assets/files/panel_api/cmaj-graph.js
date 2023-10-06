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

export default class PatchGraph extends HTMLElement
{
    constructor()
    {
        super();

        this.isActive = false;
        this.pendingTimer = null;
        this.root = this.attachShadow({ mode: "open" });
        this.root.innerHTML = `<style>${this.getCSS()}</style>${this.getHTML()}`;
        this.holder = this.shadowRoot.getElementById ("holder");
    }

    dispose()
    {
        setSession (undefined);
    }

    connectedCallback()
    {
        this.isActive = true;
        this.refresh();
    }

    disconnectedCallback()
    {
        this.isActive = false;
        this.clearGraph();
        this.cancelRefresh();
    }

    setSession (session)
    {
        this.cancelRefresh();
        this.session = session;
        this.refresh();
    }

    clearGraph()
    {
        this.holder.innerHTML = "";
    }

    refresh()
    {
        if (this.session && this.isActive && ! this.pendingTimer)
        {
            this.clearGraph();

            this.pendingTimer = setTimeout (() =>
            {
                this.session.requestGeneratedCode ("graph", {},
                    message => {
                        if (typeof message.code == "string")
                            this.holder.innerHTML = message.code;
                    });

                this.pendingTimer = undefined;
            }, 100);
        }
    }

    cancelRefresh()
    {
        if (this.pendingTimer)
        {
            clearTimeout (this.pendingTimer);
            this.pendingTimer = undefined;
        }
    }

    getHTML()
    {
        return `<div id="holder"></div>`;
    }

    getCSS()
    {
        return `
            :host {
                --bar-color: #aaffaa;
                display: block;
                overflow: auto;
            }

            #holder {
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                transform: scale(0.7);
                transform-origin: 0% 0%;
            }
            `;
    }
}

customElements.define ("cmaj-patch-graph", PatchGraph);
