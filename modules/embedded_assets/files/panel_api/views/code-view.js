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

const targetNames = { cpp: "C++", javascript: "JavaScript/WebAssembly", wast: "WAST", llvm: "LLVM native" };
const vscodeLanguages = { cpp: "cpp", wast: "wat", javascript: "javascript", llvm: "llvm" };

/// How long to wait for the server to generate code before saying that it hasn't
export const codeGenerationTimeoutMs = 120000;

/*
    The "Generated code" tab in the drawer: one sub-tab per code generation target that the
    server offers. Code is only generated when its tab is shown, and again after each build
    (each result is cached by target and build number). There's at most one request in flight
    per target and build. A request that gets no reply in time, or fails, shows an error with a
    Retry button, rather than asking again by itself.
*/
export class CodeView
{
    constructor (ctx, { timeoutMs = codeGenerationTimeoutMs } = {})
    {
        this.ctx = ctx;
        this.disposer = new Disposer();
        this.isActive = false;
        this.timeoutMs = timeoutMs;
        this.cache = new Map();         // target -> { build, text, error }
        this.inFlight = new Map();      // target -> the build it's generating for
        this.selected = null;

        this.targetSelect = el ("select", { class: "cp-select", "aria-label": "Code generation target", onchange: () => this.select (this.targetSelect.value) });
        this.copyButton = el ("button", { class: "cp-button", onclick: () => this.copy() }, "Copy");
        this.openButton = el ("button", { class: "cp-button", hidden: ! ctx.bridge.canOpenTextDocuments, onclick: () => this.openInEditor() }, "Open in editor");
        this.toolbar = el ("div", { class: "cp-tab-tools" }, this.targetSelect, this.copyButton, this.openButton);

        this.text = el ("textarea", { class: "cp-code", readOnly: true, wrap: "off", spellcheck: false, "aria-label": "Generated code" });
        this.messageText = el ("span");
        this.retryButton = el ("button", { class: "cp-button", hidden: true, onclick: () => this.retry() }, "Retry");
        this.message = el ("div", { class: "cp-empty" }, this.messageText, " ", this.retryButton);
        this.element = el ("div", { class: "cp-code-view" }, this.text, this.message);

        // (each patch remembers its own target)
        this.disposer.add (ctx.store.subscribe (["patchSettings"], () => { this.selected = ctx.settings.patch.get ("codeTarget", null); this.update(); }, true));
        this.disposer.add (ctx.store.subscribe (["codeGenTargets", "loaded", "buildNumber", "lastBuildTime"], () => this.update()));
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    setActive (isActive)
    {
        this.isActive = isActive;
        this.openButton.hidden = ! this.ctx.bridge.canOpenTextDocuments;
        this.update();
    }

    getTargets()
    {
        return (this.ctx.store.get ("codeGenTargets") ?? []).filter (t => t !== "graph");
    }

    update()
    {
        const targets = this.getTargets();

        if (this.targetSelect.options.length !== targets.length || [...this.targetSelect.options].some ((o, i) => o.value !== targets[i]))
            this.targetSelect.replaceChildren (...targets.map (t => el ("option", { value: t }, targetNames[t] ?? t)));

        if (! targets.includes (this.selected))
            this.selected = targets[0] ?? null;

        this.targetSelect.value = this.selected ?? "";
        this.targetSelect.disabled = targets.length === 0;

        if (! this.ctx.store.get ("loaded") || ! this.selected)
        {
            this.show (null, this.ctx.store.get ("loaded") ? "No code generation targets are available" : "No patch loaded");
            return;
        }

        if (! this.isActive)
            return;

        const build = this.getBuild();
        const cached = this.cache.get (this.selected);

        if (cached?.build === build)
        {
            if (cached.error)
                this.show (null, cached.error, true);
            else
                this.show (cached.text, cached.text ? null : "No code was generated");
        }
        else if (this.inFlight.get (this.selected) === build)
        {
            this.showGenerating (this.selected);
        }
        else
        {
            this.generate (this.selected, build);
        }
    }

    /// Identifies the current build: the server's build number (or, from an older server
    /// without one, the time its build log last changed)
    getBuild()
    {
        return this.ctx.store.get ("buildNumber") ?? this.ctx.store.get ("lastBuildTime");
    }

    select (target)
    {
        this.selected = target;
        this.ctx.settings.patch.set ("codeTarget", target);
        this.update();
    }

    retry()
    {
        this.cache.delete (this.selected);
        this.update();
    }

    async generate (target, build)
    {
        this.inFlight.set (target, build);
        this.showGenerating (target);
        let result;

        try
        {
            const reply = await withTimeout (this.ctx.session.requestGeneratedCode (target, {}), this.timeoutMs);

            if (reply === timedOut)
            {
                result = { build, error: `The server didn't generate the ${targetNames[target] ?? target} code in time.` };
            }
            else
            {
                let text = reply?.code ?? "";

                if (! text && reply?.messages)
                    text = getMessageListAsString (reply.messages);

                result = { build, text };
            }
        }
        catch (e)
        {
            result = { build, error: "Code generation failed: " + (e?.message ?? e) };
        }

        // (a request for a newer build has replaced it meanwhile)
        if (this.inFlight.get (target) !== build)
            return;

        this.inFlight.delete (target);
        this.cache.set (target, result);

        if (this.selected === target)
            this.update();
    }

    showGenerating (target)
    {
        this.show (null, `Generating ${targetNames[target] ?? target}…`);
    }

    show (text, message = null, canRetry = false)
    {
        this.text.hidden = text === null;

        if (text !== null && this.text.value !== text)
        {
            this.text.value = text;
            this.text.scrollTop = 0;
        }

        this.message.hidden = ! message;
        this.messageText.textContent = message ?? "";
        this.retryButton.hidden = ! canRetry;
        this.copyButton.disabled = ! text;
        this.openButton.disabled = ! text;
    }

    copy()
    {
        if (this.text.value)
            this.ctx.bridge.writeClipboard (this.text.value);
    }

    openInEditor()
    {
        if (this.text.value)
            this.ctx.bridge.openTextDocument (this.text.value, vscodeLanguages[this.selected]);
    }
}

//==============================================================================
const timedOut = Symbol ("timedOut");

/// Resolves to the promise's value, or to timedOut if it takes longer than timeoutMs
/// (sendMessageToServerWithReply() never rejects: a lost reply would wait for ever)
function withTimeout (promise, timeoutMs)
{
    let timer;
    const timeout = new Promise (resolve => timer = setTimeout (() => resolve (timedOut), timeoutMs));
    return Promise.race ([promise, timeout]).finally (() => clearTimeout (timer));
}

/// Turns the messages from a code generation reply (one, or a list) into text
export function getMessageListAsString (messages)
{
    if (Array.isArray (messages))
        return messages.map (getMessageListAsString).join ("\n");

    let description = messages?.fullDescription ?? messages?.message ?? "";

    if (messages?.annotatedLine)
        description += "\n" + messages.annotatedLine;

    return description;
}
