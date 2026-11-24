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

/*
    Remembers the panel's UI settings in localStorage: either per patch (keyed by the manifest
    file's path) or global. Storage can be unavailable (e.g. blocked in some iframes or private
    windows), in which case the settings just last as long as the page does.

        const prefs = new PanelSettings ("/path/to/MyPatch.cmajorpatch");
        prefs.get ("mainTab", "gui");
        prefs.set ("mainTab", "graph");
*/
const keyPrefix = "cmaj-patch-panel:";
const memoryFallback = new Map();

function getStorage()
{
    try
    {
        const storage = globalThis.localStorage;
        const testKey = keyPrefix + "test";
        storage.setItem (testKey, "1");
        storage.removeItem (testKey);
        return storage;
    }
    catch (e)
    {
        return null;
    }
}

export class PanelSettings
{
    /// scope is a manifest file path, or "" for settings that apply to all patches
    constructor (scope = "", storage = getStorage())
    {
        this.storage = storage;
        this.key = keyPrefix + (scope || "global");
        this.values = this.load();
    }

    load()
    {
        try
        {
            const text = this.storage ? this.storage.getItem (this.key) : memoryFallback.get (this.key);
            const values = text ? JSON.parse (text) : {};
            return (values && typeof values === "object" && ! Array.isArray (values)) ? values : {};
        }
        catch (e)
        {
            return {};
        }
    }

    save()
    {
        const text = JSON.stringify (this.values);

        try
        {
            if (this.storage)
                this.storage.setItem (this.key, text);
            else
                memoryFallback.set (this.key, text);
        }
        catch (e) {}    // (e.g. full)
    }

    get (name, defaultValue)
    {
        return Object.hasOwn (this.values, name) ? this.values[name] : defaultValue;
    }

    set (name, value)
    {
        if (value === undefined)
            delete this.values[name];
        else
            this.values[name] = value;

        this.save();
    }

    remove (name)           { this.set (name, undefined); }

    clear()
    {
        this.values = {};
        this.save();
    }
}

//==============================================================================
/*
    The panel's settings, in three scopes. Every setting is read and written through exactly one
    of them, chosen for what it means:

      - global:         the same for every patch (e.g. the colour theme)
      - patch:          only the loaded patch's own (e.g. the floating window's size, the graph's
                        zoom). Without a patch, get() returns the default and set() does nothing.
      - patchOrGlobal:  the patch's own, but a patch that hasn't chosen yet gets the last choice
                        made in any patch (e.g. the layout): set() writes both

    The patch scope follows setPatch(), which the panel calls when it connects to a patch. The
    panel also publishes it as the store's "patchSettings" key, so a view that reads a setting
    once (when it's set up) can read it again when the patch changes.

        const settings = new PanelSettingsScopes();
        settings.setPatch ("/a/B.cmajorpatch");
        settings.patch.get ("floatRect", null);
*/
export class PanelSettingsScopes
{
    constructor (storage = getStorage())
    {
        this.storage = storage;
        this.global = new PanelSettings ("", storage);
        this.current = null;     // the loaded patch's PanelSettings, or null

        this.patch = {
            get: (name, defaultValue) => this.current ? this.current.get (name, defaultValue) : defaultValue,
            set: (name, value) => this.current?.set (name, value),
        };

        this.patchOrGlobal = {
            get: (name, defaultValue) => this.current ? this.current.get (name, this.global.get (name, defaultValue))
                                                      : this.global.get (name, defaultValue),
            set: (name, value) => { this.current?.set (name, value); this.global.set (name, value); },
        };
    }

    /// Makes a patch's settings the current patch scope (or none, for ""/null), and returns them
    setPatch (manifestFile)
    {
        this.current = manifestFile ? new PanelSettings (manifestFile, this.storage) : null;
        return this.current;
    }

    /// The settings of any patch: the current patch's own object if it's that one (two objects
    /// for the same patch would overwrite each other's values), otherwise a fresh one
    forPatch (manifestFile)
    {
        if (! manifestFile)
            return null;

        if (this.current && this.current.key === keyPrefix + manifestFile)
            return this.current;

        return new PanelSettings (manifestFile, this.storage);
    }
}
