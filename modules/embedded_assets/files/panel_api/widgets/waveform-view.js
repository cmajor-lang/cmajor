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
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

import { el, Disposer, trackPointer } from "../core/dom.js";
import { getThemeColours, addThemeChangeListener } from "../core/theme.js";
import { frameScheduler } from "../core/frame-scheduler.js";
import { observeCanvasSize } from "./widget-base.js";

/*
    The waveform viewer: shows one Recording (core/recorder.js) as a lane per endpoint channel,
    under a time ruler, with its dropouts (hatched), its markers (thin lines, coloured by kind),
    a cursor readout and a selectable time range.

    - Zoom: Ctrl/Cmd + wheel (or a trackpad pinch, which sends ctrlKey wheel events, or in
      Safari gesturestart/gesturechange events) around the pointer; the + / − keys; the
      toolbar's − Fit +. From the whole recording down to
      minFramesPerPixel (a few pixels per sample).
    - Pan: a plain or shift wheel; dragging on the ruler; ← / → (Shift: a page); Home / End.
    - Select: drag in the lanes; Shift-click extends; Escape clears. "Export selection" calls
      options.onExportSelection (selection).
    - Each lane is drawn from Recording.getMinMax() (one bucket per device pixel), or, below
      samplesDrawThreshold frames per pixel, from getSamples() as a line through the samples.
    - While the recording runs, the view follows its live end, until the user pans or zooms away
      from it; the Follow button (shown then) resumes.
    - It's drawn through the frame scheduler only when something changes (new audio arriving,
      the view moving, the pointer, the theme), never in a loop of its own.

    The maths (frameToX(), zoomViewport(), getRulerTicks(), normaliseSelection()...) is in plain
    functions with no DOM, for the tests. A viewport is { start, framesPerPixel }, in frames and
    CSS pixels; start is the frame at the plot's left edge.
*/

/// The deepest zoom, in frames per CSS pixel (0.25: the samples are 4 pixels apart)
export const minFramesPerPixel = 0.25;

/// Below this many frames per pixel, the lanes show the samples as a line rather than min/max columns
export const samplesDrawThreshold = 2;

/// Below this many frames per pixel, the ruler counts samples rather than time
export const samplesRulerThreshold = 1;

/// While recording, the view's timeline is at least this long, so a new recording grows across it
export const liveWindowSeconds = 5;

/// The ruler's height, and the least room between labelled ticks (CSS pixels)
export const rulerHeight = 20;
export const minRulerTickSpacing = 84;

//==============================================================================
export function clampValue (v, lo, hi)         { return Math.max (lo, Math.min (hi, v)); }

/// The x (CSS pixels from the plot's left) of a frame, and the frame at an x
export function frameToX (frame, view)          { return (frame - view.start) / view.framesPerPixel; }
export function xToFrame (x, view)              { return view.start + x * view.framesPerPixel; }

/// The frames per pixel that shows numFrames across width pixels (never deeper than minFpp)
export function getFitFramesPerPixel (numFrames, width, minFpp = minFramesPerPixel)
{
    return Math.max (minFpp, numFrames / Math.max (1, width));
}

/// A viewport showing everything
export function fitViewport (numFrames, width, minFpp = minFramesPerPixel)
{
    return { start: 0, framesPerPixel: getFitFramesPerPixel (numFrames, width, minFpp) };
}

/// Keeps a viewport within the timeline: zoomed no further out than the whole of it and no
/// further in than minFpp, and its start between 0 and the last screenful
export function clampViewport (view, numFrames, width, minFpp = minFramesPerPixel)
{
    const fpp = clampValue (Number.isFinite (view.framesPerPixel) ? view.framesPerPixel : Infinity,
                            minFpp, getFitFramesPerPixel (numFrames, width, minFpp));
    const maxStart = Math.max (0, numFrames - fpp * Math.max (1, width));
    return { start: clampValue (Number.isFinite (view.start) ? view.start : 0, 0, maxStart), framesPerPixel: fpp };
}

/// Zooms by factor (> 1 zooms in), keeping the frame under anchorX where it is (as far as the
/// clamping allows)
export function zoomViewport (view, factor, anchorX, numFrames, width, minFpp = minFramesPerPixel)
{
    const anchorFrame = xToFrame (anchorX, view);
    const fpp = clampViewport ({ start: 0, framesPerPixel: view.framesPerPixel / factor }, numFrames, width, minFpp).framesPerPixel;
    return clampViewport ({ start: anchorFrame - anchorX * fpp, framesPerPixel: fpp }, numFrames, width, minFpp);
}

/// Moves the view by dx pixels (positive: later in time)
export function panViewport (view, dx, numFrames, width, minFpp = minFramesPerPixel)
{
    return clampViewport ({ start: view.start + dx * view.framesPerPixel, framesPerPixel: view.framesPerPixel }, numFrames, width, minFpp);
}

/// The same zoom, with its right edge at the end of the timeline (for following a live recording)
export function viewportAtEnd (view, numFrames, width, minFpp = minFramesPerPixel)
{
    return clampViewport ({ start: numFrames - view.framesPerPixel * width, framesPerPixel: view.framesPerPixel }, numFrames, width, minFpp);
}

/// True if the view shows the end of the timeline
export function isViewportAtEnd (view, numFrames, width)
{
    return view.start + view.framesPerPixel * width >= numFrames - Math.max (1, view.framesPerPixel);
}

