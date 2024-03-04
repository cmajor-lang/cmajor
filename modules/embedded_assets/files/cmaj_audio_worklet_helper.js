
import { PatchConnection } from "/cmaj_api/cmaj-patch-connection.js"

//==============================================================================
// N.B. code will be serialised to a string, so all `registerWorkletProcessor`s
// dependencies must be self contained and not capture things in the outer scope
async function serialiseWorkletProcessorFactoryToDataURI (WrapperClass, workletName)
{
    const serialisedInvocation = `(${registerWorkletProcessor.toString()}) ("${workletName}", ${WrapperClass.toString()});`

    let reader = new FileReader();
    reader.readAsDataURL (new Blob ([serialisedInvocation], { type: "text/javascript" }));

    return await new Promise (res => { reader.onloadend = () => res (reader.result); });
}

function registerWorkletProcessor (workletName, WrapperClass)
{
    function makeConsumeOutputEvents ({ wrapper, eventOutputs, dispatchOutputEvent })
    {
        const outputEventHandlers = eventOutputs.map (({ endpointID }) =>
        {
            const readCount = wrapper[`getOutputEventCount_${endpointID}`]?.bind (wrapper);
            const reset = wrapper[`resetOutputEventCount_${endpointID}`]?.bind (wrapper);
            const readEventAtIndex = wrapper[`getOutputEvent_${endpointID}`]?.bind (wrapper);

            return () =>
            {
                const count = readCount();
                for (let i = 0; i < count; ++i)
                    dispatchOutputEvent (endpointID, readEventAtIndex (i));

                reset();
            };
        });

        return () => outputEventHandlers.forEach ((consume) => consume() );
    }

    function setInitialParameterValues (parametersMap)
    {
        for (const { initialise } of Object.values (parametersMap))
            initialise();
    }

    function makeEndpointMap (wrapper, endpoints, initialValueOverrides)
    {
        const toKey = ({ endpointType, endpointID }) =>
        {
            switch (endpointType)
            {
                case "event": return `sendInputEvent_${endpointID}`;
                case "value": return `setInputValue_${endpointID}`;
            }

            throw "Unhandled endpoint type";
        };

        const lookup = {};
        for (const { endpointID, endpointType, annotation, purpose } of endpoints)
        {
            const key = toKey ({ endpointType, endpointID });
            const wrapperUpdate = wrapper[key]?.bind (wrapper);

            const snapAndConstrainValue = (value) =>
            {
                if (annotation.step != null)
                    value = Math.round (value / annotation.step) * annotation.step;

                if (annotation.min != null && annotation.max != null)
                    value = Math.min (Math.max (value, annotation.min), annotation.max);

                return value;
            };

            const update = (value, rampFrames) =>
            {
                // N.B. value clamping and rampFrames from annotations not currently applied
                const entry = lookup[endpointID];
                entry.cachedValue = value;
                wrapperUpdate (value, rampFrames);
            };

            if (update)
            {
                const initialValue = initialValueOverrides[endpointID] ?? annotation?.init;

                lookup[endpointID] = {
                    snapAndConstrainValue,
                    update,
                    initialise: initialValue != null ? () => update (initialValue) : () => {},
                    purpose,
                    cachedValue: undefined,
                };
            }
        }

        return lookup;
    }

    function makeStreamEndpointHandler ({ wrapper, toEndpoints, wrapperMethodNamePrefix })
    {
        const endpoints = toEndpoints (wrapper);
        if (endpoints.length === 0)
            return () => {};

        // N.B. we just take the first for now (and do the same when creating the node).
        // we can do better, and should probably align with something similar to what the patch player does
        const first = endpoints[0];
        const handleFrames = wrapper[`${wrapperMethodNamePrefix}_${first.endpointID}`]?.bind (wrapper);
        if (! handleFrames)
            return () => {};

        return (channels, blockSize) => handleFrames (channels, blockSize);
    }

    function makeInputStreamEndpointHandler (wrapper)
    {
        return makeStreamEndpointHandler ({
            wrapper,
            toEndpoints: wrapper => wrapper.getInputEndpoints().filter (({ purpose }) => purpose === "audio in"),
            wrapperMethodNamePrefix: "setInputStreamFrames",
        });
    }

    function makeOutputStreamEndpointHandler (wrapper)
    {
        return makeStreamEndpointHandler ({
            wrapper,
            toEndpoints: wrapper => wrapper.getOutputEndpoints().filter (({ purpose }) => purpose === "audio out"),
            wrapperMethodNamePrefix: "getOutputFrames",
        });
    }

    class WorkletProcessor extends AudioWorkletProcessor
    {
        static get parameterDescriptors()
        {
            return [];
        }

        constructor ({ processorOptions, ...options })
        {
            super (options);

            this.processImpl = undefined;
            this.consumeOutputEvents = undefined;

            const { sessionID = Date.now() & 0x7fffffff, initialValueOverrides = {} } = processorOptions;

            const wrapper = new WrapperClass();

            wrapper.initialise (sessionID, sampleRate)
                .then (() => this.initialisePatch (wrapper, initialValueOverrides))
                .catch (error => { throw new Error (error)});
        }

        process (inputs, outputs)
        {
            const input = inputs[0];
            const output = outputs[0];

            this.processImpl?.(input, output);
            this.consumeOutputEvents?.();

            return true;
        }

        sendPatchMessage (payload)
        {
            this.port.postMessage ({ type: "patch", payload });
        }

        sendParameterValueChanged (endpointID, value)
        {
            this.sendPatchMessage ({
                type: "param_value",
                message: { endpointID, value }
            });
        }

        initialisePatch (wrapper, initialValueOverrides)
        {
            try
            {
                const inputParameters = wrapper.getInputEndpoints().filter (({ purpose }) => purpose === "parameter");
                const parametersMap = makeEndpointMap (wrapper, inputParameters, initialValueOverrides);

                setInitialParameterValues (parametersMap);

                const toParameterValuesWithKey = (endpointKey, parametersMap) =>
                {
                    const toValue = ([endpoint, { cachedValue }]) => ({ [endpointKey]: endpoint, value: cachedValue });
                    return Object.entries (parametersMap).map (toValue);
                };

                const initialValues = toParameterValuesWithKey ("endpointID", parametersMap);
                const initialState = wrapper.getState();

                const resetState = () =>
                {
                    wrapper.restoreState (initialState);

                    // N.B. update cache used for `req_param_value` messages (we don't currently read from the wasm heap)
                    setInitialParameterValues (parametersMap);
                };

                const isNonAudioOrParameterEndpoint = ({ purpose }) => ! ["audio in", "parameter"].includes (purpose);
                const otherInputs = wrapper.getInputEndpoints().filter (isNonAudioOrParameterEndpoint);
                const otherInputEndpointsMap = makeEndpointMap (wrapper, otherInputs, initialValueOverrides);

                const isEvent = ({ endpointType }) => endpointType === "event";
                const eventInputs = wrapper.getInputEndpoints().filter (isEvent);
                const eventOutputs = wrapper.getOutputEndpoints().filter (isEvent);

                const makeEndpointListenerMap = (eventEndpoints) =>
                {
                    const listeners = {};

                    for (const { endpointID } of eventEndpoints)
                        listeners[endpointID] = [];

                    return listeners;
                };

                const inputEventListeners = makeEndpointListenerMap (eventInputs);
                const outputEventListeners = makeEndpointListenerMap (eventOutputs);

                this.consumeOutputEvents = makeConsumeOutputEvents ({
                    eventOutputs,
                    wrapper,
                    dispatchOutputEvent: (endpointID, event) =>
                    {
                        for (const { replyType } of outputEventListeners[endpointID] ?? [])
                        {
                            this.sendPatchMessage ({
                                type: replyType,
                                message: event.event, // N.B. chucking away frame and typeIndex info for now
                            });
                        }
                    },
                });

                const blockSize = 128;
                const prepareInputFrames = makeInputStreamEndpointHandler (wrapper);
                const processOutputFrames = makeOutputStreamEndpointHandler (wrapper);

                this.processImpl = (input, output) =>
                {
                    prepareInputFrames (input, blockSize);
                    wrapper.advance (blockSize);
                    processOutputFrames (output, blockSize);
                };

                // N.B. the message port makes things straightforward, but it allocates (when sending + receiving).
                // so, we aren't doing ourselves any favours. we probably ought to marshal raw bytes over to the gui in
                // a pre-allocated lock-free message queue (using `SharedArrayBuffer` + `Atomic`s) and transform the raw
                // messages there.
                this.port.addEventListener ("message", e =>
                {
                    if (e.data.type !== "patch")
                        return;

                    const msg = e.data.payload;

                    switch (msg.type)
                    {
                        case "req_status":
                        {
                            this.sendPatchMessage ({
                                type: "status",
                                message: {
                                    details: {
                                        inputs: wrapper.getInputEndpoints(),
                                        outputs: wrapper.getOutputEndpoints(),
                                    },
                                    sampleRate,
                                },
                            });
                            break;
                        }

                        case "req_reset":
                        {
                            resetState();
                            initialValues.forEach (v => this.sendParameterValueChanged (v.endpointID, v.value));
                            break;
                        }

                        case "req_param_value":
                        {
                            // N.B. keep a local cache here so that we can send the values back when requested.
                            // we could instead have accessors into the wasm heap.
                            const endpointID = msg.id;
                            const parameter = parametersMap[endpointID];
                            if (! parameter)
                                return;

                            const value = parameter.cachedValue;
                            this.sendParameterValueChanged (endpointID, value);
                            break;
                        }

                        case "send_value":
                        {
                            const endpointID = msg.id;
                            const parameter = parametersMap[endpointID];

                            if (parameter)
                            {
                                const newValue = parameter.snapAndConstrainValue (msg.value);
                                parameter.update (newValue, msg.rampFrames);

                                this.sendParameterValueChanged (endpointID, newValue);
                                return;
                            }

                            const inputEndpoint = otherInputEndpointsMap[endpointID];

                            if (inputEndpoint)
                            {
                                inputEndpoint.update (msg.value);

                                for (const { replyType } of inputEventListeners[endpointID] ?? [])
                                {
                                    this.sendPatchMessage ({
                                        type: replyType,
                                        message: inputEndpoint.cachedValue,
                                    });
                                }
                            }
                            break;
                        }

                        case "send_gesture_start": break;
                        case "send_gesture_end": break;

                        case "req_full_state":
                            this.sendPatchMessage ({
                                type: msg?.replyType,
                                message: {
                                    parameters: toParameterValuesWithKey ("name", parametersMap),
                                },
                            });
                            break;

                        case "send_full_state":
                        {
                            const { parameters = [] } = e.data.payload?.value || [];

                            for (const [endpointID, parameter] of Object.entries (parametersMap))
                            {
                                const namedNextValue = parameters.find (({ name }) => name === endpointID);

                                if (namedNextValue)
                                    parameter.update (namedNextValue.value);
                                else
                                    parameter.initialise();

                                this.sendParameterValueChanged (endpointID, parameter.cachedValue);
                            }
                            break;
                        }

                        case "add_endpoint_listener":
                        {
                            const insertIfValidEndpoint = (lookup, msg) =>
                            {
                                const endpointID = msg?.endpoint;
                                const listeners = lookup[endpointID]

                                if (! listeners)
                                    return false;

                                return listeners.push ({ replyType: msg?.replyType }) > 0;
                            };

                            if (! insertIfValidEndpoint (inputEventListeners, msg))
                                insertIfValidEndpoint (outputEventListeners, msg)

                            break;
                        }

                        case "remove_endpoint_listener":
                        {
                            const removeIfValidReplyType = (lookup, msg) =>
                            {
                                const endpointID = msg?.endpoint;
                                const listeners = lookup[endpointID];

                                if (! listeners)
                                    return false;

                                const index = listeners.indexOf (msg?.replyType);

                                if (index === -1)
                                    return false;

                                return listeners.splice (index, 1).length === 1;
                            };

                            if (! removeIfValidReplyType (inputEventListeners, msg))
                                removeIfValidReplyType (outputEventListeners, msg)

                            break;
                        }

                        default:
                            break;
                    }
                });

                this.port.postMessage ({ type: "initialised" });
                this.port.start();
            }
            catch (e)
            {
                this.port.postMessage (e.toString());
            }
        }
    }

    registerProcessor (workletName, WorkletProcessor);
}

