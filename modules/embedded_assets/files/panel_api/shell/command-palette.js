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
import { splitByMatches } from "../core/fuzzy.js";

/*
    The command palette (Cmd/Ctrl-K, or the search button in the top bar): a box at the top of
    the panel with a search field and the matching commands from ctx.commands. These are the
    panel's actions (with their shortcuts), and the patch's endpoints, parameters and views,
    and other patches to load. "@" searches only endpoints and parameters.

    It's an ARIA combobox: the field keeps the focus, and the arrow keys move the active option
    (aria-activedescendant). Enter runs the active option; Escape (or a click outside) closes
    it, giving the focus back to where it was.
*/
export class CommandPalette
{
    constructor (ctx, root)
    {
        this.ctx = ctx;
        this.root = root;
        this.disposer = new Disposer();
        this.results = [];
        this.activeIndex = 0;

        this.input = el ("input", { class: "cp-input cp-palette-input", type: "text", role: "combobox", spellcheck: false, autocomplete: "off",
                                    placeholder: "Type a command, or @ for endpoints and parameters…",
                                    "aria-label": "Command", "aria-expanded": "true", "aria-controls": "cp-palette-list", "aria-autocomplete": "list",
                                    oninput: () => this.update(),
                                    onkeydown: (e) => this.handleKey (e) });

        this.list = el ("div", { class: "cp-palette-list", id: "cp-palette-list", role: "listbox", "aria-label": "Commands" });
        this.emptyMessage = el ("p", { class: "cp-empty cp-palette-empty", hidden: true }, "No matching commands");

        this.element = el ("div", { class: "cp-palette", role: "dialog", "aria-label": "Command palette", hidden: true },
                           this.input, this.list, this.emptyMessage);

        this.handleOutside = (e) => { if (! e.composedPath().includes (this.element)) this.close (false); };
        this.disposer.add (() => this.close (false), () => this.element.remove());
    }

    dispose()               { this.disposer.dispose(); }

    get isOpen()            { return ! this.element.hidden; }

    toggle()                { this.isOpen ? this.close() : this.open(); }

    open (initialText = "")
    {
        if (this.isOpen)
        {
            this.input.focus();
            return;
        }

        this.previousFocus = getDeepActiveElement();

        if (this.element.parentElement !== this.root)
            this.root.append (this.element);

        this.element.hidden = false;
        this.input.value = initialText;
        this.update();
        this.input.focus();
        document.addEventListener ("pointerdown", this.handleOutside, true);
    }

    /// Closes it; restoreFocus gives the focus back to what had it before
    close (restoreFocus = true)
    {
        if (! this.isOpen)
            return;

        this.element.hidden = true;
        document.removeEventListener ("pointerdown", this.handleOutside, true);

        if (restoreFocus && this.previousFocus?.isConnected)
            this.previousFocus.focus();

        this.previousFocus = null;
    }

    //==============================================================================
    update()
    {
        this.results = this.ctx.commands.search (this.input.value);
        this.activeIndex = 0;
        this.list.replaceChildren (...this.results.map ((r, i) => this.createOption (r, i)));
        this.emptyMessage.hidden = this.results.length > 0;
        this.updateActive();
    }

    createOption ({ command, indices }, index)
    {
        const title = el ("span", { class: "cp-palette-title" },
                          splitByMatches (command.title, indices).map (run => run.isMatch ? el ("mark", { class: "cp-match" }, run.text) : run.text));
        const shortcut = command.shortcut ? el ("kbd", null, this.ctx.commands.formatShortcut (command.shortcut)) : null;

        return el ("div", { class: "cp-palette-option", role: "option", id: `cp-palette-option-${index}`, "aria-selected": "false",
                            dataset: { kind: command.kind ?? "action" },
                            onpointermove: () => { if (this.activeIndex !== index) { this.activeIndex = index; this.updateActive (false); } },
                            onpointerdown: (e) => e.preventDefault(),   // (keeps the focus in the field)
                            onclick: () => this.runResult (index) },
                   el ("span", { class: "cp-palette-text" }, title,
                       command.detail ? el ("span", { class: "cp-palette-detail" }, command.detail) : null),
                   el ("span", { class: "cp-palette-category" }, command.category ?? ""),
                   shortcut);
    }

    updateActive (scroll = true)
    {
        const options = this.list.children;

        for (let i = 0; i < options.length; ++i)
        {
            const isActive = i === this.activeIndex;
            options[i].classList.toggle ("cp-active", isActive);
            options[i].setAttribute ("aria-selected", isActive ? "true" : "false");
        }

        const active = options[this.activeIndex];

        if (active)
        {
            this.input.setAttribute ("aria-activedescendant", active.id);

            if (scroll)
                active.scrollIntoView ({ block: "nearest" });
        }
        else
        {
            this.input.removeAttribute ("aria-activedescendant");
        }
    }

    moveActive (delta, absolute)
    {
        if (this.results.length === 0)
            return;

        const last = this.results.length - 1;
        this.activeIndex = absolute !== undefined ? absolute : Math.max (0, Math.min (last, this.activeIndex + delta));
        this.updateActive();
    }

    handleKey (e)
    {
        const isListKey = this.input.value === "";
        const page = 8;

        switch (e.key)
        {
            case "ArrowDown":   this.moveActive (1); break;
            case "ArrowUp":     this.moveActive (-1); break;
            case "PageDown":    this.moveActive (page); break;
            case "PageUp":      this.moveActive (-page); break;
            case "Home":        if (! isListKey) return; this.moveActive (0, 0); break;
            case "End":         if (! isListKey) return; this.moveActive (0, this.results.length - 1); break;
            case "Enter":       this.runResult (this.activeIndex); break;
            case "Escape":      this.close(); break;
            case "Tab":         break;   // (the focus stays in the field while it's open)
            default:            return;
        }

        e.preventDefault();
        e.stopPropagation();
    }

    runResult (index)
    {
        const result = this.results[index];

        if (! result)
            return;

        // (closed first, so that a command that moves the focus keeps it there)
        this.close();
        this.ctx.commands.run (result.command);
    }
}

/// The focused element, looking inside shadow roots
function getDeepActiveElement()
{
    let active = document.activeElement;

    while (active?.shadowRoot?.activeElement)
        active = active.shadowRoot.activeElement;

    return active;
}
