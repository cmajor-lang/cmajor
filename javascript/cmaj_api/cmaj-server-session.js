//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import { PatchConnection } from "./cmaj-patch-connection.js"
import { EventListenerList } from "./cmaj-event-listener-list.js"


//==============================================================================
/*
 *  This class provides the API and manages the communication protocol between
 *  a javascript application and a Cmajor session running on some kind of server
 *  (which may be local or remote).
 *
 *  This is an abstract base class: some kind of transport layer will create a
 *  subclass of ServerSession which a client application can then use to control
 *  and interact with the server.
 */
export class ServerSession   extends EventListenerList
{
    /** A server session must be given a unique sessionID.
     * @param {string} sessionID - this must be a unique string which is safe for
     *                             use as an identifier or filename
    */
    constructor (sessionID)
    {
        super();

        this.sessionID = sessionID;
        this.activePatchConnections = new Set();
        this.status = { connected: false, loaded: false };
        this.lastServerMessageTime = Date.now();
        this.checkForServerTimer = setInterval (() => this.checkServerStillExists(), 2000);
    }

    /** Call `dispose()` when this session is no longer needed and should be released. */
    dispose()
    {
        if (this.checkForServerTimer)
        {
            clearInterval (this.checkForServerTimer);
            this.checkForServerTimer = undefined;
        }

        this.status = { connected: false, loaded: false };
    }

    //==============================================================================
    // Session status methods:

    /** Attaches a listener function which will be called when the session status changes.
     *  The listener will be called with an argument object containing lots of properties
     *  describing the state, including any errors, loaded patch manifest, etc.
     */
    addStatusListener (listener)                        { this.addEventListener    ("session_status", listener); }

    /** Removes a listener that was previously added by `addStatusListener()`
     */
    removeStatusListener (listener)                     { this.removeEventListener ("session_status", listener); }

    /** Asks the server to asynchronously send a status update message with the latest status.
     */
    requestSessionStatus()                              { this.sendMessageToServer ({ type: "req_session_status" }); }

    /** Returns the session's last known status object. */
    getCurrentStatus()                                  { return this.status; }

    //==============================================================================
    // Patch loading:

    /** Asks the server to load the specified patch into our session.
     */
    loadPatch (patchFileToLoad)
    {
        this.currentPatchLocation = patchFileToLoad;
        this.sendMessageToServer ({ type: "load_patch", file: patchFileToLoad });
    }

    /** Asynchronously returns a list of patches that it has access to.
     *  The return value is an array of manifest objects describing each of the patches.
     */
    async requestAvailablePatchList()
    {
        return await this.sendMessageToServerWithReply ({ type: "req_patchlist" });
    }

    /** Creates and returns a new PatchConnection object which can be used to control the
     *  patch that this session has loaded.
     */
    createPatchConnection()
    {
        class ServerPatchConnection  extends PatchConnection
        {
            constructor (session)
            {
                super();
                this.session = session;
                this.manifest = session.status?.manifest;
                this.session.activePatchConnections.add (this);
            }

            dispose()
            {
                this.session.activePatchConnections.delete (this);
                this.session = undefined;
            }

            sendMessageToServer (message)
            {
                this.session?.sendMessageToServer (message);
            }

            getResourceAddress (path)
            {
                if (! this.session?.status?.httpRootURL)
                    return undefined;

                return this.session.status.httpRootURL
                        + (path.startsWith ("/") ? path.substr (1) : path);
            }
        }

        return new ServerPatchConnection (this);
    }

    //==============================================================================
    // Audio input source handling:

