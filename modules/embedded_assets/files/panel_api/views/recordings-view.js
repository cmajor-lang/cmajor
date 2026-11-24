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

import { el, Disposer, setPressed } from "../core/dom.js";
import { RecordingStopReason } from "../core/recorder.js";
import { generateWAVParts, getWAVFileSize, wavFormats } from "../core/wav-encoder.js";
import { getEndpointDisplayName, getParameters } from "../widgets/registry.js";
import { WaveformView } from "../widgets/waveform-view.js";
import { Popover } from "../shell/top-bar.js";

/*
    Recording ("capture") in the panel: the patch's audio, recorded into memory by
    core/recorder.js's RecordingManager (one per panel, made in cmaj-patch-panel.js and kept as
    ctx.recordings), shown and exported here.

    - RecordingController starts and stops captures of the loaded patch: all of its audio
      outputs, and its audio inputs too if the "recordingIncludeInputs" setting is on. Each
      capture also watches the patch's event outputs, MIDI endpoints and parameters, and puts
      a marker on the timeline for each event (Recording.watchEvents()). It subscribes through
      the connection's hub itself, not a view's group, so it carries on while every view is
      hidden.
    - CaptureButton is the top bar's "● Capture" button. (The transport's ● is something else:
      the record flag it sends to the patch's host-timeline inputs.) While capturing it shows
      the elapsed time and the memory used.
    - RecordingsView is the drawer's Recordings tab: the list of captures (newest first) beside
      (or, when narrow, above) a WaveformView of the selected one, with a memory gauge, the
      memory limit ("recordingCapMB", global), the include-inputs toggle and Discard all in its
      toolbar.
    - ExportPanel is the export popover: the format (PCM 16/24/32, float; "recordingExportFormat",
      global), the endpoint (one WAV file each, or all of them), and the whole recording or the
      selection; it saves through ctx.bridge.saveFile().
    - createRecordingCommands() gives the palette Start/stop (C), Add a marker (M) and Show the
      recordings.

    The plain helpers (sanitiseFileName(), getRecordingFileName(), formatBytes(),
    formatRecordingTime(), describeSaveResult()...) have no DOM, for the tests.
*/

/// The memory limits offered, in MB (the default is core/recorder.js's 256 MB)
export const recordingCapChoicesMB = [64, 128, 256, 512];
export const defaultRecordingCapMB = 256;

/// The export formats offered, and their names
export const exportFormatNames = { pcm16: "16-bit PCM", pcm24: "24-bit PCM", pcm32: "32-bit PCM", float32: "32-bit float" };
export const defaultExportFormat = "pcm24";

/// The settings keys (all global)
export const recordingSettingsKeys = Object.freeze ({ capMB: "recordingCapMB", includeInputs: "recordingIncludeInputs", exportFormat: "recordingExportFormat" });

//==============================================================================
/// A memory limit setting, cleaned up (one of recordingCapChoicesMB)
export function normaliseCapMB (value)
{
    return recordingCapChoicesMB.includes (Number (value)) ? Number (value) : defaultRecordingCapMB;
}

/// "0 B", "512 KB", "34 MB", "1.2 GB"
export function formatBytes (bytes)
{
    const mb = 1024 * 1024;

    if (! (bytes > 0))          return "0 MB";
    if (bytes < mb)             return `${Math.max (1, Math.round (bytes / 1024))} KB`;
    if (bytes < 10 * mb)        return `${(bytes / mb).toFixed (1)} MB`;
    if (bytes < 1024 * mb)      return `${Math.round (bytes / mb)} MB`;
    return `${(bytes / (1024 * mb)).toFixed (1)} GB`;
}

/// A recording's length: "0:01.5", "1:02.0", "1:02:03"
export function formatRecordingTime (seconds)
{
    const s = Math.max (0, Number.isFinite (seconds) ? seconds : 0);
    const tenths = Math.floor (s * 10 + 1e-6);
    const h = Math.floor (tenths / 36000), m = Math.floor ((tenths % 36000) / 600), rest = (tenths % 600) / 10;
    const ss = rest.toFixed (1).padStart (4, "0");
    return h > 0 ? `${h}:${String (m).padStart (2, "0")}:${ss.substring (0, 2)}` : `${m}:${ss}`;
}

const reservedFileNames = /^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/i;

