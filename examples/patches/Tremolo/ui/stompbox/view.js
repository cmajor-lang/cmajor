import { cmajorLogoInlineSvg } from "./logo.js";

/**
 * Represents an external parameter. i.e. the actual value is held elsewhere.
 * A user interface control can action changes to it via the provided `Edit` callbacks, and subscribe to changes to its
 * value via the `subscribe` method.
 *
 * Information about the value the parameter manipulates are provided as properities (e.g. `min`, `max`, `initialValue`)
 * such that a user interface can sufficiently represent it. i.e. a slider could have its extents set to the given
 * range, and have a default value of `initialValue`, which it could restore via a gesture (such as double click).
 *
 * @typedef {Object} Parameter
 * @property {number} min the minimum value for the parameter.
 * @property {number} max the maximum value for the parameter.
 * @property {number} initialValue the value the parameter is initially set to, before any user interaction.
 * @property {() => void} onBeginEdit a callback that a user interface control should call at the start of its interaction
 * @property {(value: number) => void} onEdit a callback the a user interface control should call to request to change the backend value
 * @property {() => void} onEndEdit a callback that a user interface control should call at the end of its interaction
 * @property {(listener: (value: number) => void) => RemoveListenerFn} subscribe adds a change listener to the actual backend value, such that the user interface can be updated to reflect the latest value when it changes. upon subscribing, the backend should notify the listener of the current value. a cleanup function is returned that the user interface should call when it is no longer visualising the value.
*/

/**
 * Cleanup a listener returned from {@link Parameter.subscribe}
 * @typedef {() => void} RemoveListenerFn
 * */

/**
 * Creates a HTMLElement resembling a physical tremolo guitar stompbox / pedal.
 * It has 4 controls: 3 knobs (for controlling the rate, wave, and depth), and a button to toggle the pedal off / on.
 * There is an LED indicator that will be lit when the pedal is active.
 *
 * The controls are driven by an external {@link Parameter} backend, provided on construction for each control.
 *
 * The visual controls will only change when the backend changes, which means that user interactions signalled via the
 * `onEdit` callback may be discarded by the backend and the UI will not be out of sync.
 * This also ensures that if changes are made via other means, that the controls are updated to reflect that without
 * any additional code paths.
 * If you're familar with controlled components from React, and unidirectional dataflow paradigms, then this may feel
 * a bit familiar. This can be disabled by setting the {@link options} `controlled` property to `false`.
 *
 * @param {Object} parameters
 * @param {Parameter} parameters.bypass parameter object used by the pedal button. when not bypassed, the led element is lit.
 * @param {Parameter} parameters.rate parameter object used by the rate knob
 * @param {Parameter} parameters.wave parameter object used by the wave knob
 * @param {Parameter} parameters.depth parameter object used by the depth kn
 * @param {Object} [options]
 * @param {boolean} [options.controlled] if set to `false`, the ui controls will become stateful, such that they respond immediately to user interaction.
 * @return {HTMLElement} the stompbox element.
 */
export function createView (parameters, options)
{
    const customElementName = "cmaj-adc-2023-tremolo-view";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, PatchView);

    return new PatchView (parameters, options);
}

//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

// Inspired by assets created by Mik Skuza: https://dribbble.com/shots/10065752-Guitar-Effects-Made-In-Figma-for-you-for-free

