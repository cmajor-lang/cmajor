import {
    updateSampleRate,
    pressNote,
    updateOscilloscope,
    setPreset,
} from "../actions/actions.js";

export function makePatchEnvironment ({
    vcoShapes,
    enableSoundEffectsOnControlInteractions,
    connection,
}) {
    const { toEndpoint, toParameter } = buildLookups ({ vcoShapes });

    const routeViaPatchConnectionMiddleware = ({ dispatch }) =>
    {
        connection.addStatusListener (status => {
            const params = (status.details.inputs || []).filter (e => e.purpose === "parameter");
            params.forEach (({ endpointID }) => connection.requestParameterValue (endpointID))

            dispatch (updateSampleRate (status.sampleRate));
        });

        connection.addAllParameterListener (event => {
            if (event.endpointID === "power") {
                dispatch ({
                    type: "SET_POWER",
                    fromPatchConnection: true,
                    active: !! event.value,
                });
                return;
            }

            const { parameter, fromEndpointValue } = toParameter (event.endpointID);

            dispatch ({
                type: "SET_PARAM",
                fromPatchConnection: true,
                value: fromEndpointValue (event.value),
                ...parameter,
            });
        });

        connection.addEndpointListener ("midiIn", value => {
            const isNoteOn = ({ message }) =>
            {
                const isVoiceMessage = (message, type) => ((message >> 16) & 0xf0) === type;
                const getVelocity = message => message & 0xff;

                return isVoiceMessage (message, 0x90) && getVelocity (message) > 0;
            };

            if (! isNoteOn (value)) return;

            const pitch = (value.message >> 8) & 0xff;
            dispatch (pressNote (pitch));
        });
        connection.addEndpointListener ("snapshot", value => dispatch (updateOscilloscope (value)));

        connection.addStoredStateValueListener (event => {
            if (event.key === "preset") {
                dispatch ({
                    ...setPreset (JSON.parse (event.value)),
                    fromPatchConnection: true,
                });
            }
        });

        const deferToNextTick = (fn) => setTimeout (fn, 0);

        deferToNextTick (() => connection.requestStatusUpdate());
        deferToNextTick (() => connection.requestStoredStateValue ("preset"));

        return next => action => {
            if (action.fromPatchConnection) {
                next (action);
                return;
            }

            switch (action.type) {
                case "BEGIN_SET_PARAM":
                    connection.sendParameterGestureStart (toEndpoint (action).endpointID);
                    return;
                case "SET_PARAM":
                    const { endpointID, toEndpointValue } = toEndpoint (action);
                    connection.sendEventOrValue (endpointID, toEndpointValue (action.value));
                    return;
                case "END_SET_PARAM":
                    connection.sendParameterGestureEnd (toEndpoint (action).endpointID);
                    return;
                case "SET_POWER":
                    connection.sendEventOrValue ("power", action.active);
                    return enableSoundEffectsOnControlInteractions ? next (action) : undefined;
                case "LOAD_PRESET":
                    const send = ([module, moduleState]) => {
                        const sendAllParameters = (module, moduleIndex, paramState) => {
                            Object.entries (paramState).forEach (([param, value]) => {
                                const maybeEndpoint = toEndpoint ({ param, module, moduleIndex });
                                if (! maybeEndpoint) return;

                                const { endpointID, toEndpointValue } = maybeEndpoint;
                                connection.sendEventOrValue (endpointID, toEndpointValue (value));
                            });
                        };

                        if (moduleState instanceof Array) {
                            moduleState.forEach ((paramState, moduleIndex) => {
                                sendAllParameters (module, moduleIndex, paramState);
                            });
                            return;
                        }

                        sendAllParameters (module, false, moduleState);
                    };

                    Object.entries (action.preset).forEach (send);
                    return;
                case "SET_PRESET":
                    connection.sendStoredStateValue ("preset", JSON.stringify (action.preset));
                    // N.B. defer updating the actual store state until `onStoredStateValueChanged` is called.
                    // this is in line with what we do for patch parameters, but feels conceptually a bit
                    // muddled. storing the state is arguably more of a "fire-and-forget" type event from
                    // the views perspective, and the only reason to be notified is if the state changed
                    // via an external source, i.e. the host loading a previously saved chunk
                    return;
                default: break;
            }

            return next (action);
        };
    };

    if (! enableSoundEffectsOnControlInteractions)
        preventSoundEffectsOnControlInteractions();

    return {
        stateOverrides: {
            vcoShapes,
        },
        middleware: [routeViaPatchConnectionMiddleware],
    };
}

