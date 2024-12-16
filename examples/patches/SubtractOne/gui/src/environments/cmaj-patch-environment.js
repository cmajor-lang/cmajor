import { makePatchEnvironment } from "./cmaj-patch-utils.js";
import { makeStore } from "../store";
import Root from "../components/Controls";

export async function makeEnvironment() {
    let CmajorSingletonPatchConnection = undefined;

    if (window.frameElement && window.frameElement.CmajorSingletonPatchConnection)
        CmajorSingletonPatchConnection = window.frameElement.CmajorSingletonPatchConnection;

    const storeParams = makePatchEnvironment ({
        vcoShapes: ["sine", "triangle", "square", "sawtooth", "rampdown"],
        enableSoundEffectsOnControlInteractions: false,
        connection: CmajorSingletonPatchConnection,
    });

    return {
        Root,
        store: makeStore (storeParams),
    };
}
