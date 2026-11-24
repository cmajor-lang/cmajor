// MockServerSession / MockPatchConnection: cmaj_api-compatible stand-ins for a websocket
// session with `cmaj server`, driven by a fixture recorded from a real server
// (capture-fixtures.mjs). They behave like the server where the panel can tell:
//
//   - load_patch → "Loading" status, then the patch's "status" + a loaded session status
//   - endpoint listeners get recorded (looped) or synthesised data, at the requested granularity.
//     Like the server (Patch::setNewRenderer()), a rebuild of the same patch KEEPS them (through a
//     failed build too); loading another patch or unloading drops them. Each build (good or bad)
//     increments the status's buildNumber. An add_endpoint_listener for an existing replyType
//     replaces it.
//   - parameters remember their values and echo param_value to every connection
//   - MIDI sent to a MIDI input is echoed to its listeners and makes the audio outputs louder
//   - like the server, each build "makes" the graph once: the status has a graphVersion (a hash
//     of the graph JSON), and req_graph returns { graph, version }. req_codegen "graph" returns
//     the same JSON. setLegacyServer (true) acts like an older server: no graphVersion, no req_graph
//   - other codegen targets return a fake listing
//
// Test hooks (also on window.cmajMock): rebuild(), setBuildError (text), clearBuildError(),
// assetFilesChanged(), infiniteLoop(), disconnect(), reconnect(), setPlaying (bool),
// setGraph (model) (a source edit that changes the graph: replaces it, then rebuilds),
// setLegacyServer (bool), setGeneratorsUnsupported (bool) (an older server: generator requests are
// ignored, and set_custom_audio_input { generator } goes live), setGeneratorFromAnotherClient
// (endpointID, changes), getOpenGestures() (parameter gestures started and not yet ended; unmatchedGestureEnds
// lists ends with no start). numBuilds counts the builds.
//
// Audio input test signals work like the server's:
// set_custom_audio_input { generator } merges and clamps the fields over the running generator's
// settings (or the endpoint's remembered ones, or the defaults) and broadcasts the mode, then the
// settings; req_audio_input_generator always answers. The input's meter data follows the setting.
//
// The transport works like the server's: set_transport merges and
// clamps the fields given and broadcasts transport_state; req_transport_state broadcasts one;
// set_transport_info_rate is per client (this one, plus any set with setOtherClientTransportInfoRate()),
// the shortest non-zero interval wins, a non-zero request gets an immediate state, and while playing a
// state goes out every interval (on the 30 ms tick), plus one on the first interval after a stop. The
// frame advances only while the transport plays, the audio runs and a patch is loaded; it isn't reset by
// loading. Hooks: setTransportUnsupported (bool) (an older server: all three messages are ignored; also
// ?transport=off on the page), setTransportFromAnotherClient (props), setOtherClientTransportInfoRate (ms).
// transportStatesSent counts the broadcasts.
//
// Audio listeners (for the spectrum analyser and recording): each listener keeps
// a running frame counter (l.nextFrame, starting at the mock's frame when it's added and kept through
// a rebuild), and every chunk, min/max or full, carries frame: the index of its first frame. Chunks
// go at about one per granularity frames of elapsed time (44.1 kHz). Full-audio chunks are continuous
// signals with known content, and none are sent while the audio is stopped (nothing is rendered):
//   - an output: a 1 kHz sine at −12 dBFS on every channel (outputSignal.frequency / .levelDB), with a
//     2 kHz harmonic at −30 dB, 3 kHz at −42 dB, 60 Hz hum at −60 dB and white noise at −70 dBFS RMS;
//     channels after the first also have a 5 kHz tone at −36 dB (so L and R differ)
//   - an input: its generator's signal; live or file: a 440 Hz sine at −20 dBFS with the noise; muted: zeros
// Hooks: dropFullAudioChunks (count = 1, endpointID = null) skips the next count full-audio chunks (of
// that endpoint, or of any) as a dropout would: their frames pass, but nothing is sent;
// getListenerCount (endpointID, { fullAudioData }) counts the server listeners on an endpoint;
// fullAudioChunksSent / fullAudioChunksDropped count the chunks.
// A rebuild (rebuild()) carries every audio listener's frame count on, as the server does, but skips
// rebuildGapChunks chunks plus the partly filled one, unsent (the old build's last frames, and those
// rendered during the swap), so a recording across it gets one dropout. unload() unloads the patch
// (like the extension's { unload }); loadPatch_ (file) loads one again.

import { ServerSession } from "/cmaj_api/cmaj-server-session.js";
import { PatchConnection } from "/cmaj_api/cmaj-patch-connection.js";

const sampleRate = 44100;

