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
import { getEndpointDisplayName, getEndpointTypeDescription, getEndpointKind, getAllEndpoints, getParameters,
         countAudioChannels } from "../widgets/registry.js";
import { describeParameter, resetParameter } from "../widgets/parameter-model.js";
import { getCmajorVersion } from "../../cmaj_api/cmaj-version.js";
import { Popover } from "../shell/top-bar.js";
import { FuzzToolbar, AutomationEditor } from "../widgets/fuzz-controls.js";

/*
    The right-hand inspector:
      - Patch: what's loaded (name, description, main processor, I/O summary), with links
      - Parameters: every parameter (including ones the patch's GUI hides), filterable, showing
        its current value; click (or Enter/Space) selects one; double-click (or Backspace/Delete)
        resets it. These follow the parameters' values only while the inspector is open.
        Fuzzing (core/parameter-fuzzer.js): Randomise / Restore / Stop all above the list, and
        each row's ∿ button opens its automation popover; an automated row shows a mark.
      - Endpoint: details of the endpoint selected elsewhere (store key "selection")
      - State: copy/paste the patch's full state
*/
export class InspectorView  extends ConnectionView
{
    constructor (ctx)
    {
        super (ctx);
        this.parameterRows = new Map();
        this.attachDisposer = new Disposer();
        this.fuzzHolder = el ("div", { class: "cp-insp-fuzz" });

        this.patchSection = el ("div", { class: "cp-insp-body" });
        this.selectionSection = el ("div", { class: "cp-insp-body" });
        this.parameterList = el ("div", { class: "cp-param-list", role: "list" });
        this.filterInput = el ("input", { class: "cp-input", type: "search", placeholder: "Filter parameters…", "aria-label": "Filter parameters",
                                          oninput: () => this.applyFilter() });

        this.selectionBlock = el ("section", { class: "cp-insp-section", hidden: true },
                                  el ("h3", { class: "cp-section-title" }, "Endpoint"), this.selectionSection);
        this.parameterBlock = el ("section", { class: "cp-insp-section" },
                                  el ("h3", { class: "cp-section-title" }, "Parameters", this.parameterCount = el ("span", { class: "cp-count" })),
                                  this.fuzzHolder, this.filterInput, this.parameterList);
        this.stateBlock = el ("section", { class: "cp-insp-section" },
                              el ("h3", { class: "cp-section-title" }, "State"),
                              el ("div", { class: "cp-button-row" },
                                  el ("button", { class: "cp-button", title: "Copy the patch's full state to the clipboard", onclick: () => ctx.actions.copyState() }, "Copy"),
                                  el ("button", { class: "cp-button", title: "Apply a state from the clipboard", onclick: () => ctx.actions.pasteState() }, "Paste"),
                                  el ("button", { class: "cp-button", title: "Reset the patch (R)", onclick: () => ctx.actions.reset() }, "Reset")));

        this.element = el ("div", { class: "cp-inspector-view" },
                           el ("section", { class: "cp-insp-section" }, el ("h3", { class: "cp-section-title" }, "Patch"), this.patchSection),
                           this.selectionBlock, this.parameterBlock, this.stateBlock);

        this.disposer.add (ctx.store.subscribe (["manifest", "manifestFile", "mainProcessor", "mainProcessorLocation", "inputs", "outputs", "loaded"],
                                                () => this.updatePatch(), true));
        this.followConnection (["connection", "inputs"]);
        this.disposer.add (ctx.store.subscribe (["selection", "inputs", "outputs"], () => this.updateSelection(), true));
    }

    dispose()
    {
        super.dispose();
        this.element.remove();
    }

    //==============================================================================
    updatePatch()
    {
        const s = this.ctx.store.state;
        const m = s.manifest;
        const rows = [];

        if (! m)
        {
            this.patchSection.replaceChildren (el ("p", { class: "cp-empty" }, "No patch loaded"), el ("p", { class: "cp-dim" }, `Cmajor ${getCmajorVersion()}`));
            return;
        }

        const link = (text, location) => location ? el ("button", { class: "cp-link", title: location, onclick: () => this.ctx.bridge.openSourceFile (location) }, text)
                                                  : el ("span", null, text);

        rows.push (el ("div", { class: "cp-insp-title" }, link (m.name ?? "Untitled", s.manifestFile)));

        if (m.description)
            rows.push (el ("p", { class: "cp-insp-description" }, m.description));

        const facts = [
            ["Main processor", s.mainProcessor ? link (s.mainProcessor, cleanLocation (s.mainProcessorLocation)) : null],
            ["Version", m.version],
            ["Manufacturer", m.manufacturer],
            ["Category", m.category],
            ["Type", m.isInstrument ? "Instrument" : null],
            ["Audio", describeAudio (s.inputs, s.outputs)],
            ["MIDI", describeCount (s.inputs, s.outputs, "midi in", "midi out")],
            ["Parameters", getParameters (s).length || null],
            ["Cmajor", getCmajorVersion()],
        ];

        rows.push (el ("dl", { class: "cp-facts" },
                       facts.filter (([, v]) => v !== null && v !== undefined && v !== "")
                            .map (([k, v]) => [el ("dt", null, k), el ("dd", null, v)])));

        this.patchSection.replaceChildren (...rows);
    }

