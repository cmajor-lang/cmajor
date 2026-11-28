// The patch's view: the app, in an iframe. The app gets a connection that remembers the
// listeners it adds, because when the view is closed, the iframe goes without the app cleaning
// up after itself
class SubtractOnePatchView extends HTMLElement {
    constructor (patchConnection) {
        super();
        this.patchConnection = patchConnection;
    }

    connectedCallback() {
        const iframe = document.createElement ("iframe");
        iframe.style.display = "block";
        iframe.style.border = "0";
        iframe.style.width = "100%";
        iframe.style.height = "100%";

        const base = new URL (".", import.meta.url);
        iframe.src = new URL ("./index.html", base);

        this.connection = createListenerTrackingConnection (this.patchConnection);
        iframe.CmajorSingletonPatchConnection = this.connection;
        this.replaceChildren (iframe);
    }

    disconnectedCallback() {
        this.replaceChildren();
        this.connection?.removeAllListeners();
        this.connection = undefined;
    }
}

// Returns an object that works like patchConnection, but which keeps track of the listeners
// added through it, so that removeAllListeners() can remove any that are left
function createListenerTrackingConnection (patchConnection) {
    const connection = Object.create (patchConnection);
    const removers = new Map();

    const track = (addName, removeName, numKeyArgs) => {
        connection[addName] = (...args) => {
            patchConnection[addName] (...args);
            const key = [removeName, ...args.slice (0, numKeyArgs)];
            removers.set (key, () => patchConnection[removeName] (...key.slice (1)));
        };

        connection[removeName] = (...args) => {
            patchConnection[removeName] (...args);

            for (const key of removers.keys())
                if (key[0] === removeName && args.slice (0, numKeyArgs).every ((a, i) => a === key[i + 1])) {
                    removers.delete (key);
                    break;
                }
        };
    };

    track ("addStatusListener", "removeStatusListener", 1);
    track ("addAllParameterListener", "removeAllParameterListener", 1);
    track ("addParameterListener", "removeParameterListener", 2);
    track ("addEndpointListener", "removeEndpointListener", 2);
    track ("addStoredStateValueListener", "removeStoredStateValueListener", 1);

    connection.removeAllListeners = () => {
        for (const remove of removers.values())
            remove();

        removers.clear();
    };

    return connection;
}

export default function createView (patchConnection) {
    const customElementName = "subtract-one-patch-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, SubtractOnePatchView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
