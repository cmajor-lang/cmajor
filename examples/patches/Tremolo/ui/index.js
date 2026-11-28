import { createView } from "./stompbox/view.js";

// The patch's view: it makes the stompbox whenever the patch's status arrives, and listens for
// that only while it's showing
class TremoloPatchView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;
        this.statusListener = (status) => this.showStompbox (status);
    }

    connectedCallback()
    {
        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();
    }

    disconnectedCallback()
    {
        // When the view is closed, we must remove the listener that we added
        this.patchConnection.removeStatusListener (this.statusListener);
        this.innerHTML = "";
    }

    showStompbox (status)
    {
        const patchConnection = this.patchConnection;
        this.innerHTML = "";

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

        this.appendChild (view);
    }
}

export default function createPatchView (patchConnection)
{
    const customElementName = "tremolo-patch-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, TremoloPatchView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