//==============================================================================
/**  Creates an AudioWorkletNode that contains the
 *
 *   @param {Object} WrapperClass - the generated Cmajor class
 *   @param {AudioContext} audioContext - a web audio AudioContext object
 *   @param {string} workletName - the name to give the new worklet that is created
 *   @param {number} sessionID - an integer to use for the session ID
 *   @param {Array} patchInputList - a list of the input endpoints that the patch provides
 *   @param {Object} initialValueOverrides - optional initial values for parameter endpoints
 */
export async function createAudioWorkletNode (WrapperClass,
                                              audioContext,
                                              workletName,
                                              sessionID,
                                              initialValueOverrides)
{
    const dataURI = await serialiseWorkletProcessorFactoryToDataURI (WrapperClass, workletName);
    await audioContext.audioWorklet.addModule (dataURI);

    const audioInputEndpoints  = WrapperClass.prototype.getInputEndpoints().filter (({ purpose }) => purpose === "audio in");
    const audioOutputEndpoints = WrapperClass.prototype.getOutputEndpoints().filter (({ purpose }) => purpose === "audio out");

    // N.B. we just take the first for now (and do the same in the processor too).
    // we can do better, and should probably align with something similar to what the patch player does
    const pickFirstEndpointChannelCount = (endpoints) => endpoints.length ? endpoints[0].numAudioChannels : 0;

    const inputChannelCount = pickFirstEndpointChannelCount (audioInputEndpoints);
    const outputChannelCount = pickFirstEndpointChannelCount (audioOutputEndpoints);

    const hasInput = inputChannelCount > 0;
    const hasOutput = outputChannelCount > 0;

    const node = new AudioWorkletNode (audioContext, workletName, {
        numberOfInputs: +hasInput,
        numberOfOutputs: +hasOutput,
        channelCountMode: "explicit",
        channelCount: hasInput ? inputChannelCount : undefined,
        outputChannelCount: hasOutput ? [outputChannelCount] : [],

        processorOptions:
        {
            sessionID,
            initialValueOverrides
        }
    });

    const waitUntilWorkletInitialised = async () =>
    {
        return new Promise ((resolve) =>
        {
            const filterForInitialised = (e) =>
            {
                if (e.data.type === "initialised")
                {
                    node.port.removeEventListener ("message", filterForInitialised);
                    resolve();
                }
            };

            node.port.addEventListener ("message", filterForInitialised);
        });
    };

    node.port.start();

    await waitUntilWorkletInitialised();

    return node;
}