/// How many whole chunks each audio listener skips across a rebuild (see rebuild())
const rebuildGapChunks = 2;

//==============================================================================
export class MockPatchConnection  extends PatchConnection
{
    // (options: as for ServerSession.createPatchConnection())
    constructor (session, options)
    {
        super();
        this.session = session;
        this.options = options ?? {};
        this.manifest = session.status?.manifest;
        session.activePatchConnections.add (this);
    }

    dispose()
    {
        this.session?.activePatchConnections.delete (this);
        this.session = undefined;
    }

    sendMessageToServer (message)       { this.session?.sendMessageToServer (message); }

    getResourceAddress (path)
    {
        const root = this.session?.status?.httpRootURL;
        const address = root ? root + (path.startsWith ("/") ? path.substring (1) : path) : path;
        return this.options.transformResourceAddress ? this.options.transformResourceAddress (address, path) : address;
    }

    deliverMessageFromServer (msg)
    {
        if (! this.options.filterMessage || this.options.filterMessage (msg) !== false)
            super.deliverMessageFromServer (msg);
    }
}

//==============================================================================
export class MockServerSession  extends ServerSession
{
    constructor (sessionID, fixtureName = sessionID)
    {
        super (sessionID);

        this.fixtureName = fixtureName;
        this.messageLog = [];               // everything the client sent, for tests
        this.listeners = new Map();         // replyType -> { endpointID, granularity, fullAudioData, isAudio }
        this.parameterValues = new Map();
        this.openGestures = new Map();      // endpointID -> how many gestures are open on it
        this.unmatchedGestureEnds = [];     // endpoint IDs of gesture ends with no start
        this.storedState = new Map();
        this.audioInputModes = new Map();
        this.audioInputGenerators = new Map();  // endpointID -> its generator's settings (current, or remembered)
        // (?generators=off on the page, or the fake VS Code page around it: an older server from the start)
        const pageParams = (w) => { try { return new URLSearchParams (w?.location.search ?? ""); } catch (e) { return new URLSearchParams(); } };
        this.areGeneratorsUnsupported = pageParams (window).get ("generators") === "off" || pageParams (window.parent).get ("generators") === "off";
        this.isTransportUnsupported = pageParams (window).get ("transport") === "off" || pageParams (window.parent).get ("transport") === "off";
        this.transport = { ...defaultTransportSettings };
        this.transportFrame = 0;
        this.transportInfoRates = new Map();    // client -> intervalMs ("self" is this client)
        this.transportTimeSinceSend = 0;
        this.wasTransportPlayingAtLastInterval = false;
        this.transportStatesSent = 0;
        this.noteEnergy = 0;
        this.isPlaying = true;
        this.isLoaded = false;
        this.isConnectedToServer = true;
        this.buildError = "";
        this.cpuRate = 0;
        this.numBuilds = 0;
        this.buildNumber = 0;
        this.loadedFile = null;
        this.isLegacyServer = false;
        this.graph = null;
        this.frame = 0;
        this.fullAudioDrops = new Map();    // endpointID (or "*") -> how many full-audio chunks to drop
        this.fullAudioChunksSent = 0;
        this.fullAudioChunksDropped = 0;
        this.httpRootURL = new URL (`/session_${sessionID}/`, location.href).href;

        window.cmajMock = this;

        this.ready = fetch (`/fixtures/${fixtureName}.json`).then (r => r.json()).then (fixture =>
        {
            this.fixture = fixture;
            this.recordedData = this.indexRecordedData (fixture);
            this.graph = fixture.graph ?? null;
            this.patchFile = fixture.patchFile;
            this.ticker = setInterval (() => this.tick(), 30);

            // like a websocket opening: nothing can be sent before this
            this.isSocketOpen = true;
            this.handleSessionConnection();
        });
    }

    dispose()
    {
        clearInterval (this.ticker);
        super.dispose();
    }

    createPatchConnection (options)     { return new MockPatchConnection (this, options); }

    //==============================================================================
    sendMessageToServer (msg)
    {
        if (! this.isConnectedToServer || ! this.isSocketOpen)
            return false;

        this.messageLog.push (msg);
        setTimeout (() => this.handleClientMessage (msg), 1);
        return true;
    }

    send (type, message)
    {
        if (this.isConnectedToServer)
            this.handleMessageFromServer (message === undefined ? { type } : { type, message });
    }

    reply (msg, message)
    {
        if (msg.replyType)
            this.send (msg.replyType, message);
    }

