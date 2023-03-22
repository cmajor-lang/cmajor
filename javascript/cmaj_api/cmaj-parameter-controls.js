//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


//==============================================================================
export class ParameterControlBase  extends HTMLElement
{
    constructor()
    {
        super();

        // prevent any clicks from focusing on this element
        this.onmousedown = e => e.stopPropagation();
    }

    /// This can be used to connect the parameter to a given connection and endpoint, or
    /// to disconnect it if called with undefined arguments
    setEndpoint (patchConnection, endpointInfo)
    {
        this.#detachListener();

        this.patchConnection = patchConnection;
        this.endpointInfo = endpointInfo;
        this.defaultValue = endpointInfo.annotation?.init || endpointInfo.defaultValue || 0;

        if (this.isConnected)
            this.#attachListener();
    }

    connectedCallback()
    {
        this.#attachListener();
    }

    disconnectedCallback()
    {
        this.#detachListener();
    }

    /// Implement this in a child class to be called when the value needs refreshing
    valueChanged (newValue) {}

    setValue (value)     { this.patchConnection?.sendEventOrValue (this.endpointInfo.endpointID, value); }
    beginGesture()       { this.patchConnection?.sendParameterGestureStart (this.endpointInfo.endpointID); }
    endGesture()         { this.patchConnection?.sendParameterGestureEnd (this.endpointInfo.endpointID); }

    setValueAsGesture (value)
    {
        this.beginGesture();
        this.setValue (value);
        this.endGesture();
    }

    resetToDefault()
    {
        if (this.defaultValue !== null)
            this.setValueAsGesture (this.defaultValue);
    }

    //==============================================================================
    // private methods..
    #detachListener()
    {
        if (this.listener)
        {
            this.patchConnection?.removeParameterListener?.(this.listener.endpointID, this.listener);
            this.listener = undefined;
        }
    }

    #attachListener()
    {
        if (this.patchConnection && this.endpointInfo)
        {
            this.#detachListener();

            this.listener = newValue => this.valueChanged (newValue);
            this.listener.endpointID = this.endpointInfo.endpointID;

            this.patchConnection.addParameterListener (this.endpointInfo.endpointID, this.listener);
            this.patchConnection.requestParameterValue (this.endpointInfo.endpointID);
        }
    }
}

