import { makePatchEnvironment } from "./cmaj-patch-utils.js"
import { setupGlobalKeyboardAndMIDIListeners } from "./midi-input-utils.js";
import { makeStore } from "../store";
import { setPower } from "../actions/actions.js";
import Root from "../App";

export async function makeEnvironment() {
    const { store, onMIDIMessage } = await makeBackend (new AudioContext());

    setupGlobalKeyboardAndMIDIListeners (onMIDIMessage);

    return {
        Root,
        store,
    };
}

async function makeBackend (audioContext) {
    const { createAudioWorkletNodePatchConnection } = await import (/* webpackIgnore: true */`${process.env.PUBLIC_URL}/wasm/SubtractOneWorklet.js`);

    const { node, connection } = await createAudioWorkletNodePatchConnection ({
        audioContext,
        workletName: "cmaj-worklet-processor",
        initialValueOverrides: {
            power: false, // force off to force the user to interact with the ui, allowing us to resume the AudioContext
        },
    });

    node.connect (audioContext.destination);

    const { middleware, stateOverrides } = makePatchEnvironment ({
        vcoShapes: ["sine", "triangle", "square", "sawtooth"],
        enableSoundEffectsOnControlInteractions: true,
        connection,
    });

    const store = makeStore ({
        middleware: [
            ({ dispatch }) => {
                return next => action => {
                    switch (action.type) {
                        case "SET_POWER":
                            if (action.active)
                                audioContext.resume();
                            break;
                        case "ONBOARDING_FINISH":
                            dispatch (setPower (true));
                            audioContext.resume();
                            break;
                        default: break;
                    }

                    return next (action);
                }
            },
            ...middleware
        ],
        stateOverrides
    });

    const onMIDIMessage = async (msg) => {
        const toMIDIMessageFromBytes = (data) => (data[2] | (data[1] << 8) | (data[0] << 16));

        connection.sendMIDIInputEvent ("midiIn", toMIDIMessageFromBytes (msg));
    };

    return {
        store,
        onMIDIMessage,
    };
}