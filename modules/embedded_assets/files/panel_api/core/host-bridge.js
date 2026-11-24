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
    The HostBridge is the only place in the panel that knows whether it's running inside the
    VS Code extension or in a plain browser.

    In VS Code, the panel is in an iframe inside the extension's webview. The runner page
    (embedded_patch_runner_template.html) sets window.sendMessageToVSCode when the first
    { messageFromVScode } arrives, and passes each message to the panel's
    handleMessageFromVSCode(), which hands it to handleMessage() here first.

    Messages the extension understands (see tools/vscode/cmajor/src/cmaj_patch_runner_panel.js):
      showSourceFile, openURLInNewWindow, showErrorAlert, openTextDocument (+ language),
      writeClipboard, readClipboard, newServerStatus, serverFailedWithInfiniteLoop, saveFile
    Messages it sends: patchToLoad, unload, clipboardText, theme, saveFileResult

    { keyDown: { key, code, keyCode, shiftKey, altKey, ctrlKey, metaKey, repeat } } (see
    forwardUnusedKeys()) never reaches the extension: the webview page replays it as a keydown on
    itself, for VS Code's keybindings.

    saveFile (see HostBridge.saveFile) sends the file as one or more messages:
      { saveFile: { requestID, suggestedName, mimeType, size, chunkIndex, numChunks, dataBase64 } }
    with chunkIndex 0 .. numChunks-1, each holding the base64 of the next saveFileChunkSize bytes
    (a file of up to that size is a single message). Once it has every chunk, the extension shows
    its save dialog, writes the file, and replies
      { saveFileResult: { requestID, saved, path?, cancelled?, error? } }
*/

/// How many bytes of a file each saveFile message carries (base64 makes the message 4/3 as big)
export const saveFileChunkSize = 6 * 1024 * 1024;

/// How long saveFile waits for VS Code's answer (its save dialog may stay open a long time)
export const saveFileTimeoutMs = 10 * 60 * 1000;

/// How long a browser download's object URL is kept before it's revoked
export const downloadURLLifetimeMs = 60 * 1000;

export class HostBridge
{
    constructor (hostWindow = window)
    {
        this.window = hostWindow;
        this.themeListeners = new Set();
        this.pendingClipboardReads = [];
        this.pendingSaves = new Map();
        this.nextSaveID = 1;
        this.theme = null;
    }

    /// True when the panel is running inside VS Code (once VS Code has said hello)
    get isVSCode()                      { return typeof this.window.sendMessageToVSCode === "function"; }

    send (message)
    {
        if (this.isVSCode)
        {
            this.window.sendMessageToVSCode (message);
            return true;
        }

        return false;
    }

    //==============================================================================
    /// Opens a "file:line:column" location, in VS Code's editor if possible
    openSourceFile (location)
    {
        if (! location)
            return;

        if (! this.send ({ showSourceFile: String (location) }))
        {
            // a browser will probably block this, but there's nothing better to do..
            const file = String (location).replace (/(:\d+)*:?\s*$/, "");
            this.window.open ("file://" + file, "_blank");
        }
    }

    openURL (url)
    {
        if (! this.send ({ openURLInNewWindow: url }))
            this.window.open (url, "_blank", "noopener");
    }

    /// Opens some text (e.g. generated code) in an editor. Returns false if the host can't.
    openTextDocument (content, language)
    {
        return this.send ({ openTextDocument: content, language });
    }

    get canOpenTextDocuments()          { return this.isVSCode; }

    showErrorAlert (message)
    {
        if (! this.send ({ showErrorAlert: String (message) }))
            this.window.alert (message);
    }

    async writeClipboard (text)
    {
        if (this.send ({ writeClipboard: text }))
            return true;

        try
        {
            await this.window.navigator.clipboard.writeText (text);
            return true;
        }
        catch (e)
        {
            return false;
        }
    }

