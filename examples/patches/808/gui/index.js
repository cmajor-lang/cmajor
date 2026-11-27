//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

/*
    The 808's GUI: a 3D model of a classic-style drum machine, drawn with WebGL.

    - Drag a knob up/down to turn it (shift for fine control), or double-click to reset it.
    - Click an instrument name in the label strip to select it (and hear it). The 16 step
      keys then show and edit that instrument's steps.
    - START / STOP runs the sequencer (or press the space bar), and CLEAR empties the
      selected instrument's steps.
    - Drag anywhere on the case to look around the machine; double-click it to reset the view.

    The pattern is kept in the patch's stored state (see worker.js), and the knobs are the
    patch's parameters, so a host will save and restore them all.
*/

import { Renderer, mat4, hexToLinear } from "./renderer.js";
import { createRoundedBox, createFlatRectangle, createKnobMeshes, createLEDMeshes } from "./geometry.js";
import { paintPanel } from "./panel-art.js";
import { panelWidth, panelDepth, bodyHeight, tracks, accentTrack, layout,
         valueToPosition, positionToValue, positionToAngle,
         getMIDINoteForTrack, getTrackForMIDINote } from "./layout.js";

const keyHeight = 8, keyTravel = 2.4, keyPressTime = 140;
const ledRadius = 2, flashTime = 180;
const ledColour = [1.0, 0.1, 0.04];

const defaultCamera = { yaw: 0, pitch: 0.95 };

//==============================================================================
class DrumMachineView extends HTMLElement
{
    constructor (patchConnection)
    {
        super();
        this.patchConnection = patchConnection;

        this.knobValues = new Map (layout.knobs.map (k => [k.id, k.init]));
        this.pattern = new Array (tracks.length).fill (0);
        this.selectedTrack = 0;
        this.currentStep = -1;
        this.isRunning = false;
        this.lastHitTimes = new Array (tracks.length).fill (-1e9);
        this.keyPressTimes = new Map();
        this.camera = { ...defaultCamera };

        this.attachShadow ({ mode: "open" });
        this.shadowRoot.innerHTML = this.getHTML();
        this.canvas = this.shadowRoot.querySelector ("canvas");
        this.readout = this.shadowRoot.querySelector ("#readout");
    }

    connectedCallback()
    {
        // createPatchView() pins the view to the manifest's width and height, which hosts use as
        // its initial size. This view can draw at any size, so it fills its container instead.
        this.style.width = "100%";
        this.style.height = "100%";

        try
        {
            this.renderer = new Renderer (this.canvas);
        }
        catch (error)
        {
            this.shadowRoot.querySelector ("#error").textContent = "Sorry, this GUI needs WebGL2: " + error.message;
            return;
        }

        this.createScene();
        this.addPatchListeners();
        this.addMouseListeners();

        this.resizeObserver = new ResizeObserver (() => this.requestRender());
        this.resizeObserver.observe (this);

        this.tabIndex = 0;
        this.keyListener = (e) =>
        {
            if (e.code === "Space")
            {
                this.toggleRunning();
                e.preventDefault();
            }
        };
        this.addEventListener ("keydown", this.keyListener);

        this.requestRender();
    }

    disconnectedCallback()
    {
        this.removePatchListeners();
        this.resizeObserver?.disconnect();
        this.removeEventListener ("keydown", this.keyListener);
        cancelAnimationFrame (this.animationFrame);
        this.animationFrame = undefined;
    }

    //==============================================================================
    // Talking to the patch

