export default function createCustomPatchView (patchConnection)
{
    return new CustomPatchView (patchConnection);
}

class CustomPatchView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();

        this.patchConnection = patchConnection;
        this.state = {};
        this.parameterChangedListeners = {};

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();

        this.titleElement      = this.shadowRoot.getElementById ("patch-title");
        this.parametersElement = this.shadowRoot.getElementById ("patch-parameters");
    }

    connectedCallback()
    {
        this.connection = this.createPatchConnection();
        this.connection.requestStatusUpdate();
    }

    createPatchConnection()
    {
        this.patchConnection.onPatchStatusChanged       = this.onPatchStatusChanged.bind (this);
        this.patchConnection.onSampleRateChanged        = this.onSampleRateChanged.bind (this);
        this.patchConnection.onParameterEndpointChanged = this.onParameterEndpointChanged.bind (this);
        this.patchConnection.onOutputEvent              = this.onOutputEvent.bind (this);

        return {
            requestStatusUpdate:       this.patchConnection.requestStatusUpdate.bind (this.patchConnection),
            sendParameterGestureStart: this.patchConnection.sendParameterGestureStart.bind (this.patchConnection),
            sendEventOrValue:          this.patchConnection.sendEventOrValue.bind (this.patchConnection),
            sendParameterGestureEnd:   this.patchConnection.sendParameterGestureEnd.bind (this.patchConnection),

            performSingleEdit: (endpointID, value) =>
            {
                this.patchConnection.sendParameterGestureStart (endpointID);
                this.patchConnection.sendEventOrValue (endpointID, value);
                this.patchConnection.sendParameterGestureEnd (endpointID);
            },
        };
    }

    notifyParameterChangedListeners (endpointID, parameter)
    {
        this.parameterChangedListeners[endpointID]?.forEach (notify => notify (parameter));
    }

    onPatchStatusChanged (buildError, manifest, inputEndpoints, outputEndpoints)
    {
        this.parametersElement.innerHTML = "";

        const initialState = {
            title: manifest?.name ?? "Cmajor",
            status: buildError ?? "",
            inputs:  this.toInputControls (inputEndpoints),
            outputs: this.toOutputControls (outputEndpoints),
        };

        const clear = object => Object.keys (object).forEach (key => delete object[key]);
        clear (this.parameterChangedListeners);
        clear (this.state);
        Object.assign (this.state, initialState);

        this.renderInitialState ({
                onBeginEdit: (endpointID)              => this.connection.sendParameterGestureStart (endpointID),
                onEdit: (endpointID, value)            => this.connection.sendEventOrValue (endpointID, value),
                onEndEdit: (endpointID)                => this.connection.sendParameterGestureEnd (endpointID),
                performSingleEdit: (endpointID, value) => this.connection.performSingleEdit (endpointID, value),
            });

        this.state.inputs.forEach (({ endpointID }) => this.patchConnection.requestEndpointValue (endpointID))
    }

    onParameterEndpointChanged (endpointID, value)
    {
        const currentInputs = this.state.inputs;

        if (currentInputs)
        {
            const index = currentInputs.findIndex (p => p.endpointID === endpointID);

            if (index < 0)
                return;

            const currentParameter = currentInputs[index];
            currentParameter.value = value;
            this.notifyParameterChangedListeners (endpointID, currentParameter);
        }
    }

    onOutputEvent (endpointID, value)
    {
    }

    onSampleRateChanged()
    {
    }

    addParameterChangedListener (endpointID, update)
    {
        let listeners = this.parameterChangedListeners[endpointID];

        if (! listeners)
            listeners = this.parameterChangedListeners[endpointID] = [];

        listeners.push (update);
    }

    toInputControls (inputEndpoints)
    {
        return (inputEndpoints || []).filter (e => e.purpose === "parameter").map (this.toParameterControl);
    }

    toParameterControl ({ endpointID, annotation })
    {
        const common =
        {
            name: annotation?.name,
            endpointID,
            annotation,
            unit: annotation.unit,
            defaultValue: annotation.init,
        };

        if (annotation.boolean)
        {
            return { type: "switch", ...common, };
        }

        const textOptions = annotation.text?.split?.("|");
        if (textOptions?.length > 1)
        {
            const hasUserDefinedRange = annotation.min != null && annotation.max != null;
            const { min, step } = hasUserDefinedRange
                ? { min: annotation.min, step: (annotation.max - annotation.min) / (textOptions.length - 1) }
                : { min: 0, step: 1 };

            return {
                type: "options",
                options: textOptions.map ((text, index) => ({ value: min + (step * index), text })),
                ...common,
            };
        }

        return {
            type: "slider",
            min: annotation.min ?? 0,
            max: annotation.max ?? 1,
            ...common,
        };
    }

    toOutputControls (endpoints)
    {
        return [];
    }

    renderInitialState (backend)
    {
        this.titleElement.innerText = this.state.title;

        this.state.inputs.forEach (({ type, value, name, ...other }, index) =>
        {
            const control = this.makeControl (backend, { type, value, name, index, ...other });

            if (control)
            {
                const mapValue = control.mapValue ?? (v => v);
                const wrapped = this.makeLabelledControl (control.control, {
                    initialValue: mapValue (other.defaultValue),
                    name,
                    toDisplayValue: control.toDisplayValue,
                });

                this.addParameterChangedListener (other.endpointID, parameter => wrapped.update (mapValue (parameter.value)));
                this.parametersElement.appendChild (wrapped.element);
            }
        });
    }

    makeControl (backend, { type, min, max, defaultValue, index, ...other })
    {
        switch (type)
        {
            case "slider": return {
                control: this.makeKnob ({
                    initialValue: defaultValue,
                    min,
                    max,
                    onBeginEdit: () => backend.onBeginEdit (other.endpointID),
                    onEdit: nextValue => backend.onEdit (other.endpointID, nextValue),
                    onEndEdit: () => backend.onEndEdit (other.endpointID),
                    onReset: () => backend.performSingleEdit (other.endpointID, defaultValue),
                }),
                toDisplayValue: v => `${v.toFixed (2)} ${other.unit ?? ""}`
            }
            case "switch":
            {
                const mapValue = v => v > 0.5;
                return {
                    control: this.makeSwitch ({
                        initialValue: mapValue (defaultValue),
                        onEdit: nextValue => backend.performSingleEdit (other.endpointID, nextValue),
                    }),
                    toDisplayValue: v => `${v ? "On" : "Off"}`,
                    mapValue,
                };
            }
            case "options":
            {
                const toDisplayValue = index => other.options[index].text;

                const toIndex = (value, options) =>
                {
                    const binarySearch = (arr, toValue, target) =>
                    {
                        let low = 0;
                        let high = arr.length - 1;

                        while (low <= high)
                        {
                            const mid = low + ((high - low) >> 1);
                            const value = toValue (arr[mid]);

                            if (value < target) low = mid + 1;
                            else if (value > target) high = mid - 1;
                            else return mid;
                        }

                        return high;
                    };

                    return Math.max(0, binarySearch (options, v => v.value, value));
                };

                return {
                    control: this.makeOptions ({
                        initialSelectedIndex: toIndex (defaultValue, other.options),
                        options: other.options,
                        onEdit: (optionsIndex) =>
                        {
                            const nextValue = other.options[optionsIndex].value;
                            backend.performSingleEdit (other.endpointID, nextValue);
                        },
                        toDisplayValue,
                    }),
                    toDisplayValue,
                    mapValue: (v) => toIndex (v, other.options),
                };
            }
        }

        return undefined;
    }

    makeKnob ({
        initialValue = 0,
        min = 0,
        max = 1,
        onBeginEdit = () => {},
        onEdit = (nextValue) => {},
        onEndEdit = () => {},
        onReset = () => {},
     } = {})
    {
        // core drawing code derived from https://github.com/Kyle-Shanks/react_synth (MIT licensed)

        const isBipolar = min + max === 0;
        const type = isBipolar ? 2 : 1;

        const maxKnobRotation = 132;
        const typeDashLengths = { 1: 184, 2: 251.5 };
        const typeValueOffsets = { 1: 132, 2: 0 };
        const typePaths =
        {
            1: "M20,76 A 40 40 0 1 1 80 76",
            2: "M50.01,10 A 40 40 0 1 1 50 10"
        };

        const createSvgElement = ({ document = window.document, tag = "svg" } = {}) => document.createElementNS ("http://www.w3.org/2000/svg", tag);

        const svg = createSvgElement();
        svg.setAttribute ("viewBox", "0 0 100 100");

        const trackBackground = createSvgElement ({ tag: "path" });
        trackBackground.setAttribute ("d", "M20,76 A 40 40 0 1 1 80 76");
        trackBackground.classList.add ("knob-path");
        trackBackground.classList.add ("knob-track-background");

        const getDashOffset = (val, type) => typeDashLengths[type] - 184 / (maxKnobRotation * 2) * (val + typeValueOffsets[type]);

        const trackValue = createSvgElement ({ tag: "path" });
        trackValue.setAttribute ("d", typePaths[type]);
        trackValue.setAttribute ("stroke-dasharray", typeDashLengths[type]);
        trackValue.classList.add ("knob-path");
        trackValue.classList.add ("knob-track-value");

        const dial = document.createElement ("div");
        dial.className = "knob-dial";

        const dialTick = document.createElement ("div");
        dialTick.className = "knob-dial-tick";
        dial.appendChild (dialTick);

        const container = document.createElement ("div");
        container.className = "knob-container";

        svg.appendChild (trackBackground);
        svg.appendChild (trackValue);

        container.appendChild (svg);
        container.appendChild (dial);

        const remap = (source, sourceFrom, sourceTo, targetFrom, targetTo) =>
        {
            return targetFrom + (source - sourceFrom) * (targetTo - targetFrom) / (sourceTo - sourceFrom);
        };

        const toValue = (knobRotation) => remap (knobRotation, -maxKnobRotation, maxKnobRotation, min, max);
        const toRotation = (value) => remap (value, min, max, -maxKnobRotation, maxKnobRotation);

        const state =
        {
            rotation: toRotation (initialValue),
        };

        const update = (degrees, force) =>
        {
            if (! force && state.rotation === degrees) return;

            state.rotation = degrees;

            trackValue.setAttribute ("stroke-dashoffset", getDashOffset (state.rotation, type));
            dial.style.transform = `translate(-50%,-50%) rotate(${state.rotation}deg)`
        };

        const force = true;
        update (state.rotation, force);

        let accumlatedRotation = undefined;
        let previousScreenY = undefined;

        const onMouseMove = (event) =>
        {
            event.preventDefault(); // avoid scrolling whilst dragging

            const nextRotation = (rotation, delta) =>
            {
                const clamp = (v, min, max) => Math.min (Math.max (v, min), max);

                return clamp (rotation - delta, -maxKnobRotation, maxKnobRotation);
            };

            const workaroundBrowserIncorrectlyCalculatingMovementY = event.movementY === event.screenY;
            const movementY = workaroundBrowserIncorrectlyCalculatingMovementY
                ? event.screenY - previousScreenY
                : event.movementY;
            previousScreenY = event.screenY;

            const speedMultiplier = event.shiftKey ? 0.25 : 1.5;
            accumlatedRotation = nextRotation (accumlatedRotation, movementY * speedMultiplier);
            onEdit (toValue (accumlatedRotation));
        };

        const onMouseUp = (event) =>
        {
            previousScreenY = undefined;
            accumlatedRotation = undefined;
            window.removeEventListener ("mousemove", onMouseMove);
            window.removeEventListener ("mouseup", onMouseUp);
            onEndEdit();
        };

        const onMouseDown = (event) =>
        {
            previousScreenY = event.screenY;
            accumlatedRotation = state.rotation;
            onBeginEdit();
            window.addEventListener ("mousemove", onMouseMove);
            window.addEventListener ("mouseup", onMouseUp);
        };

        container.addEventListener ("mousedown", onMouseDown);
        container.addEventListener ("mouseup", onMouseUp);
        container.addEventListener ("dblclick", () => onReset());

        return {
            element: container,
            update: nextValue => update (toRotation (nextValue)),
        };
    }

    makeSwitch ({
        initialValue = false,
        onEdit = () => {},
    } = {})
    {
        const outer = document.createElement ("div");
        outer.classList = "switch-outline";

        const inner = document.createElement ("div");
        inner.classList = "switch-thumb";

        const state =
        {
            value: initialValue,
        };

        outer.appendChild (inner);

        const container = document.createElement ("div");
        container.classList.add ("switch-container");

        container.addEventListener ("click", () => onEdit (! state.value));

        container.appendChild (outer);

        const update = (nextValue, force) =>
        {
            if (! force && state.value === nextValue) return;

            state.value = nextValue;
            container.classList.remove ("switch-off", "switch-on");
            container.classList.add (`${nextValue ? "switch-on" : "switch-off"}`);
        };

        const force = true;
        update (initialValue, force);

        return {
            element: container,
            update,
        };
    }

    makeOptions ({
        initialSelectedIndex = -1,
        options = [],
        onEdit = () => {},
        toDisplayValue = () => {},
    } = {})
    {
        const select = document.createElement ("select");

        options.forEach ((option, index) =>
        {
            const optionElement = document.createElement ("option");
            optionElement.innerText = toDisplayValue (index);
            select.appendChild (optionElement);
        });

        const state =
        {
            selectedIndex: initialSelectedIndex,
        };

        select.addEventListener ("change", (e) =>
        {
            const incomingIndex = e.target.selectedIndex;

            // prevent local state change. the caller will update us when the backend actually applies the update
            e.target.selectedIndex = state.selectedIndex;

            onEdit?.(incomingIndex);
        });

        const update = (incomingIndex, force) =>
        {
            if (! force && state.selectedIndex === incomingIndex) return;

            state.selectedIndex = incomingIndex;
            select.selectedIndex = state.selectedIndex;
        };

        const force = true;
        update (initialSelectedIndex, force);

        const wrapper = document.createElement ("div");
        wrapper.className = "select-container";

        wrapper.appendChild (select);

        const icon = document.createElement ("span");
        icon.className = "select-icon";
        wrapper.appendChild (icon);

        return {
            element: wrapper,
            update,
        };
    }

    makeLabelledControl (childControl, {
        name = "",
        initialValue = 0,
        toDisplayValue = () => "",
    } = {})
    {
        const container = document.createElement ("div");
        container.className = "labelled-control";

        const centeredControl = document.createElement ("div");
        centeredControl.className = "labelled-control-centered-control";

        centeredControl.appendChild (childControl.element);

        const titleValueHoverContainer = document.createElement ("div");
        titleValueHoverContainer.className = "labelled-control-label-container";

        const nameText = document.createElement ("div");
        nameText.classList.add ("labelled-control-name");
        nameText.innerText = name;

        const valueText = document.createElement ("div");
        valueText.classList.add ("labelled-control-value");
        valueText.innerText = toDisplayValue (initialValue);

        titleValueHoverContainer.appendChild (nameText);
        titleValueHoverContainer.appendChild (valueText);

        container.appendChild (centeredControl);
        container.appendChild (titleValueHoverContainer);

        return {
            element: container,
            update: (nextValue) =>
            {
                childControl.update (nextValue);
                valueText.innerText = toDisplayValue (nextValue);
            },
        };
    }

    getHTML()
    {
      const newLocal = `
<style>
* {
    box-sizing: border-box;
    user-select: none;
    -webkit-user-select: none;
    -moz-user-select: none;
    -ms-user-select: none;

    margin: 0;
    padding: 0;
}

:host {
    --header-height: 40px;
    --foreground: #ffffff;
    --background: #1a551a;

    /* knob */
    --knob-track-background-color: var(--background);
    --knob-track-value-color: var(--foreground);

    --knob-dial-border-color: var(--foreground);
    --knob-dial-background-color: var(--background);
    --knob-dial-tick-color: var(--foreground);

    /* switch */
    --switch-outline-color: var(--foreground);
    --switch-thumb-color: var(--foreground);
    --switch-on-background-color: var(--background);
    --switch-off-background-color: var(--background);

    /* control value + name display */
    --labelled-control-font-color: var(--foreground);
    --labelled-control-font-size: 12px;

    display: block;
    height: 100%;
    font-family: 'IBM Plex Mono', monospace;
    background-color: var(--background);
}

.header {
    width: 100%;
    height: var(--header-height);
    border-bottom: 1px solid var(--foreground);

    display: flex;
    justify-content: space-between;
    align-items: center;
}

.header-title {
    color: var(--foreground);
    text-overflow: ellipsis;
    white-space: nowrap;
    overflow: hidden;

    cursor: default;
}

.logo {
    flex: 1;
    height: 100%;
    min-width: 100px;
}

.header-filler {
    flex: 1;
}

.app-body {
    height: calc(100% - var(--header-height));
    overflow: auto;
    padding: 1rem;
    text-align: center;
}

/* options / select */
.select-container {
    position: relative;
    display: block;
    font-size: 0.8rem;
    width: 100%;
    color: var(--foreground);
    border: 2px solid var(--foreground);
    border-radius: 0.6rem;
}

select {
    background: none;
    appearance: none;
    -webkit-appearance: none;
    font-family: inherit;
    font-size: var(--labelled-control-font-size);

    overflow: hidden;
    text-overflow: ellipsis;

    padding: 0 1.5rem 0 0.6rem;

    outline: none;
    color: var(--foreground);
    height: 2rem;
    box-sizing: border-box;
    margin: 0;
    border: none;

    width: 100%;
}

select option {
    background: var(--background);
    color: var(--foreground);
}

.select-icon {
    position: absolute;
    right: 0.3rem;
    top: 0.5rem;
    pointer-events: none;
    background-color: var(--foreground);
    width: 1.4em;
    height: 1.4em;
    mask: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath d='M17,9.17a1,1,0,0,0-1.41,0L12,12.71,8.46,9.17a1,1,0,0,0-1.41,0,1,1,0,0,0,0,1.42l4.24,4.24a1,1,0,0,0,1.42,0L17,10.59A1,1,0,0,0,17,9.17Z'/%3E%3C/svg%3E");
    mask-repeat: no-repeat;
    -webkit-mask: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath d='M17,9.17a1,1,0,0,0-1.41,0L12,12.71,8.46,9.17a1,1,0,0,0-1.41,0,1,1,0,0,0,0,1.42l4.24,4.24a1,1,0,0,0,1.42,0L17,10.59A1,1,0,0,0,17,9.17Z'/%3E%3C/svg%3E");
    -webkit-mask-repeat: no-repeat;
}

/* knob */
.knob-container {
    position: relative;
    display: inline-block;

    height: 80px;
    width: 80px;
}

.knob-path {
    fill: none;
    stroke-linecap: round;
    stroke-width: 3;
}

.knob-track-background {
    stroke: var(--knob-track-background-color);
}

.knob-track-value {
    stroke: var(--knob-track-value-color);
}

.knob-dial {
    position: absolute;
    text-align: center;
    height: 50px;
    width: 50px;
    top: 50%;
    left: 50%;
    border: 2px solid var(--knob-dial-border-color);
    border-radius: 100%;
    box-sizing: border-box;
    transform: translate(-50%,-50%);
    background-color: var(--knob-dial-background-color);
}

.knob-dial-tick {
    position: absolute;
    display: inline-block;

    height: 14px;
    width: 2px;
    background-color: var(--knob-dial-tick-color);
}

/* switch */
.switch-outline {
    position: relative;
    display: inline-block;
    height: 1.25rem;
    width: 2.5rem;
    border-radius: 10rem;
    box-shadow: 0 0 0 2px var(--switch-outline-color);
    transition: background-color 0.1s cubic-bezier(0.5, 0, 0.2, 1);
}

.switch-thumb {
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%,-50%);
    height: 1rem;
    width: 1rem;
    background-color: var(--switch-thumb-color);
    border-radius: 100%;
    transition: left 0.1s cubic-bezier(0.5, 0, 0.2, 1);
}

.switch-off .switch-thumb {
    left: 25%
}
.switch-on .switch-thumb {
    left: 75%;
}

.switch-off .switch-outline {
    background-color: var(--switch-on-background-color);
}
.switch-on .switch-outline {
    background-color: var(--switch-off-background-color);
}

.switch-container {
    position: relative;
    display: flex;
    align-items: center;
    justify-content: center;

    height: 100%;
    width: 100%;
}

/* control value + name display */
.labelled-control {
    position: relative;
    display: inline-block;

    margin: 0 0.5rem 0.5rem;
    vertical-align: top;
    text-align: left;
}

.labelled-control-centered-control {
    position: relative;
    display: flex;
    align-items: center;
    justify-content: center;

    width: 80px;
    height: 80px;
}

.labelled-control-label-container {
    position: relative;
    display: block;

    max-width: 80px;
    margin: -0.5rem auto 0.5rem;
    text-align: center;
    font-size: var(--labelled-control-font-size);
    color: var(--labelled-control-font-color);

    cursor: default;
}

.labelled-control-name {
    overflow: hidden;
    text-overflow: ellipsis;
}

.labelled-control-value {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;

    overflow: hidden;
    text-overflow: ellipsis;

    opacity: 0;
}

.labelled-control:hover .labelled-control-name,
.labelled-control:active .labelled-control-name {
    opacity: 0;
}
.labelled-control:hover .labelled-control-value,
.labelled-control:active .labelled-control-value {
    opacity: 1;
}

</style>

<div class="header">
 <span class="logo"></span>
 <h2 id="patch-title" class="header-title"></h2>
 <div class="header-filler"></div>
</div>
<div id="patch-parameters" class="app-body"></div>
`;
        return newLocal;
    }
}

if (! window.customElements.get ("custom-patch-view"))
    window.customElements.define ("custom-patch-view", CustomPatchView);
