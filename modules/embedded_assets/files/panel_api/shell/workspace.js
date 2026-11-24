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

import { el, Disposer, setPressed, trackPointer } from "../core/dom.js";

/*
    The workspace layout: the main area (GUI | Graph | I/O tabs) with the inspector on its right,
    and the drawer (Console | Problems | Generated code) along the bottom. The inspector's width
    and the drawer's height can be dragged (or changed with the arrow keys on the splitters),
    and both can be collapsed. The tab choices and sizes are kept per patch, and a new patch starts with the last layout used
    (ctx.settings.patchOrGlobal).
    When the panel is narrow, the inspector becomes an overlay that slides in from the right.

    A tab is { id, label, key (shortcut hint), view }, where view has .element, optionally
    .toolbar (shown in the tab bar while that tab is active), setActive (bool) and dispose().
    The inspector view's setActive() follows whether the inspector is open.
*/
export class Workspace
{
    constructor (ctx, { mainTabs, mainTools, inspector, drawerTabs })
    {
        this.ctx = ctx;
        this.disposer = new Disposer();
        this.inspectorView = inspector;

        this.main = new TabSet ("main", mainTabs, (id) => this.handleTabChange ("mainTab", id));
        this.drawer = new TabSet ("drawer", drawerTabs, (id) => this.handleTabChange ("drawerTab", id));

        this.inspectorToggle = el ("button", { class: "cp-button cp-icon-button", title: "Show or hide the inspector", "aria-pressed": "true",
                                               onclick: () => this.setInspectorOpen (! this.isInspectorOpen) }, "◨");
        this.drawerToggle = el ("button", { class: "cp-button cp-icon-button", title: "Show or hide the drawer", "aria-expanded": "true",
                                            onclick: () => this.setDrawerOpen (! this.isDrawerOpen) }, "▾");

        this.main.globalTools.append (...(mainTools ?? []), this.inspectorToggle);
        this.drawer.globalTools.append (this.drawerToggle);

        this.inspector = el ("aside", { class: "cp-inspector", "aria-label": "Inspector" }, inspector.element);
        this.inspectorSplitter = el ("div", { class: "cp-splitter cp-splitter-v", role: "separator", "aria-orientation": "vertical",
                                              "aria-label": "Resize the inspector", tabindex: 0 });
        this.drawerSplitter = el ("div", { class: "cp-splitter cp-splitter-h", role: "separator", "aria-orientation": "horizontal",
                                           "aria-label": "Resize the drawer", tabindex: 0 });

        this.element = el ("div", { class: "cp-workspace" },
                           el ("section", { class: "cp-main" }, this.main.element),
                           this.inspectorSplitter, this.inspector, this.drawerSplitter,
                           el ("footer", { class: "cp-drawer" }, this.drawer.element));

        this.makeSplitter (this.inspectorSplitter, "inspectorWidth", 180, 600, -1, "x");
        this.makeSplitter (this.drawerSplitter, "drawerHeight", 80, 800, -1, "y");

        this.resizeObserver = new ResizeObserver (() => this.updateNarrow());
        this.resizeObserver.observe (this.element);
        this.disposer.add (() => this.resizeObserver.disconnect());

        this.applyPrefs();
    }

    dispose()
    {
        this.disposer.dispose();
        this.main.dispose();
        this.drawer.dispose();
        this.element.remove();
    }

    /// Re-reads the layout from the settings (e.g. once the patch is known)
    applyPrefs()
    {
        const prefs = this.ctx.settings.patchOrGlobal;
        this.setSize ("inspectorWidth", prefs.get ("inspectorWidth", 280));
        this.setSize ("drawerHeight", prefs.get ("drawerHeight", 220));
        this.setInspectorOpen (prefs.get ("inspectorOpen", true) && ! this.isNarrow, false);   // (narrow: the overlay starts closed)
        this.setDrawerOpen (prefs.get ("drawerOpen", true), false);
        // (the main tab is the patch's own: a patch that hasn't chosen one starts on the first, its GUI)
        this.main.select (this.ctx.settings.patch.get ("mainTab", this.main.tabs[0].id), false);
        this.drawer.select (prefs.get ("drawerTab", this.drawer.tabs[0].id), false);
    }