class PatchView extends HTMLElement
{
    constructor (parameters = {}, { controlled = true } = {})
    {
        super();

        const shadow = this.attachShadow ({ mode: "closed" });

        const setupKnob = (elementId, config) =>
        {
            return makeRotatable ({
                ...config,
                element: shadow.getElementById (elementId),
                maxRotation: 140,
                controlled,
            });
        };

        const setupSwitch = (config)  =>
        {
            const ledElement = shadow.getElementById ("power-led");

            const setLedActive = (active) =>
            {
                ledElement.classList.remove (! active ? "led-on" : "led-off");
                ledElement.classList.add (active ? "led-on" : "led-off");
            };

            const negateArgument = (fn) => (value) => fn (! value);

            const update = makeSwitchable ({
                onValueUpdated: negateArgument (setLedActive),
                element: shadow.getElementById ("stomp-switch"),
                ...config,
                controlled,
            });

            const toBool = (v) => !! v;

            return (nextValue) => update (toBool (nextValue));
        };

        this.connectedCallbackImpl = () =>
        {
            shadow.innerHTML = getHTML();

            const subscriptions = [];

            const setupAndSubscribe = (parameter, setup) =>
            {
                const update = setup (parameter);

                const unsubscribe = parameter?.subscribe?.(update);

                if (unsubscribe)
                    subscriptions.push (unsubscribe);
            };

            setupAndSubscribe (parameters.bypass, (parameter) => setupSwitch (parameter));
            setupAndSubscribe (parameters.wave, (parameter) => setupKnob ("knob-wave", parameter));
            setupAndSubscribe (parameters.rate, (parameter) => setupKnob ("knob-rate", parameter));
            setupAndSubscribe (parameters.depth, (parameter) => setupKnob ("knob-depth", parameter));

            this.disconnectedCallbackImpl = () =>
            {
                subscriptions.forEach ((unsubscribe) => unsubscribe?.());
                subscriptions.length = 0;
            };
        };
    }

    connectedCallback()
    {
        this.connectedCallbackImpl?.();
    }

    disconnectedCallback()
    {
        this.disconnectedCallbackImpl?.();
    }
}

function resolvePath (path)
{
    return new URL (path, import.meta.url);
}

function getHTML()
{
    return `
<style>
* {
    user-select: none;
    -webkit-user-select: none;
    -moz-user-select: none;
    -ms-user-select: none;
}

.enclosure-svg {
    width: 250px;
    height: 423px;

    background-image: url(${resolvePath ("./enclosure.svg")});
}

.big-knob-svg {
    width: 68px;
    height: 68px;

    background-image: url(${resolvePath ("./big-knob-flat.svg")});
}

.smaller-knob-svg {
    width: 48px;
    height: 48px;

    background-image: url(${resolvePath ("./smaller-knob-flat.svg")});
}

.controls {
    position: absolute;
    left: 25px;
    top: 12px;
}

.slot-offset {
    position: relative;
    margin-left: 10px;
    height: 100%;
}

.rectangular-button-svg {
    width: 216px;
    height: 145px;

    background-image: url(${resolvePath ("./button.svg")});

    display: flex;
    align-items: center;
    justify-content: center;
}

.logo {
    width: 100%;
    height: 100%;

    -webkit-mask-repeat: no-repeat;
    -webkit-mask: url("${cmajorLogoInlineSvg}");
    mask: url("${cmajorLogoInlineSvg}");
    mask-repeat: no-repeat;
    background-color: #212226;
}

.logo-wrapper {
    width: 80%;
    height: 50%;

    filter: drop-shadow(-1px -1px 0px rgba(255, 255, 255, 0.06)) drop-shadow(1px 1px 0px rgba(0, 0, 0, 0.5));
}

#power-led {
    position: absolute;
    left: 110px;
    top: 20px;
}

#knob-rate {
    position: absolute;
    top: 8px;
}

#knob-depth {
    position: absolute;
    top: 8px;
    left: 111px;
}

#knob-wave {
    position: absolute;
    left: 66px;
    top: 78px;
}

#stomp-switch {
    position: absolute;
    left: 7px;
    bottom: 8px;
}

.led {
    display: flex;
    align-items: center;
    justify-content: center;

    border-radius: 50%;
    width: 10px;
    height: 10px;

    box-shadow: inset 1px 1px 2px rgba(0, 0, 0, 0.4);
}

.led-reflection {
    border-radius: 50%;

    clip-path: path("M6.00001 1.66622C5.54611 1.4185 5.02548 1.27771 4.47196 1.27771C2.70782 1.27771 1.2777 2.70783 1.2777 4.47196C1.2777 5.02548 1.41849 5.5461 1.6662 6C0.673474 5.45819 0 4.40487 0 3.19425C0 1.43012 1.43012 0 3.19425 0C4.40488 0 5.45821 0.673483 6.00001 1.66622Z");

    background: rgba(255, 255, 255, 0.5);
    width: 6px;
    height: 6px;
}

.led-on {
    background: rgba(255, 0, 0, 1.0);
}

.led-off {
    background: rgba(255, 0, 0, 0.5);
}
</style>

<div class="enclosure-svg">
    <div class="slot-offset">
        <div id="power-led" class="led led-off">
            <div class="led-reflection"></div>
        </div>
        <div class="controls">
            <div id="knob-rate" class="big-knob-svg"></div>
            <div id="knob-depth" class="big-knob-svg"></div>
            <div id="knob-wave" class="smaller-knob-svg"></div>
        </div>
        <div id="stomp-switch" class="rectangular-button-svg">
            <div class="logo-wrapper">
                <div class="logo"></div>
            </div>
        </div>
    </div>
</div>
    `;
}