//==============================================================================
/**  This class provides a PatchConnection that controls a Cmajor audio worklet
 *   node.
 */
export class AudioWorkletPatchConnection extends PatchConnection
{
    constructor (audioNode, manifest)
    {
        super();

        this.manifest = manifest;
        this.audioNode = audioNode;

        audioNode.port.addEventListener ("message", e =>
        {
            if (e.data.type === "patch")
            {
                const msg = e.data.payload;

                if (msg?.type === "status")
                    msg.message = { manifest, ...msg.message };

                this.deliverMessageFromServer (msg)
            }
        });

        this.cachedState = {};
    }

    sendMessageToServer (msg)
    {
        this.audioNode.port.postMessage ({ type: "patch", payload: msg });
    }

    requestStoredStateValue (key)
    {
        const maybeValue = this.cachedState[key];
        if (maybeValue != null)
            this.dispatchEvent ("state_key_value", { key, value: maybeValue });
    }

    sendStoredStateValue (key, newValue)
    {
        const changed = this.cachedState[key] != newValue;

        if (changed)
        {
            const shouldRemove = newValue == null;
            if (shouldRemove)
            {
                delete this.cachedState[key];
                return;
            }

            this.cachedState[key] = newValue;
            // N.B. notifying the client only when updating matches behaviour of the patch player
            this.dispatchEvent ("state_key_value", { key, value: newValue });
        }
    }

