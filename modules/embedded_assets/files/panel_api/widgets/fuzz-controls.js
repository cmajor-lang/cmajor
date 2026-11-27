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

import { el, Disposer, setPressed } from "../core/dom.js";
import { automationModes, automationModeNames, automationRateLimits, isPeriodMode, isLikelyOutputLevel,
         normaliseAutomationSettings } from "../core/parameter-fuzzer.js";
import { valueToSliderPosition, sliderPositionToValue, formatFrequency, formatSeconds, roundToSignificantFigures } from "./signal-generator.js";
import { getEndpointDisplayName } from "./widget-base.js";

/*
    The parameter fuzzer's controls (core/parameter-fuzzer.js):

      - FuzzToolbar: Randomise, Restore (enabled while there's a snapshot of the values from
        before fuzzing, which a chip also shows), and the number of automations running with a
        Stop all. The I/O view puts it in its Parameters header; the inspector a compact one
        (icon buttons) in its Parameters section.
      - AutomationEditor: the content of a parameter's automation popover: the mode (Off, Ramp,
        Sine, Triangle, Random), the rate (Hz for the LFOs, a period in seconds for ramp and
        random), the depth (a percentage of the range), smoothing (random only) and whether
        Randomise leaves the parameter out. Changes apply at once: choosing a mode starts it,
        Off stops it, and the rate and depth change a running automation in place.
*/