function makeRotatable ({
    initialValue,
    min = 0,
    max = 1,
    onBeginEdit = () => {},
    onEdit = () => {},
    onEndEdit = () => {},
    maxRotation = 132,
    element,
    controlled = true,
} = {})
{
    initialValue = initialValue ?? min;

    const remap = (source, sourceFrom, sourceTo, targetFrom, targetTo) =>
    {
        return targetFrom + (source - sourceFrom) * (targetTo - targetFrom) / (sourceTo - sourceFrom);
    };

    const toValue = (knobRotation) => remap (knobRotation, -maxRotation, maxRotation, min, max);
    const toRotation = (value) => remap (value, min, max, -maxRotation, maxRotation);

    const state =
    {
        rotation: undefined,
    };

    const update = (nextValue, force) =>
    {
        const degrees = toRotation (nextValue);

        if (! force && state.rotation === degrees) return;

        state.rotation = degrees;

        element.style.transform = `rotate(${degrees}deg)`
    };

    onEdit = toStatefulEditCallback (controlled, onEdit, update);

    const force = true;
    update (initialValue, force);

    let accumulatedRotation = undefined;

    const onMouseMove = (event) =>
    {
        event.preventDefault(); // avoid scrolling whilst dragging

        const nextRotation = (rotation, delta) =>
        {
            const clamp = (v, min, max) => Math.min (Math.max (v, min), max);

            return clamp (rotation - delta, -maxRotation, maxRotation);
        };

        const speedMultiplier = event.shiftKey ? 0.25 : 1.5;
        accumulatedRotation = nextRotation (accumulatedRotation, event.movementY * speedMultiplier);
        onEdit?.(toValue (accumulatedRotation));
    };

    const onMouseUp = () =>
    {
        accumulatedRotation = undefined;
        window.removeEventListener ("mousemove", onMouseMove);
        window.removeEventListener ("mouseup", onMouseUp);
        onEndEdit?.();
    };

    const onMouseDown = () =>
    {
        accumulatedRotation = state.rotation;
        onBeginEdit?.();
        window.addEventListener ("mousemove", onMouseMove);
        window.addEventListener ("mouseup", onMouseUp);
    };

    const onReset = () => setValueAsGesture (initialValue, { onBeginEdit, onEdit, onEndEdit });

    element.addEventListener ("mousedown", onMouseDown);
    element.addEventListener ("dblclick", onReset);

    return update;
}

function makeSwitchable ({
    initialValue = false,
    onBeginEdit = () => {},
    onEdit = () => {},
    onEndEdit = () => {},
    element,
    onValueUpdated,
    controlled = true,
} = {})
{
    let active = initialValue;

    const update = (nextValue, force) =>
    {
        if (! force && active === nextValue) return;

        active = nextValue;

        onValueUpdated (active);
    };

    onEdit = toStatefulEditCallback (controlled, onEdit, update);

    const force = true;
    update (active, force);

    element.addEventListener ("click", () => setValueAsGesture (! active, { onBeginEdit, onEdit, onEndEdit }));

    return update;
}

function setValueAsGesture (value, { onBeginEdit, onEdit, onEndEdit })
{
    onBeginEdit?.();
    onEdit?.(value);
    onEndEdit?.();
}

function toStatefulEditCallback (controlled, onEdit, update)
{
    if (controlled)
        return onEdit;

    return (nextValue) =>
    {
        onEdit?.(nextValue);
        update?.(nextValue);
    };
}
