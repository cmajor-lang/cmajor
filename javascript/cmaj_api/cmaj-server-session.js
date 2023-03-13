//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"

import { PatchConnection } from "/cmaj_api/cmaj-patch-connection.js"
import { EventListenerList } from "/cmaj_api/cmaj-event-listener-list.js"


//==============================================================================
/// This class provides the API and manages the communication protocol between
/// a javascript application and a Cmajor session running on some kind of server
/// (which may be local or remote).
///
/// This is an abstract base class: some kind of transport layer will create a
/// subclass of ServerSession which a client application can then use to control
/// and interact with the server.
export class ServerSession   extends EventListenerList
{
    /// A server session must be given a unique string sessionID.
    constructor (sessionID)
    {
        super();

        this.sessionID = sessionID;
        this.activePatchConnections = new Set();
        this.status = { connected: false, loaded: false };
        this.lastServerMessageTime = Date.now();
        this.pingTimer = setInterval (() => this.pingServer(), 1311);
    }

    /// Call `dispose()` when this session is no longer needed and should be released.
    dispose()
    {
        if (this.pingTimer)
        {
            clearInterval (this.pingTimer);
            this.pingTimer = undefined;
        }

        this.status = { connected: false, loaded: false };
    }

    //==============================================================================
    // Session status methods:

    /// Attaches a listener function which will be called when the session status changes.
    /// The function will be passed an argument object containing lots of properties describing the
    /// state, including any errors, loaded patch manifest, etc.
    addStatusListener (listener)                        { this.addEventListener    ("session_status", listener); }

    /// Removes a listener that was previously added by `addStatusListener()`
    removeStatusListener (listener)                     { this.removeEventListener ("session_status", listener); }

    /// Asks the server to asynchronously send a status update message with the latest status.
    requestSessionStatus()                              { this.sendMessageToServer ({ type: "req_session_status" }); }

    /// Returns the session's last known status object.
    getCurrentStatus()                                  { return this.status; }

    //==============================================================================
    // Patch loading:

    /// Asks the server to load the specified patch into our session.
    loadPatch (patchFileToLoad)
    {
        this.currentPatchLocation = patchFileToLoad;
        this.sendMessageToServer ({ type: "load_patch", file: patchFileToLoad });
    }

    /// Tells the server to asynchronously generate a list of patches that it knows about.
    /// The function provided will be called back with an array of manifest objects describing
    /// each of the patches.
    requestAvailablePatchList (callbackFunction)
    {
        const replyType = this.#createReplyID ("patchlist_");
        this.addSingleUseListener (replyType, callbackFunction);
        this.sendMessageToServer ({ type: "req_patchlist",
                                    replyType: replyType });
    }

    /// Creates and returns a new PatchConnection object which can be used to control the
    /// patch that this session has loaded.
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

            /// Attaches a listener function that can monitor floating-point audio data on a suitable
            /// input or output endpoint.
            /// If an endpoint has the right shape to be treated as "audio" then this can be used to
            /// get a stream of updates of the min/max range of chunks of data that is flowing through it.
            /// There will be one callback per chunk of data, and the size of chunks can be modified by
            /// calling setAudioDataGranularity().
            /// The listener will receive an argument object containing two properties 'min' and 'max',
            /// which are each an array of values, one element per audio channel. This allows you to
            /// find the highest and lowest samples in that chunk for each channel.
            addEndpointAudioListener (endpointID, listener)
            {
                this.addEventListener ("audio_data_" + endpointID, listener);
                this.setAudioDataGranularity (endpointID, undefined);
            }

            /// Removes a listener that was previously added with addEndpointAudioListener()
            removeEndpointAudioListener (endpointID, listener)
            {
                this.removeEventListener ("audio_data_" + endpointID, listener);
                this.setAudioDataGranularity (endpointID, undefined);
            }

