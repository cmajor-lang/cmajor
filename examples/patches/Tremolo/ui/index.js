import { createView } from "./stompbox/view.js";

export default function createPatchView (patchConnection)
{
    const container = document.createElement ("div");
    const clear = () => container.innerHTML = "";

    patchConnection.addStatusListener ((status) =>
    {
        clear();

        const toParameter = (endpointIDToFind) =>
        {
            const endpointInfo = status?.details?.inputs?.find (({ endpointID }) => endpointID === endpointIDToFind);

            const { endpointID } = endpointInfo;

            return {
                min: endpointInfo?.annotation?.min,
                max: endpointInfo?.annotation?.max,
                initialValue: endpointInfo?.annotation?.init,
                onBeginEdit: () => patchConnection.sendParameterGestureStart (endpointID),
                onEdit: (nextValue) => patchConnection.sendEventOrValue (endpointID, nextValue),
                onEndEdit: () => patchConnection.sendParameterGestureEnd (endpointID),
                subscribe: (listener) =>
                {
                    patchConnection.addParameterListener (endpointID, listener);
                    patchConnection.requestParameterValue (endpointID);

                    return () => patchConnection.removeParameterListener (endpointID, listener);
                }
            };
        };

        const view = createView ({
            bypass: toParameter ("bypass"),
            rate: toParameter ("rate"),
            wave: toParameter ("shape"),
            depth: toParameter ("depth"),
        });

        container.appendChild (view);
    });

    patchConnection.requestStatusUpdate();

    return container;
}