    addPatchListeners()
    {
        const pc = this.patchConnection;

        this.parameterListener = ({ endpointID, value }) =>
        {
            if (this.knobValues.has (endpointID))
            {
                this.knobValues.set (endpointID, value);
                this.requestRender();
            }
        };

        // The patch's status tells us the real ranges and names of the parameters
        this.statusListener = (status) =>
        {
            for (const input of status?.details?.inputs ?? [])
            {
                const knob = layout.knobs.find (k => k.id === input.endpointID);
                const annotation = input.annotation;

                if (knob && annotation)
                {
                    knob.min  = annotation.min  ?? knob.min;
                    knob.max  = annotation.max  ?? knob.max;
                    knob.init = annotation.init ?? knob.init;
                    knob.name = annotation.name;
                }
            }

            this.requestRender();
        };

        this.stateListener = ({ key, value }) =>
        {
            if (key === "pattern" && Array.isArray (value) && value.length === tracks.length)
            {
                this.pattern = value.slice();
                this.requestRender();
            }
        };

        this.stepListener = (step) =>
        {
            this.currentStep = step;
            this.isRunning = step >= 0;

            if (step >= 0)
                for (let track = 0; track < accentTrack; ++track)
                    if (this.isStepSet (track, step))
                        this.lastHitTimes[track] = performance.now();

            this.requestRender();
        };

        this.midiListener = ({ message }) =>
        {
            const midi = pc.utilities.midi;
            const track = midi.isNoteOn (message) ? getTrackForMIDINote (midi.getNoteNumber (message)) : undefined;

            if (track !== undefined)
            {
                this.lastHitTimes[track] = performance.now();
                this.requestRender();
            }
        };

        pc.addAllParameterListener (this.parameterListener);
        pc.addStatusListener (this.statusListener);
        pc.addStoredStateValueListener (this.stateListener);
        pc.addEndpointListener ("currentStep", this.stepListener);
        pc.addEndpointListener ("midiIn", this.midiListener);

        pc.requestStatusUpdate();
        pc.requestStoredStateValue ("pattern");

        for (const knob of layout.knobs)
            pc.requestParameterValue (knob.id);
    }

    removePatchListeners()
    {
        const pc = this.patchConnection;

        if (! this.parameterListener)
            return;

        pc.removeAllParameterListener (this.parameterListener);
        pc.removeStatusListener (this.statusListener);
        pc.removeStoredStateValueListener (this.stateListener);
        pc.removeEndpointListener ("currentStep", this.stepListener);
        pc.removeEndpointListener ("midiIn", this.midiListener);
        this.parameterListener = undefined;
    }

    isStepSet (track, step)      { return (this.pattern[track] & (1 << step)) !== 0; }

    setPattern (newPattern)
    {
        this.pattern = newPattern;
        this.patchConnection.sendStoredStateValue ("pattern", newPattern);
        this.requestRender();
    }

    toggleStep (step)
    {
        const newPattern = this.pattern.slice();
        newPattern[this.selectedTrack] ^= (1 << step);
        this.setPattern (newPattern);
    }

    clearSelectedTrack()
    {
        const newPattern = this.pattern.slice();
        newPattern[this.selectedTrack] = 0;
        this.setPattern (newPattern);
    }

    toggleRunning()
    {
        this.isRunning = ! this.isRunning;

        if (! this.isRunning)
            this.currentStep = -1;

        this.patchConnection.sendEventOrValue ("run", this.isRunning);
        this.requestRender();
    }

    selectTrack (track)
    {
        this.selectedTrack = track;

        const note = getMIDINoteForTrack (track);

        if (note !== undefined)
        {
            this.patchConnection.sendMIDIInputEvent ("midiIn", 0x900000 | (note << 8) | 100);
            setTimeout (() => this.patchConnection.sendMIDIInputEvent ("midiIn", 0x800000 | (note << 8)), 100);
        }

        this.requestRender();
    }

    setKnobPosition (knob, position)
    {
        const value = positionToValue (knob, position);
        this.knobValues.set (knob.id, value);
        this.patchConnection.sendEventOrValue (knob.id, value);
        this.requestRender();
    }

    //==============================================================================
    // Building and drawing the 3D scene