/// True if the view shows the whole timeline
export function isViewportFitted (view, numFrames, width, minFpp = minFramesPerPixel)
{
    return view.start <= 0 && view.framesPerPixel >= getFitFramesPerPixel (numFrames, width, minFpp) * (1 - 1e-9);
}

/// The zoom factor for a wheel event's deltaY (in pixels, or lines with deltaMode 1): a mouse
/// wheel's notch (~100 px) is about ×1.5, and a pinch's small steps are smooth
export function getWheelZoomFactor (deltaY, deltaMode = 0)
{
    const pixels = deltaMode === 1 ? deltaY * 16 : (deltaMode === 2 ? deltaY * 400 : deltaY);
    return Math.exp (-clampValue (pixels, -400, 400) * 0.004);
}

/// "minmax" (columns from getMinMax) or "samples" (a line through each sample)
export function chooseWaveformDrawMode (framesPerPixel)
{
    return framesPerPixel < samplesDrawThreshold ? "samples" : "minmax";
}

//==============================================================================
/// The smallest of 1, 2, 5 × 10^n that's ≥ value
export function niceStep (value)
{
    if (! (value > 0))
        return 1;

    const power = Math.pow (10, Math.floor (Math.log10 (value)));

    for (const m of [1, 2, 5])
        if (m * power >= value * (1 - 1e-9))
            return m * power;

    return 10 * power;
}

/// The time steps (in seconds) for long views: whole seconds, then minutes
const longTimeSteps = [1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];

/** The ruler's ticks for a view width pixels wide. Labelled (major) ticks are at least
    minSpacing pixels apart, on a round step: 1-2-5 × 10^n seconds (then 10 s, 15 s, 30 s,
    minutes), or, below samplesRulerThreshold frames per pixel, 1-2-5 × 10^n samples. Between
    them are unlabelled minor ticks (5 per step, or 2 for a step of 2 × 10^n).
    Returns { ticks: [{ frame, x, label, isMajor }], unit: "time" | "samples", step } (step in
    seconds or samples). */
export function getRulerTicks (view, width, sampleRate, minSpacing = minRulerTickSpacing)
{
    const minFrames = minSpacing * view.framesPerPixel;
    const isSamples = view.framesPerPixel < samplesRulerThreshold;
    let stepFrames, step;

    if (isSamples)
    {
        step = Math.max (1, niceStep (minFrames));
        stepFrames = step;
    }
    else
    {
        const minSeconds = minFrames / sampleRate;
        step = minSeconds < 1 ? niceStep (minSeconds) : (longTimeSteps.find (s => s >= minSeconds) ?? Math.ceil (minSeconds / 3600) * 3600);
        stepFrames = step * sampleRate;
    }

    const mantissa = Math.round (step / Math.pow (10, Math.floor (Math.log10 (step) + 1e-9)));
    const numMinor = mantissa === 2 ? 2 : 5;
    const minorFrames = stepFrames / numMinor;
    const endFrame = view.start + width * view.framesPerPixel;
    const ticks = [];
    const first = Math.ceil (view.start / minorFrames - 1e-9);

    for (let i = first; i * minorFrames <= endFrame + 1e-9 && ticks.length < 2000; ++i)
    {
        const frame = i * minorFrames;
        const isMajor = i % numMinor === 0;
        const label = isMajor ? (isSamples ? String (Math.round (frame)) : formatRulerTime (frame / sampleRate, step)) : null;
        ticks.push ({ frame, x: frameToX (frame, view), label, isMajor });
    }

    return { ticks, unit: isSamples ? "samples" : "time", step };
}

/// A ruler label: "0 s", "1.5 s", "0.125 s", or "1:30" once the step is a minute or more
export function formatRulerTime (seconds, step)
{
    if (step >= 60)
    {
        const m = Math.floor (seconds / 60 + 1e-9), s = Math.round (seconds - m * 60);
        return `${m}:${String (s).padStart (2, "0")}`;
    }

    const decimals = step >= 1 ? 0 : Math.min (6, Math.ceil (-Math.log10 (step) - 1e-9));
    return `${seconds.toFixed (decimals)} s`;
}

//==============================================================================
/// The range between two frames, in whole frames, kept within [0, numFrames]:
/// { startFrame, endFrame }, or null if it's empty
export function normaliseSelection (a, b, numFrames)
{
    const lo = clampValue (Math.round (Math.min (a, b)), 0, numFrames);
    const hi = clampValue (Math.round (Math.max (a, b)), 0, numFrames);
    return hi > lo ? { startFrame: lo, endFrame: hi } : null;
}

/// Shift-click: moves the selection's nearer edge to frame (the farther one stays put). With
/// no selection, it selects from anchorFrame (e.g. the last click) to frame.
export function extendSelection (selection, frame, numFrames, anchorFrame = 0)
{
    if (! selection)
        return normaliseSelection (anchorFrame, frame, numFrames);

    return normaliseSelection (getSelectionAnchor (selection, frame), frame, numFrames);
}

/// The edge of a selection that stays put when it's extended towards frame (the farther one)
export function getSelectionAnchor (selection, frame)
{
    return Math.abs (frame - selection.startFrame) < Math.abs (frame - selection.endFrame) ? selection.endFrame : selection.startFrame;
}

