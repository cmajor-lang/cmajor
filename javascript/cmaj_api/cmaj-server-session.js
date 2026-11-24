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
        /** @type {StatusMessage} */
        this.status = { connected: false, loaded: false };
        this.lastServerMessageTime = Date.now();
        /** @type {number | undefined} */
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
     *  @param {Function} listener
     */
    addStatusListener (listener)                        { this.addEventListener    ("session_status", listener); }

    /** Removes a listener that was previously added by `addStatusListener()`
     *  @param {Function} listener
     */
    removeStatusListener (listener)                     { this.removeEventListener ("session_status", listener); }

    /** Subclasses must override this to send a message to the server.
     *  @param {*} _msg
     */
    sendMessageToServer (_msg) {}

    /** Asks the server to asynchronously send a status update message with the latest status.
     */
    requestSessionStatus()                              { this.sendMessageToServer ({ type: "req_session_status" }); }

    /** Returns the session's last known status object.
     *  @returns {Object}
     */
    getCurrentStatus()                                  { return this.status; }

    //==============================================================================
    // Patch loading:

    /** Asks the server to load the specified patch into our session.
     *  @param {string} patchFileToLoad
     */
    loadPatch (patchFileToLoad)
    {
        this.currentPatchLocation = patchFileToLoad;
        this.sendMessageToServer ({ type: "load_patch", file: patchFileToLoad });
    }

    /** Asynchronously returns a list of patches that it has access to.
     *  The return value is an array of manifest objects describing each of the patches.
     *  @returns {Promise<Object[]>}
     */
    async requestAvailablePatchList()
    {
        return await this.sendMessageToServerWithReply ({ type: "req_patchlist" });
    }

    /** Creates and returns a new PatchConnection object which can be used to control the
     *  patch that this session has loaded.
     *
     *  The optional `options` let a host adjust the connection without overriding its methods:
     *   - `transformResourceAddress (address, path)` is called with each address that
     *     `getResourceAddress (path)` would return, and returns the one to use instead (e.g. to
     *     make a view load fresh copies of its files after they've changed).
     *   - `filterMessage (message)` is called with each message from the server before the
     *     connection delivers it to its listeners; if it returns false, the message is dropped.
     *
     *  @param {PatchConnectionOptions} [options]
     *  @returns {PatchConnection}
     */
    createPatchConnection (options)
    {
        class ServerPatchConnection  extends PatchConnection
        {
            /** @param {ServerSession} session @param {PatchConnectionOptions} [options] */
            constructor (session, options)
            {
                super();
                /** @type {ServerSession | undefined} */
                this.session = session;
                /** @type {PatchConnectionOptions} */
                this.options = options ?? {};
                this.manifest = session.status?.manifest;
                this.session.activePatchConnections.add (this);
            }

            dispose()
            {
                this.session?.activePatchConnections.delete (this);
                this.session = undefined;
            }

            /** @override @param {*} message */
            sendMessageToServer (message)
            {
                this.session?.sendMessageToServer (message);
            }

            /** @override @param {string} path @returns {string} */
            getResourceAddress (path)
            {
                const address = this.session?.status?.httpRootURL
                                  ? this.session.status.httpRootURL + (path.startsWith ("/") ? path.substr (1) : path)
                                  : path;

                return this.options.transformResourceAddress ? this.options.transformResourceAddress (address, path) : address;
            }

            /** @override @param {*} msg */
            deliverMessageFromServer (msg)
            {
                if (! this.options.filterMessage || this.options.filterMessage (msg) !== false)
                    super.deliverMessageFromServer (msg);
            }
        }

        return new ServerPatchConnection (this, options);
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
     *  @param {Uint8Array | number[]} fileDataToPlay - if this is some kind of array containing
     *  binary data that can be parsed as an audio file, then it will be sent across for the
     *  server to play as a looped input sample.
     */
    setAudioInputSource (endpointID, shouldMute, fileDataToPlay)
    {
        const loopFile = "_audio_source_" + endpointID;

        if (fileDataToPlay)
        {
            const byteLength = fileDataToPlay instanceof Uint8Array ? fileDataToPlay.byteLength : fileDataToPlay.length;

            this.registerFile (loopFile,
            {
               size: byteLength,
               read: /** @param {number} start @param {number} length */ (start, length) => { return new Blob ([/** @type {BlobPart} */ (/** @type {unknown} */ (fileDataToPlay.slice (start, start + length)))]); }
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
     *  @param {string} endpointID
     *  @param {Function} listener
     */
    addAudioInputModeListener (endpointID, listener)    { this.addEventListener    ("audio_input_mode_" + endpointID, listener); }

    /** Removes a listener previously added with `addAudioInputModeListener()`
     *  @param {string} endpointID
     *  @param {Function} listener
     */
    removeAudioInputModeListener (endpointID, listener) { this.removeEventListener ("audio_input_mode_" + endpointID, listener); }

    /** Asks the server to send an update with the latest status to any audio mode listeners that
     *  are attached to the given endpoint.
     *  @param {string} endpointID
     */
    requestAudioInputMode (endpointID)                  { this.sendMessageToServer ({ type: "req_audio_input_mode", endpoint: endpointID }); }

    /**
     *  Makes the server play a built-in test signal into an audio input endpoint, or changes
     *  the settings of the one that it's already playing.
     *
     *  If the endpoint is already playing a generator, the given fields are merged into its
     *  settings in place (so a slider can be dragged without gaps or clicks). Otherwise a new
     *  generator is started, using the given fields merged over the last settings that this
     *  endpoint's generator had (or the defaults). So passing an empty object switches back to
     *  the last-used signal.
     *
     *  Afterwards, the audio input mode listeners are told that the mode is "generator", and the
     *  generator listeners (see `addAudioInputGeneratorListener()`) get the complete settings.
     *
     *  @param {string} endpointID
     *  @param {Object} settings - any of these (all optional):
     *    - `type`: "sine", "sweep", "white", "pink", "impulse" or "silence"
     *    - `level`: the peak level in dBFS (-120 to 0, default -12)
     *    - `frequency`: the sine's frequency in Hz (default 440)
     *    - `startFrequency`, `endFrequency`: the sweep's range in Hz (default 20 to 20000)
     *    - `sweepSeconds`: the length of the sweep (default 5)
     *    - `impulseRate`: the number of impulses per second (default 2)
     */
    setAudioInputGenerator (endpointID, settings)
    {
        const loopFile = "_audio_source_" + endpointID;

        // (only once: this is called for every step of a dragged slider)
        if (this.files?.has (loopFile))
            this.removeFile (loopFile);

        this.sendMessageToServer ({ type: "set_custom_audio_input",
                                    endpoint: endpointID,
                                    generator: settings ?? {} });
    }

    /** Attaches a listener function to be told the settings of the test signal generator for a
     *  particular endpoint. It's called with an object holding all of the settings that
     *  `setAudioInputGenerator()` takes, whenever they change, and in reply to
     *  `requestAudioInputGenerator()`.
     *  @param {string} endpointID
     *  @param {Function} listener
     */
    addAudioInputGeneratorListener (endpointID, listener)    { this.addEventListener    ("audio_input_generator_" + endpointID, listener); }

    /** Removes a listener previously added with `addAudioInputGeneratorListener()`
     *  @param {string} endpointID
     *  @param {Function} listener
     */
    removeAudioInputGeneratorListener (endpointID, listener) { this.removeEventListener ("audio_input_generator_" + endpointID, listener); }

    /** Asks the server to send the generator settings for the given endpoint to any generator
     *  listeners that are attached to it. If the endpoint isn't playing a generator, these are the
     *  settings that it last used (or the defaults), which `setAudioInputGenerator()` with an empty
     *  object would restore.
     *  @param {string} endpointID
     */
    requestAudioInputGenerator (endpointID)                  { this.sendMessageToServer ({ type: "req_audio_input_generator", endpoint: endpointID }); }

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
     *  @param {Object} newProperties
     */
    setAudioDeviceProperties (newProperties)            { this.sendMessageToServer ({ type: "set_audio_device_props", properties: newProperties }); }

    /** Attaches a listener function which will be called when the audio device properties are
     *  changed.
     *
     *  You can remove the listener when it's no longer needed with `removeAudioDevicePropertiesListener()`.
     *
     *  @param {Function} listener - this callback will receive an argument object containing all the
     *                    details about the device.
     */
    addAudioDevicePropertiesListener (listener)         { this.addEventListener    ("audio_device_properties", listener); }

    /** Removes a listener that was added with `addAudioDevicePropertiesListener()`
     *  @param {Function} listener
     */
    removeAudioDevicePropertiesListener (listener)      { this.removeEventListener ("audio_device_properties", listener); }

    /** Causes an asynchronous callback to any audio device listeners that are registered. */
    requestAudioDeviceProperties()                      { this.sendMessageToServer ({ type: "req_audio_device_props" }); }

    //==============================================================================
    // Transport methods:

    /** Changes the session's transport: the tempo, time signature and playing, recording and
     *  looping flags that the patch's timeline endpoints receive, and its position.
     *
     *  Only the fields that are given are changed. Afterwards, the transport state listeners
     *  (see `addTransportStateListener()`) are sent the new state. The settings are kept by the
     *  session, so they apply to every patch that it loads, and survive rebuilds.
     *
     *  An older server ignores this message, and its transport always plays at 120 BPM in 4/4.
     *
     *  @param {Object} props - any of these (all optional):
     *    - `tempo`: in BPM (the server clamps it to 1 to 999)
     *    - `numerator`, `denominator`: the time signature (1 to 64, and 1 to 32 rounded down to a
     *      power of two)
     *    - `playing`, `recording`, `looping`: booleans (these flags are independent)
     *    - `position`: the timeline position, in frames
     *    - `rewind`: true to set the position to 0 (overriding `position`)
     */
    setTransport (props)                                { this.sendMessageToServer ({ type: "set_transport", ...props }); }

    /** Asks the server to send the transport's state to any transport state listeners. */
    requestTransportState()                             { this.sendMessageToServer ({ type: "req_transport_state" }); }

    /** Attaches a listener function to be told the transport's state. It's called with an object
     *  `{ tempo, numerator, denominator, playing, recording, looping, frame, quarterNote,
     *  barStartQuarterNote, sampleRate }` after every change, in reply to
     *  `requestTransportState()`, and (while the transport is playing) every `intervalMs`, so
     *  that a display of the position can keep up. `frame` is the timeline position in frames.
     *
     *  While any of these listeners are attached, the session asks the server for these updates;
     *  the interval is shared by all of the session's transport state listeners (the last one
     *  given is used), and when the last listener is removed, the session stops asking.
     *
     *  @param {Function} listener
     *  @param {number} [intervalMs] - how often to be sent the position while playing (the
     *                                 server clamps it to 30 to 2000 ms; the default is 50)
     */
    addTransportStateListener (listener, intervalMs)
    {
        if (intervalMs !== undefined)
            this.transportUpdateIntervalMs = intervalMs;

        this.addEventListener ("transport_state", listener);
        this.updateTransportInfoRate();
    }

    /** Removes a listener that was previously attached with `addTransportStateListener()`.
     *  @param {Function} listener
     */
    removeTransportStateListener (listener)             { this.removeEventListener ("transport_state", listener); this.updateTransportInfoRate(); }

    //==============================================================================
    /** Asks the server to asynchronously generate some code from the currently loaded patch.
     *
     *  @param {string} codeType - this must be one of the strings that are listed in the
     *                             status's `codeGenTargets` property. For example, "cpp"
     *                             would request a C++ version of the patch.
     *  @param {Object} [extraOptions] - this optionally provides target-specific properties.
     *  @returns {Promise<Object>} an object containing the code, errors and other metadata about the patch.
     */
    async requestGeneratedCode (codeType, extraOptions)
    {
        return await this.sendMessageToServerWithReply ({ type: "req_codegen",
                                                          codeType: codeType,
                                                          options: extraOptions, });
    }

    /** Asks the server for a description of the loaded patch's graph: its nodes, endpoints and
     *  connections (the same JSON that the "graph" code generation target produces).
     *
     *  The server makes this once per build, so asking for it is cheap. The session status has a
     *  `graphVersion` property which changes whenever the graph does, so a client only needs to
     *  ask again when that changes. A server whose status has no `graphVersion` doesn't support
     *  this message: use `requestGeneratedCode ("graph")` with it instead.
     *
     *  @returns {Promise<{ graph: Object | undefined, version: string, manifestFile: string | undefined }>}
     *           `graph` is undefined if there's no successful build, `version` matches the status's
     *           `graphVersion`, and `manifestFile` is the patch that the graph is for (the loaded
     *           patch may have changed since the request was made). An older server doesn't send
     *           `manifestFile`.
     */
    async requestGraph()
    {
        const reply = await this.sendMessageToServerWithReply ({ type: "req_graph" });

        return { graph: reply?.graph ? JSON.parse (reply.graph) : undefined,
                 version: reply?.version ?? "",
                 manifestFile: reply?.manifestFile || undefined };
    }

    //==============================================================================
    // File change monitoring:

    /** Attaches a listener to be told when a file change is detected in the currently-loaded
     *  patch. The function will be called with an object that gives rough details about the
     *  type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
     *  won't provide any information about exactly which files are involved.
     *  @param {Function} listener
     */
    addFileChangeListener (listener)                    { this.addEventListener    ("patch_source_changed", listener); }

    /** Removes a listener that was previously added with `addFileChangeListener()`.
     *  @param {Function} listener
     */
    removeFileChangeListener (listener)                 { this.removeEventListener ("patch_source_changed", listener); }

    //==============================================================================
    // CPU level monitoring methods:

    /** Attaches a listener function which will be sent messages containing CPU info.
     *  To remove the listener, call `removeCPUListener()`. To change the rate of these
     *  messages, pass `framesPerUpdate`, or use `setCPULevelUpdateRate()` (either way, the
     *  rate is shared by all the session's CPU listeners).
     *  @param {Function} listener
     *  @param {number} [framesPerUpdate]
     */
    addCPUListener (listener, framesPerUpdate)
    {
        if (framesPerUpdate !== undefined)
            this.cpuFramesPerUpdate = framesPerUpdate;

        this.addEventListener ("cpu_info", listener);
        this.updateCPULevelUpdateRate();
    }

    /** Removes a listener that was previously attached with `addCPUListener()`.
     *  @param {Function} listener
     */
    removeCPUListener (listener)                    { this.removeEventListener ("cpu_info", listener); this.updateCPULevelUpdateRate(); }

    /** Changes the frequency at which CPU level update messages are sent to listeners.
     *  @param {number} framesPerUpdate
     */
    setCPULevelUpdateRate (framesPerUpdate)         { this.cpuFramesPerUpdate = framesPerUpdate; this.updateCPULevelUpdateRate(); }

    /** Attaches a listener to be notified when an infinite loop is detected in the patch.
     *  @param {Function} listener
     */
    addInfiniteLoopListener (listener)              { this.addEventListener    ("infinite_loop_detected", listener); }

    /** Removes a listener that was previously added with `addInfiniteLoopListener()`.
     *  @param {Function} listener
     */
    removeInfiniteLoopListener (listener)           { this.removeEventListener ("infinite_loop_detected", listener); }

    //==============================================================================
    /** Registers a virtual file with the server, under the given name.
     *
     *  @param {string} filename - the full path name of the file
     *  @param {{size: number, read: Function}} contentProvider - this object must have a property
     *            called `size` which is a constant size in bytes for the file, and a method
     *            `read (offset, size)` which returns an array (or UInt8Array) of bytes for the
     *            data in a given chunk of the file. The server may repeatedly call this method at
     *            any time until `removeFile()` is called to deregister the file.
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

    /** Removes a file that was previously registered with `registerFile()`.
     *  @param {string} filename
     */
    removeFile (filename)
    {
        this.sendMessageToServer ({ type: "remove_file",
                                    filename: filename });
        this.files?.delete (filename);
    }

    //==============================================================================
    // Private methods from this point...

    /** An implementation subclass must call this when the session first connects. */
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

    /** An implementation subclass must call this when a message arrives from the server.
     *  @param {*} msg
     */
    handleMessageFromServer (msg)
    {
        this.lastServerMessageTime = Date.now();
        const type = msg.type;
        const message = msg.message;

        switch (type)
        {
            case "cpu_info":
            case "transport_state":
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
                if (type.startsWith ("audio_input_mode_") || type.startsWith ("audio_input_generator_") || type.startsWith ("reply_"))
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

    /** @private @param {StatusMessage} newStatus */
    setNewStatus (newStatus)
    {
        const wasConnected = this.status?.connected;
        this.status = newStatus;
        this.dispatchEvent ("session_status", this.status);
        this.updateCPULevelUpdateRate();

        // (a server that has just been (re)connected to doesn't know about the request, but
        // each request is answered with a state, so it's only sent again then)
        if (newStatus?.connected && ! wasConnected && this.getNumListenersForType ("transport_state") > 0)
            this.updateTransportInfoRate (true);
    }

    /** @private @param {boolean} [force] */
    updateTransportInfoRate (force)
    {
        const interval = this.getNumListenersForType ("transport_state") > 0 ? (this.transportUpdateIntervalMs || 50) : 0;

        // (each non-zero request is answered with a state, so an unchanged one isn't repeated)
        if (interval === this.lastTransportInfoRate && interval !== 0 && ! force)
            return;

        this.lastTransportInfoRate = interval;
        this.sendMessageToServer ({ type: "set_transport_info_rate",
                                    intervalMs: interval });
    }

    /** @private */
    updateCPULevelUpdateRate()
    {
        const rate = this.getNumListenersForType ("cpu_info") > 0 ? (this.cpuFramesPerUpdate || 15000) : 0;
        this.sendMessageToServer ({ type: "set_cpu_info_rate",
                                    framesPerCallback: rate });
    }

    /** @private @param {*} request */
    handleFileReadRequest (request)
    {
        const contentProvider = this.files?.get (request?.file);

        if (contentProvider && request.offset !== null && request.size !== 0)
        {
            const data = contentProvider.read (request.offset, request.size);
            const reader = new FileReader();

            reader.onloadend = (e) =>
            {
                const base64 = (/** @type {string | undefined} */ (e.target?.result))?.split (",", 2)[1];

                if (base64)
                    this.sendMessageToServer ({ type: "file_content",
                                                file: request.file,
                                                data: base64,
                                                start: request.offset });
            };

            reader.readAsDataURL (data);
        }
    }

    /** @private @param {*} message */
    sendMessageToServerWithReply (message)
    {
        return new Promise ((resolve, _reject) =>
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
