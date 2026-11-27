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
import { countAudioChannels } from "../widgets/registry.js";
import { CPUMeter } from "../widgets/cpu-meter.js";
import { CPUSparkline, CPUHistoryView, formatLoad } from "../widgets/cpu-history-chart.js";
import { CPUHistory } from "../core/cpu-history.js";
import { countErrors } from "./diagnostics.js";
import { ariaShortcut } from "../core/commands.js";
import { TransportControl } from "./transport-control.js";
import { getCmajorVersion } from "../../cmaj_api/cmaj-version.js";

/*
    The bar along the top: the logo (which opens the about box), the patch's name (a link to its manifest), a chip with the
    build state, a chip with the audio setup, then the transport (for a patch that reads the host's
    timeline: see transport-control.js), the CPU meter (a button, with a sparkline, that opens the
    CPU history), the Capture button (options.captureButton: see views/recordings-view.js), audio
    on/off, reset, settings and (in a browser) the theme and unload buttons.
*/
export class TopBar
{
    constructor (ctx, settingsView, { captureButton = null } = {})
    {
        this.ctx = ctx;
        this.disposer = new Disposer();

        const s = ctx.store;

        this.nameElement = el ("button", { class: "cp-patch-name cp-link", onclick: () => ctx.bridge.openSourceFile (s.get ("manifestFile")) });
        this.buildDot = el ("span", { class: "cp-dot" });
        this.buildText = el ("span", null, "Connecting…");
        this.buildChip = el ("button", { class: "cp-chip cp-build-chip", title: "Build status", onclick: () => ctx.actions.showDrawer ("problems") },
                             this.buildDot, this.buildText);
        this.infoChip = el ("span", { class: "cp-chip cp-info-chip", hidden: true });

        this.cpu = new CPUMeter (ctx.session);
        this.disposer.add (() => this.cpu.dispose());
        this.cpu.element.removeAttribute ("title");

        // the CPU history: kept for the whole session, with a break wherever the audio stopped
        this.cpuHistory = new CPUHistory();
        this.disposer.add (this.cpuHistory.attach (ctx.session));
        this.cpuSparkline = new CPUSparkline (this.cpuHistory, { scheduler: ctx.scheduler });
        this.disposer.add (() => this.cpuSparkline.dispose());
        this.cpuHistoryView = new CPUHistoryView (ctx, this.cpuHistory);
        this.disposer.add (() => this.cpuHistoryView.dispose());
        this.cpuPopover = new Popover (this.cpuHistoryView.element, () => this.cpuHistoryView.setActive (true), () => this.cpuHistoryView.setActive (false));
        this.cpuPopover.element.classList.add ("cp-cpu-popover");
        this.cpuPopover.element.setAttribute ("aria-label", "CPU history");
        this.disposer.add (() => this.cpuPopover.dispose());
        this.cpuButton = el ("button", { class: "cp-cpu-button", title: "Audio thread CPU load: click for its history", "aria-haspopup": "dialog",
                                         "aria-expanded": "false", "aria-label": "CPU load: show the history",
                                         onclick: () => this.cpuPopover.toggle (this.cpuButton) },
                             this.cpu.element, this.cpuSparkline.element);
        this.disposer.add (this.cpuHistory.addListener (() => this.updateCPULabel()));

        this.transport = new TransportControl (ctx);
        this.disposer.add (() => this.transport.dispose());

        this.audioButton = el ("button", { class: "cp-button cp-audio-button", title: "Start or stop audio (Space)", "aria-pressed": "false",
                                           onclick: () => ctx.actions.toggleAudio() });
        this.resetButton = el ("button", { class: "cp-button", title: "Reset the patch to its initial state (R)", onclick: () => ctx.actions.reset() },
                               el ("span", { class: "cp-button-glyph cp-glyph-arrow", "aria-hidden": "true" }, "⟲"), "Reset");

        this.settingsPopover = new Popover (settingsView.element, () => settingsView.refresh());
        this.disposer.add (() => this.settingsPopover.dispose());
        this.settingsButton = el ("button", { class: "cp-button cp-settings-button", title: "Audio and display settings", "aria-haspopup": "dialog",
                                              onclick: () => this.settingsPopover.toggle (this.settingsButton) },
                                  el ("span", { class: "cp-button-glyph cp-glyph-gear", "aria-hidden": "true" }, "⚙"), el ("span", { class: "cp-button-label" }, "Settings"));

        this.themeButton = el ("button", { class: "cp-button cp-icon-button", title: "Switch between light and dark", onclick: () => ctx.actions.cycleTheme() }, "◐");

        const paletteShortcut = ctx.commands.get ("palette.show")?.shortcut ?? "Mod+K";
        this.paletteButton = el ("button", { class: "cp-button cp-palette-button", title: `Search the commands, endpoints and views (${ctx.commands.formatShortcut (paletteShortcut)})`,
                                             "aria-haspopup": "dialog", "aria-keyshortcuts": ariaShortcut (paletteShortcut, ctx.commands.isMac),
                                             onclick: () => ctx.actions.showCommandPalette() },
                                 el ("span", { class: "cp-palette-glyph", "aria-hidden": "true" }, "⌕"), el ("span", { class: "cp-button-label" }, "Commands"),
                                 el ("kbd", null, ctx.commands.formatShortcut (paletteShortcut)));
        this.unloadButton = el ("button", { class: "cp-button", title: "Unload this patch", onclick: () => ctx.actions.unloadPatch() }, "Unload");

        this.aboutPopover = new Popover (createAboutContent (ctx));
        this.aboutPopover.element.classList.add ("cp-about-popover");
        this.aboutPopover.element.setAttribute ("aria-label", "About Cmajor");
        this.disposer.add (() => this.aboutPopover.dispose());
        this.logoButton = el ("button", { class: "cp-logo", title: "About Cmajor", "aria-label": "About Cmajor", "aria-haspopup": "dialog",
                                          "aria-expanded": "false", onclick: () => this.toggleAbout() });

        this.element = el ("header", { class: "cp-topbar" },
                           this.logoButton,
                           this.nameElement, this.buildChip, this.infoChip,
                           el ("span", { class: "cp-spacer" }),
                           this.transport.element, this.cpuButton, captureButton?.element, this.audioButton, this.resetButton, this.unloadButton, this.paletteButton, this.settingsButton, this.themeButton);

        this.disposer.add (s.subscribe (null, () => this.update(), true));
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    showSettings()
    {
        if (! this.settingsPopover.isOpen)
            this.settingsPopover.open (this.settingsButton);
    }

    /// Opens (or closes) the about box, under the wordmark
    toggleAbout()
    {
        if (this.aboutPopover.isOpen)
            this.aboutPopover.close();
        else
            this.aboutPopover.open (this.logoButton, { align: "start", focus: ".cp-link" });
    }

    showAbout()
    {
        if (! this.aboutPopover.isOpen)
            this.toggleAbout();
    }

    /// Opens the CPU history popover (under the CPU button, or the settings button while that's hidden)
    showCPUHistory()
    {
        if (! this.cpuPopover.isOpen)
            this.cpuPopover.open (this.cpuButton.hidden ? this.settingsButton : this.cpuButton);
    }

    updateCPULabel()
    {
        const latest = this.cpuHistory.latest;
        const label = latest ? `CPU load ${formatLoad (latest.level)}: show the history` : "CPU load: show the history";

        // (only when the rounded value changes, so a screen reader isn't told every update)
        if (this.cpuButton.getAttribute ("aria-label") !== label)
            this.cpuButton.setAttribute ("aria-label", label);
    }

    update()
    {
        const s = this.ctx.store.state;
        const name = s.manifest?.name ?? (s.manifestFile ? s.manifestFile.split (/[\\/]/).pop() : "");

        this.nameElement.textContent = name || "Cmajor";
        this.nameElement.title = s.manifestFile ? `Open ${s.manifestFile}` : "";
        this.nameElement.disabled = ! s.manifestFile;

        // the build chip
        const errors = countErrors (s.diagnostics ?? []);
        let state = "idle", text;

        if (! s.connected)                                          { state = "off"; text = s.statusText || "Connecting…"; }
        else if (s.error)                                           { state = "error"; text = errors > 1 ? `${errors} errors` : "Build failed"; }
        else if (! s.loaded && s.statusText?.startsWith ("Loading")) { state = "busy"; text = "Building…"; }
        else if (s.loaded)                                          { state = "ok"; text = describeBuild (s.buildLog, s.lastBuildTime); }
        else                                                        { text = s.statusText || "No patch loaded"; }

        this.buildChip.dataset.state = state;
        this.buildText.textContent = text;
        this.buildChip.title = state === "error" ? "Show the problems" : (s.buildLog || text);

        // the audio setup
        const info = describeAudioSetup (s);
        this.infoChip.hidden = ! info;
        this.infoChip.textContent = info;

        const hasPatch = !! s.connection;
        this.audioButton.hidden = ! hasPatch;
        this.audioButton.textContent = s.playing ? "■ Audio on" : "▶ Audio off";
        this.audioButton.append (el ("kbd", null, "Space"));
        setPressed (this.audioButton, s.playing);
        this.resetButton.hidden = ! hasPatch;
        this.cpuButton.hidden = ! (hasPatch && s.playing);
        this.unloadButton.hidden = this.ctx.isFixedPatch || ! (s.loaded || s.error);
        this.themeButton.hidden = !! s.hostTheme;

        if (! s.playing)
            this.cpu.reset();

        // (a break in the history when the audio stops or the patch changes)
        if (! s.playing || s.connection !== this.lastConnection)
            this.cpuHistory.addGap();

        this.lastConnection = s.connection;
        this.cpuSparkline.setActive (! this.cpuButton.hidden);
    }
}

/// The about box: the wordmark, the version, what Cmajor is, links to the website, the docs
/// and the source, and the licence
function createAboutContent (ctx)
{
    const link = (label, url) => el ("button", { class: "cp-link", title: url, onclick: () => ctx.bridge.openURL (url) }, label);

    return el ("div", { class: "cp-about" },
               el ("span", { class: "cp-about-logo", role: "img", "aria-label": "Cmajor" }),
               el ("p", { class: "cp-about-version cp-mono" }, `Version ${getCmajorVersion()}`),
               el ("p", { class: "cp-about-blurb" }, "A C-family language for writing fast, portable audio software."),
               el ("p", { class: "cp-about-links" },
                   link ("cmajor.dev", "https://cmajor.dev"),
                   link ("Documentation", "https://cmajor.dev/docs/GettingStarted"),
                   link ("GitHub", "https://github.com/cmajor-lang/cmajor")),
               el ("p", { class: "cp-about-legal cp-dim" },
                   "© Cmajor Software Ltd. Available under the GPLv3, or a commercial licence: see cmajor.dev."));
}

function describeBuild (buildLog, time)
{
    const ms = /Total build time:\s*([\d.]+)\s*ms/.exec (buildLog ?? "")?.[1];
    const when = time ? new Date (time).toLocaleTimeString ([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }) : "";
    return ms ? `built in ${Math.round (Number (ms))} ms${when ? " · " + when : ""}` : (when ? `built · ${when}` : "Loaded");
}

function describeAudioSetup (s)
{
    if (! s.loaded)
        return "";

    const p = s.deviceProperties;
    const parts = [];

    if (p?.rate)
        parts.push (`${(Number (p.rate) / 1000).toFixed (Number (p.rate) % 1000 ? 1 : 0)} kHz`);

    if (p?.blockSize)
        parts.push (String (p.blockSize));

    parts.push (`${countAudioChannels (s.inputs, "audio in")} in / ${countAudioChannels (s.outputs, "audio out")} out`);
    return parts.join (" · ");
}

//==============================================================================
/** A popover attached under a button (or over it, if there's no room below); closes on Escape
    (which gives the focus back to the anchor) or a click outside it.

    open (anchor, { align: "end" | "start", focus, positionFrom }) lines its right (or left) edge
    up with the anchor's, and focuses the first element matching the focus selector. The anchor
    gets aria-expanded. positionFrom is an element to place it by instead of the anchor (e.g.
    the whole widget whose name is the anchor); clicks on that don't count as outside either.
*/
export class Popover
{
    constructor (content, onOpen, onClose)
    {
        this.onOpen = onOpen;
        this.onClose = onClose;
        this.align = "end";
        this.positionFrom = null;
        this.element = el ("div", { class: "cp-popover", role: "dialog", hidden: true }, content);
        this.handleOutside = (e) => { if (! this.element.contains (e.target) && ! this.anchor?.contains (e.target) && ! this.positionFrom?.contains (e.target)) this.close(); };
        this.handleKey = (e) => { if (e.key === "Escape") { this.close(); this.anchor?.focus(); } };
    }