    createScene()
    {
        const r = this.renderer;

        const knobMeshes = new Map();
        const keyMeshes = new Map();

        const getKnobMeshes = (style) =>
        {
            if (! knobMeshes.has (style))
            {
                const { body, cap, pointer } = createKnobMeshes (style);
                knobMeshes.set (style, { body: r.createMesh (body), cap: r.createMesh (cap), pointer: r.createMesh (pointer) });
            }

            return knobMeshes.get (style);
        };

        const getKeyMesh = (width, depth) =>
        {
            const key = `${width}x${depth}`;

            if (! keyMeshes.has (key))
                keyMeshes.set (key, r.createMesh (createRoundedBox (width, keyHeight, depth, 2.2, 3)));

            return keyMeshes.get (key);
        };

        const led = createLEDMeshes (ledRadius);

        this.scene = {
            // The case is a little bigger than the panel, so that the panel covers its flat top
            body:       r.createMesh (createRoundedBox (panelWidth + 8, bodyHeight, panelDepth + 8, 4, 5)),
            endCheek:   r.createMesh (createRoundedBox (16, bodyHeight + 8, panelDepth + 14, 5, 5)),
            panel:      r.createMesh (createFlatRectangle (panelWidth, panelDepth)),
            panelImage: r.uploadTexture (paintPanel()),
            unitSquare: r.createMesh (createFlatRectangle (1, 1)),
            ledBezel:   r.createMesh (led.bezel),
            ledLens:    r.createMesh (led.lens),

            knobs: layout.knobs.map (knob => ({ knob, meshes: getKnobMeshes (knob.style),
                                                capColour: hexToLinear (knob.style.cap),
                                                pointerColour: hexToLinear (knob.style.pointer) })),

            keys: [...layout.stepKeys, layout.startKey, layout.clearKey]
                     .map (key => ({ key, mesh: getKeyMesh (key.width, key.depth), colour: hexToLinear (key.colour) })),
        };

        this.materials = {
            body:     { colour: hexToLinear ("#1d1d20"), specular: 0.35, shininess: 30 },
            cheek:    { colour: hexToLinear ("#111112"), specular: 0.3,  shininess: 25 },
            panel:    { texture: this.scene.panelImage, specular: 0.2, shininess: 50 },
            knobBody: { colour: hexToLinear ("#141415"), specular: 0.7, shininess: 90 },
            bezel:    { colour: hexToLinear ("#0b0b0b"), specular: 0.4, shininess: 40 },
        };
    }

    requestRender()
    {
        if (this.animationFrame === undefined && this.renderer)
            this.animationFrame = requestAnimationFrame (() =>
            {
                this.animationFrame = undefined;

                if (this.render())
                    this.requestRender();
            });
    }

    getCameraMatrices()
    {
        const aspect = Math.max (0.1, this.canvas.clientWidth / Math.max (1, this.canvas.clientHeight));
        const projection = mat4.perspective (28 * Math.PI / 180, aspect, 50, 20000);
        const target = [panelWidth / 2, -bodyHeight * 0.35, panelDepth / 2];
        const { yaw, pitch } = this.camera;
        const direction = [Math.sin (yaw) * Math.cos (pitch), Math.sin (pitch), Math.cos (yaw) * Math.cos (pitch)];

        const getMatrices = (distance) =>
        {
            const eye = target.map ((t, i) => t + direction[i] * distance);
            const viewProjection = mat4.multiply (projection, mat4.lookAt (eye, target, [0, 1, 0]));
            return { eye, viewProjection };
        };

        // Find the distance at which the whole machine just fits in the view
        const corners = [];

        for (const x of [-30, panelWidth + 30])
            for (const y of [-bodyHeight, 15])
                for (const z of [-12, panelDepth + 12])
                    corners.push ([x, y, z]);

        const getExtent = (m) => Math.max (...corners.map (c => { const p = mat4.transformPoint (m, c); return Math.max (Math.abs (p[0]), Math.abs (p[1])); }));

        let near = 200, far = 20000;

        for (let i = 0; i < 30; ++i)
        {
            const mid = (near + far) / 2;

            if (getExtent (getMatrices (mid).viewProjection) > 0.95)
                near = mid;
            else
                far = mid;
        }

        return getMatrices (far);
    }