    /// Returns the clipboard's text, or undefined if it can't be read
    async readClipboard()
    {
        if (this.isVSCode)
        {
            return new Promise (resolve =>
            {
                this.pendingClipboardReads.push (resolve);
                this.send ({ readClipboard: true });

                // (in case the extension never answers)
                setTimeout (() =>
                {
                    const i = this.pendingClipboardReads.indexOf (resolve);

                    if (i >= 0)
                    {
                        this.pendingClipboardReads.splice (i, 1);
                        resolve (undefined);
                    }
                }, 5000);
            });
        }

        try
        {
            return await this.window.navigator.clipboard.readText();
        }
        catch (e)
        {
            return undefined;
        }
    }

    //==============================================================================
    /**
        Saves a file, letting the user choose where.
          suggestedName  the file name to offer, e.g. "Tremolo.wav" (its extension picks VS Code's file-type filter)
          data           ArrayBuffer | Uint8Array (or other ArrayBufferView) | Blob
          mimeType       e.g. "audio/wav" (default "application/octet-stream")
        Resolves (it never rejects) to { saved, path?, cancelled?, error? }:
          - in VS Code: the extension's save dialog, then { saved: true, path } or { saved: false, cancelled: true },
            or { saved: false, error } (a write failure, or no answer in saveFileTimeoutMs);
          - in a browser: a download through an <a download> link, then { saved: true } with no
            path (where it goes, and whether the user cancelled, is the browser's business).
        Several saves may be in progress at once.
    */
    async saveFile ({ suggestedName = "untitled", data, mimeType = "application/octet-stream" } = {})
    {
        try
        {
            if (this.isVSCode)
                return await this.saveFileInVSCode (String (suggestedName), await getBytes (data), mimeType);

            return this.downloadFile (String (suggestedName), data, mimeType);
        }
        catch (e)
        {
            return { saved: false, error: String (e?.message ?? e) };
        }
    }

    /// @internal
    async saveFileInVSCode (suggestedName, bytes, mimeType)
    {
        const requestID = `save-${this.nextSaveID++}-${Date.now()}`;
        const numChunks = Math.max (1, Math.ceil (bytes.length / saveFileChunkSize));

        const result = new Promise (resolve =>
        {
            const timer = setTimeout (() =>
            {
                this.pendingSaves.delete (requestID);
                resolve ({ saved: false, error: "VS Code didn't answer the request to save the file" });
            }, saveFileTimeoutMs);

            this.pendingSaves.set (requestID, { resolve, timer });
        });

        for (let chunkIndex = 0; chunkIndex < numChunks; ++chunkIndex)
        {
            // (yielding between chunks keeps the panel responsive while a big file is sent)
            if (chunkIndex > 0)
                await new Promise (r => setTimeout (r, 0));

            if (! this.pendingSaves.has (requestID))
                break;

            const chunk = bytes.subarray (chunkIndex * saveFileChunkSize, (chunkIndex + 1) * saveFileChunkSize);

            this.send ({ saveFile: { requestID, suggestedName, mimeType, size: bytes.length,
                                     chunkIndex, numChunks, dataBase64: bytesToBase64 (chunk) } });
        }

        return result;
    }

    /// @internal
    downloadFile (suggestedName, data, mimeType)
    {
        const doc = this.window.document;
        const URLs = this.window.URL;
        const blob = data instanceof Blob ? data : new Blob ([data], { type: mimeType });
        const url = URLs.createObjectURL (blob);
        const link = doc.createElement ("a");

        link.href = url;
        link.download = suggestedName;
        link.style.display = "none";
        doc.body.appendChild (link);
        link.click();
        link.remove();

        // (revoking it straight away can cancel the download in some browsers)
        setTimeout (() => URLs.revokeObjectURL (url), downloadURLLifetimeMs);
        return { saved: true };
    }