    handleTabChange (key, id)
    {
        if (key === "mainTab")
            this.ctx.settings.patch.set (key, id);
        else
            this.ctx.settings.patchOrGlobal.set (key, id);

        if (key === "drawerTab" && ! this.isDrawerOpen)
            this.setDrawerOpen (true);
    }

    /// Shows a main tab; save = false for a switch the panel makes itself, which isn't the user's choice
    showMainTab (id, save = true)       { this.main.select (id, save); }

    showDrawerTab (id)
    {
        this.drawer.select (id);
        this.setDrawerOpen (true);
    }

    get mainTab()                       { return this.main.selected; }
    get drawerTab()                     { return this.drawer.selected; }

    setInspectorOpen (shouldBeOpen, save = true)
    {
        this.isInspectorOpen = !! shouldBeOpen;
        this.element.toggleAttribute ("data-inspector-open", this.isInspectorOpen);
        setPressed (this.inspectorToggle, this.isInspectorOpen);
        this.inspector.inert = ! this.isInspectorOpen;
        this.inspectorView.setActive?.(this.isInspectorOpen);

        if (save)
            this.ctx.settings.patchOrGlobal.set ("inspectorOpen", this.isInspectorOpen);
    }

    setDrawerOpen (shouldBeOpen, save = true)
    {
        this.isDrawerOpen = !! shouldBeOpen;
        this.element.toggleAttribute ("data-drawer-open", this.isDrawerOpen);
        this.drawerToggle.textContent = this.isDrawerOpen ? "▾" : "▴";
        this.drawerToggle.setAttribute ("aria-expanded", this.isDrawerOpen ? "true" : "false");
        this.drawer.setPanesActive (this.isDrawerOpen);

        if (save)
            this.ctx.settings.patchOrGlobal.set ("drawerOpen", this.isDrawerOpen);
    }

    setSize (key, value)
    {
        const property = key === "inspectorWidth" ? "--cp-inspector-w" : "--cp-drawer-h";
        this.element.style.setProperty (property, Math.round (value) + "px");
        this[key] = value;
    }

    updateNarrow()
    {
        const isNarrow = this.element.clientWidth < 720;

        if (isNarrow !== this.isNarrow)
        {
            this.isNarrow = isNarrow;
            this.element.toggleAttribute ("data-narrow", isNarrow);

            // (the overlay inspector starts closed)
            if (isNarrow && this.isInspectorOpen)
                this.setInspectorOpen (false, false);
            else if (! isNarrow)
                this.setInspectorOpen (this.ctx.settings.patchOrGlobal.get ("inspectorOpen", true), false);
        }
    }

    makeSplitter (splitter, key, min, max, direction, axis)
    {
        const clampSize = (v) => Math.max (min, Math.min (max, v, (axis === "x" ? this.element.clientWidth - 240 : this.element.clientHeight - 120)));

        splitter.addEventListener ("pointerdown", (e) =>
        {
            if (e.button !== 0)
                return;

            e.preventDefault();
            const start = axis === "x" ? e.clientX : e.clientY;
            const startSize = this[key];
            this.element.classList.add ("cp-resizing");

            trackPointer (e, splitter,
                          (ev) => this.setSize (key, clampSize (startSize + direction * ((axis === "x" ? ev.clientX : ev.clientY) - start))),
                          () =>
                          {
                              this.element.classList.remove ("cp-resizing");
                              this.ctx.settings.patchOrGlobal.set (key, this[key]);
                          });
        });

        splitter.addEventListener ("keydown", (e) =>
        {
            // (left grows the inspector, up grows the drawer)
            const keys = axis === "x" ? { ArrowLeft: 1, ArrowRight: -1 } : { ArrowUp: 1, ArrowDown: -1 };

            if (keys[e.key])
            {
                e.preventDefault();
                this.setSize (key, clampSize (this[key] + keys[e.key] * (e.shiftKey ? 50 : 10)));
                this.ctx.settings.patchOrGlobal.set (key, this[key]);
            }
        });

        splitter.addEventListener ("dblclick", () => axis === "x" ? this.setInspectorOpen (! this.isInspectorOpen)
                                                                  : this.setDrawerOpen (! this.isDrawerOpen));
    }
}