/// A frame as seconds, for the readouts: "1.23456 s" (enough digits to tell the samples apart)
export function formatFrameTime (frame, sampleRate)
{
    return `${(frame / sampleRate).toFixed (sampleRate > 10000 ? 5 : 4)} s`;
}

/// "0.500 s (22050 samples)"
export function describeSelectionLength (selection, sampleRate)
{
    const n = selection.endFrame - selection.startFrame;
    return `${(n / sampleRate).toFixed (3)} s (${n} sample${n === 1 ? "" : "s"})`;
}

/// The selection, for screen readers: "Selection from 0.500 s to 1.000 s: 0.500 s (22050 samples)"
export function describeSelection (selection, sampleRate)
{
    if (! selection)
        return "No selection. Drag across the waveform, or Shift-click, to select a range.";

    return `Selection from ${(selection.startFrame / sampleRate).toFixed (3)} s to ${(selection.endFrame / sampleRate).toFixed (3)} s: `
             + describeSelectionLength (selection, sampleRate);
}

/// A sample value for the readout: "0.2512", "−0.0031" (with a real minus sign)
export function formatSampleValue (v)
{
    return (Math.abs (v) < 0.00005 ? 0 : v).toFixed (4).replace ("-", "\u2212");
}

/// The lanes of a recording: one per endpoint channel, [{ endpointID, channel, label, shortLabel }]
export function getWaveformLanes (endpoints, getEndpointName = (id) => id)
{
    const lanes = [];

    for (const e of endpoints)
    {
        const n = Math.max (1, e.numChannels || 0);
        const name = getEndpointName (e.endpointID);

        for (let c = 0; c < n; ++c)
        {
            const channelName = n === 2 ? (c === 0 ? "L" : "R") : String (c + 1);
            lanes.push ({ endpointID: e.endpointID, channel: c, label: n === 1 ? name : `${name} · ${channelName}`, shortLabel: n === 1 ? name : channelName });
        }
    }

    return lanes;
}

const markerColourKeys = { user: "waveMarker", event: "waveMarkerEvent", midi: "waveMarkerEvent", parameter: "waveMarkerParameter" };
const markerKindNames = { user: "Marker", event: "Event", midi: "MIDI", parameter: "Parameter" };

/// A marker's tooltip: what it is, when, and how exact that is
export function describeMarker (marker, sampleRate)
{
    const kind = markerKindNames[marker.kind] ?? "Marker";
    const label = marker.label ? `${kind}: ${marker.label}` : kind;
    const accuracy = marker.isApproximate ? "Approximate: stamped with the latest audio received, so it may be up to one chunk (~1024 frames) late"
                                          : "Exact";
    return `${label}\nat ${(marker.frame / sampleRate).toFixed (3)} s (frame ${marker.frame})\n${accuracy}`;
}

//==============================================================================
let waveformCounter = 0;