    handleClientMessage (msg)
    {
        const endpoint = this.findEndpoint (msg.id ?? msg.endpoint);

        switch (msg.type)
        {
            case "ping":                    break;
            case "req_session_status":      this.sendSessionStatus(); break;
            case "load_patch":              this.loadPatch_ (msg.file); break;
            case "req_patchlist":           this.reply (msg, this.fixture.patchList ?? []); break;
            case "set_audio_playback_active": this.setPlaying (!! msg.active); break;
            case "req_audio_device_props":  this.send ("audio_device_properties", this.getDeviceProperties()); break;
            case "set_audio_device_props":  this.deviceProperties = { ...this.getDeviceProperties(), ...msg.properties };
                                            this.send ("audio_device_properties", this.deviceProperties); break;
            case "set_cpu_info_rate":       this.cpuRate = msg.framesPerCallback ?? 0; break;
            case "req_status":              this.sendPatchStatus(); break;
            case "req_reset":               this.resetParameters(); break;

            case "add_endpoint_listener":
                if (endpoint)
                    this.listeners.set (msg.replyType, { endpointID: endpoint.endpointID, replyType: msg.replyType,
                                                         granularity: clampGranularity (msg.granularity),
                                                         fullAudioData: !! msg.fullAudioData, isAudio: !! endpoint.numAudioChannels,
                                                         endpoint, position: 0, frames: 0, nextFrame: Math.floor (this.frame) });
                break;

            case "remove_endpoint_listener":
                this.listeners.delete (msg.replyType);
                break;

            case "send_value":
                this.receiveValue (endpoint, msg.value);
                break;

            case "req_param_value":
                if (endpoint?.purpose === "parameter")
                    this.send ("param_value", { endpointID: endpoint.endpointID, value: this.getParameterValue (endpoint) });
                break;

            case "send_gesture_start":
            case "send_gesture_end":
            {
                // (tracked, so tests can check that gestures are balanced: see getOpenGestures())
                const open = this.openGestures.get (msg.id) ?? 0;

                if (msg.type === "send_gesture_start")
                    this.openGestures.set (msg.id, open + 1);
                else if (open > 0)
                    open === 1 ? this.openGestures.delete (msg.id) : this.openGestures.set (msg.id, open - 1);
                else
                    this.unmatchedGestureEnds.push (msg.id);

                break;
            }

            case "req_full_state":
                this.reply (msg, { parameters: this.getParameters().map (p => ({ name: p.endpointID, value: this.getParameterValue (p) })),
                                   values: Object.fromEntries (this.storedState) });
                break;

            case "send_full_state":
                for (const p of msg.value?.parameters ?? [])
                    this.receiveValue (this.findEndpoint (p.name), p.value);
                break;

            case "req_state_value":         this.send ("state_key_value", { key: msg.key, value: this.storedState.get (msg.key) }); break;
            case "send_state_value":        this.storedState.set (msg.key, msg.value);
                                            this.send ("state_key_value", { key: msg.key, value: msg.value }); break;
            case "clear_all_state_values":  this.storedState.clear(); break;

            case "req_graph":
                if (! this.isLegacyServer)
                    setTimeout (() => this.reply (msg, this.isLoaded && this.graph ? { graph: JSON.stringify (this.graph), version: this.getGraphVersion(),
                                                                                       manifestFile: this.fixture.status.manifestFile }
                                                                                    : { graph: "", version: "", manifestFile: "" }), 5);
                break;

            case "req_codegen":
                setTimeout (() => this.reply (msg, this.generateCode (msg.codeType)), msg.codeType === "graph" ? 30 : 250);
                break;

            case "set_custom_audio_input":
                if (endpoint)
                {
                    const id = endpoint.endpointID;
                    const isGenerator = ! msg.file && ! this.areGeneratorsUnsupported && msg.generator !== null && typeof msg.generator === "object";

                    if (isGenerator)
                        this.audioInputGenerators.set (id, mergeGeneratorSettings (this.getGeneratorSettings (id), msg.generator));

                    this.audioInputModes.set (id, msg.file ? "file" : (isGenerator ? "generator" : (msg.mute ? "mute" : "live")));
                    this.sendAudioInputMode (id);
                }
                break;

            case "req_audio_input_mode":
                this.sendAudioInputMode (msg.endpoint);
                break;

            case "set_transport":
                if (! this.isTransportUnsupported)
                {
                    mergeTransportSettings (this, msg);
                    this.sendTransportState();
                }
                break;

            case "req_transport_state":
                if (! this.isTransportUnsupported)
                    this.sendTransportState();
                break;

            case "set_transport_info_rate":
                if (! this.isTransportUnsupported)
                    this.setTransportInfoRate ("self", msg.intervalMs);
                break;

            case "req_audio_input_generator":
                if (! this.areGeneratorsUnsupported)
                    this.send ("audio_input_generator_" + msg.endpoint, this.getGeneratorSettings (msg.endpoint));
                break;

            default:
                break;
        }
    }

