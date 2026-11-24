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
import { PatchGUIView } from "../views/gui-view.js";

const titleBarHeight = 28;
const minBodyWidth = 160, minBodyHeight = 100;

/*
    The floating GUI window: a draggable, resizable window inside the page (a positioned element,
    not a popup, so it works in VS Code's iframe), holding a SECOND, independent instance of the
    patch's view on the same PatchConnection. The GUI tab keeps its own instance, so both can be
    live at once, which deliberately tests patch views that assume they're the only one.

    The title bar has a live W × H readout of the view's area, size presets (the manifest's
    size, ¾, 1½ and 2×), "Fixed" (which simulates a non-resizable plugin window in a DAW: no
    resizing, at the manifest's size; it starts on if the manifest says resizable: false) and
    a close button. There's no placement logic: it goes where the user puts it, and is only
    kept inside the panel so it can't be lost. Its position, size and "Fixed" are kept per patch
    (only: another patch starts at its own manifest's size).
*/
export class FloatingGUIWindow
{
    constructor (ctx, container)
    {
        this.ctx = ctx;
        this.container = container;
        this.disposer = new Disposer();
        this.guiView = null;
        this.isOpen = false;

        this.titleText = el ("span", { class: "cp-float-title" });
        this.sizeText = el ("span", { class: "cp-float-size", "aria-live": "off" });
        this.presetSelect = el ("select", { class: "cp-select", title: "Window size", "aria-label": "Window size",
                                            onchange: () => this.applyPreset (this.presetSelect.value) });
        this.fixedButton = el ("button", { class: "cp-button", "aria-pressed": "false",
                                           title: "Simulate a plugin window that can't be resized (manifest: \"resizable\": false)",
                                           onclick: () => this.setFixed (! this.isFixed) }, "Fixed");
        this.closeButton = el ("button", { class: "cp-button cp-icon-button", title: "Close this instance (the GUI tab keeps its own) (G)",
                                           "aria-label": "Close the floating GUI", onclick: () => ctx.actions.setFloatingGUIOpen (false) }, "✕");

        this.titleBar = el ("div", { class: "cp-float-titlebar" },
                            this.titleText, this.sizeText, el ("span", { class: "cp-spacer" }),
                            this.presetSelect, this.fixedButton, this.closeButton);
        this.body = el ("div", { class: "cp-float-body" });

        this.element = el ("div", { class: "cp-float", role: "dialog", "aria-label": "Floating patch GUI", hidden: true },
                           this.titleBar, this.body,
                           el ("div", { class: "cp-float-handle cp-float-r", dataset: { resize: "r" } }),
                           el ("div", { class: "cp-float-handle cp-float-b", dataset: { resize: "b" } }),
                           el ("div", { class: "cp-float-handle cp-float-rb", dataset: { resize: "rb" } }));

        this.disposer.listen (this.titleBar, "pointerdown", (e) => this.startMove (e));

        for (const handle of this.element.querySelectorAll ("[data-resize]"))
            this.disposer.listen (handle, "pointerdown", (e) => this.startResize (e, handle.dataset.resize));

        this.disposer.listen (this.element, "pointerdown", () => this.element.classList.add ("cp-focused"));
        this.disposer.listen (document, "pointerdown", (e) => { if (! this.element.contains (e.target)) this.element.classList.remove ("cp-focused"); }, true);
        this.disposer.listen (window, "resize", () => this.clampIntoView());

        this.bodyObserver = new ResizeObserver (() => this.updateSizeText());
        this.bodyObserver.observe (this.body);
        this.disposer.add (() => this.bodyObserver.disconnect());

        this.disposer.add (ctx.store.subscribe (["manifest"], () => this.updateTitle(), true));
        container.append (this.element);
    }

    dispose()
    {
        this.close();
        this.disposer.dispose();
        this.element.remove();
    }

    //==============================================================================
    open()
    {
        if (this.isOpen)
            return;

        this.isOpen = true;
        this.guiView = new PatchGUIView (this.ctx, { fill: true });
        this.body.append (this.guiView.element);
        this.element.hidden = false;
        this.element.classList.add ("cp-focused");

        const manifestSize = this.guiView.getManifestSize();
        const saved = this.ctx.settings.patch.get ("floatRect", null);
        this.updatePresets();

        if (saved && saved.w > 0 && saved.h > 0)
        {
            this.setPosition (saved.x, saved.y);
            this.setBodySize (saved.w, saved.h, false);
        }
        else
        {
            // the first time: bottom-right of the panel
            const r = this.container.getBoundingClientRect();
            this.setBodySize (manifestSize.width, manifestSize.height, false);
            this.setPosition (r.width - this.element.offsetWidth - 40, r.height - this.element.offsetHeight - 60);
        }

        this.setFixed (this.ctx.settings.patch.get ("floatFixed", ! manifestSize.resizable), false);
        this.clampIntoView();
    }

    close()
    {
        if (! this.isOpen)
            return;

        this.isOpen = false;
        this.guiView?.dispose();     // disposes this instance of the patch view
        this.guiView = null;
        this.element.hidden = true;
    }