    /// Draws a frame, and returns true if something is still animating
    render()
    {
        const r = this.renderer, scene = this.scene, materials = this.materials;
        const now = performance.now();
        let isAnimating = false;

        const { eye, viewProjection } = this.getCameraMatrices();
        this.viewProjection = viewProjection;
        r.beginFrame (viewProjection, eye);

        // The case, its end cheeks and the printed panel
        r.drawMesh (scene.body, mat4.translation (panelWidth / 2, -bodyHeight, panelDepth / 2), materials.body);
        r.drawMesh (scene.endCheek, mat4.translation (-12, -bodyHeight - 2, panelDepth / 2), materials.cheek);
        r.drawMesh (scene.endCheek, mat4.translation (panelWidth + 12, -bodyHeight - 2, panelDepth / 2), materials.cheek);
        r.drawMesh (scene.panel, mat4.translation (0, 0.05, 0), materials.panel);

        for (const { knob, meshes, capColour, pointerColour } of scene.knobs)
        {
            const angle = positionToAngle (valueToPosition (knob, this.knobValues.get (knob.id)));
            const model = mat4.multiply (mat4.translation (knob.x, 0, knob.z), mat4.rotationY (-angle));

            r.drawMesh (meshes.body, model, materials.knobBody);
            r.drawMesh (meshes.cap, model, { colour: capColour, specular: 0.45, shininess: 60 });
            r.drawMesh (meshes.pointer, model, { colour: pointerColour, specular: 0.3, shininess: 30 });
        }

        for (const { key, mesh, colour } of scene.keys)
        {
            const sincePress = now - (this.keyPressTimes.get (key) ?? -1e9);
            const press = sincePress < keyPressTime ? Math.sin (Math.PI * sincePress / keyPressTime) : 0;
            isAnimating ||= sincePress < keyPressTime;

            r.drawMesh (mesh, mat4.translation (key.x + key.width / 2, -press * keyTravel, key.z + key.depth / 2),
                        { colour, specular: 0.4, shininess: 45 });
        }

        // The LEDs: first the solid parts, then the glows on top
        const glows = [];

        const drawLED = (x, z, brightness) =>
        {
            const model = mat4.translation (x, 0, z);
            r.drawMesh (scene.ledBezel, model, materials.bezel);
            r.drawMesh (scene.ledLens, model, { colour: [0.16, 0.012, 0.006], specular: 1.0, shininess: 150,
                                                emissive: ledColour.map (c => c * brightness * 2.5) });

            if (brightness > 0.01)
                glows.push ({ x, z, brightness });
        };

        for (const { step, x, z } of layout.stepLEDs)
        {
            const isPlayhead = this.isRunning && step === this.currentStep;
            drawLED (x, z, isPlayhead ? 1 : (this.isStepSet (this.selectedTrack, step) ? 0.4 : 0));
        }

        for (const { track, x, z } of layout.trackLEDs)
        {
            const sinceHit = now - this.lastHitTimes[track];
            const flash = sinceHit < flashTime ? 1 - sinceHit / flashTime : 0;
            isAnimating ||= flash > 0;

            const steady = track === this.selectedTrack ? 0.55 : 0;
            drawLED (x, z, Math.max (steady, flash * (track === this.selectedTrack ? 1 : 0.6)));
        }

        for (const { x, z, brightness } of glows)
            r.drawSprite (scene.unitSquare, { x, y: 2.2, z, width: 18, depth: 18, colour: ledColour, intensity: brightness * 0.8 });

        // A soft shadow on the "table" under the machine
        r.drawSprite (scene.unitSquare, { x: panelWidth / 2, y: -bodyHeight - 0.5, z: panelDepth / 2,
                                          width: panelWidth + 140, depth: panelDepth + 110,
                                          isShadow: true, intensity: 0.75, softness: 0.08 });

        // Ease the camera back after a double-click
        if (this.cameraTarget)
        {
            this.camera.yaw   += (this.cameraTarget.yaw   - this.camera.yaw)   * 0.15;
            this.camera.pitch += (this.cameraTarget.pitch - this.camera.pitch) * 0.15;

            if (Math.abs (this.camera.yaw - this.cameraTarget.yaw) + Math.abs (this.camera.pitch - this.cameraTarget.pitch) < 0.001)
                this.cameraTarget = undefined;

            isAnimating = true;
        }

        return isAnimating;
    }

    //==============================================================================
    // Mouse handling