    //==============================================================================
    get details()                       { return this.fixture.status.details; }
    getInputs()                         { return this.isLoaded ? this.details?.inputs ?? [] : []; }
    getOutputs()                        { return this.isLoaded ? this.details?.outputs ?? [] : []; }
    getParameters()                     { return this.getInputs().filter (e => e.purpose === "parameter"); }

    findEndpoint (id)
    {
        return [...(this.details?.inputs ?? []), ...(this.details?.outputs ?? [])].find (e => e.endpointID === id);
    }

    getGeneratorSettings (endpointID)   { return { ...(this.audioInputGenerators.get (endpointID) ?? defaultGeneratorSettings) }; }

    sendAudioInputMode (endpointID)
    {
        const mode = this.audioInputModes.get (endpointID) ?? "live";
        this.send ("audio_input_mode_" + endpointID, mode);

        if (mode === "generator")
            this.send ("audio_input_generator_" + endpointID, this.getGeneratorSettings (endpointID));
    }

    getParameterValue (endpoint)
    {
        return this.parameterValues.has (endpoint.endpointID) ? this.parameterValues.get (endpoint.endpointID)
                                                               : (endpoint.annotation?.init ?? endpoint.annotation?.min ?? 0);
    }

    getStatus()
    {
        const status = { httpRootURL: this.httpRootURL, playing: this.isPlaying, buildNumber: this.buildNumber };

        if (this.isLoaded)
        {
            const { manifestFile, manifest, details, codeGenTargets, log } = this.fixture.status;
            Object.assign (status, { manifestFile, manifest, details, codeGenTargets, loaded: true,
                                     status: "Loaded: " + (manifest?.name ?? ""), log: this.buildLog ?? log });

            if (this.graph && ! this.isLegacyServer)
                status.graphVersion = this.getGraphVersion();
        }
        else
        {
            status.loaded = false;

            if (this.loadingFile)
                status.status = "Loading: " + this.loadingFile.split ("/").pop();
        }

        if (this.buildError)
        {
            status.error = this.buildError;
            status.manifestFile = this.fixture.status.manifestFile;
            status.manifest = this.fixture.status.manifest;
            delete status.status;
        }

        return status;
    }

    sendSessionStatus()                 { this.send ("session_status", this.getStatus()); }

    sendPatchStatus()
    {
        if (this.isLoaded)
        {
            const { manifest, details } = this.fixture.status;
            this.send ("status", { warning: "", manifest, details, sampleRate, host: "Cmajor mock server" });
        }
    }

    loadPatch_ (file)
    {
        // (the server keeps its listeners only through a rebuild of the same patch)
        if (file !== this.loadedFile)
            this.listeners.clear();

        this.loadedFile = file || null;

        if (! file)
        {
            this.isLoaded = false;
            this.loadingFile = null;
            this.sendSessionStatus();
            return;
        }

        this.loadingFile = file;
        this.isLoaded = false;
        this.sendSessionStatus();

        setTimeout (() =>
        {
            this.loadingFile = null;

            ++this.buildNumber;

            if (this.buildError)
            {
                this.sendSessionStatus();
                return;
            }

            this.isLoaded = true;
            ++this.numBuilds;
            this.graphVersionCache = null;
            this.buildLog = `Total build time: ${(20 + Math.random() * 80).toFixed (2)} ms`;
            this.sendPatchStatus();
            this.sendSessionStatus();
        }, 150);
    }

    setPlaying (shouldPlay)
    {
        this.isPlaying = shouldPlay;
        this.sendSessionStatus();
    }

    resetParameters()
    {
        this.parameterValues.clear();

        for (const p of this.getParameters())
            this.send ("param_value", { endpointID: p.endpointID, value: this.getParameterValue (p) });
    }

    /// Test hook: the parameters with a gesture open, as { endpointID: count }
    getOpenGestures()                   { return Object.fromEntries (this.openGestures); }

    /// Test hook: the short MIDI messages the client has sent to a MIDI input (or to any), in order
    getSentMIDI (endpointID)
    {
        return this.messageLog.filter (m => m.type === "send_value" && typeof m.value?.message === "number" && (! endpointID || m.id === endpointID))
                              .map (m => m.value.message);
    }

    receiveValue (endpoint, value)
    {
        if (! endpoint)
            return;

        if (endpoint.purpose === "parameter")
        {
            this.parameterValues.set (endpoint.endpointID, typeof value === "number" ? value : Number (value));
            this.send ("param_value", { endpointID: endpoint.endpointID, value: this.parameterValues.get (endpoint.endpointID) });
        }

        if (endpoint.purpose === "midi in" && typeof value?.message === "number" && (value.message >> 20) === 9 && (value.message & 0x7f) > 0)
            this.noteEnergy = Math.min (1.5, this.noteEnergy + 0.6);

        // event inputs can be listened to, so tell their listeners
        for (const l of this.listeners.values())
            if (l.endpointID === endpoint.endpointID && ! l.isAudio)
                this.send (l.replyType, value);
    }

