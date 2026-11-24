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

import { el, Disposer } from "../core/dom.js";
import { ConnectionView } from "./connection-view.js";
import { createEndpointWidget, createWidgetOptions } from "../widgets/registry.js";
import { FuzzToolbar } from "../widgets/fuzz-controls.js";
import { addSpectrumToggle, isSpectrumEndpoint } from "../widgets/spectrum-analyser.js";

/// The per-patch setting that remembers which endpoints' spectrums the I/O view shows
export const ioSpectrumSettingsKey = "spectrumEndpoints";

/*
    The I/O tab: a full-size widget for each of the patch's endpoints, in groups. The widgets are
    only re-created when the endpoint list really changes (the store keeps the same array
    otherwise), so meters and history survive status updates. While the tab is hidden (or the
    whole page is), its subscriptions are paused, so it costs no server traffic.

    Each audio widget has a Spectrum toggle in its header (widgets/spectrum-analyser.js), which
    shows an analyser under its meter; which ones are open is remembered per patch
    (ioSpectrumSettingsKey).

    The Parameters header has the fuzzing toolbar (Randomise, Restore, Stop all: see
    core/parameter-fuzzer.js), and a parameter that's being automated shows a mark by its name.
*/
export class IOView  extends ConnectionView
{
    constructor (ctx)
    {
        super (ctx);
        this.widgets = [];
        this.attachDisposer = new Disposer();
        this.content = el ("div", { class: "cp-io-content" });
        this.element = el ("div", { class: "cp-io-view" }, this.content);
        this.followConnection();
    }

    dispose()
    {
        super.dispose();
        this.element.remove();
    }

    onNoConnection()
    {
        this.content.replaceChildren (el ("p", { class: "cp-empty" }, "No patch loaded"));
    }

    onDetach()
    {
        this.attachDisposer.dispose();

        for (const w of this.widgets)
            w.dispose();

        this.widgets = [];
        this.content.replaceChildren();
    }

    onAttach (connection, group)
    {
        const s = this.ctx.store.state;
        const options = createWidgetOptions (this.ctx, connection, group, { size: "full" });

        const sections = [
            { title: "Inputs",      items: (s.inputs ?? []).filter (e => e.purpose !== "parameter"), isInput: true },
            { title: "Parameters",  items: (s.inputs ?? []).filter (e => e.purpose === "parameter" && ! e.annotation?.hidden), isInput: true, isParameters: true },
            { title: "Outputs",     items: s.outputs ?? [], isInput: false },
        ];

        this.content.replaceChildren();

        for (const section of sections)
        {
            const widgets = section.items.map (e => createEndpointWidget (e, options, section.isInput)).filter (Boolean);

            for (const w of widgets)
                if (isSpectrumEndpoint (w.endpoint))
                    addSpectrumToggle (w, ioSpectrumSettingsKey);

            if (widgets.length === 0)
                continue;

            this.widgets.push (...widgets);
            const title = el ("h3", { class: "cp-section-title" }, section.title, el ("span", { class: "cp-count" }, widgets.length));
            const header = section.isParameters && connection.fuzzer ? el ("div", { class: "cp-io-section-header" }, title, this.createFuzzToolbar (connection.fuzzer))
                                                                     : title;

            this.content.append (el ("section", { class: "cp-io-section" }, header,
                                     el ("div", { class: "cp-io-grid" }, widgets.map (w => w.element))));
        }

        if (this.widgets.length === 0)
            this.content.append (el ("p", { class: "cp-empty" }, "This patch has no endpoints that can be shown here"));
    }

    createFuzzToolbar (fuzzer)
    {
        this.fuzzToolbar = new FuzzToolbar (fuzzer);
        const updateMarks = () =>
        {
            for (const w of this.widgets)
                if (w.endpoint?.purpose === "parameter")
                    w.element.classList.toggle ("cp-automated", fuzzer.isAutomating (w.endpoint.endpointID));
        };

        this.attachDisposer.add (() => { this.fuzzToolbar?.dispose(); this.fuzzToolbar = null; },
                                 fuzzer.addListener (updateMarks));
        updateMarks();
        return this.fuzzToolbar.element;
    }

    findWidget (endpointID)             { return this.widgets.find (w => w.endpoint?.endpointID === endpointID); }
    hasWidgetFor (endpointID)           { return !! this.findWidget (endpointID); }

    /// The analyser shown under an endpoint's widget, or null
    findSpectrum (endpointID)           { return this.findWidget (endpointID)?.spectrumToggle?.view ?? null; }

    /// Shows or hides an audio endpoint's spectrum (and remembers it). Returns false if it has none.
    setSpectrumShown (endpointID, isShown)
    {
        const toggle = this.findWidget (endpointID)?.spectrumToggle;
        toggle?.setShown (isShown);
        return !! toggle;
    }

    /// Scrolls to an endpoint's widget, and focuses its first control (or the widget itself)
    reveal (endpointID)
    {
        const widget = this.findWidget (endpointID);

        if (! widget)
            return false;

        const focusable = "input, select, button, [tabindex]:not([tabindex='-1'])";
        const control = widget.bodyElement?.querySelector (focusable) ?? widget.element.querySelector (focusable);
        widget.element.scrollIntoView ({ block: "nearest" });
        widget.element.classList.remove ("cp-revealed");
        void widget.element.offsetWidth;   // (restarts the highlight animation)
        widget.element.classList.add ("cp-revealed");

        if (control)
        {
            control.focus ({ preventScroll: true });
        }
        else
        {
            widget.element.tabIndex = -1;
            widget.element.focus ({ preventScroll: true });
        }

        return true;
    }
}