//==============================================================================
export class FuzzToolbar
{
    /// options: { compact }
    constructor (fuzzer, { compact = false } = {})
    {
        this.fuzzer = fuzzer;
        this.disposer = new Disposer();

        const label = (text) => compact ? null : el ("span", { class: "cp-button-label" }, text);
        const buttonClass = ["cp-button", compact && "cp-icon-button"];

        this.randomiseButton = el ("button", { class: [...buttonClass, "cp-fuzz-randomise"], "aria-label": "Randomise all parameters",
                                               onclick: () => this.fuzzer.randomiseAll() },
                                   el ("span", { class: "cp-button-glyph cp-glyph-die", "aria-hidden": "true" }, "⚄"), label ("Randomise"));
        this.restoreButton = el ("button", { class: [...buttonClass, "cp-fuzz-restore"], "aria-label": "Restore the parameters from before fuzzing",
                                             onclick: () => this.fuzzer.restore() },
                                 el ("span", { class: "cp-button-glyph", "aria-hidden": "true" }, "↺"), label ("Restore"));
        this.snapshotChip = el ("span", { class: "cp-chip cp-fuzz-snapshot", hidden: true },
                                el ("span", { class: "cp-dot", "aria-hidden": "true" }), compact ? null : "Snapshot");
        this.countText = el ("span", { class: "cp-fuzz-count", "aria-live": "polite" });
        this.stopButton = el ("button", { class: "cp-small-button cp-fuzz-stop", title: "Stop all the parameter automation",
                                          onclick: () => this.fuzzer.stopAll() }, "Stop all");

        this.element = el ("div", { class: ["cp-fuzz-toolbar", compact && "cp-fuzz-compact"], role: "toolbar", "aria-label": "Parameter fuzzing" },
                           this.randomiseButton, this.restoreButton, this.snapshotChip,
                           el ("span", { class: "cp-fuzz-running" }, this.countText, this.stopButton));

        this.disposer.add (fuzzer.addListener (() => this.update()));
        this.update();
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    update()
    {
        const f = this.fuzzer;
        const exclusions = f.getExclusions().size;
        const automations = f.numAutomations;

        this.randomiseButton.disabled = ! f.canRandomise;
        this.randomiseButton.title = f.canRandomise
            ? `Give every parameter a random value${exclusions ? ` (except the ${exclusions} excluded)` : ""}${automations ? " (except the automated ones)" : ""}`
            : "There are no parameters to randomise (they're all excluded or automated)";

        this.restoreButton.disabled = ! f.hasSnapshot;
        this.restoreButton.title = f.hasSnapshot ? "Stop the automation, and put back the values the parameters had before fuzzing"
                                                 : "Nothing to restore: the values are saved when you first randomise or automate";
        this.snapshotChip.hidden = ! f.hasSnapshot;
        this.snapshotChip.title = `The values from before fuzzing are saved (${f.snapshotSize} parameters): Restore puts them back`;

        const running = this.element.querySelector (".cp-fuzz-running");
        running.hidden = automations === 0;
        this.countText.textContent = automations === 0 ? "" : `${automations} automated`;
    }
}

//==============================================================================
const rateSliderRange = Object.freeze ({ min: automationRateLimits.min, max: automationRateLimits.max, isLog: true });
const periodSliderRange = Object.freeze ({ min: 1 / automationRateLimits.max, max: 1 / automationRateLimits.min, isLog: true });

/// "0.5 Hz", "20 Hz" / "2 s", "0.05 s"
export function formatAutomationRate (mode, rate)
{
    return isPeriodMode (mode) ? formatSeconds (roundToSignificantFigures (1 / rate)) : formatFrequency (roundToSignificantFigures (rate));
}

/// Reads a typed rate (a frequency for the LFOs, a period for the others; with or without its
/// unit, and "ms" or "kHz" too) as a rate in Hz, or null
export function parseAutomationRate (mode, text)
{
    const match = /^\s*([-+]?[\d.]+(?:e[-+]?\d+)?)\s*(ms|s|khz|hz)?\s*$/i.exec (String (text ?? ""));
    const n = match ? Number (match[1]) : NaN;

    if (! Number.isFinite (n) || n <= 0)
        return null;

    const unit = match[2]?.toLowerCase() ?? (isPeriodMode (mode) ? "s" : "hz");
    const rate = unit === "ms" ? 1000 / n : unit === "s" ? 1 / n : unit === "khz" ? n * 1000 : n;
    return Math.max (automationRateLimits.min, Math.min (automationRateLimits.max, rate));
}

export function formatDepth (depth)         { return `${Math.round (depth * 100)}%`; }

export function parseDepth (text)
{
    const n = Number (String (text ?? "").replace ("%", "").trim());
    return Number.isFinite (n) && String (text).trim() !== "" ? Math.max (0, Math.min (1, n / 100)) : null;
}

//==============================================================================
export class AutomationEditor
{
    constructor (fuzzer)
    {
        this.fuzzer = fuzzer;
        this.endpoint = null;
        this.settings = normaliseAutomationSettings();
        this.disposer = new Disposer();

        this.titleName = el ("span", { class: "cp-count" });
        this.modeButtons = ["off", ...automationModes].map (mode =>
            el ("button", { class: "cp-seg-button", dataset: { mode }, "aria-pressed": "false",
                            onclick: () => this.setMode (mode) }, mode === "off" ? "Off" : automationModeNames[mode]));

        this.rateLabel = el ("span", { class: "cp-fuzz-label" }, "Rate");
        this.rateSlider = el ("input", { type: "range", class: "cp-slider", min: 0, max: 100, step: 1, "aria-label": "Rate",
                                         oninput: () => this.setRateFromSlider() });
        this.rateInput = el ("input", { type: "text", class: "cp-input cp-fuzz-value", spellcheck: false, autocomplete: "off", "aria-label": "Rate",
                                        onchange: () => this.setRateFromText(), onkeydown: (e) => this.handleFieldKey (e) });

        this.depthSlider = el ("input", { type: "range", class: "cp-slider", min: 0, max: 100, step: 1, "aria-label": "Depth",
                                          oninput: () => this.apply ({ depth: Number (this.depthSlider.value) / 100 }, false) });
        this.depthInput = el ("input", { type: "text", class: "cp-input cp-fuzz-value", inputMode: "decimal", spellcheck: false, autocomplete: "off", "aria-label": "Depth",
                                         onchange: () => this.setDepthFromText(), onkeydown: (e) => this.handleFieldKey (e) });

        this.smoothSwitch = el ("button", { class: "cp-toggle", role: "switch", "aria-checked": "false", "aria-label": "Smooth the random steps",
                                            onclick: () => this.apply ({ smooth: ! this.settings.smooth }) },
                                el ("span", { class: "cp-toggle-knob" }));
        this.excludeSwitch = el ("button", { class: "cp-toggle", role: "switch", "aria-checked": "false", "aria-label": "Exclude from Randomise",
                                             onclick: () => this.fuzzer.setExcluded (this.endpoint.endpointID, ! this.fuzzer.isExcluded (this.endpoint.endpointID)) },
                                 el ("span", { class: "cp-toggle-knob" }));
        this.excludeHint = el ("p", { class: "cp-fuzz-hint", hidden: true }, "Its name suggests it sets a level: you may want to leave it out of Randomise.");

        this.smoothRow = el ("div", { class: "cp-fuzz-row cp-fuzz-switch-row" }, el ("span", { class: "cp-fuzz-label" }, "Smooth"), this.smoothSwitch);

        this.element = el ("div", { class: "cp-fuzz-editor" },
                           el ("h3", { class: "cp-section-title" }, "Automate", this.titleName),
                           el ("div", { class: "cp-fuzz-form" },
                               el ("div", { class: "cp-seg cp-fuzz-modes", role: "group", "aria-label": "Automation" }, this.modeButtons),
                               el ("div", { class: "cp-fuzz-row" }, this.rateLabel, this.rateSlider, this.rateInput),
                               el ("div", { class: "cp-fuzz-row" }, el ("span", { class: "cp-fuzz-label" }, "Depth"), this.depthSlider, this.depthInput),
                               this.smoothRow,
                               el ("div", { class: "cp-fuzz-row cp-fuzz-switch-row cp-fuzz-exclude-row" },
                                   el ("span", { class: "cp-fuzz-label" }, "Exclude from Randomise"), this.excludeSwitch),
                               this.excludeHint));

        this.disposer.add (fuzzer.addListener (() => this.update()));
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    /// Shows a parameter's automation (an endpoint from the patch's inputs)
    setParameter (endpoint)
    {
        this.endpoint = endpoint;
        this.titleName.textContent = getEndpointDisplayName (endpoint);
        this.settings = this.fuzzer.getAutomationSettings (endpoint.endpointID);
        this.update (true);
    }

    get isRunning()         { return !! this.endpoint && this.fuzzer.isAutomating (this.endpoint.endpointID); }

    setMode (mode)
    {
        if (! this.endpoint)
            return;

        if (mode === "off")
            this.fuzzer.stopAutomation (this.endpoint.endpointID);
        else
            this.apply ({ mode }, true, true);
    }

    /// Changes the settings: a running automation takes them at once (and choosing a mode
    /// starts one); otherwise they're kept for when it starts
    apply (changes, updateFields = true, shouldStart = false)
    {
        this.settings = normaliseAutomationSettings ({ ...this.settings, ...changes });

        if (this.isRunning || shouldStart)
            this.fuzzer.automate (this.endpoint.endpointID, this.settings);
        else
            this.fuzzer.saveAutomationSettings (this.endpoint.endpointID, this.settings);

        this.update (updateFields);
    }

    setRateFromSlider()
    {
        const position = Number (this.rateSlider.value) / 100;
        const mode = this.settings.mode;
        const rate = isPeriodMode (mode) ? 1 / sliderPositionToValue (position, periodSliderRange) : sliderPositionToValue (position, rateSliderRange);
        this.apply ({ rate }, false);
        this.rateInput.value = formatAutomationRate (mode, this.settings.rate);
    }

    setRateFromText()
    {
        const rate = parseAutomationRate (this.settings.mode, this.rateInput.value);

        if (rate === null)
            this.update (true);
        else
            this.apply ({ rate });
    }

    setDepthFromText()
    {
        const depth = parseDepth (this.depthInput.value);

        if (depth === null)
            this.update (true);
        else
            this.apply ({ depth });
    }

    handleFieldKey (e)
    {
        if (e.key === "Enter")
        {
            e.preventDefault();
            e.target.dispatchEvent (new Event ("change"));
        }
    }

    /// Shows the state: always the mode and switches; the fields and sliders too if updateFields
    /// (not while one is being dragged or typed in)
    update (updateFields = false)
    {
        if (! this.endpoint)
            return;

        const id = this.endpoint.endpointID;
        const running = this.fuzzer.getAutomation (id);

        if (running)
            this.settings = { ...running };

        const s = this.settings;
        const mode = running ? s.mode : "off";

        for (const b of this.modeButtons)
            setPressed (b, b.dataset.mode === mode);

        this.rateLabel.textContent = isPeriodMode (s.mode) ? "Period" : "Rate";
        this.rateSlider.setAttribute ("aria-label", isPeriodMode (s.mode) ? "Period" : "Rate");
        this.rateInput.setAttribute ("aria-label", isPeriodMode (s.mode) ? "Period" : "Rate");

        if (updateFields)
        {
            const position = isPeriodMode (s.mode) ? valueToSliderPosition (1 / s.rate, periodSliderRange) : valueToSliderPosition (s.rate, rateSliderRange);
            setSlider (this.rateSlider, position);
            this.rateInput.value = formatAutomationRate (s.mode, s.rate);
            setSlider (this.depthSlider, s.depth);
            this.depthInput.value = formatDepth (s.depth);
        }
        else
        {
            setSlider (this.depthSlider, s.depth, false);
            this.depthInput.value = formatDepth (s.depth);
        }

        this.rateSlider.setAttribute ("aria-valuetext", formatAutomationRate (s.mode, s.rate));
        this.depthSlider.setAttribute ("aria-valuetext", formatDepth (s.depth));

        this.smoothRow.hidden = s.mode !== "random";
        setSwitch (this.smoothSwitch, s.smooth);

        const excluded = this.fuzzer.isExcluded (id);
        setSwitch (this.excludeSwitch, excluded);
        this.excludeHint.hidden = excluded || ! isLikelyOutputLevel (this.endpoint);
    }
}

function setSlider (slider, position, setValue = true)
{
    if (setValue)
        slider.value = String (Math.round (position * 100));

    slider.style.setProperty ("--cp-fill", (Number (slider.value)).toFixed (0) + "%");
}

function setSwitch (button, isOn)
{
    button.classList.toggle ("cp-on", !! isOn);
    button.setAttribute ("aria-checked", isOn ? "true" : "false");
}