    getDeviceProperties()
    {
        return this.deviceProperties ??= {
            audioAPI: "CoreAudio", availableAPIs: ["CoreAudio"],
            availableInputDevices: [{ ID: "mic", name: "MacBook Pro Microphone" }],
            availableOutputDevices: [{ ID: "spk", name: "MacBook Pro Speakers" }],
            input: "", output: "", rate: sampleRate, blockSize: 128,
            sampleRates: [44100, 48000, 96000], blockSizes: [32, 64, 128, 256, 512]
        };
    }

    generateCode (codeType)
    {
        if (! this.isLoaded)
            return { code: "", messages: [] };

        if (codeType === "graph")
            return { code: this.graph ? JSON.stringify (this.graph) : "", messages: [] };

        const name = this.fixture.status.manifest?.name ?? "Patch";
        const lines = [`// ${codeType} code generated for ${name} (mock)`, ""];

        for (let i = 0; i < 400; ++i)
            lines.push (`    // line ${i + 1} of some ${codeType} output for ${name}`);

        return { code: lines.join ("\n"), mainClass: name.replace (/\W/g, ""), messages: [] };
    }

    //==============================================================================
    // Data: each tick (30ms) sends what the server would have sent in that time

    indexRecordedData (fixture)
    {
        const byEndpoint = new Map();

        for (const [endpointID, replyType] of Object.entries (fixture.listeners ?? {}))
        {
            const messages = fixture.messages.filter (m => m.type === replyType);

            if (messages.length > 0)
                byEndpoint.set (endpointID, messages);
        }

        return byEndpoint;
    }

    tick()
    {
        if (! this.isConnectedToServer)
            return;

        this.tickTransport (30);

        if (! this.isLoaded)
            return;

        const framesPerTick = sampleRate * 0.03;
        this.frame += framesPerTick;
        this.noteEnergy *= 0.96;

        if (this.cpuRate > 0 && this.isPlaying && Math.random() < 0.15)
            this.sendCPUInfo();

        for (const l of [...this.listeners.values()])
        {
            if (l.isAudio)
            {
                // (with the audio stopped, nothing is rendered, so there are no samples to send)
                if (l.fullAudioData && ! this.isPlaying)
                    continue;

                l.frames += framesPerTick;

                while (l.frames >= l.granularity && this.listeners.get (l.replyType) === l)
                {
                    l.frames -= l.granularity;
                    const frame = l.nextFrame;
                    const chunk = this.createAudioChunk (l);
                    l.nextFrame = frame + l.granularity;

                    if (l.fullAudioData && this.shouldDropFullAudioChunk (l.endpointID))
                    {
                        ++this.fullAudioChunksDropped;
                        continue;
                    }

                    if (l.fullAudioData)
                        ++this.fullAudioChunksSent;

                    this.send (l.replyType, { ...chunk, frame });
                }
            }
            else if (this.isPlaying && l.endpoint.purpose !== "midi in" && l.endpoint.purpose !== "parameter")
            {
                // loop the recorded events of outputs, roughly every two seconds
                const recorded = this.recordedData.get (l.endpointID);

                if (recorded && Math.random() < 0.05)
                    this.send (l.replyType, recorded[(l.position++) % recorded.length].message);
            }
        }
    }

    createAudioChunk (l)
    {
        const numChannels = l.endpoint.numAudioChannels ?? 1;
        const isInput = l.endpoint.purpose === "audio in";
        const mode = this.audioInputModes.get (l.endpointID) ?? "live";
        const recorded = this.recordedData.get (l.endpointID);
        let level;

        if (isInput && mode === "generator")
            return this.createGeneratorChunk (l, numChannels);

        if (! this.isPlaying || (isInput && mode === "mute"))
            level = 0;
        else if (recorded)
            level = Math.max (...recorded[(l.position++) % recorded.length].message.max.map (Math.abs));
        else
            level = 0.3 + 0.2 * Math.sin (this.frame / sampleRate * 2);

        if (this.fixture.status.manifest?.isInstrument)
            level = Math.min (1, (level || 0.4) * this.noteEnergy);

        if (l.fullAudioData)
        {
            const signal = isInput ? (mode === "mute" ? null : inputSignal) : outputSignal;
            return { data: Array.from ({ length: numChannels }, (_, c) => createSignalChannel (signal, l.nextFrame, l.granularity, c)) };
        }

        const min = [], max = [];

        for (let c = 0; c < numChannels; ++c)
        {
            const v = level * (0.9 + 0.1 * Math.random()) * (c === 1 ? 0.92 : 1);
            min.push (-v);
            max.push (v);
        }

        return { min, max };
    }

