
// this imports the built-in Cmajor parameter library
import * as Controls from "../cmaj_api/cmaj-parameter-controls.js"

// This is the web-component that we'll return for our patch's view
class DalekView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;
        this.classList = "ringmod-main";
        this.innerHTML = this.getHTML();
    }

    connectedCallback()
    {
        // To connect some controls to our parameters, we need to wait for the status info
        // to arrive when our patch connection is established.
        // This listener will get called when that happens, so that it can create some
        // parameter controls and add them to our panel..
        this.statusListener = status =>
        {
            this.status = status;

            const paramControlHolder = this.querySelector ("#ringmod-parameters");
            paramControlHolder.innerHTML = ""; // delete any old controls before adding new ones

            const drive = Controls.createLabelledControlForEndpointID (this.patchConnection, status, "drive");
            paramControlHolder.appendChild (drive);

            const freq = Controls.createLabelledControlForEndpointID (this.patchConnection, status, "modulationFrequency");
            paramControlHolder.appendChild (freq);
        };

        this.patchConnection.addStatusListener (this.statusListener);
        this.patchConnection.requestStatusUpdate();

        // when our two trigger buttons are pushed, we'll send events to the patch to trigger playback..
        this.querySelector ("#playSample1").onclick = () => { this.patchConnection.sendEventOrValue ("triggerSample", 0); };
        this.querySelector ("#playSample2").onclick = () => { this.patchConnection.sendEventOrValue ("triggerSample", 1); };
    }

    getHTML()
    {
        return `
        <style>
            .ringmod-main {
                background-color: #124;
                color: white;
                display: block;
                padding: 1rem;
            }

            .ringmod-main * {
                --knob-dial-background-color: transparent;
                --knob-dial-border-color: white;
                --knob-dial-tick-color: white;
                font-family: Arial, Helvetica, sans-serif;
            }

            .ringmod-controls input {
                font-size: 1rem;
                display: inline;
                padding: 0.5rem;
            }

            .ringmod-controls p {
                text-align: center;
                font-size: 1.5rem;
                margin: 2rem;
            }

            .ringmod-controls #ringmod-parameters {
                display: inline;
            }

            ${Controls.getAllCSS()}
        </style>

        <div class="ringmod-controls">
         <p>Ring Modulator Demo</p>
         <input type="button" id="playSample1" value="Sample 1"></input>
         <input type="button" id="playSample2" value="Sample 2"></input>
         <div id="ringmod-parameters"></div>
        </div>`;
    }
}

window.customElements.define ("dalek-view", DalekView);

/* This is the function that a host (the command line patch player, or a Cmajor plugin
   loader, or our VScode extension, etc) will call in order to create a view for your patch.

   Ultimately, a DOM element must be returned to the caller for it to append to its document.
   However, this function can be `async` if you need to perform asyncronous tasks, such as
   fetching remote resources for use in the view, before completing.

   When using libraries such as React, this is where the call to `ReactDOM.createRoot` would
   go, rendering into a container component before returning.
*/
export default function createPatchView (patchConnection)
{
    return new DalekView (patchConnection);
}