    /// Finds the point on the panel under the mouse, by casting a ray from the camera
    getPanelPosition (event)
    {
        if (! this.viewProjection)
            return undefined;

        const bounds = this.canvas.getBoundingClientRect();
        const ndcX = ((event.clientX - bounds.left) / bounds.width) * 2 - 1;
        const ndcY = 1 - ((event.clientY - bounds.top) / bounds.height) * 2;

        const inverse = mat4.invert (this.viewProjection);
        const near = mat4.transformPoint (inverse, [ndcX, ndcY, -1]);
        const far  = mat4.transformPoint (inverse, [ndcX, ndcY, 1]);

        // Intersect with a plane at about the height of the tops of the keys
        const planeHeight = 5;
        const t = (planeHeight - near[1]) / (far[1] - near[1]);

        if (! (t > 0))
            return undefined;

        return { x: near[0] + (far[0] - near[0]) * t, z: near[2] + (far[2] - near[2]) * t };
    }

    findControlAt (position)
    {
        if (! position)
            return undefined;

        const { x, z } = position;
        const isInside = (r, margin = 1) => x >= r.x - margin && x <= r.x + r.width + margin && z >= r.z - margin && z <= r.z + r.depth + margin;

        const knob = layout.knobs.find (k => Math.hypot (x - k.x, z - k.z) < k.style.skirtRadius + 3);
        if (knob)  return { type: "knob", knob };

        const stepKey = layout.stepKeys.find (k => isInside (k));
        if (stepKey)  return { type: "step", key: stepKey };

        if (isInside (layout.startKey))  return { type: "start", key: layout.startKey };
        if (isInside (layout.clearKey))  return { type: "clear", key: layout.clearKey };

        const selector = layout.trackSelectors.find (s => isInside (s, 0));
        if (selector)  return { type: "select", track: selector.track };

        const isOnMachine = x > -20 && x < panelWidth + 20 && z > -10 && z < panelDepth + 10;
        return isOnMachine ? { type: "case" } : undefined;
    }

    addMouseListeners()
    {
        const canvas = this.canvas;

        canvas.addEventListener ("pointerdown", (e) =>
        {
            this.focus();

            const control = this.findControlAt (this.getPanelPosition (e));
            const drag = { startX: e.clientX, startY: e.clientY, control };

            if (control?.key)
                this.keyPressTimes.set (control.key, performance.now());

            switch (control?.type)
            {
                case "step":    this.toggleStep (control.key.step); break;
                case "start":   this.toggleRunning(); break;
                case "clear":   this.clearSelectedTrack(); break;
                case "select":  this.selectTrack (control.track); break;

                case "knob":
                    drag.startPosition = valueToPosition (control.knob, this.knobValues.get (control.knob.id));
                    this.patchConnection.sendParameterGestureStart (control.knob.id);
                    this.showReadout (control.knob, e);
                    break;

                default:
                    drag.startCamera = { ...this.camera };
                    this.cameraTarget = undefined;
                    break;
            }

            this.drag = drag;
            canvas.setPointerCapture (e.pointerId);
            this.requestRender();
            e.preventDefault();
        });

        canvas.addEventListener ("pointermove", (e) =>
        {
            const drag = this.drag;

            if (! drag)
            {
                const control = this.findControlAt (this.getPanelPosition (e));
                canvas.style.cursor = ! control ? "default"
                                    : control.type === "knob" ? "ns-resize"
                                    : control.type === "case" ? "grab" : "pointer";
                return;
            }

            if (drag.control?.type === "knob")
            {
                const sensitivity = e.shiftKey ? 1200 : 250;
                const knob = drag.control.knob;
                this.setKnobPosition (knob, drag.startPosition + (drag.startY - e.clientY) / sensitivity);
                this.showReadout (knob, e);
            }
            else if (drag.startCamera)
            {
                canvas.style.cursor = "grabbing";
                this.camera.yaw   = Math.max (-0.8, Math.min (0.8,  drag.startCamera.yaw   - (e.clientX - drag.startX) * 0.004));
                this.camera.pitch = Math.max (0.3,  Math.min (1.45, drag.startCamera.pitch + (e.clientY - drag.startY) * 0.004));
                this.requestRender();
            }
        });

        const endDrag = (e) =>
        {
            if (this.drag?.control?.type === "knob")
                this.patchConnection.sendParameterGestureEnd (this.drag.control.knob.id);

            this.drag = undefined;
            this.readout.style.display = "none";
            canvas.releasePointerCapture?.(e.pointerId);
        };

        canvas.addEventListener ("pointerup", endDrag);
        canvas.addEventListener ("pointercancel", endDrag);

        canvas.addEventListener ("dblclick", (e) =>
        {
            const control = this.findControlAt (this.getPanelPosition (e));

            if (control?.type === "knob")
            {
                this.patchConnection.sendParameterGestureStart (control.knob.id);
                this.setKnobPosition (control.knob, valueToPosition (control.knob, control.knob.init));
                this.patchConnection.sendParameterGestureEnd (control.knob.id);
            }
            else if (! control || control.type === "case")
            {
                this.cameraTarget = { ...defaultCamera };
                this.requestRender();
            }
        });

        canvas.addEventListener ("wheel", (e) =>
        {
            const control = this.findControlAt (this.getPanelPosition (e));

            if (control?.type === "knob")
            {
                const knob = control.knob;
                const position = valueToPosition (knob, this.knobValues.get (knob.id));
                this.setKnobPosition (knob, position - Math.sign (e.deltaY) * 0.02);
                e.preventDefault();
            }
        }, { passive: false });

        canvas.addEventListener ("contextmenu", (e) => e.preventDefault());
    }

