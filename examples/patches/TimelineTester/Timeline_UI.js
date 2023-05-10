

class TimelineView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;
        this.innerHTML = this.getHTML();
    }

    connectedCallback()
    {
        this.patchConnection.addEndpointEventListener ("timeSigOut", value =>
        {
            this.querySelector ("#timesig").innerText = "Time-signature:   " + value.numerator + "/" + value.denominator;
        });

        this.patchConnection.addEndpointEventListener ("positionOut", value =>
        {
            this.querySelector ("#frames").innerText  = "Frame position:   " + value.frameIndex;
            this.querySelector ("#ppq").innerText     = "PPQ position:     " + value.quarterNote;
            this.querySelector ("#bar-ppq").innerText = "Bar start PPQ:    " + value.barStartQuarterNote;
        });

        this.patchConnection.addEndpointEventListener ("transportStateOut", value =>
        {
            let state = "Stopped";

            if (value.flags == 1) state = "Playing";
            if (value.flags >= 2) state = "Recording";

            this.querySelector ("#transport").innerText = "Transport:        " + state;
        });

        this.patchConnection.addEndpointEventListener ("tempoOut", value =>
        {
            this.querySelector ("#tempo").innerText     = "Tempo:            " + value.bpm + " bpm";
        });
    }

    getHTML()
    {
        return `
        <style>
            .main {
                background: #345;
                display: block;
                width: 100%;
                height: 100%;
                padding: 10px;
                overflow: auto;
                color: #eee;
            }

            .main pre {
                font-size: 1.3em;
                font-family: monospace;
                margin-bottom: 0.3em;
            }

        </style>

        <div class="main">
          <pre id="transport"></pre>
          <pre id="tempo"></pre>
          <pre id="timesig"></pre>
          <pre id="frames"></pre>
          <pre id="ppq"></pre>
          <pre id="bar-ppq"></pre>
        </div>`;
    }
}

window.customElements.define ("timeline-patch-view", TimelineView);

export default function createPatchView (patchConnection)
{
    return new TimelineView (patchConnection);
}