    /// An input's chunk while it plays a test signal: its peak follows the level (silence is
    /// exact zeros, an impulse shows only in the chunks that hold one, pink noise peaks higher)
    createGeneratorChunk (l, numChannels)
    {
        const g = this.getGeneratorSettings (l.endpointID);
        const gain = Math.pow (10, g.level / 20);
        const startFrame = l.generatorFrame ?? 0;
        l.generatorFrame = startFrame + l.granularity;

        const hasImpulse = Math.floor ((startFrame + l.granularity) * g.impulseRate / sampleRate) !== Math.floor (startFrame * g.impulseRate / sampleRate)
                             || startFrame === 0;

        const sample = (frame) =>
        {
            const t = frame / sampleRate;

            switch (g.type)
            {
                case "sine":    return gain * Math.sin (2 * Math.PI * g.frequency * t);
                case "sweep":   { const p = (t % g.sweepSeconds) / g.sweepSeconds;
                                  return gain * Math.sin (2 * Math.PI * g.startFrequency * Math.pow (g.endFrequency / g.startFrequency, p) * t); }
                case "white":   return gain * (Math.random() * 2 - 1);
                case "pink":    return Math.max (-1, Math.min (1, gain * 1.6 * (Math.random() * 2 - 1)));
                case "impulse": return Math.floor ((frame + 1) * g.impulseRate / sampleRate) !== Math.floor (frame * g.impulseRate / sampleRate) ? gain : 0;
                default:        return 0;
            }
        };

        if (l.fullAudioData)
        {
            const channel = Array.from ({ length: l.granularity }, (_, i) => this.isPlaying ? sample (startFrame + i) : 0);
            return { data: Array.from ({ length: numChannels }, () => [...channel]) };
        }

        let lo = 0, hi = 0;

        if (this.isPlaying)
        {
            switch (g.type)
            {
                case "sine":
                case "sweep":   lo = -gain; hi = gain; break;
                case "white":   hi = gain * (0.97 + 0.03 * Math.random()); lo = -hi; break;
                case "pink":    hi = Math.min (1, gain * (1.3 + 0.4 * Math.random())); lo = -hi; break;
                case "impulse": hi = hasImpulse ? gain : 0; break;
                default:        break;
            }
        }

        return { min: new Array (numChannels).fill (lo), max: new Array (numChannels).fill (hi) };
    }

    //==============================================================================
    // The transport

    getTransportState()
    {
        const t = this.transport;
        const rate = this.getDeviceProperties().rate ?? sampleRate;
        const quarterNote = rate > 0 ? this.transportFrame / (rate * 60 / t.tempo) : 0;
        const barLength = 4 * t.numerator / t.denominator;

        return { tempo: t.tempo, numerator: t.numerator, denominator: t.denominator, playing: t.playing, recording: t.recording, looping: t.looping,
                 frame: Math.floor (this.transportFrame), quarterNote, barStartQuarterNote: rate > 0 ? Math.floor (quarterNote / barLength) * barLength : 0,
                 sampleRate: rate };
    }

    sendTransportState()
    {
        ++this.transportStatesSent;
        this.send ("transport_state", this.getTransportState());
    }

    get transportInfoInterval()
    {
        const intervals = [...this.transportInfoRates.values()].filter (i => i > 0);
        return intervals.length ? Math.min (...intervals) : 0;
    }

    setTransportInfoRate (client, intervalMs)
    {
        const interval = typeof intervalMs === "number" && Number.isFinite (intervalMs) && Math.round (intervalMs) !== 0
                           ? Math.max (30, Math.min (2000, Math.round (intervalMs))) : 0;
        const previous = this.transportInfoInterval;

        if (interval)
            this.transportInfoRates.set (client, interval);
        else
            this.transportInfoRates.delete (client);

        if (this.transportInfoInterval !== previous)
            this.transportTimeSinceSend = 0;

        if (interval)
            this.sendTransportState();
    }

    tickTransport (ms)
    {
        const t = this.transport;

        if (t.playing && this.isPlaying && this.isLoaded)
            this.transportFrame += (this.getDeviceProperties().rate ?? sampleRate) * ms / 1000;

        const interval = this.transportInfoInterval;

        if (! interval || this.isTransportUnsupported)
            return;

        this.transportTimeSinceSend += ms;

        if (this.transportTimeSinceSend >= interval)
        {
            this.transportTimeSinceSend = 0;

            if (t.playing || this.wasTransportPlayingAtLastInterval)
                this.sendTransportState();

            this.wasTransportPlayingAtLastInterval = t.playing;
        }
    }