function buildLookups ({ vcoShapes }) {
    const toDefaultMapper = ({ toEndpointID }) => ({
        toEndpointID,
        toEndpointValue: v => v,
        fromEndpointValue: v => v,
    });

    const makeEnvelopeMappings = (prefix) => {
        const toEndpointID = (suffix) => `${prefix}Eg${suffix}`;

        return {
            attack: toDefaultMapper ({
                toEndpointID: () => toEndpointID ("Attack")
            }),
            decay: toDefaultMapper ({
                toEndpointID: () => toEndpointID ("Decay")
            }),
            sustain: toDefaultMapper ({
                toEndpointID: () => toEndpointID ("Sustain")
            }),
            release: toDefaultMapper ({
                toEndpointID: () => toEndpointID ("Release")
            }),
        };
    };

    const makeOscillatorMappings = () => {
        const toEndpointID = (index, suffix) => `vco${index + 1}${suffix}`;
        return {
            semitones: toDefaultMapper ({
                toEndpointID: (index) => toEndpointID (index, "Semitones")
            }),
            detune: toDefaultMapper ({
                toEndpointID: (index) => toEndpointID (index, "Detune")
            }),
            gain: toDefaultMapper ({
                toEndpointID: (index) => toEndpointID (index, "Level")
            }),
            type: {
                toEndpointID: index => toEndpointID (index, "Shape"),
                toEndpointValue: (waveform) => vcoShapes.indexOf (waveform),
                fromEndpointValue: (index) => vcoShapes[index | 0],
            },
        };
    };

    const modules = {
        amp: {
            parameters: {
                gain: toDefaultMapper ({
                    toEndpointID: () => "masterGain",
                }),
            }
        },
        general: {
            parameters: {
                octave: toDefaultMapper ({
                    toEndpointID: () => "masterOctave",
                }),
                glide: toDefaultMapper ({
                    toEndpointID: () => "masterGlide",
                }),
            },
        },
        vco: {
            count: 3,
            parameters: {
                ...makeOscillatorMappings(),
            },
        },
        envelope: {
            parameters: {
                ...makeEnvelopeMappings ("amp"),
            },
        },
        filter: {
            parameters: {
                frequency: toDefaultMapper ({
                    toEndpointID: () => "filterCutoff",
                }),
                resonance: toDefaultMapper ({
                    toEndpointID: () => "filterResonance",
                }),
            },
        },
        filterEnvelope: {
            parameters: {
                intensity: toDefaultMapper ({
                    toEndpointID: () => "filterEgInt",
                }),
                ...makeEnvelopeMappings ("filter"),
            },
        },
    };

    const endpointToParameter = {};

    for (const [module, {parameters, count}] of Object.entries (modules)) {
        for (const [param, mapper] of Object.entries (parameters)) {
            for (let i = 0; i < (count || 1); ++i) {
                const moduleIndex = count ? i : false;
                endpointToParameter[mapper.toEndpointID (moduleIndex)] = ({
                    parameter: { param, module, moduleIndex },
                    fromEndpointValue: mapper.fromEndpointValue,
                });
            }
        }
    }

    return {
        toEndpoint: ({ param, module, moduleIndex }) => {
            const maybeModule = modules[module];
            if (! maybeModule)
                return undefined;

            const maybeMapper = maybeModule.parameters[param];
            return maybeMapper ? {
                endpointID: maybeMapper.toEndpointID (moduleIndex),
                toEndpointValue: maybeMapper.toEndpointValue,
            } : undefined;
        },
        toParameter: (endpointID) => endpointToParameter[endpointID],
    };
}

function preventSoundEffectsOnControlInteractions() {
    window.Audio = class {
        play () {}
    };
}