    //==============================================================================
    onNoConnection()
    {
        this.parameterBlock.hidden = true;
        this.parameterCount.textContent = "";
        this.parameterList.replaceChildren();
    }

    onDetach()
    {
        this.attachDisposer.dispose();
        this.parameterRows.clear();
        this.parameterList.replaceChildren();
        this.fuzzHolder.replaceChildren();
    }

    /// The fuzzing toolbar and the automation popover, for a connection's fuzzer
    attachFuzzer (fuzzer)
    {
        const toolbar = new FuzzToolbar (fuzzer, { compact: true });
        this.automationEditor = new AutomationEditor (fuzzer);
        this.automationPopover = new Popover (this.automationEditor.element);
        this.automationPopover.element.classList.add ("cp-fuzz-popover");
        this.automationPopover.element.setAttribute ("aria-label", "Parameter automation");
        this.fuzzHolder.replaceChildren (toolbar.element);

        this.attachDisposer.add (() => { toolbar.dispose(); this.automationPopover.dispose(); this.automationEditor.dispose();
                                         this.automationPopover = null; this.automationEditor = null; });
        this.attachDisposer.add (fuzzer.addListener (() => this.updateAutomationMarks (fuzzer)));
    }

    /// Opens a parameter's automation popover, under its row's button
    showAutomation (endpointID)
    {
        const entry = this.parameterRows.get (endpointID);

        if (! entry?.automateButton || ! this.automationPopover)
            return false;

        if (this.automationPopover.isOpen && this.automationEditor.endpoint === entry.endpoint)
        {
            this.automationPopover.close();
            return true;
        }

        this.automationEditor.setParameter (entry.endpoint);
        this.automationPopover.open (entry.automateButton, { align: "end", focus: ".cp-seg-button.cp-on", positionFrom: entry.row });
        return true;
    }

    updateAutomationMarks (fuzzer)
    {
        for (const [id, { row, automateButton }] of this.parameterRows)
        {
            const isAutomated = fuzzer.isAutomating (id);
            row.classList.toggle ("cp-automated", isAutomated);
            row.classList.toggle ("cp-fuzz-excluded", fuzzer.isExcluded (id));
            automateButton?.classList.toggle ("cp-on", isAutomated);
        }
    }

    onAttach (connection, group)
    {
        const parameters = getParameters (this.ctx.store.state);
        const fuzzer = connection.fuzzer ?? null;
        this.parameterBlock.hidden = parameters.length === 0;
        this.parameterCount.textContent = parameters.length || "";

        if (fuzzer && parameters.length > 0)
            this.attachFuzzer (fuzzer);

        const rows = parameters.map (endpoint =>
        {
            const model = describeParameter (endpoint);
            const select = () => this.ctx.actions.select ({ kind: "endpoint", endpointID: endpoint.endpointID });
            const reset = () => { fuzzer?.handleUserGesture (endpoint.endpointID); resetParameter (connection.patchConnection, endpoint); };
            const value = el ("span", { class: "cp-param-value" }, model.format (model.init));
            const bar = el ("div", { class: "cp-param-bar" });
            const name = getEndpointDisplayName (endpoint);

            // (its own keys and clicks stay out of the row's: Enter mustn't select, a double-click mustn't reset)
            const automateButton = fuzzer ? el ("button", { class: "cp-small-button cp-param-automate", "aria-haspopup": "dialog", "aria-expanded": "false",
                                                            "aria-label": `Automate ${name}`, title: "Automate this parameter (an LFO, a ramp or random steps)",
                                                            onclick: (e) => { e.stopPropagation(); this.showAutomation (endpoint.endpointID); },
                                                            ondblclick: (e) => e.stopPropagation(),
                                                            onkeydown: (e) => e.stopPropagation() },
                                                el ("span", { "aria-hidden": "true" }, "∿"))
                                          : null;
            const row = el ("div", { class: ["cp-param-row", model.isHidden && "cp-param-hidden"], role: "listitem", tabindex: 0,
                                     title: `${endpoint.endpointID}${model.isHidden ? " (hidden in the patch's GUI)" : ""}\nDouble-click (or Backspace) to reset to ${model.format (model.init)}`,
                                     onclick: select,
                                     ondblclick: reset,
                                     onkeydown: (e) =>
                                     {
                                         if (e.key === "Enter" || e.key === " ")                { e.preventDefault(); select(); }
                                         else if (e.key === "Backspace" || e.key === "Delete")  { e.preventDefault(); reset(); }
                                     } },
                                el ("div", { class: "cp-param-name" }, el ("span", { class: "cp-param-label" }, name),
                                    el ("span", { class: "cp-param-tools" }, automateButton, value)),
                                el ("div", { class: "cp-param-track" }, bar));

            const show = (v) =>
            {
                value.textContent = model.format (v);
                bar.style.width = (model.toNormalised (Number (v)) * 100).toFixed (1) + "%";
            };

            show (model.init);
            group.subscribeParameter (endpoint.endpointID, show);
            this.parameterRows.set (endpoint.endpointID, { row, endpoint, automateButton });
            return row;
        });

        this.parameterList.replaceChildren (...rows);

        if (fuzzer)
            this.updateAutomationMarks (fuzzer);

        this.applyFilter();
        this.updateSelection();
    }

