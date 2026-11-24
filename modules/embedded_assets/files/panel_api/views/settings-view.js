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

/*
    The settings popover: the server's audio device settings, whether the transport always shows,
    and (in a browser) the panel's colour theme. The device properties arrive in the store as
    "deviceProperties".
*/
export class SettingsView
{
    constructor (ctx)
    {
        this.ctx = ctx;
        this.disposer = new Disposer();
        this.deviceSection = el ("div", { class: "cp-form" });
        this.themeSection = el ("div", { class: "cp-form" });

        // (the top bar shows the transport for a patch that reads the host's timeline; this shows it for every patch)
        this.transportSwitch = el ("button", { class: "cp-toggle", type: "button", role: "switch", "aria-checked": "false", "aria-label": "Always show the transport",
                                               title: "Show the transport controls in the top bar for every patch, not only the ones with timeline inputs",
                                               onclick: () => ctx.actions.setAlwaysShowTransport (! ctx.store.get ("alwaysShowTransport")) },
                                   el ("span", { class: "cp-toggle-knob" }));
        this.transportSection = el ("div", { class: "cp-form" },
                                    el ("div", { class: "cp-form-row cp-switch-row" }, el ("span", { "aria-hidden": "true" }, "Always show the transport"), this.transportSwitch));

        this.element = el ("div", { class: "cp-settings" },
                           el ("h3", { class: "cp-section-title" }, "Audio device"), this.deviceSection,
                           el ("div", { class: "cp-settings-transport" }, el ("h3", { class: "cp-section-title" }, "Transport"), this.transportSection),
                           el ("div", { class: "cp-settings-theme" }, el ("h3", { class: "cp-section-title" }, "Appearance"), this.themeSection));

        this.disposer.add (ctx.store.subscribe (["deviceProperties"], () => this.updateDevice(), true));
        this.disposer.add (ctx.store.subscribe (["alwaysShowTransport"], () => this.updateTransport(), true));
        this.updateTheme();
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    /// Called when the popover opens
    refresh()
    {
        this.ctx.session.requestAudioDeviceProperties();
        this.updateTheme();
    }

    updateDevice()
    {
        const p = this.ctx.store.get ("deviceProperties");

        if (! p)
        {
            this.deviceSection.replaceChildren (el ("p", { class: "cp-empty" }, "Waiting for the server…"));
            return;
        }

        const rows = [];

        const addRow = (label, key, list, current) =>
        {
            if (! Array.isArray (list))
                return;

            const select = el ("select", { class: "cp-select", "aria-label": label,
                                           onchange: () => this.ctx.session.setAudioDeviceProperties ({ ...p, [key]: select.value }) },
                               list.map (item => item.divider ? el ("option", { disabled: true }, "──────────")
                                                              : el ("option", { value: String (item.ID ?? item), selected: String (item.ID ?? item) === String (current ?? "") },
                                                                    String (item.name ?? item))));
            rows.push (el ("label", { class: "cp-form-row" }, el ("span", null, label), select));
        };

        addRow ("Audio API", "audioAPI", p.availableAPIs, p.audioAPI);

        if (p.availableOutputDevices)
            addRow ("Output", "output", [{ ID: "", name: "Default device" }, { divider: true }, ...p.availableOutputDevices], p.output);

        if (p.availableInputDevices)
            addRow ("Input", "input", [{ ID: "", name: "Default device" }, { divider: true }, ...p.availableInputDevices], p.input);

        addRow ("Sample rate", "rate", p.sampleRates, p.rate);
        addRow ("Block size", "blockSize", p.blockSizes, p.blockSize);

        this.deviceSection.replaceChildren (...rows);
    }

    updateTransport()
    {
        const isOn = !! this.ctx.store.get ("alwaysShowTransport");
        this.transportSwitch.classList.toggle ("cp-on", isOn);
        this.transportSwitch.setAttribute ("aria-checked", isOn ? "true" : "false");
    }

    updateTheme()
    {
        // (inside VS Code, the panel follows the editor's theme)
        const isHostThemed = this.ctx.bridge.getTheme() !== null;
        this.themeSection.parentElement.hidden = isHostThemed;

        if (isHostThemed)
            return;

        const current = this.ctx.settings.global.get ("theme", "auto");
        const select = el ("select", { class: "cp-select", "aria-label": "Theme", onchange: () => this.ctx.actions.setThemePreference (select.value) },
                           [["auto", "Match the system"], ["dark", "Dark"], ["light", "Light"]].map (([v, t]) => el ("option", { value: v, selected: v === current }, t)));

        this.themeSection.replaceChildren (el ("label", { class: "cp-form-row" }, el ("span", null, "Theme"), select));
    }
}