/// Makes some text safe as (part of) a file name on any platform: no path separators, no
/// characters Windows forbids (<>:"/\|?* and control characters), no leading dots, no trailing
/// dots or spaces, runs of whitespace as one space, at most maxLength characters, and never a
/// reserved Windows device name. An empty result is "untitled".
export function sanitiseFileName (text, maxLength = 100)
{
    let name = String (text ?? "")
                 .replace (/[\u0000-\u001f\u007f<>:"/\\|?*]+/g, "-")
                 .replace (/\s+/g, " ")
                 .replace (/-{2,}/g, "-")
                 .trim()
                 .replace (/^[.\s-]+/, "")
                 .substring (0, maxLength)
                 .replace (/[.\s-]+$/, "");

    if (reservedFileNames.test (name))
        name = "_" + name;

    return name || "untitled";
}

/// The file a recording's endpoint exports to: "<patch>-<recording>-<endpoint>.wav"
export function getRecordingFileName (patchName, recordingName, endpointID, extension = "wav")
{
    const parts = [patchName, recordingName, endpointID].filter (p => p !== undefined && p !== null && String (p).trim() !== "")
                                                        .map (p => sanitiseFileName (p, 60));
    return `${parts.join ("-") || "recording"}.${extension}`;
}

/// A saveFile() result, for the export panel: { text, state: "ok" | "cancelled" | "error" }
export function describeSaveResult (result, fileName)
{
    if (result?.saved)
        return { text: result.path ? `Saved to ${result.path}` : `Downloaded ${fileName}`, state: "ok" };

    if (result?.cancelled)
        return { text: "Cancelled", state: "cancelled" };

    return { text: `Couldn't save ${fileName}: ${result?.error ?? "unknown error"}`, state: "error" };
}

/// Builds a WAV file as a Blob from a WAVBlockSource (Recording.getWAVBlockSource()) without
/// holding it in one buffer, yielding to the browser every few blocks so that a big one doesn't
/// freeze the panel; onProgress (0..1) is called as it goes
export async function createWAVBlob (source, sampleRate, format, onProgress)
{
    getWAVFileSize (source.numChannels, source.numFrames, format);   // (throws for a file over 4 GB, before any work)

    const parts = [];
    const numParts = 2 + Math.ceil (source.numFrames / source.blockSize);
    const yieldEvery = 4;

    for (const part of generateWAVParts (source, sampleRate, { format }))
    {
        parts.push (part);

        if (parts.length % yieldEvery === 0)
        {
            onProgress?.(Math.min (1, parts.length / numParts));
            await new Promise (r => setTimeout (r, 0));
        }
    }

    onProgress?.(1);
    return new Blob (parts, { type: "audio/wav" });
}

/// The patch's name, for file names
export function getPatchName (state)
{
    const name = state?.manifest?.name;

    if (typeof name === "string" && name.trim())
        return name.trim();

    const file = state?.manifestFile ?? "";
    return file.split (/[\\/]/).pop().replace (/\.cmajorpatch$/i, "") || "patch";
}

//==============================================================================
/*
    Starts and stops captures of the loaded patch (see the comment at the top).
    ctx needs { store, settings, log }; manager is the panel's RecordingManager.
*/
export class RecordingController
{
    constructor (ctx, manager)
    {
        this.ctx = ctx;
        this.manager = manager;
        this.markerCount = 0;
        this.listeners = new Set();
    }

    /// fn() when the include-inputs setting changes (the manager's own listeners cover the rest)
    addListener (fn)
    {
        this.listeners.add (fn);
        return () => this.listeners.delete (fn);
    }

    get includeInputs()         { return !! this.ctx.settings.global.get (recordingSettingsKeys.includeInputs, false); }

    setIncludeInputs (shouldInclude)
    {
        this.ctx.settings.global.set (recordingSettingsKeys.includeInputs, !! shouldInclude);

        for (const fn of [...this.listeners])
            fn();
    }

    /// The audio endpoints a capture would record: { outputs, inputs } (inputs only if they're included)
    getEndpoints()
    {
        const s = this.ctx.store.state;
        const outputs = (s.outputs ?? []).filter (e => e.purpose === "audio out");
        const inputs = this.includeInputs ? (s.inputs ?? []).filter (e => e.purpose === "audio in") : [];
        return { outputs, inputs };
    }

    /// Why a capture can't start now, or null if it can
    getDisabledReason()
    {
        const s = this.ctx.store.state;

        if (! s.connection)
            return "No patch is loaded";

        const { outputs, inputs } = this.getEndpoints();

        if (outputs.length + inputs.length === 0)
        {
            const hasInputs = (s.inputs ?? []).some (e => e.purpose === "audio in");
            return hasInputs ? "This patch has no audio outputs: turn on \"Include inputs\" in the Recordings tab to record its inputs"
                             : "This patch has no audio inputs or outputs to record";
        }

        if (! (s.deviceProperties?.rate > 0))
            return "The audio device's sample rate isn't known yet";

        return null;
    }

    get canStart()              { return this.getDisabledReason() === null; }

    /// The capture that's running (the newest, if there are several)
    get activeRecording()
    {
        const running = this.manager.recordings.filter (r => r.isRecording);
        return running[running.length - 1] ?? null;
    }

    get isRecording()           { return !! this.activeRecording; }

    /// Starts a capture; returns the Recording, or null (with the reason logged) if it can't
    start()
    {
        const reason = this.getDisabledReason();

        if (reason)
        {
            this.ctx.log?.("warning", "panel", `Can't record: ${reason}`);
            return null;
        }

        const s = this.ctx.store.state;
        const { outputs, inputs } = this.getEndpoints();

        try
        {
            const recording = this.manager.start ({ endpoints: [...outputs, ...inputs].map (e => e.endpointID),
                                                    connection: s.connection, sampleRate: s.deviceProperties.rate });
            this.markerCount = 0;

            // events become markers on the timeline
            const events = [...(s.outputs ?? []).filter (e => e.endpointType === "event" || e.purpose === "midi out"),
                            ...(s.inputs ?? []).filter (e => e.purpose === "midi in")]
                             .map (e => ({ endpointID: e.endpointID, kind: (e.purpose ?? "").startsWith ("midi") ? "midi" : "event", name: getEndpointDisplayName (e) }));
            const parameters = getParameters (s).map (e => ({ endpointID: e.endpointID, name: getEndpointDisplayName (e) }));
            recording.watchEvents ({ events, parameters });

            this.ctx.log?.("info", "panel", `Recording ${recording.endpointIDs.join (", ")}`);
            return recording;
        }
        catch (e)
        {
            this.ctx.log?.("error", "panel", `Couldn't start recording: ${e?.message ?? e}`);
            return null;
        }
    }

    stop()
    {
        const running = this.manager.recordings.filter (r => r.isRecording);

        for (const r of running)
            r.stop (RecordingStopReason.user);

        return running.length > 0;
    }

    toggle()                    { return this.isRecording ? (this.stop(), null) : this.start(); }

    /// Adds a user marker at the latest audio received, to the running capture
    addMarker (label)
    {
        const recording = this.activeRecording;

        if (! recording)
            return null;

        return recording.addMarker ({ kind: "user", label: label ?? `Marker ${++this.markerCount}` });
    }
}

//==============================================================================
/// The palette's recording commands. options: { controller, showRecordings () }
export function createRecordingCommands ({ controller, showRecordings })
{
    return [
        { id: "recording.toggle",  title: "Start or stop recording",  category: "Recording", shortcut: "C",
          keywords: ["capture", "record", "wav", "audio", "outputs"],
          run: () => controller.toggle(), isEnabled: () => controller.isRecording || controller.canStart },
        { id: "recording.marker",  title: "Add a marker to the recording", category: "Recording", shortcut: "M",
          keywords: ["capture", "mark", "flag", "bookmark"],
          run: () => controller.addMarker(), isEnabled: () => controller.isRecording },
        { id: "recording.show",    title: "Show the recordings",      category: "Recording", kind: "view",
          keywords: ["capture", "waveform", "export", "wav", "drawer"],
          run: () => showRecordings() },
    ];
}

//==============================================================================
/// The top bar's "● Capture" button (see the comment at the top)
export class CaptureButton
{
    constructor (ctx, controller)
    {
        this.ctx = ctx;
        this.controller = controller;
        this.disposer = new Disposer();

        const shortcut = ctx.commands?.get ("recording.toggle")?.shortcut ?? "C";

        this.dot = el ("span", { class: "cp-capture-dot", "aria-hidden": "true" }, "●");
        this.label = el ("span", { class: "cp-button-label" }, "Capture");
        this.time = el ("span", { class: "cp-capture-time", hidden: true });
        this.memory = el ("span", { class: "cp-capture-memory", hidden: true });
        this.element = el ("button", { class: "cp-button cp-capture-button", type: "button", "aria-pressed": "false",
                                       onclick: () => controller.toggle() },
                           this.dot, this.label, this.time, this.memory, el ("kbd", null, shortcut));

        this.update = () => this.refresh();
        this.disposer.add (controller.manager.addListener (this.update), controller.addListener (this.update),
                           ctx.store.subscribe (["connection", "inputs", "outputs", "deviceProperties"], this.update));
        this.refresh();
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    refresh()
    {
        const recording = this.controller.activeRecording;
        const manager = this.controller.manager;
        const isRecording = !! recording;
        const reason = isRecording ? null : this.controller.getDisabledReason();

        setPressed (this.element, isRecording);
        this.element.classList.toggle ("cp-capturing", isRecording);
        this.element.disabled = !! reason;
        this.element.hidden = ! this.ctx.store.get ("connection");
        this.label.hidden = isRecording;
        this.time.hidden = this.memory.hidden = ! isRecording;
        this.dot.textContent = isRecording ? "■" : "●";

        if (isRecording)
        {
            setText (this.time, formatRecordingTime (recording.durationSeconds));
            setText (this.memory, `${formatBytes (manager.usedBytes)} / ${formatBytes (manager.capBytes)}`);
        }

        const title = isRecording ? `Stop recording (C): ${formatRecordingTime (recording.durationSeconds)}, ${formatBytes (manager.usedBytes)} of ${formatBytes (manager.capBytes)} used`
                                  : (reason ? `Can't record: ${reason}` : "Record the patch's audio outputs into memory, to view and export as WAV (C). (The transport's ● is the host's record flag, which is different.)");
        const ariaLabel = isRecording ? `Stop recording, ${formatRecordingTime (recording.durationSeconds)} recorded` : "Capture: record the patch's audio";

        if (this.element.title !== title)                       this.element.title = title;
        if (this.element.getAttribute ("aria-label") !== ariaLabel)   this.element.setAttribute ("aria-label", ariaLabel);
    }
}

//==============================================================================
/*
    The export popover's content (see the comment at the top). open (anchor, recording,
    { range: "whole" | "selection", selection }) shows it under anchor.
*/
export class ExportPanel
{
    constructor (ctx)
    {
        this.ctx = ctx;
        this.recording = null;
        this.selection = null;
        this.isBusy = false;
        this.lastResults = [];

        this.formatSelect = el ("select", { class: "cp-select", "aria-label": "Format",
                                            onchange: () => { ctx.settings.global.set (recordingSettingsKeys.exportFormat, this.formatSelect.value); this.updateFileName(); } },
                                wavFormats.map (f => el ("option", { value: f }, exportFormatNames[f] ?? f)));
        this.endpointSelect = el ("select", { class: "cp-select", "aria-label": "Endpoint", onchange: () => this.updateFileName() });
        this.endpointRow = el ("label", { class: "cp-form-row" }, el ("span", null, "Endpoint"), this.endpointSelect);

        this.rangeButtons = ["whole", "selection"].map (range =>
            el ("button", { class: "cp-seg-button", type: "button", dataset: { range }, "aria-pressed": "false",
                            onclick: () => this.setRange (range) }, range === "whole" ? "Whole recording" : "Selection"));

        this.fileName = el ("span", { class: "cp-export-file" });
        this.status = el ("p", { class: "cp-export-status", role: "status", "aria-live": "polite" });
        this.progress = el ("progress", { class: "cp-export-progress", max: 1, value: 0, hidden: true, "aria-label": "Export progress" });
        this.exportButton = el ("button", { class: "cp-button cp-export-button", type: "button", onclick: () => this.export() }, "Export");

        this.element = el ("div", { class: "cp-export" },
                           el ("h3", { class: "cp-section-title" }, "Export as WAV"),
                           el ("div", { class: "cp-form" },
                               el ("label", { class: "cp-form-row" }, el ("span", null, "Format"), this.formatSelect),
                               this.endpointRow,
                               el ("div", { class: "cp-form-row" }, el ("span", null, "Range"),
                                   el ("div", { class: "cp-seg", role: "group", "aria-label": "Range" }, this.rangeButtons)),
                               el ("div", { class: "cp-form-row" }, el ("span", null, "File"), this.fileName)),
                           el ("div", { class: "cp-export-footer" }, this.progress, this.status, el ("span", { class: "cp-spacer" }), this.exportButton));

        this.popover = new Popover (this.element, null, null);
        this.popover.element.classList.add ("cp-export-popover");
        this.popover.element.setAttribute ("aria-label", "Export recording");
    }

    dispose()
    {
        this.popover.dispose();
    }

    get isOpen()                { return this.popover.isOpen; }
    close()                     { this.popover.close(); }

    open (anchor, recording, { range = "whole", selection = null } = {})
    {
        if (! this.isBusy)
        {
            this.recording = recording;
            this.selection = selection;
            this.formatSelect.value = wavFormats.includes (this.ctx.settings.global.get (recordingSettingsKeys.exportFormat))
                                         ? this.ctx.settings.global.get (recordingSettingsKeys.exportFormat) : defaultExportFormat;

            const ids = recording.endpointIDs;
            this.endpointSelect.replaceChildren (...ids.map (id => el ("option", { value: id }, `${this.getEndpointName (id)} (${recording.getEndpointInfo (id)?.numChannels ?? 0} ch)`)),
                                                 ids.length > 1 ? el ("option", { value: "*" }, `All (${ids.length} files)`) : null);
            this.endpointSelect.value = ids[0];
            this.endpointRow.hidden = ids.length < 2;
            this.setRange (range === "selection" && selection ? "selection" : "whole");
            this.setStatus ("", "");
        }

        this.popover.open (anchor, { focus: ".cp-export-button" });
    }

    getEndpointName (endpointID)
    {
        const s = this.ctx.store.state;
        const endpoint = [...(s.outputs ?? []), ...(s.inputs ?? [])].find (e => e.endpointID === endpointID);
        return endpoint ? getEndpointDisplayName (endpoint) : endpointID;
    }

    setRange (range)
    {
        this.range = range === "selection" && this.selection ? "selection" : "whole";

        for (const b of this.rangeButtons)
        {
            setPressed (b, b.dataset.range === this.range);
            b.disabled = b.dataset.range === "selection" && ! this.selection;
        }

        this.rangeButtons[1].title = this.selection ? "" : "Select a range in the waveform first";
        this.updateFileName();
    }

    /// The files an export would make: [{ endpointID, fileName, startFrame, endFrame }]
    getPlannedFiles()
    {
        const r = this.recording;

        if (! r)
            return [];

        const ids = this.endpointSelect.value === "*" ? r.endpointIDs : [this.endpointSelect.value || r.endpointIDs[0]];
        const range = this.range === "selection" && this.selection ? this.selection : { startFrame: 0, endFrame: r.numFrames };
        const patchName = getPatchName (this.ctx.store.state);
        const suffix = this.range === "selection" ? " (selection)" : "";

        return ids.map (endpointID => ({ endpointID, fileName: getRecordingFileName (patchName, r.name + suffix, endpointID),
                                         startFrame: range.startFrame, endFrame: range.endFrame }));
    }

    updateFileName()
    {
        const files = this.getPlannedFiles();
        const r = this.recording;

        if (! r || files.length === 0)
            return setText (this.fileName, "");

        const f = files[0];
        const size = files.reduce ((n, file) => n + safeWAVSize (r.getEndpointInfo (file.endpointID)?.numChannels ?? 1, file.endFrame - file.startFrame, this.formatSelect.value), 0);
        setText (this.fileName, (files.length === 1 ? f.fileName : `${files.length} files`) + ` · ${formatBytes (size)}`);
        this.fileName.title = files.map (file => file.fileName).join ("\n");
    }

    setStatus (text, state)
    {
        setText (this.status, text);
        this.status.dataset.state = state ?? "";
    }

    setBusy (isBusy)
    {
        this.isBusy = isBusy;
        this.element.setAttribute ("aria-busy", isBusy ? "true" : "false");

        for (const control of [this.formatSelect, this.endpointSelect, this.exportButton, ...this.rangeButtons])
            control.disabled = isBusy || (control.dataset.range === "selection" && ! this.selection);

        this.progress.hidden = ! isBusy;
    }

    /// Encodes and saves the planned files, one after another; resolves to the saveFile() results
    async export()
    {
        const r = this.recording;

        if (! r || this.isBusy)
            return [];

        const files = this.getPlannedFiles();
        const format = this.formatSelect.value;
        const results = [];
        this.setBusy (true);

        try
        {
            for (const [i, file] of files.entries())
            {
                const prefix = files.length > 1 ? `${i + 1}/${files.length}: ` : "";
                const source = r.getWAVBlockSource (file.endpointID, { startFrame: file.startFrame, endFrame: file.endFrame });

                this.progress.removeAttribute ("value");
                this.setStatus (`${prefix}Encoding…`, "busy");

                const blob = await createWAVBlob (source, r.sampleRate, format, (p) =>
                {
                    this.progress.value = p;
                    this.setStatus (`${prefix}Encoding… ${Math.round (p * 100)}%`, "busy");
                });

                this.progress.removeAttribute ("value");
                this.setStatus (`${prefix}${this.ctx.bridge.isVSCode ? "Saving (choose where in VS Code)…" : "Saving…"}`, "busy");

                const result = await this.ctx.bridge.saveFile ({ suggestedName: file.fileName, data: blob, mimeType: "audio/wav" });
                results.push ({ ...result, fileName: file.fileName });

                if (! result.saved)
                    break;
            }
        }
        catch (e)
        {
            results.push ({ saved: false, error: String (e?.message ?? e), fileName: files[results.length]?.fileName ?? "" });
        }

        this.setBusy (false);
        this.lastResults = results;

        const last = results[results.length - 1];
        const description = describeSaveResult (last, last?.fileName);

        if (last?.saved && results.length > 1)
            description.text = results.every (x => x.path) ? `Saved ${results.length} files to ${dirName (results[0].path)}` : `Downloaded ${results.length} files`;

        this.setStatus (description.text, description.state);

        if (description.state === "error")
            this.ctx.log?.("error", "panel", description.text);
        else if (description.state === "ok")
            this.ctx.log?.("info", "panel", description.text);

        return results;
    }
}

//==============================================================================
/*
    The drawer's Recordings tab (see the comment at the top).
    options: { manager, controller }
*/
export class RecordingsView
{
    constructor (ctx, { manager, controller })
    {
        this.ctx = ctx;
        this.manager = manager;
        this.controller = controller;
        this.disposer = new Disposer();
        this.rows = new Map();          // recording id -> { recording, element, ... }
        this.selectedID = null;
        this.isActive = false;
        this.confirmTimer = null;

        this.waveform = new WaveformView ({ scheduler: ctx.scheduler, getEndpointName: (id) => this.exporter.getEndpointName (id),
                                            onExportSelection: (selection, anchor) => this.showExport (this.selectedRecording, anchor, "selection") });
        this.waveform.setActive (false);
        this.exporter = new ExportPanel (ctx);
        this.disposer.add (() => { this.waveform.dispose(); this.exporter.dispose(); clearTimeout (this.confirmTimer); });

        this.list = el ("ul", { class: "cp-rec-list", "aria-label": "Recordings" });
        this.emptyMessage = el ("p", { class: "cp-empty cp-rec-empty" });
        this.viewerEmpty = el ("p", { class: "cp-empty cp-rec-viewer-empty" }, "Select a recording to see its waveform");
        this.viewer = el ("div", { class: "cp-rec-viewer" }, this.waveform.element, this.viewerEmpty);

        this.element = el ("div", { class: "cp-recordings" },
                           el ("div", { class: "cp-rec-layout" },
                               el ("div", { class: "cp-rec-list-pane" }, this.emptyMessage, this.list),
                               this.viewer));

        this.createToolbar();

        this.update = () => this.refresh();
        this.disposer.add (manager.addListener (this.update), controller.addListener (this.update),
                           ctx.store.subscribe (["connection", "inputs", "outputs", "deviceProperties", "manifest"], this.update));
        this.refresh();
    }

    dispose()
    {
        this.disposer.dispose();
        this.element.remove();
    }

    setActive (isActive)
    {
        this.isActive = !! isActive;
        this.waveform.setActive (this.isActive);

        if (this.isActive)
            this.refresh();
    }

    get selectedRecording()     { return this.selectedID === null ? null : this.manager.getRecording (this.selectedID) ?? null; }

    createToolbar()
    {
        const settings = this.ctx.settings.global;

        this.gaugeFill = el ("span", { class: "cp-rec-gauge-fill" });
        this.gaugeText = el ("span", { class: "cp-rec-gauge-text" });
        this.gauge = el ("span", { class: "cp-rec-gauge", role: "meter", "aria-label": "Recording memory", "aria-valuemin": "0" },
                         el ("span", { class: "cp-rec-gauge-track", "aria-hidden": "true" }, this.gaugeFill), this.gaugeText);

        this.capSelect = el ("select", { class: "cp-select cp-rec-cap", "aria-label": "Memory limit", title: "The most memory all the recordings may use",
                                         onchange: () => this.setCapMB (Number (this.capSelect.value)) },
                             recordingCapChoicesMB.map (mb => el ("option", { value: String (mb) }, `${mb} MB`)));
        this.capSelect.value = String (normaliseCapMB (settings.get (recordingSettingsKeys.capMB, defaultRecordingCapMB)));

        this.inputsButton = el ("button", { class: "cp-small-button cp-rec-inputs", type: "button", "aria-pressed": "false",
                                            title: "Also record the patch's audio inputs (as files of their own)",
                                            onclick: () => this.controller.setIncludeInputs (! this.controller.includeInputs) }, "Include inputs");

        this.discardAllButton = el ("button", { class: "cp-small-button cp-rec-discard-all", type: "button", title: "Discard every recording",
                                                onclick: () => this.handleDiscardAll() }, "Discard all");

        this.toolbar = el ("div", { class: "cp-tab-tools cp-rec-tools" }, this.gauge, this.capSelect, this.inputsButton, this.discardAllButton);
    }

    setCapMB (mb)
    {
        const capMB = normaliseCapMB (mb);
        this.ctx.settings.global.set (recordingSettingsKeys.capMB, capMB);
        this.manager.setCapBytes (capMB * 1024 * 1024);
        this.capSelect.value = String (capMB);
        this.refresh();
    }

    handleDiscardAll()
    {
        // (two clicks: the first asks)
        if (! this.discardAllButton.classList.contains ("cp-confirming"))
        {
            this.discardAllButton.classList.add ("cp-confirming");
            this.discardAllButton.textContent = "Discard all?";
            clearTimeout (this.confirmTimer);
            this.confirmTimer = setTimeout (() => this.resetDiscardAll(), 3000);
            return;
        }

        this.resetDiscardAll();
        this.manager.discardAll();
    }

    resetDiscardAll()
    {
        clearTimeout (this.confirmTimer);
        this.discardAllButton.classList.remove ("cp-confirming");
        this.discardAllButton.textContent = "Discard all";
    }

    //==============================================================================
    refresh()
    {
        const recordings = [...this.manager.recordings].reverse();   // (newest first)
        const m = this.manager;

        // the toolbar
        const fraction = m.capBytes > 0 ? m.usedBytes / m.capBytes : 0;
        this.gaugeFill.style.setProperty ("--cp-fill", `${Math.min (100, fraction * 100).toFixed (1)}%`);
        this.gauge.dataset.level = fraction >= 0.999 ? "full" : (fraction > 0.8 ? "high" : "");
        setText (this.gaugeText, `${formatBytes (m.usedBytes)} of ${formatBytes (m.capBytes)}`);
        this.gauge.setAttribute ("aria-valuemax", String (m.capBytes));
        this.gauge.setAttribute ("aria-valuenow", String (m.usedBytes));
        this.gauge.setAttribute ("aria-valuetext", `${formatBytes (m.usedBytes)} of ${formatBytes (m.capBytes)} used`);
        setPressed (this.inputsButton, this.controller.includeInputs);
        this.inputsButton.hidden = ! (this.ctx.store.get ("inputs") ?? []).some (e => e.purpose === "audio in");
        this.discardAllButton.disabled = recordings.length === 0;

        // the rows: kept per recording, and updated in place (so a rename in progress isn't lost)
        for (const [id, row] of this.rows)
        {
            if (! recordings.includes (row.recording))
            {
                row.element.remove();
                this.rows.delete (id);
            }
        }

        recordings.forEach ((r, index) =>
        {
            let row = this.rows.get (r.id);

            if (! row)
            {
                row = this.createRow (r);
                this.rows.set (r.id, row);
            }

            if (this.list.children[index] !== row.element)
                this.list.insertBefore (row.element, this.list.children[index] ?? null);

            this.updateRow (row);
        });

        // a new recording (or the only one) is selected; a discarded one's selection moves on
        const newest = recordings[0];

        if (newest && newest.id !== this.lastNewestID)
            this.select (newest.id);
        else if (this.selectedID !== null && ! this.manager.getRecording (this.selectedID))
            this.select (newest?.id ?? null);

        this.lastNewestID = newest?.id ?? null;

        const reason = this.controller.getDisabledReason();
        setText (this.emptyMessage, reason && reason !== "The audio device's sample rate isn't known yet"
                                       ? `No recordings. ${reason}.`
                                       : "No recordings yet. Press ● Capture in the top bar (or C) to record the patch's outputs.");
        this.emptyMessage.hidden = recordings.length > 0;
        this.list.hidden = recordings.length === 0;
    }

    createRow (recording)
    {
        const row = { recording };

        row.name = el ("span", { class: "cp-rec-name" });
        row.details = el ("span", { class: "cp-rec-details" });
        row.badges = el ("span", { class: "cp-rec-badges" });
        row.selectButton = el ("button", { class: "cp-rec-select", type: "button", "aria-pressed": "false",
                                           onclick: () => this.select (recording.id),
                                           ondblclick: () => this.startRename (row) },
                               row.name, row.details, row.badges);

        row.renameInput = el ("input", { class: "cp-input cp-rec-rename", type: "text", hidden: true, "aria-label": "Recording name",
                                         onkeydown: (e) => { if (e.key === "Enter") { e.preventDefault(); this.finishRename (row, true); }
                                                             else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); this.finishRename (row, false); } },
                                         onblur: () => this.finishRename (row, true) });

        row.stopButton = el ("button", { class: "cp-small-button cp-rec-stop", type: "button", title: "Stop recording", onclick: () => recording.stop() }, "■ Stop");
        row.renameButton = el ("button", { class: "cp-small-button", type: "button", title: "Rename", "aria-label": "Rename", onclick: () => this.startRename (row) }, "✎");
        row.exportButton = el ("button", { class: "cp-small-button cp-rec-export", type: "button", title: "Export as WAV",
                                           onclick: () => this.showExport (recording, row.exportButton, "whole") }, "Export…");
        row.discardButton = el ("button", { class: "cp-small-button cp-rec-discard", type: "button", title: "Discard this recording", "aria-label": "Discard",
                                            onclick: () => recording.discard() }, "✕");

        row.element = el ("li", { class: "cp-rec-row", dataset: { recording: String (recording.id) } },
                          row.selectButton, row.renameInput,
                          el ("span", { class: "cp-rec-actions" }, row.stopButton, row.renameButton, row.exportButton, row.discardButton));

        row.removeListener = recording.addListener (() => this.updateRow (row));
        this.disposer.add (() => row.removeListener?.());
        return row;
    }

    updateRow (row)
    {
        const r = row.recording;

        if (r.isDiscarded)
            return;

        const info = r.endpoints;
        const dropouts = r.dropouts;
        const isSelected = r.id === this.selectedID;
        const names = info.map (e => this.exporter.getEndpointName (e.endpointID)).join (", ");
        const stopReason = r.stopReason && r.stopReason.code !== RecordingStopReason.user ? r.stopReason.message : "";

        setText (row.name, r.name);
        setText (row.details, `${formatRecordingTime (r.durationSeconds)} · ${names || r.endpointIDs.join (", ")} · ${formatBytes (r.usedBytes)}`);

        const badges = [];

        if (r.isRecording)
            badges.push (["cp-rec-live", "● Recording", "Recording now"]);

        if (dropouts.length)
        {
            const frames = dropouts.reduce ((n, d) => n + d.numFrames, 0);
            badges.push (["cp-rec-dropouts", `${dropouts.length} dropout${dropouts.length === 1 ? "" : "s"}`,
                          `${frames} frames (${(frames / r.sampleRate * 1000).toFixed (1)} ms) weren't received, and are silence in the recording`]);
        }

        if (r.isUnaligned)
            badges.push (["cp-rec-unaligned", "unaligned", "The server didn't say where each chunk belongs (an older server), so dropouts can't be found and the endpoints may not line up"]);

        if (stopReason)
            badges.push (["cp-rec-stop-reason", stopReason, stopReason]);

        const key = JSON.stringify (badges);

        if (row.badgesKey !== key)
        {
            row.badgesKey = key;
            row.badges.replaceChildren (...badges.map (([cls, text, title]) => el ("span", { class: ["cp-rec-badge", cls], title }, text)));
        }

        row.element.classList.toggle ("cp-selected", isSelected);
        row.selectButton.setAttribute ("aria-pressed", isSelected ? "true" : "false");
        row.selectButton.setAttribute ("aria-label", `${r.name}: ${formatRecordingTime (r.durationSeconds)}${dropouts.length ? `, ${dropouts.length} dropouts` : ""}${r.isRecording ? ", recording" : ""}`);
        row.stopButton.hidden = ! r.isRecording;
        row.exportButton.disabled = r.numFrames === 0;
    }

    select (id)
    {
        this.selectedID = id;

        for (const row of this.rows.values())
            this.updateRow (row);

        const recording = this.selectedRecording;
        this.waveform.setRecording (recording);
        this.waveform.element.hidden = ! recording;
        this.viewerEmpty.hidden = !! recording || this.manager.recordings.length === 0;
    }

    startRename (row)
    {
        row.renameInput.value = row.recording.name;
        row.renameInput.hidden = false;
        row.selectButton.hidden = true;
        row.renameInput.focus();
        row.renameInput.select();
    }

    finishRename (row, shouldKeep)
    {
        if (row.renameInput.hidden)
            return;

        const name = row.renameInput.value.trim();
        row.renameInput.hidden = true;
        row.selectButton.hidden = false;

        if (shouldKeep && name && name !== row.recording.name)
            row.recording.setName (name);

        row.selectButton.focus();
    }

    showExport (recording, anchor, range)
    {
        if (! recording)
            return;

        const selection = recording === this.waveform.recording ? this.waveform.selection : null;
        this.exporter.open (anchor, recording, { range, selection });
    }
}

//==============================================================================
function setText (element, text)
{
    if (element.textContent !== text)
        element.textContent = text;
}

function safeWAVSize (numChannels, numFrames, format)
{
    try { return getWAVFileSize (numChannels, Math.max (0, numFrames), format); }
    catch (e) { return Infinity; }
}

function dirName (path)
{
    const i = Math.max (path.lastIndexOf ("/"), path.lastIndexOf ("\\"));
    return i > 0 ? path.substring (0, i) : path;
}