    //==============================================================================
    // Test hooks

    /// Drops the next count full-audio chunks of an endpoint (or of any, if endpointID is null), as a
    /// dropout would: their frames go by, but they're never sent
    dropFullAudioChunks (count = 1, endpointID = null)
    {
        const key = endpointID ?? "*";
        this.fullAudioDrops.set (key, (this.fullAudioDrops.get (key) ?? 0) + count);
    }

    shouldDropFullAudioChunk (endpointID)
    {
        for (const key of [endpointID, "*"])
        {
            const count = this.fullAudioDrops.get (key) ?? 0;

            if (count > 0)
            {
                count === 1 ? this.fullAudioDrops.delete (key) : this.fullAudioDrops.set (key, count - 1);
                return true;
            }
        }

        return false;
    }

    /// The number of server listeners on an endpoint (only the full-audio ones, or only the others,
    /// if fullAudioData is given)
    getListenerCount (endpointID, { fullAudioData } = {})
    {
        return [...this.listeners.values()].filter (l => l.endpointID === endpointID
                                                         && (fullAudioData === undefined || l.fullAudioData === !! fullAudioData)).length;
    }

    /// Acts like a server from before the transport could be controlled (it ignores the messages)
    setTransportUnsupported (isUnsupported)     { this.isTransportUnsupported = isUnsupported; }

    /// Another client of the session changes the transport (nothing is logged as sent by this one)
    setTransportFromAnotherClient (props)       { this.handleClientMessage ({ type: "set_transport", ...props }); }

    /// Another client asks for position updates (0 withdraws its request)
    setOtherClientTransportInfoRate (intervalMs) { this.setTransportInfoRate ("other", intervalMs); }

    /// Acts like a server from before test signals existed
    setGeneratorsUnsupported (isUnsupported)
    {
        this.areGeneratorsUnsupported = isUnsupported;
    }

    /// Another client of the session changes an input's test signal (nothing is logged as sent by this one)
    setGeneratorFromAnotherClient (endpointID, changes)
    {
        this.handleClientMessage ({ type: "set_custom_audio_input", endpoint: endpointID, generator: changes });
    }

    /// Rebuilds the patch the way the server does after a source change (keeping the listeners)
    rebuild()
    {
        // (the server keeps counting frames through a rebuild, but the partly filled chunk and the
        // frames rendered during the swap are never sent)
        for (const l of this.listeners.values())
        {
            if (l.isAudio)
            {
                l.nextFrame += Math.floor (l.frames) + rebuildGapChunks * l.granularity;
                l.frames = 0;
            }
        }

        this.loadPatch_ (this.patchFile);
    }

    /// Unloads the patch, as the extension's { unload } does
    unload()
    {
        this.loadPatch_ (null);
    }

    setBuildError (text)
    {
        const file = this.fixture.status.details?.mainProcessorLocation?.split (":")[0] ?? "/path/to/Patch.cmajor";
        this.buildError = text ?? `${file}:12:5: error: Cannot find symbol 'frequncy'\n        out <- sin (frequncy * phase);\n                    ^`;
        this.isLoaded = false;
        ++this.buildNumber;
        this.sendSessionStatus();
    }

    clearBuildError()
    {
        this.buildError = "";
        this.rebuild();
    }

    /// Like the server's: a hash of the graph JSON, made once per build
    getGraphVersion()
    {
        if (! this.graphVersionCache)
        {
            const text = JSON.stringify (this.graph);
            let hash = 5381;

            for (let i = 0; i < text.length; ++i)
                hash = ((hash * 33) ^ text.charCodeAt (i)) >>> 0;

            this.graphVersionCache = hash.toString (16);
        }

        return this.graphVersionCache;
    }

    /// A source edit that changes the graph structure
    setGraph (model)
    {
        this.graph = model;
        this.rebuild();
    }

    setLegacyServer (isLegacy)
    {
        this.isLegacyServer = isLegacy;
        this.sendSessionStatus();
    }

    /// Sends a cpu_info like the server's: the smoothed level, and the peak (the slowest block since
    /// the last report), which is now and then a spike over the budget (1.0). Pass a level and peak to
    /// send those; a peak of null leaves it out, as an older server does.
    sendCPUInfo (level, peak)
    {
        level ??= 0.04 + 0.03 * Math.random() + 0.05 * this.noteEnergy;

        if (peak === undefined)
            peak = Math.random() < 0.06 ? 1.05 + 0.4 * Math.random() : level * (1.2 + 0.8 * Math.random());

        this.send ("cpu_info", peak === null ? { level } : { level, peak });
    }