    /**
     *  Sets a custom audio input source for a particular endpoint.
     *
     *  When a source is changed, a callback is sent to any audio input mode listeners (see
     *  `addAudioInputModeListener()`)
     *
     *  @param {Object} endpointID
     *  @param {boolean} shouldMute - if true, the endpoint will be muted
     *  @param {Uint8Array | Array} fileDataToPlay - if this is some kind of array containing
     *  binary data that can be parsed as an audio file, then it will be sent across for the
     *  server to play as a looped input sample.
     */
    setAudioInputSource (endpointID, shouldMute, fileDataToPlay)
    {
        const loopFile = "_audio_source_" + endpointID;

        if (fileDataToPlay)
        {
            this.registerFile (loopFile,
            {
               size: fileDataToPlay.byteLength,
               read: (start, length) => { return new Blob ([fileDataToPlay.slice (start, start + length)]); }
            });

            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        file: loopFile });
        }
        else
        {
            this.removeFile (loopFile);

            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        mute: !! shouldMute });
        }
    }

    /** Attaches a listener function to be told when the input source for a particular
     *  endpoint is changed by a call to `setAudioInputSource()`.
     */
    addAudioInputModeListener (endpointID, listener)    { this.addEventListener    ("audio_input_mode_" + endpointID, listener); }

    /** Removes a listener previously added with `addAudioInputModeListener()` */
    removeAudioInputModeListener (endpointID, listener) { this.removeEventListener ("audio_input_mode_" + endpointID, listener); }

    /** Asks the server to send an update with the latest status to any audio mode listeners that
     *  are attached to the given endpoint.
     *  @param {string} endpointID
     */
    requestAudioInputMode (endpointID)                  { this.sendMessageToServer ({ type: "req_audio_input_mode", endpoint: endpointID }); }

    //==============================================================================
    // Audio device methods:

    /** Enables or disables audio playback.
     *  When playback state changes, a status update is sent to any status listeners.
     * @param {boolean} shouldBeActive
     */
    setAudioPlaybackActive (shouldBeActive)             { this.sendMessageToServer ({ type: "set_audio_playback_active", active: shouldBeActive }); }

    /** Asks the server to apply a new set of audio device properties.
     *  The properties object uses the same format as the object that is passed to the listeners
     *  (see `addAudioDevicePropertiesListener()`).
     */
    setAudioDeviceProperties (newProperties)            { this.sendMessageToServer ({ type: "set_audio_device_props", properties: newProperties }); }

    /** Attaches a listener function which will be called when the audio device properties are
     *  changed.
     *
     *  You can remove the listener when it's no longer needed with `removeAudioDevicePropertiesListener()`.
     *
     *  @param listener - this callback will receive an argument object containing all the
     *                    details about the device.
     */
    addAudioDevicePropertiesListener (listener)         { this.addEventListener    ("audio_device_properties", listener); }

    /** Removes a listener that was added with `addAudioDevicePropertiesListener()` */
    removeAudioDevicePropertiesListener (listener)      { this.removeEventListener ("audio_device_properties", listener); }

    /** Causes an asynchronous callback to any audio device listeners that are registered. */
    requestAudioDeviceProperties()                      { this.sendMessageToServer ({ type: "req_audio_device_props" }); }

    //==============================================================================
    /** Asks the server to asynchronously generate some code from the currently loaded patch.
     *
     *  @param {string} codeType - this must be one of the strings that are listed in the
     *                             status's `codeGenTargets` property. For example, "cpp"
     *                             would request a C++ version of the patch.
     *  @param {Object} [extraOptions] - this optionally provides target-specific properties.
     *  @returns an object containing the code, errors and other metadata about the patch.
     */
    async requestGeneratedCode (codeType, extraOptions)
    {
        return await this.sendMessageToServerWithReply ({ type: "req_codegen",
                                                          codeType: codeType,
                                                          options: extraOptions, });
    }

    //==============================================================================
    // File change monitoring:

    /** Attaches a listener to be told when a file change is detected in the currently-loaded
     *  patch. The function will be called with an object that gives rough details about the
     *  type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
     *  won't provide any information about exactly which files are involved.
     */
    addFileChangeListener (listener)                    { this.addEventListener    ("patch_source_changed", listener); }

    /** Removes a listener that was previously added with `addFileChangeListener()`.
     */
    removeFileChangeListener (listener)                 { this.removeEventListener ("patch_source_changed", listener); }

    //==============================================================================
    // CPU level monitoring methods:

    /** Attaches a listener function which will be sent messages containing CPU info.
     *  To remove the listener, call `removeCPUListener()`. To change the rate of these
     *  messages, use `setCPULevelUpdateRate()`.
     */
    addCPUListener (listener)                       { this.addEventListener    ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /** Removes a listener that was previously attached with `addCPUListener()`. */
    removeCPUListener (listener)                    { this.removeEventListener ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /** Changes the frequency at which CPU level update messages are sent to listeners. */
    setCPULevelUpdateRate (framesPerUpdate)         { this.cpuFramesPerUpdate = framesPerUpdate; this.updateCPULevelUpdateRate(); }

    /** Attaches a listener to be told when a file change is detected in the currently-loaded
     *  patch. The function will be called with an object that gives rough details about the
     *  type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
     *  won't provide any information about exactly which files are involved.
     */
    addInfiniteLoopListener (listener)              { this.addEventListener    ("infinite_loop_detected", listener); }

    /** Removes a listener that was previously added with `addFileChangeListener()`. */
    removeInfiniteLoopListener (listener)           { this.removeEventListener ("infinite_loop_detected", listener); }

    //==============================================================================
    /** Registers a virtual file with the server, under the given name.
     *
     *  @param {string} filename - the full path name of the file
     *  @param {Object} contentProvider - this object must have a property called `size` which is a
     *            constant size in bytes for the file, and a method `read (offset, size)` which
     *            returns an array (or UInt8Array) of bytes for the data in a given chunk of the file.
     *            The server may repeatedly call this method at any time until `removeFile()` is
     *            called to deregister the file.
     */
    registerFile (filename, contentProvider)
    {
        if (! this.files)
            this.files = new Map();

        this.files.set (filename, contentProvider);

        this.sendMessageToServer ({ type: "register_file",
                                    filename: filename,
                                    size: contentProvider.size });
    }

    /** Removes a file that was previously registered with `registerFile()`. */
    removeFile (filename)
    {
        this.sendMessageToServer ({ type: "remove_file",
                                    filename: filename });
        this.files?.delete (filename);
    }

    //==============================================================================
    // Private methods from this point...

    /** An implementation subclass must call this when the session first connects
     *  @private
     */
    handleSessionConnection()
    {
        if (! this.status.connected)
        {
            this.requestSessionStatus();
            this.requestAudioDeviceProperties();

            if (this.currentPatchLocation)
            {
                this.loadPatch (this.currentPatchLocation);
                this.currentPatchLocation = undefined;
            }
        }
    }

    /** An implementation subclass must call this when a message arrives
     *  @private
     */
    handleMessageFromServer (msg)
    {
        this.lastServerMessageTime = Date.now();
        const type = msg.type;
        const message = msg.message;

        switch (type)
        {
            case "cpu_info":
            case "audio_device_properties":
            case "patch_source_changed":
            case "infinite_loop_detected":
                this.dispatchEvent (type, message);
                break;

            case "session_status":
                message.connected = true;
                this.setNewStatus (message);
                break;

            case "req_file_read":
                this.handleFileReadRequest (message);
                break;

            case "ping":
                this.sendMessageToServer ({ type: "ping" });
                break;

            default:
                if (type.startsWith ("audio_input_mode_") || type.startsWith ("reply_"))
                {
                    this.dispatchEvent (type, message);
                    break;
                }

                for (const c of this.activePatchConnections)
                    c.deliverMessageFromServer (msg);

                break;
        }
    }

    /** @private */
    checkServerStillExists()
    {
        if (Date.now() > this.lastServerMessageTime + 10000)
            this.setNewStatus ({
                connected: false,
                loaded: false,
                status: "Cannot connect to the Cmajor server"
            });
    }

    /** @private */
    setNewStatus (newStatus)
    {
        this.status = newStatus;
        this.dispatchEvent ("session_status", this.status);
        this.updateCPULevelUpdateRate();
    }

    /** @private */
    updateCPULevelUpdateRate()
    {
        const rate = this.getNumListenersForType ("cpu_info") > 0 ? (this.cpuFramesPerUpdate || 15000) : 0;
        this.sendMessageToServer ({ type: "set_cpu_info_rate",
                                    framesPerCallback: rate });
    }

    /** @private */
    handleFileReadRequest (request)
    {
        const contentProvider = this.files?.get (request?.file);

        if (contentProvider && request.offset !== null && request.size != 0)
        {
            const data = contentProvider.read (request.offset, request.size);
            const reader = new FileReader();

            reader.onloadend = (e) =>
            {
                const base64 = e.target?.result?.split?.(",", 2)[1];

                if (base64)
                    this.sendMessageToServer ({ type: "file_content",
                                                file: request.file,
                                                data: base64,
                                                start: request.offset });
            };

            reader.readAsDataURL (data);
        }
    }

    /** @private */
    sendMessageToServerWithReply (message)
    {
        return new Promise ((resolve, reject) =>
        {
            const replyType = "reply_" + message.type + "_" + this.createRandomID();
            this.addSingleUseListener (replyType, resolve);
            this.sendMessageToServer ({ ...message, replyType });
        });
    }

    /** @private */
    createRandomID()
    {
        return (Math.floor (Math.random() * 100000000)).toString();
    }
}