    sendFullStoredState (fullState)
    {
        const currentStateCleared = (() =>
        {
            const out = {};
            Object.keys (this.cachedState).forEach (k => out[k] = undefined);
            return out;
        })();

        const incomingStateValues = fullState.values ?? {};
        const nextStateValues = { ...currentStateCleared, ...incomingStateValues };

        Object.entries (nextStateValues).forEach (([key, value]) => this.sendStoredStateValue (key, value));

        // N.B. worklet will handle the `parameters` part
        super.sendFullStoredState (fullState);
    }

    requestFullStoredState (callback)
    {
        // N.B. the worklet only handles the `parameters` part, so we patch the key-value state in here
        super.requestFullStoredState (msg => callback ({ values: { ...this.cachedState }, ...msg }));
    }

    getResourceAddress (path)
    {
        return path.startsWith ("/") ? path : ("/" + path);
    }
}


//==============================================================================
async function connectToAudioIn (audioContext, node)
{
    try
    {
        const input = await navigator.mediaDevices.getUserMedia ({
            audio: {
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl:  false,
        }});

        if (! input)
            throw new Error();

        const source = audioContext.createMediaStreamSource (input);

        if (! source)
            throw new Error();

        source.connect (node);
    }
    catch (e)
    {
        console.warn (`Could not open audio input`);
    }
}