    showReadout (knob, event)
    {
        const bounds = this.getBoundingClientRect();
        this.readout.textContent = `${knob.name ?? knob.caption}: ${formatKnobValue (knob, this.knobValues.get (knob.id))}`;
        this.readout.style.display = "block";
        this.readout.style.left = `${event.clientX - bounds.left + 16}px`;
        this.readout.style.top  = `${event.clientY - bounds.top - 28}px`;
    }

    getHTML()
    {
        return `
        <style>
            :host {
                display: block;
                position: relative;
                width: 100%;
                height: 100%;
                min-width: 0;
                min-height: 0;
                overflow: hidden;
                outline: none;
                user-select: none;
                -webkit-user-select: none;
                background: radial-gradient(ellipse at 50% 40%, #4a4845 0%, #262523 55%, #121211 100%);
            }

            /* (absolutely positioned, so that the size of its drawing buffer, which is set
               from its size on screen, can never feed back into the layout) */
            canvas {
                position: absolute;
                inset: 0;
                width: 100%;
                height: 100%;
                touch-action: none;
            }

            #readout {
                display: none;
                position: absolute;
                pointer-events: none;
                padding: 4px 8px;
                border-radius: 4px;
                background: #000000cc;
                color: #f2efe6;
                font: 600 12px "Helvetica Neue", Helvetica, Arial, sans-serif;
                white-space: nowrap;
            }

            #error {
                position: absolute;
                inset: 0;
                display: grid;
                place-items: center;
                color: #f2efe6;
                font: 14px sans-serif;
                pointer-events: none;
            }
        </style>

        <canvas></canvas>
        <div id="readout"></div>
        <div id="error"></div>`;
    }
}

//==============================================================================
function formatKnobValue (knob, value)
{
    if (knob.units)
        return `${value.toFixed (knob.decimals)} ${knob.units}`;

    if (knob.isBipolar)
        return `${value >= 0 ? "+" : ""}${value.toFixed (1)} semitones`;

    if (knob.caption === "DECAY")
    {
        // This matches the mapping in ChannelControls::setDecay() in 808.cmajor
        if (value >= 0.999)
            return "natural";

        const seconds = 0.03 * Math.pow (100, value);
        return seconds < 1 ? `${Math.round (seconds * 1000)} ms` : `${seconds.toFixed (2)} s`;
    }

    return (value * 10).toFixed (1);
}

//==============================================================================
export default function createPatchView (patchConnection)
{
    const customElementName = "cmaj-808-drum-machine";

    if (! window.customElements.get (customElementName))
        window.customElements.define (customElementName, DrumMachineView);

    return new (window.customElements.get (customElementName)) (patchConnection);
}
