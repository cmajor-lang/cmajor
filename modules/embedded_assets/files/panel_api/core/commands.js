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

import { fuzzyMatch } from "./fuzzy.js";

/*
    The panel's commands: everything the command palette can find, and the one place the
    keyboard shortcuts are defined (the panel's keydown handler looks them up here, so the
    palette shows exactly the shortcuts that work).

    A command is:
        { id, title, category, run(),
          shortcut?   e.g. "Space", "R", "1", "/", "Mod+K" (Mod is Cmd on a Mac, Ctrl elsewhere)
          keywords?   extra words to match
          detail?     a muted second line, e.g. an endpoint's ID
          isEnabled?  () => bool; disabled commands aren't offered, and their shortcuts do nothing
          kind?       "action" (default) | "endpoint" | "parameter" | "view" | "patch" }

    register (command) adds one, and returns a function that removes it. addProvider (fn) adds a
    function that returns a list of commands when asked (for the ones that come from the
    patch, like its endpoints), and returns a function that removes it.
*/
export class CommandRegistry
{
    constructor ({ isMac = isMacPlatform() } = {})
    {
        this.isMac = isMac;
        this.commands = new Map();
        this.providers = new Set();
        this.recent = [];
    }

    register (command)
    {
        this.commands.set (command.id, command);
        return () => { if (this.commands.get (command.id) === command) this.commands.delete (command.id); };
    }

    registerAll (commands)
    {
        const removers = commands.map (c => this.register (c));
        return () => removers.forEach (r => r());
    }

    addProvider (provider)
    {
        this.providers.add (provider);
        return () => this.providers.delete (provider);
    }

    get (id)                { return this.commands.get (id) ?? this.getAll().find (c => c.id === id); }

    /// All the enabled commands: the registered ones, then the providers'
    getAll()
    {
        const all = [...this.commands.values()];

        for (const provider of this.providers)
        {
            try { all.push (...(provider() ?? [])); }
            catch (e) { console.error (e); }
        }

        return all.filter (isEnabled);
    }

    /// Runs a command (by id, or the command itself) if it's enabled; returns true if it ran
    run (commandOrID)
    {
        const command = typeof commandOrID === "string" ? this.get (commandOrID) : commandOrID;

        if (! command || ! isEnabled (command))
            return false;

        this.noteUsed (command.id);
        command.run();
        return true;
    }

    noteUsed (id)
    {
        this.recent = [id, ...this.recent.filter (r => r !== id)].slice (0, maxRecent);
    }

    /// The enabled command whose shortcut is this key event, if any
    findShortcut (event)
    {
        for (const command of this.getAll())
            if (command.shortcut && shortcutMatches (command.shortcut, event, this.isMac))
                return command;

        return null;
    }

    /// The shortcut as it should be shown, e.g. "⌘K" on a Mac, "Ctrl+K" elsewhere
    formatShortcut (shortcut)
    {
        return formatShortcut (shortcut, this.isMac);
    }

    /** The commands that match the query, best first: [{ command, score, indices }], where
        indices are the matched character positions in command.title. An empty query gives the
        recently used commands first, then the rest in order.

        A query starting with "@" only searches endpoints and parameters. */
    search (query, { limit = 100 } = {})
    {
        query = String (query ?? "");
        let commands = this.getAll();

        if (query.startsWith ("@"))
        {
            query = query.substring (1);
            commands = commands.filter (c => c.kind === "endpoint" || c.kind === "parameter");
        }

        const recency = (id) => { const i = this.recent.indexOf (id); return i < 0 ? 0 : (maxRecent - i) * recencyBonus; };
        const results = [];

        for (const command of commands)
        {
            const match = matchCommand (query, command);

            if (match)
                results.push ({ command, score: match.score + recency (command.id), indices: match.indices });
        }

        if (query.trim() === "")
            results.sort ((a, b) => b.score - a.score);   // (stable, so the rest keep their order)
        else
            results.sort ((a, b) => b.score - a.score || a.command.title.localeCompare (b.command.title));

        return results.slice (0, limit);
    }
}

const maxRecent = 8;
const recencyBonus = 0.75;

function isEnabled (command)
{
    try { return command.isEnabled ? !! command.isEnabled() : true; }
    catch (e) { return false; }
}

/// The match of the query against a command's title or, when the title doesn't match, the best
/// (scoring less) against its category and keywords. Only title matches give highlight indices.
/// (A category match mustn't outrank a title match: "graph" would put every command in the
/// Graph category ahead of "Show the graph".)
function matchCommand (query, command)
{
    const title = fuzzyMatch (query, command.title);

    if (title)
        return { score: title.score, indices: title.indices };

    let best = null;

    const others = [command.category, ...(command.keywords ?? []), command.detail].filter (Boolean);

    for (const text of [...others, `${command.category ?? ""} ${command.title}`])
    {
        const match = fuzzyMatch (query, text);

        if (match && (! best || match.score * 0.8 > best.score))
            best = { score: match.score * 0.8, indices: [] };
    }

    return best;
}

//==============================================================================
function isMacPlatform()
{
    const platform = globalThis.navigator?.userAgentData?.platform ?? globalThis.navigator?.platform ?? "";
    return /mac|iphone|ipad/i.test (platform);
}

/// Splits "Mod+Shift+K" into { mod, shift, alt, key }
export function parseShortcut (shortcut)
{
    const parts = String (shortcut).split ("+");
    const key = parts.pop();
    const mods = new Set (parts.map (p => p.toLowerCase()));
    return { mod: mods.has ("mod"), shift: mods.has ("shift"), alt: mods.has ("alt"), key: key === "Space" ? " " : key.toLowerCase() };
}

export function shortcutMatches (shortcut, event, isMac)
{
    const s = parseShortcut (shortcut);
    const mod = isMac ? event.metaKey : event.ctrlKey;
    const otherMod = isMac ? event.ctrlKey : event.metaKey;

    if (s.mod !== !! mod || otherMod || s.alt !== !! event.altKey)
        return false;

    // A shift without a Mod is part of a character (e.g. "/" is Shift+7 on some layouts), and the
    // single-key shortcuts have always ignored Shift, so only check it when there's a Mod.
    if (s.mod && s.shift !== !! event.shiftKey)
        return false;

    return String (event.key ?? "").toLowerCase() === s.key;
}

export function formatShortcut (shortcut, isMac)
{
    if (! shortcut)
        return "";

    const s = parseShortcut (shortcut);
    const key = getKeyName (s.key);

    if (isMac)
        return (s.mod ? "⌘" : "") + (s.alt ? "⌥" : "") + (s.shift && s.mod ? "⇧" : "") + key;

    return [s.mod && "Ctrl", s.alt && "Alt", s.shift && s.mod && "Shift", key].filter (Boolean).join ("+");
}

/// A parsed shortcut's key as it's shown: "K", "Space", "Home"
function getKeyName (key)
{
    if (key === " ")
        return "Space";

    return key.length > 1 ? key[0].toUpperCase() + key.substring (1) : key.toUpperCase();
}

/// The value for an aria-keyshortcuts attribute
export function ariaShortcut (shortcut, isMac)
{
    const s = parseShortcut (shortcut);
    const key = getKeyName (s.key);
    return [s.mod && (isMac ? "Meta" : "Control"), s.alt && "Alt", s.shift && s.mod && "Shift", key].filter (Boolean).join ("+");
}
