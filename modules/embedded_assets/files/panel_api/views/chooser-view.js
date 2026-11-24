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
    The landing page when `cmaj server` is opened in a browser without a fixed patch: the
    patches the server knows about, and a drop zone for a patch folder (whose files are then
    served to the server from the browser).
*/
export class ChooserView
{
    constructor (ctx)
    {
        this.ctx = ctx;
        this.disposer = new Disposer();

        this.list = el ("div", { class: "cp-chooser-list", role: "list" });
        this.dropZone = el ("div", { class: "cp-drop-zone" },
                            el ("strong", null, "Drop a patch folder here"),
                            el ("span", null, "(a folder containing a .cmajorpatch file)"));
        this.element = el ("div", { class: "cp-chooser", hidden: true },
                           el ("h2", null, "Choose a patch"), this.list, this.dropZone);

        this.disposer.add (ctx.store.subscribe (["availablePatches", "connected"], () => this.updateList(), true));
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    updateList()
    {
        const patches = this.ctx.store.get ("availablePatches") ?? [];

        if (patches.length === 0)
        {
            this.list.replaceChildren (el ("p", { class: "cp-empty" }, this.ctx.store.get ("connected") ? "The server has no patches in its folders" : "Connecting…"));
            return;
        }

        this.list.replaceChildren (...patches.map (manifest =>
            el ("button", { class: "cp-chooser-item", role: "listitem", onclick: () => this.ctx.actions.loadPatch (manifest.manifestFile) },
                el ("strong", null, manifest.name || manifest.manifestFile),
                manifest.description ? el ("span", null, manifest.description) : null,
                el ("small", null, manifest.manifestFile))));
    }
}

//==============================================================================
/// Reads the files from a drop of a patch folder, registers them with the session, and
/// returns the path of the .cmajorpatch to load (or null)
export async function registerDroppedPatchFiles (session, dataTransfer, maxNumItems = 200)
{
    const entries = [];
    let tooMany = false;

    const addEntry = async (entry) =>
    {
        if (tooMany)
            return;

        if (entry.isDirectory)
        {
            const reader = entry.createReader();

            for (;;)
            {
                const batch = await new Promise ((resolve, reject) => reader.readEntries (resolve, reject));

                if (batch.length === 0)
                    break;

                for (const e of batch)
                    await addEntry (e);
            }
        }
        else if (entry.isFile)
        {
            if (entries.length < maxNumItems)
                entries.push (entry);
            else
                tooMany = true;
        }
    };

    for (const item of dataTransfer?.items ?? [])
    {
        const entry = item.getAsEntry?.() || item.webkitGetAsEntry?.();

        if (entry)
            await addEntry (entry);
    }

    if (tooMany || entries.length === 0)
        return null;

    const files = await Promise.all (entries.map (entry => new Promise ((resolve, reject) =>
        entry.file (f => f.arrayBuffer().then (content => resolve ({ entry, size: content.byteLength, content }), reject), reject))));

    const manifest = files.find (f => f.entry.fullPath.endsWith (".cmajorpatch"));

    if (! manifest)
        return null;

    const prefix = "/uploaded-session-files";

    for (const f of files)
        session.registerFile (prefix + f.entry.fullPath, {
            size: f.size,
            read: (offset, numBytes) => new Blob ([f.content.slice (offset, offset + numBytes)])
        });

    return prefix + manifest.entry.fullPath;
}