/*
    The viewer (see the comment at the top).

    options: { scheduler, getEndpointName (endpointID) -> string, onExportSelection (selection) }
    setRecording (recording | null) shows one: it starts fitted, and following the live end if
    the recording is running.
*/
export class WaveformView
{
    constructor ({ scheduler = frameScheduler, getEndpointName = (id) => id, onExportSelection = null } = {})
    {
        this.scheduler = scheduler;
        this.getEndpointName = getEndpointName;
        this.onExportSelection = onExportSelection;
        this.disposer = new Disposer();
        this.recording = null;
        this.removeRecordingListener = null;
        this.view = { start: 0, framesPerPixel: 1 };
        this.isFitted = true;
        this.isFollowing = false;
        this.selection = null;
        this.selectionAnchor = 0;
        this.pointer = null;            // { x, y } in CSS pixels, while it's over the canvas
        this.gestureScale = undefined;  // the last scale of a Safari pinch gesture, while one is going on
        this.drag = null;
        this.isActive = true;
        this.drawCount = 0;
        this.lanes = [];
        this.lanesKey = "";
        this.minMax = { min: new Float32Array (0), max: new Float32Array (0) };
        this.samples = new Float32Array (0);
        this.labelFont = "";

        this.createElements();

        this.draw = () => { this.render(); return false; };
        this.redraw = () => { if (this.isActive) this.scheduler.schedule (this.draw); };

        this.disposer.add (observeCanvasSize (this.canvas, () => { this.labelFont = ""; this.applyViewConstraints(); this.updateReadout(); this.redraw(); }),
                           addThemeChangeListener (this.redraw),
                           () => this.scheduler.cancel (this.draw),
                           () => this.removeRecordingListener?.());
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    createElements()
    {
        const segButton = (label, title, onclick, ariaLabel = title) =>
            el ("button", { class: "cp-seg-button", type: "button", title, "aria-label": ariaLabel, onclick }, label);

        this.zoomOutButton = segButton ("−", "Zoom out (−)", () => this.zoomBy (0.5));
        this.fitButton = segButton ("Fit", "Show the whole recording (F)", () => this.fit(), "Fit: show the whole recording");
        this.zoomInButton = segButton ("+", "Zoom in (+)", () => this.zoomBy (2));
        this.followButton = el ("button", { class: "cp-small-button cp-wave-follow", type: "button", hidden: true,
                                            title: "Follow the end of the recording as it grows", onclick: () => this.setFollowing (true) }, "Follow ▸");
        this.readout = el ("span", { class: "cp-wave-readout", "aria-live": "off" });
        this.selectionText = el ("span", { class: "cp-wave-selection-text", hidden: true });
        this.exportSelectionButton = el ("button", { class: "cp-small-button", type: "button", disabled: true, title: "Export the selected range as a WAV file",
                                                     onclick: () => { if (this.selection) this.onExportSelection?.(this.selection, this.exportSelectionButton); } },
                                         "Export selection…");

        this.toolbar = el ("div", { class: "cp-wave-toolbar", role: "toolbar", "aria-label": "Waveform" },
                           el ("div", { class: "cp-seg", role: "group", "aria-label": "Zoom" }, this.zoomOutButton, this.fitButton, this.zoomInButton),
                           this.followButton, this.readout, el ("span", { class: "cp-spacer" }), this.selectionText, this.exportSelectionButton);

        this.defaultTitle = "Drag to select, Shift-click to extend · drag the ruler or scroll to pan · Ctrl/⌘-scroll or pinch to zoom · keys: + − ← → Home End F Escape";
        this.description = el ("span", { class: "cp-visually-hidden", id: `cp-wave-desc-${++waveformCounter}`, "aria-live": "polite" });
        this.canvas = el ("canvas", { class: "cp-wave-canvas", role: "img", tabindex: "0", "aria-roledescription": "waveform",
                                      "aria-label": "Waveform: no recording", "aria-describedby": this.description.id, title: this.defaultTitle });
        this.element = el ("div", { class: "cp-wave" }, this.toolbar, el ("div", { class: "cp-wave-plot" }, this.canvas, this.description));

        this.disposer.listen (this.canvas, "wheel", (e) => this.handleWheel (e), { passive: false });

        // (Safari sends a trackpad pinch as WebKit gesture events rather than ctrlKey wheels,
        // and zooms the page unless they're cancelled; other browsers never send these)
        this.disposer.listen (this.canvas, "gesturestart", (e) => this.handleGesture (e, "start"));
        this.disposer.listen (this.canvas, "gesturechange", (e) => this.handleGesture (e, "change"));
        this.disposer.listen (this.canvas, "gestureend", (e) => this.handleGesture (e, "end"));
        this.disposer.listen (this.canvas, "pointerdown", (e) => this.handlePointerDown (e));
        this.disposer.listen (this.canvas, "pointermove", (e) => { if (! this.drag) this.setPointer (e); });
        this.disposer.listen (this.canvas, "pointerleave", () => { if (! this.drag) this.clearPointer(); });
        this.disposer.listen (this.canvas, "keydown", (e) => this.handleKey (e));
        this.updateSelectionText();
        this.applyViewConstraints();
    }

    //==============================================================================
    /// Shows a recording (or nothing, for null)
    setRecording (recording)
    {
        if (recording === this.recording)
            return;

        this.removeRecordingListener?.();
        this.removeRecordingListener = null;
        this.recording = recording ?? null;
        this.selection = null;
        this.selectionAnchor = 0;
        this.isFitted = true;
        this.isFollowing = !! recording?.isRecording;

        if (recording)
            this.removeRecordingListener = recording.addListener (() => this.handleRecordingChanged());

        this.updateLanes();
        this.applyViewConstraints();
        this.updateSelectionText();
        this.updateReadout();
        this.redraw();
    }

    /// Pauses drawing while the view can't be seen
    setActive (isActive)
    {
        this.isActive = !! isActive;

        if (this.isActive)
        {
            this.applyViewConstraints();
            this.updateReadout();
            this.redraw();
        }
    }

    handleRecordingChanged()
    {
        if (this.recording?.isDiscarded)
            return this.setRecording (null);

        if (! this.isActive)
            return;

        this.updateLanes();
        this.applyViewConstraints();
        this.updateReadout();
        this.redraw();
    }

    updateLanes()
    {
        const endpoints = this.recording?.endpoints ?? [];
        const key = endpoints.map (e => `${e.endpointID}:${e.numChannels}`).join ("|");

        if (key !== this.lanesKey)
        {
            this.lanesKey = key;
            this.lanes = getWaveformLanes (endpoints, this.getEndpointName);
        }
    }

    /// The plot's size, in CSS pixels
    get plotWidth()             { return Math.max (1, this.canvas.clientWidth || this.canvas.width / (globalThis.devicePixelRatio || 1)); }
    get plotHeight()            { return Math.max (1, this.canvas.clientHeight || this.canvas.height / (globalThis.devicePixelRatio || 1)); }
    get sampleRate()            { return this.recording?.sampleRate ?? 48000; }
    get numFrames()             { return this.recording?.numFrames ?? 0; }

    /// The length the view can show: the recording, or while it's running, at least liveWindowSeconds
    get timelineFrames()
    {
        const r = this.recording;

        if (! r)
            return 0;

        return r.isRecording ? Math.max (r.numFrames, Math.round (liveWindowSeconds * r.sampleRate)) : r.numFrames;
    }

    /// Re-applies the fit / follow / clamping rules after the recording or the size changes
    applyViewConstraints()
    {
        const n = this.timelineFrames, w = this.plotWidth;

        if (! this.recording?.isRecording)
            this.isFollowing = false;

        if (this.isFitted)
            this.view = fitViewport (n, w);
        else if (this.isFollowing)
            this.view = clampViewport ({ start: this.numFrames - this.view.framesPerPixel * w, framesPerPixel: this.view.framesPerPixel }, n, w);
        else
            this.view = clampViewport (this.view, n, w);

        this.followButton.hidden = ! (this.recording?.isRecording && ! this.isFollowing);
        this.zoomInButton.disabled = ! this.recording || this.view.framesPerPixel <= minFramesPerPixel * 1.0001;
        this.zoomOutButton.disabled = this.fitButton.disabled = ! this.recording || this.isFitted;
    }

    /// Sets the view (after a zoom or pan), and works out whether it's still fitted, or following
    setView (view)
    {
        const n = this.timelineFrames, w = this.plotWidth;
        this.view = clampViewport (view, n, w);
        this.isFitted = isViewportFitted (this.view, n, w);

        if (this.recording?.isRecording)
            this.isFollowing = this.isFitted || isViewportAtEnd (this.view, this.numFrames, w);

        this.applyViewConstraints();
        this.updateReadout();
        this.redraw();
    }

    fit()
    {
        this.isFitted = true;
        this.isFollowing = !! this.recording?.isRecording;
        this.applyViewConstraints();
        this.updateReadout();
        this.redraw();
    }

    setFollowing (shouldFollow)
    {
        this.isFollowing = !! shouldFollow && !! this.recording?.isRecording;
        this.applyViewConstraints();
        this.updateReadout();
        this.redraw();
    }

    /// Zooms around an x (CSS pixels): the pointer's, or the middle of the view
    zoomBy (factor, anchorX = this.pointer?.x ?? this.plotWidth / 2)
    {
        if (this.recording)
            this.setView (zoomViewport (this.view, factor, anchorX, this.timelineFrames, this.plotWidth));
    }

    panBy (dx)
    {
        if (this.recording)
            this.setView (panViewport (this.view, dx, this.timelineFrames, this.plotWidth));
    }

    /// Shows a range of frames
    showRange (startFrame, endFrame)
    {
        this.setView ({ start: startFrame, framesPerPixel: Math.max (minFramesPerPixel, (endFrame - startFrame) / this.plotWidth) });
    }

    /// The frames in view: { startFrame, endFrame }
    get visibleRange()          { return { startFrame: this.view.start, endFrame: xToFrame (this.plotWidth, this.view) }; }

    setSelection (selection)
    {
        this.selection = selection ? normaliseSelection (selection.startFrame, selection.endFrame, this.numFrames) : null;
        this.updateSelectionText();
        this.redraw();
    }

    updateSelectionText()
    {
        const s = this.selection, rate = this.sampleRate;
        this.selectionText.textContent = s ? `Selection ${describeSelectionLength (s, rate)}` : "";
        this.selectionText.hidden = ! s;
        this.exportSelectionButton.disabled = ! s;
        this.description.textContent = this.recording ? describeSelection (s, rate) : "";
    }

    //==============================================================================
    /// Where a pointer event is, in CSS pixels from the canvas's top left
    getPosition (e)
    {
        const r = this.canvas.getBoundingClientRect();
        return { x: clampValue (e.clientX - r.left, 0, r.width), y: e.clientY - r.top };
    }

    setPointer (e)
    {
        this.pointer = this.getPosition (e);
        this.canvas.dataset.region = this.pointer.y < rulerHeight ? "ruler" : "lanes";
        this.updateReadout();
        this.updateMarkerTooltip();
        this.redraw();
    }

    clearPointer()
    {
        this.pointer = null;
        delete this.canvas.dataset.region;
        this.updateReadout();
        this.redraw();
    }

    handleWheel (e)
    {
        if (! this.recording)
            return;

        e.preventDefault();
        const { x } = this.getPosition (e);

        if (e.ctrlKey || e.metaKey)
        {
            // (during a Safari gesture the pinch is handled by handleGesture(), so a browser
            // that sent both couldn't zoom twice)
            if (this.gestureScale === undefined)
                this.zoomBy (getWheelZoomFactor (e.deltaY, e.deltaMode), x);
        }
        else
        {
            const delta = Math.abs (e.deltaX) > Math.abs (e.deltaY) ? e.deltaX : e.deltaY;
            this.panBy (delta * (e.deltaMode === 1 ? 16 : 1));
        }
    }

    /// A Safari trackpad pinch: e.scale is the total scale since gesturestart, so each change
    /// zooms by its ratio to the last one, around the gesture's centre
    handleGesture (e, phase)
    {
        e.preventDefault();

        if (phase === "start")
        {
            this.gestureScale = 1;
            return;
        }

        if (phase === "end")
        {
            this.gestureScale = undefined;
            return;
        }

        const scale = Number (e.scale);
        const factor = scale / (this.gestureScale ?? 1);

        if (! (scale > 0) || ! Number.isFinite (factor) || factor === 1)
            return;

        this.gestureScale = scale;

        if (this.recording)
            this.zoomBy (factor, Number.isFinite (e.clientX) ? this.getPosition (e).x : undefined);
    }

    handlePointerDown (e)
    {
        if (e.button !== 0 || ! this.recording)
            return;

        e.preventDefault();
        this.canvas.focus ({ preventScroll: true });

        const start = this.getPosition (e);
        const startView = { ...this.view };
        const startFrame = xToFrame (start.x, startView);
        const isRuler = start.y < rulerHeight;
        const isExtending = ! isRuler && e.shiftKey;
        const anchor = isExtending ? (this.selection ? getSelectionAnchor (this.selection, startFrame) : this.selectionAnchor) : startFrame;
        let hasMoved = false;

        this.drag = { isRuler };
        this.canvas.classList.add ("cp-dragging");

        if (isExtending)
            this.setSelection (normaliseSelection (anchor, startFrame, this.numFrames));

        trackPointer (e, this.canvas, (ev) =>
        {
            const p = this.getPosition (ev);
            hasMoved = hasMoved || Math.abs (p.x - start.x) > 3;
            this.pointer = p;

            if (isRuler)
                this.setView ({ start: startView.start - (p.x - start.x) * startView.framesPerPixel, framesPerPixel: startView.framesPerPixel });
            else if (hasMoved || isExtending)
                this.setSelection (normaliseSelection (anchor, xToFrame (p.x, this.view), this.numFrames));

            this.updateReadout();
        },
        () =>
        {
            this.drag = null;
            this.canvas.classList.remove ("cp-dragging");

            // (a plain click clears the selection, and is where a later Shift-click extends from)
            if (! isRuler && ! hasMoved && ! isExtending)
            {
                this.selectionAnchor = clampValue (Math.round (startFrame), 0, this.numFrames);
                this.setSelection (null);
            }

            this.redraw();
        });
    }

    handleKey (e)
    {
        if (! this.recording || e.ctrlKey || e.metaKey || e.altKey)
            return;

        const w = this.plotWidth;

        switch (e.key)
        {
            case "+": case "=":     this.zoomBy (2, w / 2); break;
            case "-": case "_":     this.zoomBy (0.5, w / 2); break;
            case "ArrowRight":      this.panBy (w * (e.shiftKey ? 0.9 : 0.1)); break;
            case "ArrowLeft":       this.panBy (-w * (e.shiftKey ? 0.9 : 0.1)); break;
            case "PageDown":        this.panBy (w * 0.9); break;
            case "PageUp":          this.panBy (-w * 0.9); break;
            case "Home":            this.setView ({ start: 0, framesPerPixel: this.view.framesPerPixel }); break;
            case "End":             this.setView (viewportAtEnd (this.view, this.timelineFrames, w)); break;
            case "f": case "F":     this.fit(); break;
            case "Escape":          if (! this.selection) return; this.setSelection (null); break;
            default:                return;
        }

        e.preventDefault();
        e.stopPropagation();
    }

    //==============================================================================
    /// The readout: the time under the pointer and each lane's value there (or its min/max over
    /// the pixel, when zoomed out); without a pointer, the range in view
    updateReadout()
    {
        const r = this.recording;
        let text = "";

        if (r && this.pointer && this.pointer.y >= rulerHeight)
        {
            const frame = Math.max (0, Math.round (xToFrame (this.pointer.x, this.view)));
            const parts = [`${formatFrameTime (frame, r.sampleRate)} · ${frame}`];
            const showSamples = chooseWaveformDrawMode (this.view.framesPerPixel) === "samples";
            const isMultiEndpoint = r.endpointIDs.length > 1;

            if (frame < r.numFrames)
            {
                for (const lane of this.lanes)
                {
                    const label = isMultiEndpoint ? lane.label : lane.shortLabel;

                    if (showSamples)
                    {
                        parts.push (`${label} ${formatSampleValue (r.getSamples (lane.endpointID, lane.channel, frame, 1)[0])}`);
                    }
                    else
                    {
                        const a = Math.floor (xToFrame (this.pointer.x - 0.5, this.view)), b = Math.ceil (xToFrame (this.pointer.x + 0.5, this.view));
                        const { min, max } = r.getMinMax (lane.endpointID, lane.channel, a, b, 1);
                        parts.push (`${label} ${formatSampleValue (min[0])}…${formatSampleValue (max[0])}`);
                    }
                }
            }

            text = parts.join ("  ");
        }
        else if (r)
        {
            const { startFrame, endFrame } = this.visibleRange;
            text = `${formatFrameTime (startFrame, r.sampleRate)} – ${formatFrameTime (Math.min (this.timelineFrames, endFrame), r.sampleRate)}`;
        }

        if (this.readout.textContent !== text)
            this.readout.textContent = text;

        this.readout.classList.toggle ("cp-dim", ! this.pointer);
        this.updateSummary();
    }

    /// The canvas's aria-label: what's recorded, and the range in view
    updateSummary()
    {
        const r = this.recording;
        let label = "Waveform: no recording";

        if (r)
        {
            const endpoints = r.endpoints.map (e => `${this.getEndpointName (e.endpointID)} (${e.numChannels} channel${e.numChannels === 1 ? "" : "s"})`).join (", ");
            const dropouts = r.dropouts.length, markers = r.markers.length;
            const { startFrame, endFrame } = this.visibleRange;
            label = `Waveform of ${r.name}: ${r.durationSeconds.toFixed (2)} s of ${endpoints || "audio"}`
                    + (dropouts ? `, ${dropouts} dropout${dropouts === 1 ? "" : "s"}` : "")
                    + (markers ? `, ${markers} marker${markers === 1 ? "" : "s"}` : "")
                    + `. Showing ${(startFrame / r.sampleRate).toFixed (2)} s to ${(Math.min (this.timelineFrames, endFrame) / r.sampleRate).toFixed (2)} s.`;
        }

        if (this.canvas.getAttribute ("aria-label") !== label)
            this.canvas.setAttribute ("aria-label", label);
    }

    /// The marker within a few pixels of x, if any
    findMarkerAt (x)
    {
        let best = null, bestDistance = 4;

        for (const m of this.recording?.markers ?? [])
        {
            const d = Math.abs (frameToX (m.frame, this.view) - x);

            if (d <= bestDistance)
            {
                best = m;
                bestDistance = d;
            }
        }

        return best;
    }

    updateMarkerTooltip()
    {
        const m = this.pointer ? this.findMarkerAt (this.pointer.x) : null;
        const text = m ? describeMarker (m, this.sampleRate) : this.defaultTitle;

        if (this.canvas.title !== text)
            this.canvas.title = text;
    }

    //==============================================================================
    render()
    {
        const canvas = this.canvas, g = canvas.getContext ("2d");

        if (! g || canvas.width < 2 || canvas.height < 2)
            return;

        ++this.drawCount;
        const colours = getThemeColours (canvas);
        const w = this.plotWidth, h = this.plotHeight;
        const dpr = canvas.width / w;
        const r = this.recording;

        if (! this.labelFont)
            this.labelFont = `10px ${getComputedStyle (canvas).fontFamily || "monospace"}`;

        g.setTransform (1, 0, 0, 1, 0, 0);
        g.clearRect (0, 0, canvas.width, canvas.height);
        g.setTransform (dpr, 0, 0, dpr, 0, 0);
        g.fillStyle = colours.waveBg;
        g.fillRect (0, 0, w, h);
        g.font = this.labelFont;
        g.textBaseline = "top";

        if (! r)
            return;

        const lanesTop = rulerHeight;
        const numLanes = Math.max (1, this.lanes.length);
        const laneGap = 1;
        const laneHeight = Math.max (8, (h - lanesTop - laneGap * (numLanes - 1)) / numLanes);
        const endX = clampValue (frameToX (r.numFrames, this.view), 0, w);
        const hairline = 1 / dpr;

        this.drawRuler (g, colours, w, hairline);

        // the selection, under the traces
        if (this.selection)
        {
            const x0 = clampValue (frameToX (this.selection.startFrame, this.view), -1, w + 1);
            const x1 = clampValue (frameToX (this.selection.endFrame, this.view), -1, w + 1);
            g.fillStyle = colours.waveSelection;
            g.fillRect (x0, lanesTop, Math.max (hairline, x1 - x0), h - lanesTop);
            g.fillStyle = colours.waveSelectionEdge;
            g.fillRect (x0, lanesTop, hairline, h - lanesTop);
            g.fillRect (x1 - hairline, lanesTop, hairline, h - lanesTop);
        }

        const dropouts = r.dropouts;
        const mode = chooseWaveformDrawMode (this.view.framesPerPixel);

        this.lanes.forEach ((lane, i) =>
        {
            const top = lanesTop + i * (laneHeight + laneGap);
            const mid = Math.round ((top + laneHeight / 2) * dpr) / dpr;

            g.fillStyle = colours.waveGrid;

            if (i > 0)
                g.fillRect (0, top - laneGap, w, laneGap);

            // the centre line, as far as the recording goes
            g.fillRect (0, mid, endX, hairline);

            for (const d of dropouts)
                if (d.endpointID === lane.endpointID)
                    this.drawDropout (g, colours, d, top, laneHeight, w);

            g.save();
            g.beginPath();
            g.rect (0, top, w, laneHeight);
            g.clip();

            if (mode === "samples")
                this.drawSamples (g, colours, lane, mid, laneHeight / 2 - 2, w, dpr);
            else
                this.drawMinMax (g, colours, lane, mid, laneHeight / 2 - 2, w, dpr);

            g.restore();

            g.fillStyle = colours.waveLabel;
            g.fillText (lane.label, 6, top + 4);
        });

        this.drawMarkers (g, colours, lanesTop, h, w, dpr);

        // the pointer's time
        if (this.pointer && this.pointer.y >= rulerHeight && ! this.drag?.isRuler)
        {
            g.globalAlpha = 0.5;
            g.fillStyle = colours.waveCursor;
            g.fillRect (Math.round (this.pointer.x * dpr) / dpr, lanesTop, hairline, h - lanesTop);
            g.globalAlpha = 1;
        }
    }

    drawRuler (g, colours, w, hairline)
    {
        const { ticks } = getRulerTicks (this.view, w, this.sampleRate);
        const end = frameToX (this.timelineFrames, this.view);
        let lastLabelEnd = -Infinity;

        g.fillStyle = colours.waveGrid;
        g.fillRect (0, rulerHeight - hairline, w, hairline);

        for (const t of ticks)
        {
            const x = Math.round (t.x / hairline) * hairline;

            if (x < 0 || x > w || t.x > end + 0.5)
                continue;

            const tickHeight = t.isMajor ? 6 : 3;
            g.fillStyle = t.isMajor ? colours.waveLabel : colours.waveGrid;
            g.fillRect (x, rulerHeight - tickHeight, hairline, tickHeight);

            if (t.label && x + 3 > lastLabelEnd)
            {
                g.fillStyle = colours.waveLabel;
                g.fillText (t.label, x + 3, 3);
                lastLabelEnd = x + 3 + g.measureText (t.label).width + 6;
            }
        }
    }

    drawDropout (g, colours, dropout, top, height, w)
    {
        let x0 = frameToX (dropout.startFrame, this.view);
        let x1 = frameToX (dropout.startFrame + dropout.numFrames, this.view);

        if (x1 < 0 || x0 > w)
            return;

        // (at least 3 pixels wide, so that a short one shows at any zoom)
        if (x1 - x0 < 3)
        {
            const centre = (x0 + x1) / 2;
            x0 = centre - 1.5;
            x1 = centre + 1.5;
        }

        x0 = Math.max (-1, x0);
        x1 = Math.min (w + 1, x1);

        g.fillStyle = colours.waveDropout;
        g.fillRect (x0, top, x1 - x0, height);

        // diagonal hatching, and a mark on the ruler
        g.save();
        g.beginPath();
        g.rect (x0, top, x1 - x0, height);
        g.clip();
        g.strokeStyle = colours.waveDropoutHatch;
        g.lineWidth = 1;
        g.beginPath();

        for (let x = Math.floor ((x0 - height) / 6) * 6; x < x1; x += 6)
        {
            g.moveTo (x, top + height);
            g.lineTo (x + height, top);
        }

        g.stroke();
        g.restore();

        g.fillStyle = colours.waveDropoutHatch;
        g.fillRect (x0, rulerHeight - 3, x1 - x0, 3);
    }

    drawMinMax (g, colours, lane, mid, halfHeight, w, dpr)
    {
        const r = this.recording;
        const columnStart = Math.max (0, Math.floor (frameToX (0, this.view) * dpr));
        const columnEnd = Math.min (Math.round (w * dpr), Math.ceil (frameToX (r.numFrames, this.view) * dpr));
        const n = columnEnd - columnStart;

        if (n <= 0)
            return;

        if (this.minMax.min.length !== n)
            this.minMax = { min: new Float32Array (n), max: new Float32Array (n) };

        const startFrame = Math.max (0, xToFrame (columnStart / dpr, this.view));
        const endFrame = Math.min (r.numFrames, xToFrame (columnEnd / dpr, this.view));
        const { min, max } = r.getMinMax (lane.endpointID, lane.channel, startFrame, endFrame, n, this.minMax);
        const minHeight = 1 / dpr;

        g.fillStyle = colours.waveTrace;
        g.beginPath();

        for (let i = 0; i < n; ++i)
        {
            const yTop = mid - clampValue (max[i], -1, 1) * halfHeight;
            const yBottom = mid - clampValue (min[i], -1, 1) * halfHeight;
            g.rect ((columnStart + i) / dpr, yTop, 1 / dpr, Math.max (minHeight, yBottom - yTop));
        }

        g.fill();
    }

    drawSamples (g, colours, lane, mid, halfHeight, w, dpr)
    {
        const r = this.recording;
        const first = Math.max (0, Math.floor (this.view.start) - 1);
        const last = Math.min (r.numFrames, Math.ceil (xToFrame (w, this.view)) + 2);
        const n = last - first;

        if (n <= 0)
            return;

        if (this.samples.length < n)
            this.samples = new Float32Array (n);

        const samples = r.getSamples (lane.endpointID, lane.channel, first, n, this.samples);
        const yOf = (v) => mid - clampValue (v, -1.05, 1.05) * halfHeight;

        g.strokeStyle = colours.waveTrace;
        g.lineWidth = Math.max (1, 1.25 * dpr) / dpr;
        g.lineJoin = "round";
        g.beginPath();

        for (let i = 0; i < n; ++i)
        {
            const x = frameToX (first + i, this.view), y = yOf (samples[i]);
            i === 0 ? g.moveTo (x, y) : g.lineTo (x, y);
        }

        g.stroke();

        // (far enough in to tell the samples apart: a dot on each)
        if (this.view.framesPerPixel <= 0.34)
        {
            g.fillStyle = colours.waveTrace;
            g.beginPath();

            for (let i = 0; i < n; ++i)
            {
                const x = frameToX (first + i, this.view), y = yOf (samples[i]);
                g.moveTo (x + 1.75, y);
                g.arc (x, y, 1.75, 0, Math.PI * 2);
            }

            g.fill();
        }
    }

    drawMarkers (g, colours, top, h, w, dpr)
    {
        let lastLabelEnd = -Infinity;

        for (const m of this.recording.markers)
        {
            const x = frameToX (m.frame, this.view);

            if (x < -1 || x > w + 1)
                continue;

            const px = Math.round (x * dpr) / dpr;
            g.fillStyle = colours[markerColourKeys[m.kind] ?? "waveMarker"];
            g.fillRect (px, top, 1 / dpr, h - top);

            // a small flag at the top, with the label beside it where there's room
            g.fillRect (px, top, 5, 4);

            if (m.label && px + 7 > lastLabelEnd)
            {
                const label = m.label.length > 28 ? m.label.substring (0, 27) + "…" : m.label;
                g.fillText (label, px + 7, top + 1);
                lastLabelEnd = px + 7 + g.measureText (label).width + 8;
            }
        }
    }
}
