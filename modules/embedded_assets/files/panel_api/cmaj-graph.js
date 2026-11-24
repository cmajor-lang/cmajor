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

import { PatchGraphView } from "./cmaj-patch-graph/patch-graph-view.js";

/*
    <cmaj-patch-graph> shows an interactive diagram of the session's patch, using the JSON
    from the "graph" code generation target. Clicking a name, type, port or connection calls
    window.openSourceFile(), which the panel forwards to VS Code when it's running inside it.
*/
export default class PatchGraph extends HTMLElement
{
    constructor()
    {
        super();

        this.isActive = false;
        this.pendingTimer = null;
        this.requestNumber = 0;
        this.hasUserMovedView = false;

        this.root = this.attachShadow ({ mode: "open" });
        this.root.innerHTML = `<style>${this.getCSS()}</style>${this.getHTML()}`;
        this.holder = this.root.getElementById ("holder");
        this.messageElement = this.root.getElementById ("message");

        this.view = new PatchGraphView (this.holder, { onOpenSource: (source) => window.openSourceFile?.(source),
                                                       fitTopMargin: 36 });    // (below the toolbar)

        // once the user has panned or zoomed, resizing stops re-fitting the graph
        this.holder.addEventListener ("wheel", () => this.hasUserMovedView = true, { passive: true });
        this.holder.addEventListener ("pointermove", (e) => { if (e.buttons !== 0) this.hasUserMovedView = true; });

        this.root.getElementById ("zoomIn").onclick  = () => this.zoomBy (1.25);
        this.root.getElementById ("zoomOut").onclick = () => this.zoomBy (0.8);
        this.root.getElementById ("fit").onclick = () => this.fit();
        this.root.getElementById ("expandAll").onclick = () => this.view.setAllCollapsed (false);
        this.root.getElementById ("collapseAll").onclick = () => this.view.setAllCollapsed (true);

        this.resizeObserver = new ResizeObserver (() =>
        {
            if (! this.hasUserMovedView)
                this.fitIfVisible();
        });

        this.resizeObserver.observe (this.holder);
    }

    dispose()
    {
        this.setSession (undefined);
        this.resizeObserver.disconnect();
        this.view.dispose();
    }

    connectedCallback()
    {
        this.isActive = true;
        this.refresh();
    }

    disconnectedCallback()
    {
        this.isActive = false;
        this.cancelRefresh();
    }

    setSession (session)
    {
        this.cancelRefresh();
        this.session = session;
        this.refresh();
    }

    refresh()
    {
        if (this.session && this.isActive && ! this.pendingTimer)
        {
            this.pendingTimer = setTimeout (() =>
            {
                this.pendingTimer = undefined;
                this.requestGraph();
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

    async requestGraph()
    {
        const requestNumber = ++this.requestNumber;

        try
        {
            const message = await this.session.requestGeneratedCode ("graph", {});

            // a newer request has been made since this one
            if (requestNumber !== this.requestNumber)
                return;

            if (typeof message?.code !== "string" || message.code.length === 0)
            {
                this.view.clear();
                this.showMessage ("No patch loaded");
                return;
            }

            this.showMessage (null);
            this.hasUserMovedView = false;
            await this.view.setModel (JSON.parse (message.code));
            this.fitIfVisible();
        }
        catch (e)
        {
            this.showMessage ("Couldn't show the graph: " + (e?.message ?? e));
        }
    }

    fit()
    {
        this.hasUserMovedView = false;
        this.fitIfVisible();
    }

    zoomBy (factor)
    {
        this.hasUserMovedView = true;
        this.view.zoomBy (factor);
    }

    fitIfVisible()
    {
        // while it's hidden (e.g. in a collapsed panel, or a hidden VS Code tab) it has no size to fit to
        if (this.holder.clientWidth > 0 && this.holder.clientHeight > 0)
            this.view.fit();
    }

    showMessage (text)
    {
        this.messageElement.textContent = text ?? "";
        this.messageElement.style.display = text ? "block" : "none";
    }

    getHTML()
    {
        return `
            <div id="holder"></div>
            <div id="toolbar">
              <button id="zoomIn" title="Zoom in (or pinch, or ctrl/cmd + scroll)">+</button>
              <button id="zoomOut" title="Zoom out (or pinch, or ctrl/cmd + scroll)">−</button>
              <button id="fit" title="Fit the graph to the panel (or double-click the background)">Fit</button>
              <button id="expandAll">Expand all</button>
              <button id="collapseAll">Collapse nested</button>
            </div>
            <div id="message"></div>`;
    }

    getCSS()
    {
        return `
            :host {
                /* the graph's colours follow the panel's (see render-svg.js for the full list) */
                --cmaj-graph-background: color-mix(in srgb, var(--background, #445550) 55%, black);
                --cmaj-graph-fill-0: color-mix(in srgb, var(--background, #445550) 75%, black);
                --cmaj-graph-fill-1: color-mix(in srgb, var(--background, #445550) 90%, black);
                --cmaj-graph-fill-2: var(--background, #445550);
                --cmaj-graph-text: var(--foreground, #dddddd);

                display: block;
                position: relative;
                height: max(24rem, 65vh);
                min-height: 10rem;
                resize: vertical;
                overflow: hidden;
            }

            #holder {
                position: absolute;
                inset: 0;
                background: var(--cmaj-graph-background);
            }

            #toolbar {
                position: absolute;
                top: 0.4rem;
                right: 0.6rem;
                display: flex;
                gap: 0.3rem;
            }

            #toolbar button {
                font: inherit;
                font-size: 0.75rem;
                color: var(--foreground, #dddddd);
                background: var(--background, #445550);
                border: 1px solid color-mix(in srgb, var(--foreground, #dddddd) 35%, transparent);
                border-radius: 3px;
                padding: 0.15rem 0.5rem;
                cursor: pointer;
                opacity: 0.85;
            }

            #toolbar button:hover {
                opacity: 1;
            }

            #message {
                display: none;
                position: absolute;
                top: 0.6rem;
                left: 0.8rem;
                color: var(--foreground, #dddddd);
                opacity: 0.7;
            }
            `;
    }
}

customElements.define ("cmaj-patch-graph", PatchGraph);