    /// Scrolls to a parameter's row and focuses it (clearing the filter if it hides the row)
    revealParameter (endpointID)
    {
        const entry = this.parameterRows.get (endpointID);

        if (! entry)
            return false;

        if (entry.row.hidden)
        {
            this.filterInput.value = "";
            this.applyFilter();
        }

        entry.row.scrollIntoView ({ block: "nearest" });
        entry.row.focus ({ preventScroll: true });
        return true;
    }

    applyFilter()
    {
        const text = this.filterInput.value.trim().toLowerCase();

        for (const { row, endpoint } of this.parameterRows.values())
            row.hidden = text !== "" && ! (endpoint.endpointID.toLowerCase().includes (text)
                                            || getEndpointDisplayName (endpoint).toLowerCase().includes (text));
    }

    //==============================================================================
    updateSelection()
    {
        const s = this.ctx.store.state;
        const selection = s.selection;
        const endpoint = selection?.kind === "endpoint"
                            ? getAllEndpoints (s).find (e => e.endpointID === selection.endpointID) : null;

        for (const { row, endpoint: e } of this.parameterRows.values())
            row.classList.toggle ("cp-selected", e === endpoint);

        this.selectionBlock.hidden = ! endpoint;

        if (! endpoint)
            return;

        const isInput = (s.inputs ?? []).includes (endpoint);
        const a = endpoint.annotation ?? {};

        this.selectionSection.replaceChildren (
            el ("div", { class: "cp-insp-title" }, getEndpointDisplayName (endpoint)),
            el ("div", { class: "cp-insp-sub" }, `${isInput ? "input" : "output"} ${endpoint.endpointID}`),
            endpoint.source ? el ("button", { class: "cp-link cp-mono", onclick: () => this.ctx.bridge.openSourceFile (endpoint.source) },
                                  cleanLocation (endpoint.source).split (/[\\/]/).pop()) : null,
            el ("dl", { class: "cp-facts" },
                [["Type", el ("span", { class: `cp-kind-text cp-kind-${getEndpointKind (endpoint)}` }, getEndpointTypeDescription (endpoint))],
                 ["Purpose", endpoint.purpose],
                 ["Channels", endpoint.numAudioChannels],
                 ...Object.entries (a).filter (([k]) => k !== "name").map (([k, v]) => [k, typeof v === "object" ? JSON.stringify (v) : String (v)])]
                    .filter (([, v]) => v !== undefined && v !== null && v !== "")
                    .map (([k, v]) => [el ("dt", null, k), el ("dd", null, v)])));
    }
}

/// The server's locations can end with ": " (e.g. "file:8:11: ")
function cleanLocation (location)
{
    return (location ?? "").replace (/[:\s]+$/, "");
}

function describeAudio (inputs, outputs)
{
    const ins = countAudioChannels (inputs, "audio in"), outs = countAudioChannels (outputs, "audio out");
    return (ins + outs) ? `${ins} in, ${outs} out` : null;
}

function describeCount (inputs, outputs, inPurpose, outPurpose)
{
    const ins = (inputs ?? []).filter (e => e.purpose === inPurpose).length;
    const outs = (outputs ?? []).filter (e => e.purpose === outPurpose).length;
    return (ins + outs) ? `${ins} in, ${outs} out` : null;
}