//==============================================================================
export class Knob  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        this.innerHTML = "";
        this.className = "knob-container";
        this.min = this.endpointInfo.min || 0;
        this.max = this.endpointInfo.max || 1;

        const createSvgElement = tag => window.document.createElementNS ("http://www.w3.org/2000/svg", tag);

        const svg = createSvgElement ("svg");
        svg.setAttribute ("viewBox", "0 0 100 100");

        const trackBackground = createSvgElement ("path");
        trackBackground.setAttribute ("d", "M20,76 A 40 40 0 1 1 80 76");
        trackBackground.classList.add ("knob-path");
        trackBackground.classList.add ("knob-track-background");

        const maxKnobRotation = 132;
        const isBipolar = this.min + this.max === 0;
        const dashLength = isBipolar ? 251.5 : 184;
        const valueOffset = isBipolar ? 0 : 132;
        this.getDashOffset = val => dashLength - 184 / (maxKnobRotation * 2) * (val + valueOffset);

        this.trackValue = createSvgElement ("path");

        this.trackValue.setAttribute ("d", isBipolar ? "M50.01,10 A 40 40 0 1 1 50 10"
                                                     : "M20,76 A 40 40 0 1 1 80 76");
        this.trackValue.setAttribute ("stroke-dasharray", dashLength);
        this.trackValue.classList.add ("knob-path");
        this.trackValue.classList.add ("knob-track-value");

        this.dial = document.createElement ("div");
        this.dial.className = "knob-dial";

        const dialTick = document.createElement ("div");
        dialTick.className = "knob-dial-tick";
        this.dial.appendChild (dialTick);

        svg.appendChild (trackBackground);
        svg.appendChild (this.trackValue);

        this.appendChild (svg);
        this.appendChild (this.dial);

        const min = endpointInfo?.annotation?.min || 0;
        const max = endpointInfo?.annotation?.max || 1;

        const remap = (source, sourceFrom, sourceTo, targetFrom, targetTo) =>
                        (targetFrom + (source - sourceFrom) * (targetTo - targetFrom) / (sourceTo - sourceFrom));

        const toValue = (knobRotation) => remap (knobRotation, -maxKnobRotation, maxKnobRotation, min, max);
        this.toRotation = (value) => remap (value, min, max, -maxKnobRotation, maxKnobRotation);

        this.rotation = this.toRotation (this.defaultValue);
        this.#setRotation (this.rotation, true);

        const onMouseMove = (event) =>
        {
            event.preventDefault(); // avoid scrolling whilst dragging

            const nextRotation = (rotation, delta) =>
            {
                const clamp = (v, min, max) => Math.min (Math.max (v, min), max);
                return clamp (rotation - delta, -maxKnobRotation, maxKnobRotation);
            };

            const workaroundBrowserIncorrectlyCalculatingMovementY = event.movementY === event.screenY;
            const movementY = workaroundBrowserIncorrectlyCalculatingMovementY ? event.screenY - this.previousScreenY
                                                                               : event.movementY;
            this.previousScreenY = event.screenY;

            const speedMultiplier = event.shiftKey ? 0.25 : 1.5;
            this.accumlatedRotation = nextRotation (this.accumlatedRotation, movementY * speedMultiplier);
            this.setValue (toValue (this.accumlatedRotation));
        };

        const onMouseUp = (event) =>
        {
            this.previousScreenY = undefined;
            this.accumlatedRotation = undefined;
            window.removeEventListener ("mousemove", onMouseMove);
            window.removeEventListener ("mouseup", onMouseUp);
            this.endGesture();
        };

        const onMouseDown = (event) =>
        {
            this.previousScreenY = event.screenY;
            this.accumlatedRotation = this.rotation;
            this.beginGesture();
            window.addEventListener ("mousemove", onMouseMove);
            window.addEventListener ("mouseup", onMouseUp);
            event.preventDefault();
        };

        this.addEventListener ("mousedown", onMouseDown);
        this.addEventListener ("mouseup", onMouseUp);
        this.addEventListener ("dblclick", () => this.resetToDefault());
    }

    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter";
    }

    #setRotation (degrees, force)
    {
        if (force || this.rotation !== degrees)
        {
            this.rotation = degrees;
            this.trackValue.setAttribute ("stroke-dashoffset", this.getDashOffset (this.rotation));
            this.dial.style.transform = `translate(-50%,-50%) rotate(${degrees}deg)`;
        }
    }

    valueChanged (newValue)       { this.#setRotation (this.toRotation (newValue), false); }
    getDisplayValue (v)           { return `${v.toFixed (2)} ${this.endpointInfo.annotation?.unit ?? ""}`; }

    static getCSS()
    {
        return `
        .knob-container {
            --knob-track-background-color: var(--background);
            --knob-track-value-color: var(--foreground);

            --knob-dial-border-color: var(--foreground);
            --knob-dial-background-color: var(--background);
            --knob-dial-tick-color: var(--foreground);

            position: relative;
            display: inline-block;
            height: 5rem;
            width: 5rem;
            margin: 0;
            padding: 0;
        }

        .knob-path {
            fill: none;
            stroke-linecap: round;
            stroke-width: 0.15rem;
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
            height: 3.3rem;
            width: 3.3rem;
            top: 50%;
            left: 50%;
            border: 0.15rem solid var(--knob-dial-border-color);
            border-radius: 100%;
            box-sizing: border-box;
            transform: translate(-50%,-50%);
            background-color: var(--knob-dial-background-color);
        }

        .knob-dial-tick {
            position: absolute;
            display: inline-block;

            height: 1rem;
            width: 0.15rem;
            background-color: var(--knob-dial-tick-color);
        }`;
    }
}

//==============================================================================
export class Switch  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        const outer = document.createElement ("div");
        outer.classList = "switch-outline";

        const inner = document.createElement ("div");
        inner.classList = "switch-thumb";

        this.innerHTML = "";
        this.currentValue = this.defaultValue > 0.5;
        this.valueChanged (this.currentValue);
        this.classList.add ("switch-container");

        outer.appendChild (inner);
        this.appendChild (outer);
        this.addEventListener ("click", () => this.setValueAsGesture (this.currentValue ? 0 : 1.0));
    }

    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter"
                && endpointInfo.annotation?.boolean;
    }

    valueChanged (newValue)
    {
        const b = newValue > 0.5;
        this.currentValue = b;
        this.classList.remove (! b ? "switch-on" : "switch-off");
        this.classList.add (b ? "switch-on" : "switch-off");
    }

    getDisplayValue (v)   { return `${v > 0.5 ? "On" : "Off"}`; }

    static getCSS()
    {
        return `
        .switch-container {
            --switch-outline-color: var(--foreground);
            --switch-thumb-color: var(--foreground);
            --switch-on-background-color: var(--background);
            --switch-off-background-color: var(--background);

            position: relative;
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            width: 100%;
            margin: 0;
            padding: 0;
        }

        .switch-outline {
            position: relative;
            display: inline-block;
            height: 1.25rem;
            width: 2.5rem;
            border-radius: 10rem;
            box-shadow: 0 0 0 0.15rem var(--switch-outline-color);
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
            left: 25%;
            background: none;
            border: var(--switch-thumb-color) solid 0.1rem;
            height: 0.8rem;
            width: 0.8rem;
        }
        .switch-on .switch-thumb {
            left: 75%;
        }

        .switch-off .switch-outline {
            background-color: var(--switch-on-background-color);
        }
        .switch-on .switch-outline {
            background-color: var(--switch-off-background-color);
        }`;
    }
}