    assetFilesChanged()                 { this.send ("patch_source_changed", { assetFilesChanged: true }); }
    infiniteLoop()                      { this.send ("infinite_loop_detected", {}); }
    disconnect()                        { this.isConnectedToServer = false; this.lastServerMessageTime = 0; }

    reconnect()
    {
        this.isConnectedToServer = true;
        this.status.connected = false;
        this.handleSessionConnection();
    }
}

//==============================================================================
// The server's generator settings (SignalGeneratorSettings): a field that's missing, non-numeric
// or non-finite is left as it was, an unknown type is ignored, and the numbers are clamped.

const defaultGeneratorSettings = Object.freeze ({ type: "sine", level: -12, frequency: 440, startFrequency: 20, endFrequency: 20000,
                                                  sweepSeconds: 5, impulseRate: 2 });

const generatorTypes = ["sine", "sweep", "white", "pink", "impulse", "silence"];

const generatorRanges = { level: [-120, 0], frequency: [1, 48000], startFrequency: [1, 48000], endFrequency: [1, 48000],
                          sweepSeconds: [0.1, 600], impulseRate: [0.1, 1000] };

function mergeGeneratorSettings (settings, changes)
{
    const result = { ...settings };

    if (generatorTypes.includes (changes.type))
        result.type = changes.type;

    for (const [field, [lo, hi]] of Object.entries (generatorRanges))
        if (typeof changes[field] === "number" && Number.isFinite (changes[field]))
            result[field] = Math.max (lo, Math.min (hi, changes[field]));

    return result;
}

//==============================================================================
// The server's transport settings (Session::transportSettings): set_transport changes only the fields
// given; a missing, wrong-typed or non-finite one is ignored

const defaultTransportSettings = Object.freeze ({ tempo: 120, numerator: 4, denominator: 4, playing: true, recording: false, looping: false });

function mergeTransportSettings (session, msg)
{
    const t = session.transport;
    const isNumber = (v) => typeof v === "number" && Number.isFinite (v);

    if (isNumber (msg.tempo))           t.tempo = Math.max (1, Math.min (999, msg.tempo));
    if (isNumber (msg.numerator))       t.numerator = Math.max (1, Math.min (64, Math.round (msg.numerator)));
    if (isNumber (msg.denominator))     t.denominator = 2 ** Math.floor (Math.log2 (Math.max (1, Math.min (32, Math.round (msg.denominator)))));

    for (const flag of ["playing", "recording", "looping"])
        if (typeof msg[flag] === "boolean")
            t[flag] = msg[flag];

    if (isNumber (msg.position))        session.transportFrame = Math.max (0, Math.floor (msg.position));
    if (msg.rewind === true)            session.transportFrame = 0;
}

//==============================================================================
// The full-audio signals: sums of sines (at frequencies in Hz and levels in dBFS) plus white noise,
// worked out from the absolute frame index, so consecutive chunks join up exactly

/// An output's signal (the spectrum analyser's tests look for its fundamental)
export const outputSignal = Object.freeze ({
    frequency: 1000, levelDB: -12,
    tones: [{ frequency: 1000, levelDB: -12 }, { frequency: 2000, levelDB: -30 }, { frequency: 3000, levelDB: -42 }, { frequency: 60, levelDB: -60 }],
    extraTones: [{ frequency: 5000, levelDB: -36 }],      // (on channels after the first)
    noiseDB: -70,
});

/// A live (or file) input's signal
export const inputSignal = Object.freeze ({ frequency: 440, levelDB: -20, tones: [{ frequency: 440, levelDB: -20 }], extraTones: [], noiseDB: -70 });

function createSignalChannel (signal, startFrame, numFrames, channel)
{
    const samples = new Array (numFrames).fill (0);

    if (! signal)
        return samples;

    const tones = channel > 0 ? [...signal.tones, ...signal.extraTones] : signal.tones;
    const noise = Math.pow (10, signal.noiseDB / 20) * Math.sqrt (3);   // (uniform noise of that RMS)

    for (const tone of tones)
    {
        const gain = Math.pow (10, tone.levelDB / 20);
        const step = 2 * Math.PI * tone.frequency / sampleRate;

        for (let i = 0; i < numFrames; ++i)
            samples[i] += gain * Math.sin (step * ((startFrame + i) % sampleRate));
    }

    for (let i = 0; i < numFrames; ++i)
        samples[i] += noise * (Math.random() * 2 - 1);

    return samples;
}

function clampGranularity (g)
{
    return (g >= 64 && g <= 8192) ? g : 500;
}
