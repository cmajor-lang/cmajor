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
    Build problems. Until the server sends structured diagnostics, they're parsed from the
    status's error text, which has lines like:

        /path/File.cmajor:12:5: error: Cannot find symbol 'x'
            out <- x;
                   ^
        /path/File.cmajorpatch: error: something without a position

    Each diagnostic's following indented lines (the annotated source line and caret) are kept
    with it. (This uses the same patterns as the VS Code extension's refreshDiagnostics().)
*/
const withPosition = /^(.*?):(\d+):(\d+):\s+(error|warning|note):\s+(.*)$/;
const withoutPosition = /^(.*?):\s+(error|warning|note):\s+(.*)$/;

export function parseDiagnostics (errorText)
{
    const diagnostics = [];

    if (! errorText)
        return diagnostics;

    for (const line of String (errorText).split ("\n"))
    {
        let m = withPosition.exec (line);

        if (m)
        {
            diagnostics.push ({ file: m[1], line: Number (m[2]), column: Number (m[3]), severity: m[4], message: m[5].trim(), context: [] });
            continue;
        }

        m = withoutPosition.exec (line);

        if (m && ! m[1].includes (" "))
        {
            diagnostics.push ({ file: m[1], line: 0, column: 0, severity: m[2], message: m[3].trim(), context: [] });
            continue;
        }

        if (line.trim() === "")
            continue;

        if (diagnostics.length > 0)
            diagnostics.at (-1).context.push (line);
        else
            diagnostics.push ({ file: "", line: 0, column: 0, severity: "error", message: line.trim(), context: [] });
    }

    return diagnostics;
}

export function getDiagnosticLocation (d)
{
    return d.file ? (d.line > 0 ? `${d.file}:${d.line}:${d.column}` : d.file) : "";
}

export function countErrors (diagnostics)
{
    return diagnostics.filter (d => d.severity === "error").length;
}

//==============================================================================
/// The Problems tab in the drawer
export class ProblemsView
{
    constructor (ctx)
    {
        this.ctx = ctx;
        this.disposer = new Disposer();
        this.list = el ("ul", { class: "cp-problems", "aria-label": "Problems" });
        this.element = el ("div", { class: "cp-problems-view" }, this.list);
        this.toolbar = el ("div", { class: "cp-tab-tools" },
                           el ("button", { class: "cp-button", title: "Copy the full error text",
                                           onclick: () => ctx.bridge.writeClipboard (ctx.store.get ("error") ?? "") }, "Copy"));

        this.disposer.add (ctx.store.subscribe (["diagnostics", "loaded", "lastBuildTime"], () => this.update(), true));
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    setActive() {}

    update()
    {
        const diagnostics = this.ctx.store.get ("diagnostics") ?? [];
        this.toolbar.hidden = diagnostics.length === 0;

        if (diagnostics.length === 0)
        {
            const time = this.ctx.store.get ("lastBuildTime");
            this.list.replaceChildren (el ("li", { class: "cp-problem cp-problem-none" },
                                           this.ctx.store.get ("loaded") ? `No problems${time ? " (built at " + new Date (time).toLocaleTimeString() + ")" : ""}`
                                                                         : "No problems"));
            return;
        }

        this.list.replaceChildren (...diagnostics.map (d =>
        {
            const location = getDiagnosticLocation (d);
            const fileName = d.file ? d.file.split (/[\\/]/).pop() + (d.line > 0 ? `:${d.line}:${d.column}` : "") : "";

            return el ("li", { class: ["cp-problem", `cp-problem-${d.severity}`] },
                       el ("span", { class: "cp-problem-icon", "aria-label": d.severity }, d.severity === "error" ? "✕" : d.severity === "warning" ? "!" : "i"),
                       location ? el ("button", { class: "cp-link cp-problem-location", title: location,
                                                  onclick: () => this.ctx.bridge.openSourceFile (location) }, fileName)
                                : null,
                       el ("span", { class: "cp-problem-message" }, d.message),
                       d.context.length ? el ("pre", { class: "cp-problem-context" }, d.context.join ("\n")) : null);
        }));
    }
}