async function connectToMIDI (connection)
{
    try
    {
        if (! navigator.requestMIDIAccess)
            throw new Error ("Web MIDI API not supported.");

        const midiAccess = await navigator.requestMIDIAccess ({ sysex: true, software: true });

        for (const input of midiAccess.inputs.values())
        {
            input.onmidimessage = ({ data }) =>
                connection.sendMIDIInputEvent ("midiIn", data[2] | (data[1] << 8) | (data[0] << 16));
        }
    }
    catch (e)
    {
        console.warn (`Could not open MIDI devices: ${e}`);
    }
}

/**  Takes an audio node and connection that were returned by `createAudioWorkletNodePatchConnection()`
 *   and attempts to hook them up to the default audio and MIDI channels.
 *
 *   @param {AudioWorkletNode} node - the audio node
 *   @param {PatchConnection} connection - the PatchConnection object created by `createAudioWorkletNodePatchConnection()`
 *   @param {AudioContext} audioContext - a web audio AudioContext object
 *   @param {Array} patchInputList - a list of the input endpoints that the patch provides
 */
export async function connectDefaultAudioAndMIDI ({ node, connection, audioContext, patchInputList })
{
    function hasInputWithPurpose (purpose)
    {
        for (const i of patchInputList)
            if (i.purpose === purpose)
                return true;

        return false;
    }

    if (hasInputWithPurpose ("midi in"))
        connectToMIDI (connection);

    if (hasInputWithPurpose ("audio in"))
        connectToAudioIn (audioContext, node);

    node.connect (audioContext.destination);
}