    updateTitle()
    {
        this.titleText.textContent = this.ctx.store.get ("manifest")?.name ?? "Patch GUI";

        if (this.isOpen)
            this.updatePresets();
    }

    //==============================================================================
    getPresets()
    {
        const { width, height } = this.guiView?.getManifestSize() ?? { width: 600, height: 400 };
        const scaled = (f) => `${Math.round (width * f)}x${Math.round (height * f)}`;

        // (short labels, as the title bar of a small view is narrow; the readout shows the size)
        return [
            { value: "manifest", size: [width, height], text: "Manifest", title: `The manifest's size (${width} × ${height})` },
            { value: scaled (0.75), size: [Math.round (width * 0.75), Math.round (height * 0.75)], text: "Small ¾" },
            { value: scaled (1.5), size: [Math.round (width * 1.5), Math.round (height * 1.5)], text: "Large 1½×" },
            { value: scaled (2), size: [width * 2, height * 2], text: "2×" },
        ];
    }

    updatePresets()
    {
        this.presetSelect.replaceChildren (...this.getPresets().map (p => el ("option", { value: p.value, title: p.title ?? p.value.replace ("x", " × ") }, p.text)),
                                           el ("option", { value: "custom", disabled: true }, "Custom"));
        this.updateSizeText();
    }

    applyPreset (value)
    {
        const preset = this.getPresets().find (p => p.value === value);

        if (preset)
        {
            this.setBodySize (preset.size[0], preset.size[1]);
            this.clampIntoView();
        }
    }

    setFixed (shouldBeFixed, save = true)
    {
        this.isFixed = !! shouldBeFixed;
        this.element.classList.toggle ("cp-fixed", this.isFixed);
        this.fixedButton.classList.toggle ("cp-on", this.isFixed);
        this.fixedButton.setAttribute ("aria-pressed", this.isFixed ? "true" : "false");
        this.presetSelect.disabled = this.isFixed;

        if (this.isFixed)
            this.applyPreset ("manifest");

        if (save)
            this.ctx.settings.patch.set ("floatFixed", this.isFixed);
    }

    //==============================================================================
    setPosition (x, y)
    {
        this.element.style.left = Math.round (x) + "px";
        this.element.style.top = Math.round (y) + "px";
    }

    /// Sets the size of the area the patch view gets (the window is the title bar + borders bigger)
    setBodySize (w, h, save = true)
    {
        this.element.style.width = Math.round (Math.max (minBodyWidth, w)) + 2 + "px";
        this.element.style.height = Math.round (Math.max (minBodyHeight, h)) + titleBarHeight + 2 + "px";
        this.updateSizeText();

        if (save)
            this.saveRect();
    }

    updateSizeText()
    {
        const w = this.body.clientWidth, h = this.body.clientHeight;
        this.sizeText.textContent = `${w} × ${h}`;
        const preset = this.getPresets().find (p => p.size[0] === w && p.size[1] === h);
        this.presetSelect.value = preset ? preset.value : "custom";
    }

    saveRect()
    {
        if (this.isOpen)
            this.ctx.settings.patch.set ("floatRect", { x: this.element.offsetLeft, y: this.element.offsetTop,
                                               w: this.body.clientWidth, h: this.body.clientHeight });
    }

    /// Keeps at least part of the title bar inside the panel, so the window can't be lost
    clampIntoView()
    {
        if (! this.isOpen)
            return;

        const r = this.container.getBoundingClientRect();
        const x = Math.max (Math.min (0, r.width - this.element.offsetWidth), Math.min (r.width - 80, this.element.offsetLeft));
        const y = Math.max (0, Math.min (r.height - titleBarHeight - 4, this.element.offsetTop));
        this.setPosition (x, y);
    }

    startMove (e)
    {
        if (e.button !== 0 || e.target.closest ("button, select, input"))
            return;

        e.preventDefault();
        const startX = e.clientX - this.element.offsetLeft, startY = e.clientY - this.element.offsetTop;
        this.track (e, this.titleBar, (ev) =>
        {
            this.setPosition (ev.clientX - startX, ev.clientY - startY);
            this.clampIntoView();
        });
    }

    startResize (e, mode)
    {
        if (e.button !== 0 || this.isFixed)
            return;

        e.preventDefault();
        const startX = e.clientX, startY = e.clientY;
        const startW = this.body.clientWidth, startH = this.body.clientHeight;

        this.track (e, e.currentTarget, (ev) =>
            this.setBodySize (mode === "b" ? startW : startW + ev.clientX - startX,
                              mode === "r" ? startH : startH + ev.clientY - startY, false));
    }

    track (e, target, onMove)
    {
        target.setPointerCapture (e.pointerId);
        this.element.classList.add ("cp-dragging");

        const up = () =>
        {
            target.removeEventListener ("pointermove", onMove);
            target.removeEventListener ("pointerup", up);
            target.removeEventListener ("pointercancel", up);
            this.element.classList.remove ("cp-dragging");
            this.saveRect();
        };

        target.addEventListener ("pointermove", onMove);
        target.addEventListener ("pointerup", up);
        target.addEventListener ("pointercancel", up);
    }
}