//==============================================================================
/// A tab bar plus the panes it switches between
class TabSet
{
    constructor (name, tabs, onChange)
    {
        this.name = name;
        this.tabs = tabs;
        this.onChange = onChange;
        this.selected = null;
        this.panesActive = true;

        this.tabTools = el ("div", { class: "cp-tabbar-tools" });
        this.globalTools = el ("div", { class: "cp-tabbar-tools" });
        this.tabList = el ("div", { class: "cp-tablist", role: "tablist", "aria-label": name === "main" ? "Views" : "Drawer" });
        this.panes = el ("div", { class: "cp-panes" });

        for (const tab of tabs)
        {
            const paneID = `cp-${name}-pane-${tab.id}`, tabID = `cp-${name}-tab-${tab.id}`;

            tab.badge = el ("span", { class: "cp-badge", hidden: true });
            tab.button = el ("button", { class: "cp-tab", role: "tab", id: tabID, "aria-controls": paneID, "aria-selected": "false", tabindex: -1,
                                         dataset: { tab: tab.id }, onclick: () => this.select (tab.id) },
                             tab.label, tab.badge, tab.key ? el ("kbd", null, tab.key) : null);
            tab.pane = el ("div", { class: "cp-pane", role: "tabpanel", id: paneID, "aria-labelledby": tabID, hidden: true }, tab.view.element);
            this.tabList.append (tab.button);
            this.panes.append (tab.pane);
        }

        this.tabList.addEventListener ("keydown", (e) => this.handleKey (e));

        this.element = el ("div", { class: ["cp-tabset", `cp-tabset-${name}`] },
                           el ("div", { class: "cp-tabbar" }, this.tabList, el ("span", { class: "cp-spacer" }), this.tabTools, this.globalTools),
                           this.panes);
    }

    dispose()
    {
        for (const tab of this.tabs)
            tab.view.dispose?.();
    }

    select (id, notify = true)
    {
        const tab = this.tabs.find (t => t.id === id) ?? this.tabs[0];

        if (this.selected === tab.id)
        {
            if (notify)
                this.onChange (tab.id);

            return;
        }

        this.selected = tab.id;

        for (const t of this.tabs)
        {
            const on = t === tab;
            t.button.setAttribute ("aria-selected", on ? "true" : "false");
            t.button.tabIndex = on ? 0 : -1;
            t.button.classList.toggle ("cp-active", on);
            t.pane.hidden = ! on;
        }

        this.tabTools.replaceChildren (...(tab.view.toolbar ? [tab.view.toolbar] : []));
        this.updateActiveViews();

        if (notify)
            this.onChange (tab.id);
    }

    setPanesActive (isActive)
    {
        this.panesActive = isActive;
        this.updateActiveViews();
    }

    updateActiveViews()
    {
        for (const t of this.tabs)
            t.view.setActive?.(this.panesActive && t.id === this.selected);
    }

    setBadge (id, text, kind)
    {
        const tab = this.tabs.find (t => t.id === id);

        if (tab)
        {
            tab.badge.hidden = text === null || text === undefined || text === "";
            tab.badge.textContent = text ?? "";
            tab.badge.dataset.kind = kind ?? "";
        }
    }

    handleKey (e)
    {
        const i = this.tabs.findIndex (t => t.id === this.selected);
        const next = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: this.tabs.length - 1 }[e.key];

        if (next !== undefined)
        {
            e.preventDefault();
            const tab = this.tabs[(next + this.tabs.length) % this.tabs.length];
            this.select (tab.id);
            tab.button.focus();
        }
    }
}
