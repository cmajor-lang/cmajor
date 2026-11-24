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
    A tiny observable store. The state is a flat object; set() merges changes into it and tells
    the subscribers which keys changed (compared with ===, so give it new objects rather than
    mutating old ones).

        const store = new Store ({ loaded: false });
        const unsubscribe = store.subscribe (["loaded"], (state) => ...);
        store.set ({ loaded: true });

    applySessionStatus() turns a server session status into store keys, keeping the previous
    objects where nothing has changed, so that e.g. the endpoint list only counts as changed
    when the endpoints really did change, rather than on every status message.
*/
export class Store
{
    constructor (initialState = {})
    {
        this.state = { ...initialState };
        this.subscribers = new Set();
        this.batchDepth = 0;
        this.batchedKeys = null;
    }

    get (key)                       { return this.state[key]; }

    /// Merges some changes into the state, and notifies the subscribers that are interested
    /// in any of the keys that changed (at the end of the batch, inside batch()). Returns the
    /// list of changed keys.
    set (changes)
    {
        const changed = [];

        for (const [key, value] of Object.entries (changes))
        {
            if (this.state[key] !== value)
            {
                this.state = { ...this.state, [key]: value };
                changed.push (key);
            }
        }

        if (changed.length > 0)
        {
            if (this.batchDepth > 0)
                changed.forEach (k => this.batchedKeys.add (k));
            else
                this.notify (new Set (changed));
        }

        return changed;
    }

    /// Runs fn(), holding back notifications until it has finished, so that subscribers are
    /// told once about everything it changed (e.g. a new connection and its endpoints together)
    batch (fn)
    {
        if (this.batchDepth++ === 0)
            this.batchedKeys = new Set();

        try
        {
            return fn();
        }
        finally
        {
            if (--this.batchDepth === 0)
            {
                const keys = this.batchedKeys;
                this.batchedKeys = null;

                if (keys.size > 0)
                    this.notify (keys);
            }
        }
    }

    notify (changedSet)
    {
        for (const s of [...this.subscribers])
            if (this.subscribers.has (s) && (! s.keys || s.keys.some (k => changedSet.has (k))))
                s.listener (this.state, changedSet);
    }

    /// keys is an array of the keys to be told about (or null for all of them). If callNow is
    /// true, the listener is also called straight away.
    subscribe (keys, listener, callNow = false)
    {
        const subscriber = { keys: keys ?? null, listener };
        this.subscribers.add (subscriber);

        if (callNow)
            listener (this.state, new Set (keys ?? Object.keys (this.state)));

        return () => this.subscribers.delete (subscriber);
    }
}

//==============================================================================
/// Returns a deep equality check suitable for JSON-like data
export function isEqualJSON (a, b)
{
    if (a === b)
        return true;

    if (typeof a !== "object" || typeof b !== "object" || a === null || b === null)
        return false;

    if (Array.isArray (a) !== Array.isArray (b))
        return false;

    const keysA = Object.keys (a), keysB = Object.keys (b);

    if (keysA.length !== keysB.length)
        return false;

    for (const k of keysA)
        if (! isEqualJSON (a[k], b[k]))
            return false;

    return true;
}

/// Returns the new value, or the old one if they're equal, so unchanged data keeps its identity
export function keepIfEqual (oldValue, newValue)
{
    return isEqualJSON (oldValue, newValue) ? oldValue : newValue;
}

/// True if a status that isn't "loaded" is only temporary: the patch is rebuilding, has a build
/// error, or the server can't be reached at the moment
export function isTransientStatus (status)
{
    return ! status?.connected || !! status?.error || (status?.status ?? "").startsWith ("Loading");
}

/// The keys that applySessionStatus() maintains
export const sessionStatusKeys = ["connected", "loaded", "playing", "statusText", "error", "buildLog", "lastBuildTime", "buildNumber",
                                  "manifest", "manifestFile", "mainProcessor", "mainProcessorLocation",
                                  "inputs", "outputs", "codeGenTargets", "graphVersion", "httpRootURL", "rawStatus"];

/// Updates a store from a session status message (see ServerSession.addStatusListener())
export function applySessionStatus (store, status)
{
    const s = store.state;
    const details = status?.details;
    const buildLog = status?.log ?? s.buildLog ?? "";

    // while the same patch rebuilds (or has failed to), or the server is briefly unreachable
    // (e.g. busy with a long build), keep its last endpoints, so that the widgets showing them
    // survive until it's back
    const isRebuilding = isTransientStatus (status);
    const getEndpoints = (list, previous) => status?.loaded ? (list ?? []) : (isRebuilding ? (previous ?? []) : []);

    return store.set ({
        connected:              !! status?.connected,
        loaded:                 !! status?.loaded,
        playing:                !! status?.playing,
        statusText:             status?.status ?? "",
        error:                  status?.error ?? "",
        buildLog,
        lastBuildTime:          (status?.log && status.log !== s.buildLog) ? Date.now() : (s.lastBuildTime ?? null),
        // (from the server: goes up with every build, good or bad, even when nothing else changes)
        buildNumber:            status?.buildNumber ?? s.buildNumber ?? null,
        manifest:               keepIfEqual (s.manifest, status?.manifest ?? (isRebuilding ? s.manifest ?? null : null)),
        manifestFile:           status?.manifestFile ?? s.manifestFile ?? "",
        mainProcessor:          details?.mainProcessor ?? ((status?.loaded || isRebuilding) ? s.mainProcessor : "") ?? "",
        mainProcessorLocation:  details?.mainProcessorLocation ?? ((status?.loaded || isRebuilding) ? s.mainProcessorLocation : "") ?? "",
        inputs:                 keepIfEqual (s.inputs, getEndpoints (details?.inputs, s.inputs)),
        outputs:                keepIfEqual (s.outputs, getEndpoints (details?.outputs, s.outputs)),
        codeGenTargets:         keepIfEqual (s.codeGenTargets, status?.codeGenTargets ?? []),
        graphVersion:           status?.graphVersion ?? (isRebuilding ? s.graphVersion ?? "" : ""),
        httpRootURL:            status?.httpRootURL ?? s.httpRootURL ?? "",
        rawStatus:              status ?? null,
    });
}