//==============================================================================
export class Options  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super();
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        const optionList = endpointInfo.annotation.text.split ("|");
        let min = 0, step = 1;

        if (endpointInfo.annotation.min != null && endpointInfo.annotation.max != null)
        {
            min = endpointInfo.annotation.min;
            step = (endpointInfo.annotation.max - endpointInfo.annotation.min) / (optionList.length - 1);
        }

        this.innerHTML = "";
        this.options = optionList.map ((text, index) => ({ value: min + (step * index), text }));

        this.select = document.createElement ("select");

        for (const option of this.options)
        {
            const optionElement = document.createElement ("option");
            optionElement.innerText = option.text;
            this.select.appendChild (optionElement);
        }

        this.selectedIndex = this.#toIndex (this.defaultValue);

        this.select.addEventListener ("change", (e) =>
        {
            const newIndex = e.target.selectedIndex;

            // prevent local state change. the caller will update us when the backend actually applies the update
            e.target.selectedIndex = this.selectedIndex;

            this.setValueAsGesture (this.options[newIndex].value)
        });

        this.valueChanged (this.selectedIndex);

        this.className = "select-container";
        this.appendChild (this.select);

        const icon = document.createElement ("span");
        icon.className = "select-icon";
        this.appendChild (icon);
    }

    static canBeUsedFor (endpointInfo)
    {
        return endpointInfo.purpose === "parameter"
                && endpointInfo.annotation?.text?.split?.("|").length > 1;
    }

    #toIndex (value)
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

        return Math.max (0, binarySearch (this.options, v => v.value, value));
    }

    valueChanged (newValue)
    {
        const index = this.#toIndex (newValue);
        this.selectedIndex = index;
        this.select.selectedIndex = index;
    }

    getDisplayValue (v)    { return this.options[this.#toIndex(v)].text; }

    static getCSS()
    {
        return `
        .select-container {
            position: relative;
            display: block;
            font-size: 0.8rem;
            width: 100%;
            color: var(--foreground);
            border: 0.15rem solid var(--foreground);
            border-radius: 0.6rem;
            margin: 0;
            padding: 0;
        }

        select {
            background: none;
            appearance: none;
            -webkit-appearance: none;
            font-family: inherit;
            font-size: 0.8rem;

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
        }`;
    }
}

//==============================================================================
export class LabelledControlHolder  extends ParameterControlBase
{
    constructor (patchConnection, endpointInfo, childControl)
    {
        super();
        this.childControl = childControl;
        this.setEndpoint (patchConnection, endpointInfo);
    }

    setEndpoint (patchConnection, endpointInfo)
    {
        super.setEndpoint (patchConnection, endpointInfo);

        this.innerHTML = "";
        this.className = "labelled-control";

        const centeredControl = document.createElement ("div");
        centeredControl.className = "labelled-control-centered-control";

        centeredControl.appendChild (this.childControl);

        const titleValueHoverContainer = document.createElement ("div");
        titleValueHoverContainer.className = "labelled-control-label-container";

        const nameText = document.createElement ("div");
        nameText.classList.add ("labelled-control-name");
        nameText.innerText = endpointInfo.annotation?.name || endpointInfo.name || endpointInfo.endpointID || "";

        this.valueText = document.createElement ("div");
        this.valueText.classList.add ("labelled-control-value");

        titleValueHoverContainer.appendChild (nameText);
        titleValueHoverContainer.appendChild (this.valueText);

        this.appendChild (centeredControl);
        this.appendChild (titleValueHoverContainer);
    }

    valueChanged (newValue)
    {
        this.valueText.innerText = this.childControl?.getDisplayValue (newValue);
    }

    static getCSS()
    {
        return `
        .labelled-control {
            --labelled-control-font-color: var(--foreground);
            --labelled-control-font-size: 0.8rem;

            position: relative;
            display: inline-block;
            margin: 0 0.4rem 0.4rem;
            vertical-align: top;
            text-align: left;
            padding: 0;
        }

        .labelled-control-centered-control {
            position: relative;
            display: flex;
            align-items: center;
            justify-content: center;

            width: 5.5rem;
            height: 5rem;
        }

        .labelled-control-label-container {
            position: relative;
            display: block;
            max-width: 5.5rem;
            margin: -0.4rem auto 0.4rem;
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
        }`;
    }
}

window.customElements.define ("cmaj-knob-control", Knob);
window.customElements.define ("cmaj-switch-control", Switch);
window.customElements.define ("cmaj-options-control", Options);
window.customElements.define ("cmaj-labelled-control-holder", LabelledControlHolder);


//==============================================================================
export function createControl (patchConnection, endpointInfo)
{
    if (Switch.canBeUsedFor (endpointInfo))
        return new Switch (patchConnection, endpointInfo);

    if (Options.canBeUsedFor (endpointInfo))
        return new Options (patchConnection, endpointInfo);

    if (Knob.canBeUsedFor (endpointInfo))
        return new Knob (patchConnection, endpointInfo);

    return undefined;
}

//==============================================================================
export function createLabelledControl (patchConnection, endpointInfo)
{
    const control = createControl (patchConnection, endpointInfo);

    if (control)
        return new LabelledControlHolder (patchConnection, endpointInfo, control);

    return undefined;
}