            /// This can be used to change the size of chunk being sent to an audio endpoint listener
            /// for a given endpoint. See addEndpointAudioListener() to attach the listeners.
            setAudioDataGranularity (endpointID, framesPerCallback)
            {
                if (! this.endpointAudioGranularities)
                    this.endpointAudioGranularities = {};

                if (framesPerCallback && framesPerCallback > 0 && framesPerCallback <= 96000)
                    this.endpointAudioGranularities[endpointID] = framesPerCallback;

                const currentGranularity = this.endpointAudioGranularities[endpointID] || 1024;
                const gran = this.getNumListenersForType ("audio_data_" + endpointID) > 0 ? currentGranularity : 0;
                this.sendMessageToServer ({ type: "set_endpoint_audio_monitoring", endpoint: endpointID, granularity: gran });
            }
        }

        return new ServerPatchConnection (this);
    }

    //==============================================================================
    // Audio input source handling:

    /// Sets a custom audio input source for a particular endpoint.
    /// If shouldMute is true, it will be muted. If fileDataToPlay is a blob of data (e.g. a
    /// data URL) that can be loaded as an audio file, then it will be sent across for the
    /// server to play as a loop.
    /// When a source is changed, a callback is sent to any audio input mode listeners (see
    /// `addAudioInputModeListener()`)
    setAudioInputSource (endpointID, shouldMute, fileDataToPlay)
    {
        if (fileDataToPlay)
            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        file: fileDataToPlay });
        else
            this.sendMessageToServer ({ type: "set_custom_audio_input",
                                        endpoint: endpointID,
                                        mute: !! shouldMute });
    }

    /// Attaches a listener function to be told when the input source for a particular
    /// endpoint is changed by a call to `setAudioInputSource()`.
    addAudioInputModeListener (endpointID, listener)    { this.addEventListener    ("audio_input_mode_" + endpointID, listener); }
    /// Removes a listener previously added with `addAudioInputModeListener()`
    removeAudioInputModeListener (endpointID, listener) { this.removeEventListener ("audio_input_mode_" + endpointID, listener); }

    /// Asks the server to send an update with the latest status to any audio mode listeners that
    /// are attached to the given endpoint.
    requestAudioInputMode (endpointID)                  { this.sendMessageToServer ({ type: "req_audio_input_mode", endpoint: endpointID }); }

    //==============================================================================
    // Audio device methods:

    /// Enables or disables audio playback.
    /// When playback state changes, a status update is sent to any status listeners.
    setAudioPlaybackActive (shouldBeActive)             { this.sendMessageToServer ({ type: "set_audio_playback_active", active: shouldBeActive }); }

    /// Asks the server to apply a new set of audio device properties.
    /// The properties object uses the same format as the object that is passed to the listeners
    /// (see `addAudioDevicePropertiesListener()`).
    setAudioDeviceProperties (newProperties)            { this.sendMessageToServer ({ type: "set_audio_device_props", properties: newProperties }); }

    /// Attaches a listener function which will be called when the audio device properties are
    /// changed. The listener function will be passed an argument object containing all the
    /// details about the device.
    /// Remove the listener when it's no longer needed with `removeAudioDevicePropertiesListener()`.
    addAudioDevicePropertiesListener (listener)         { this.addEventListener    ("audio_device_properties", listener); }

    /// Removes a listener that was added with `addAudioDevicePropertiesListener()`
    removeAudioDevicePropertiesListener (listener)      { this.removeEventListener ("audio_device_properties", listener); }

    /// Causes an asynchronous callback to any audio device listeners that are registered.
    requestAudioDeviceProperties()                      { this.sendMessageToServer ({ type: "req_audio_device_props" }); }

    //==============================================================================
    /// Asks the server to asynchronously generate some code from the currently loaded patch.
    /// The `codeType` must be one of the strings that are listed in the status's `codeGenTargets`
    /// property.
    /// `extraOptions` is an object containing any target-specific properties, and may be undefined.
    /// The `callbackFunction` is a function that will be called with the result when it has
    /// been generated. The results object will contain properties for any code, errors and other
    /// metadata about the generated data.
    requestGeneratedCode (codeType, extraOptions, callbackFunction)
    {
        const replyType = this.#createReplyID ("codegen_");
        this.addSingleUseListener (replyType, callbackFunction);
        this.sendMessageToServer ({ type: "req_codegen",
                                    codeType: codeType,
                                    options: extraOptions,
                                    replyType: replyType });
    }

    //==============================================================================
    // File change monitoring:

    /// Attaches a listener to be told when a file change is detected in the currently-loaded
    /// patch. The function will be called with an object that gives rough details about the
    /// type of change, i.e. whether it's a manifest or asset file, or a cmajor file, but it
    /// won't provide any information about exactly which files are involved.
    addFileChangeListener (listener)                    { this.addEventListener    ("patch_source_changed", listener); }
    /// Removes a listener that was previously added with `addFileChangeListener()`.
    removeFileChangeListener (listener)                 { this.removeEventListener ("patch_source_changed", listener); }

    //==============================================================================
    // CPU level monitoring methods:

    /// Attaches a listener function which will be sent messages containing CPU info.
    /// To remove the listener, call `removeCPUListener()`. To change the rate of these
    /// messages, use `setCPULevelUpdateRate()`.
    addCPUListener (listener)                       { this.addEventListener    ("cpu_info", listener); this.#updateCPULevelUpdateRate(); }

    /// Removes a listener that was previously attached with `addCPUListener()`.
    removeCPUListener (listener)                    { this.removeEventListener ("cpu_info", listener); this.#updateCPULevelUpdateRate(); }

    /// Changes the frequency at which CPU level update messages are sent to listeners.
    setCPULevelUpdateRate (framesPerUpdate)         { this.cpuFramesPerUpdate = framesPerUpdate; this.#updateCPULevelUpdateRate(); }

    //==============================================================================
    /// Sends a ping message to the server.
    /// You shouldn't need to call this - the ServerSession class takes care of sending
    /// a ping at regular intervals.
    pingServer()
    {
        this.sendMessageToServer ({ type: "ping" });

        if (Date.now() > this.lastServerMessageTime + 10000)
            this.#setNewStatus ({
                connected: false,
                loaded: false,
                status: "Cannot connect to the Cmajor server"
            });
    }

    //==============================================================================
    // Private methods from this point...

    // An implementation subclass must call this when the session first connects
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

    // An implementation subclass must call this when a message arrives
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
                this.dispatchEvent (type, message);
                break;

            case "session_status":
                message.connected = true;
                this.#setNewStatus (message);
                break;

            case "ping":
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

    #setNewStatus (newStatus)
    {
        this.status = newStatus;
        this.dispatchEvent ("session_status", this.status);
        this.#updateCPULevelUpdateRate();
    }

    #updateCPULevelUpdateRate()
    {
        const rate = this.getNumListenersForType ("cpu_info") > 0 ? (this.cpuFramesPerUpdate || 15000) : 0;
        this.sendMessageToServer ({ type: "set_cpu_info_rate",
                                    framesPerCallback: rate });
    }

    #createReplyID (stem)
    {
        return "reply_" + stem + (Math.floor (Math.random() * 100000000)).toString();
    }
}