    /// In VS Code, sends the host the keys the panel didn't use, so that its keybindings (the
    /// command palette, switching editors, etc.) still work while the panel has the focus: the
    /// panel is in a cross-origin iframe, so VS Code can't see its keys otherwise. That's any
    /// key with Cmd/Ctrl, and the function keys, that nothing in the panel prevented the default
    /// of, except the editing keys (copy, paste, undo, select all...), which belong to whatever
    /// has the focus here. Listens on the window, so it sees each key after the panel's own
    /// handlers. Returns a function that stops it.
    forwardUnusedKeys()
    {
        const listener = (e) =>
        {
            if (this.isVSCode && ! e.defaultPrevented && shouldForwardKey (e))
                this.send ({ keyDown: { key: e.key, code: e.code, keyCode: e.keyCode, shiftKey: e.shiftKey, altKey: e.altKey,
                                        ctrlKey: e.ctrlKey, metaKey: e.metaKey, repeat: e.repeat } });
        };

        this.window.addEventListener ("keydown", listener);
        return () => this.window.removeEventListener ("keydown", listener);
    }

    /// Tells the host about a new session status (VS Code uses it for the tab title and its
    /// Problems list)
    notifyStatus (status)
    {
        this.send ({ newServerStatus: status });
    }

    notifyInfiniteLoop()
    {
        this.send ({ serverFailedWithInfiniteLoop: true });
    }

    //==============================================================================
    /// Theme colours from the host, as an object of --vscode-* names to values, or null when
    /// the host doesn't provide any (then the panel follows prefers-color-scheme)
    getTheme()                          { return this.theme; }

    addThemeListener (listener)
    {
        this.themeListeners.add (listener);
        return () => this.themeListeners.delete (listener);
    }

    /// Handles a message from VS Code. Returns true if the bridge dealt with it, or false if
    /// it's for the panel.
    handleMessage (message)
    {
        if (message?.clipboardText !== undefined)
        {
            const readers = this.pendingClipboardReads;
            this.pendingClipboardReads = [];

            for (const resolve of readers)
                resolve (message.clipboardText);

            return readers.length > 0;
        }

        if (message?.theme)
        {
            this.theme = message.theme;

            for (const listener of [...this.themeListeners])
                listener (this.theme);

            return true;
        }

        if (message?.saveFileResult)
        {
            const reply = message.saveFileResult;
            const pending = this.pendingSaves.get (reply.requestID);

            if (! pending)
                return false;

            this.pendingSaves.delete (reply.requestID);
            clearTimeout (pending.timer);

            const result = { saved: !! reply.saved };

            if (reply.path !== undefined)       result.path = reply.path;
            if (reply.cancelled)                result.cancelled = true;
            if (reply.error !== undefined)      result.error = String (reply.error);

            pending.resolve (result);
            return true;
        }

        return false;
    }
}

//==============================================================================
const editingKeys = new Set (["a", "c", "v", "x", "z", "y"]);

/// Whether HostBridge.forwardUnusedKeys() passes a keydown on to the host
export function shouldForwardKey (e)
{
    if (/^F\d{1,2}$/.test (e.key ?? ""))
        return true;

    if (! (e.ctrlKey || e.metaKey))
        return false;

    if (["Control", "Meta", "Shift", "Alt"].includes (e.key))
        return false;

    return e.altKey || ! editingKeys.has (String (e.key).toLowerCase());
}

async function getBytes (data)
{
    if (data instanceof Uint8Array)         return data;
    if (data instanceof ArrayBuffer)        return new Uint8Array (data);
    if (ArrayBuffer.isView (data))          return new Uint8Array (data.buffer, data.byteOffset, data.byteLength);
    if (data instanceof Blob)               return new Uint8Array (await data.arrayBuffer());

    throw new Error ("saveFile needs its data as an ArrayBuffer, a typed array or a Blob");
}

/// Base64 of some bytes (Uint8Array.toBase64 where the browser has it)
export function bytesToBase64 (bytes)
{
    if (typeof bytes.toBase64 === "function")
        return bytes.toBase64();

    const pieces = [];

    for (let i = 0; i < bytes.length; i += 0x8000)
        pieces.push (String.fromCharCode.apply (null, bytes.subarray (i, i + 0x8000)));

    return btoa (pieces.join (""));
}