    dispose()
    {
        this.close();
        this.element.remove();
    }

    get isOpen()        { return ! this.element.hidden; }

    toggle (anchor)
    {
        this.isOpen ? this.close() : this.open (anchor);
    }

    open (anchor, { align = "end", focus = "select, button, input", positionFrom = null } = {})
    {
        this.anchor = anchor;
        this.align = align;
        this.positionFrom = positionFrom;
        const root = anchor.closest ("cmaj-patch-panel") ?? document.body;

        if (this.element.parentElement !== root)
            root.append (this.element);

        this.onOpen?.();
        this.element.hidden = false;
        this.position();

        anchor.setAttribute ("aria-expanded", "true");
        document.addEventListener ("pointerdown", this.handleOutside, true);
        document.addEventListener ("keydown", this.handleKey, true);
        this.element.querySelector (focus)?.focus();
    }

    /// Places it again (e.g. after its anchor has moved)
    position()
    {
        const root = this.element.parentElement;
        const anchor = this.positionFrom ?? this.anchor;

        if (! root || ! anchor || ! this.isOpen)
            return;

        const align = this.align;
        const a = anchor.getBoundingClientRect(), r = root.getBoundingClientRect();
        const width = this.element.offsetWidth, height = this.element.offsetHeight;
        const gap = 6, margin = 8;
        const below = a.bottom - r.top + gap, above = a.top - r.top - gap - height;
        const left = align === "start" ? a.left - r.left : a.right - r.left - width;

        this.element.style.top = (below + height <= r.height - margin || above < margin ? below : above) + "px";
        this.element.style.left = Math.max (margin, Math.min (r.width - width - margin, left)) + "px";
    }

    close()
    {
        if (! this.isOpen)
            return;

        this.element.hidden = true;
        this.anchor?.setAttribute ("aria-expanded", "false");
        document.removeEventListener ("pointerdown", this.handleOutside, true);
        document.removeEventListener ("keydown", this.handleKey, true);
        this.onClose?.();
    }
}
