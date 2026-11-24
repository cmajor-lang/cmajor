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

import { subscribeToFullAudio } from "./audio-streams.js";
import { getMIDIDescription } from "../../cmaj_api/cmaj-midi-helpers.js";

/*
    The recorder: records the full audio stream of one or more audio endpoints into memory, for
    the recordings view and the waveform viewer to show, and to save as WAV files. No DOM.

    A RecordingManager (one per panel; it outlives patch changes) holds the recordings and the
    memory budget they share:

        const manager = new RecordingManager ({ capBytes, scheduleNotification });
        const recording = manager.start ({ endpoints: ["out", "in"], connection, sampleRate: 48000 });
        ...
        recording.stop();
        const blob = new Blob ([...generateWAVParts (recording.getWAVBlockSource ("out"), recording.sampleRate)]);
        recording.discard();

    RecordingManager
      new RecordingManager ({ capBytes = defaultRecordingCapBytes (256 MB),
                              scheduleNotification = fn => setTimeout (fn, 16) })
          scheduleNotification (fn) must call fn once, later. Progress notifications (new audio,
          memory use) are coalesced through it, so there's at most one per call of fn. In the
          panel, pass fn => ctx.scheduler.schedule (() => { fn(); return false; }) to get at
          most one per animation frame.
      start ({ endpoints, connection | hub, sampleRate, name? })  -> Recording
          endpoints: [endpointID | { endpointID }, ...] (audio endpoints; one or more).
          connection: the store's { patchConnection, hub, ... }; or hub: an EndpointDataHub or
          a SubscriptionGroup. Pass the hub itself, or a group that stays active: an inactive
          group gets no audio, and its gap is recorded as a dropout. sampleRate: the patch's.
          Throws an Error for bad options.
      recordings                  Recording[], oldest first (discarded ones are removed)
      getRecording (id)           -> Recording | undefined
      isRecording                 true if any recording is running
      usedBytes, capBytes, remainingBytes
      setCapBytes (bytes)         a running recording stops when its next block wouldn't fit
      stopAll (reason?)           stops the running recordings (reason defaults to "user")
      discardAll()
      handleConnectionChanged (connection)
          Call when the store's "connection" changes: running recordings that were started on a
          different connection (or hub) stop with the reason "connection-lost". (A rebuild keeps
          the connection and hub, so it doesn't stop anything; the server carries on counting
          frames and the hub keeps its listeners, so the recording continues, with a dropout
          for the frames that weren't sent.)
      stopRecordingsFor (connectionOrHub, reason = "connection-lost")
      addListener (fn)            -> remove(); fn (manager) after a recording is added, stopped
                                  or discarded (straight away) and on progress or memory use
                                  changes (throttled)
      dispose()                   stops and discards everything (reason "disposed")

    Recording
      id, name, setName (name), sampleRate, startTime / stopTime (Date.now() values; stopTime is
      null while recording), state ("recording" | "stopped" | "discarded"), isRecording,
      isDiscarded, stopReason (null while recording, else { code, message }: see
      RecordingStopReason), connection, hub.
      numFrames             length of the common timeline (the longest endpoint)
      durationSeconds       numFrames / sampleRate
      usedBytes             the memory this recording's blocks take
      originFrame           the server frame that is recording frame 0 (undefined until known,
                            or if no endpoint's chunks have frames)
      isUnaligned           true if any endpoint's chunks came without frame (an older server):
                            they're appended back to back, so dropouts can't be detected
      endpointIDs           string[]
      endpoints             [{ endpointID, numChannels, numFrames, leadingSilenceFrames,
                               trimmedLeadingFrames, dropouts: [{ startFrame, numFrames }],
                               overlaps: { count, frames }, isUnaligned, firstServerFrame }]
                            (a fresh snapshot each time it's read)
      getEndpointInfo (endpointID)   one of those
      dropouts              [{ endpointID, startFrame, numFrames }], sorted by startFrame
      markers               [{ id, kind, label, endpointID?, frame, isApproximate, time, value? }]
      droppedMarkerCount    markers not added because there were maxRecordingMarkers already
      addListener (fn)      -> remove(); fn (recording), throttled like the manager's
      stop (reason = "user")         idempotent
      discard()             stops it, frees its blocks, and removes it from the manager

      Markers
      addMarker ({ kind = "user", label = "", endpointID?, value?, frame? })  -> marker | null
          kind: "event" | "midi" | "parameter" | "user". Without frame, it's stamped with the
          latest audio frame received (the end of the newest chunk), so it can be late by up to
          a chunk (fullAudioGranularity frames): isApproximate is true. With frame (e.g. a user
          marker placed in the viewer) it's exact. A "parameter" marker for the same endpoint at
          the same frame as the previous marker replaces that one's label and value, so a
          parameter moving fast (a fuzzer) leaves one marker per chunk, not one per change.
          Returns null once there are maxRecordingMarkers (counted in droppedMarkerCount).
      removeMarker (marker)
      watchEvents ({ events: [{ endpointID, kind?: "event" | "midi", name? }],
                     parameters: [endpointID | { endpointID, name? }],
                     formatLabel?: (kind, name, value) => string })  -> unsubscribe()
          Adds a marker for every event, MIDI message and parameter change, through the same hub.
          (The value a parameter already has when this is called isn't a change.) It all stops
          when the recording stops.

      Reading the audio (frames count from the recording's start; outside what an endpoint
      recorded, the signal is 0)
      getSamples (endpointID, channel, startFrame, numFrames, out?)  -> Float32Array (out or new)
      getMinMax (endpointID, channel, startFrame, endFrame, numBuckets, out?)  -> { min, max }
          Float32Array[numBuckets] each (out = { min, max } to reuse arrays). Bucket i covers
          frames [floor (start + i * span), floor (start + (i + 1) * span)), span = (end - start)
          / numBuckets, and at least one frame. The result is exact (it matches scanning the
          samples), and fast at any zoom: each bucket uses the coarsest mip level whose buckets
          fit inside it, the finer level for its edges and raw samples only for what's left,
          so a bucket costs at most ~2 * 256 + 2 * 16 + span / 4096 reads.
      getWAVBlockSource (endpointID, { startFrame = 0, endFrame = numFrames }?)  -> WAVBlockSource
          { numChannels, numFrames, blockSize, getBlock (channel, index) } for wav-encoder.js's
          encodeWAVFromBlocks / generateWAVParts. Without a range, or a range starting on a
          block boundary (a multiple of recordingBlockSize), getBlock returns the stored blocks
          (no copying; silent stretches share one zero block: treat them all as read-only).
          A range starting mid-block copies each block into a new array as it's asked for.
          The file covers the common timeline, so every endpoint's file has the same length.
          Throws for an unknown endpoint.

    Storage
      - Per endpoint and channel, recordingBlockSize (65536)-frame Float32Array blocks, allocated
        as the audio reaches them, with min/max mip levels at 256 and 4096 frames per bucket,
        allocated with each block (bytesPerRecordingBlock in all) and updated as chunks arrive.
        A block that lies wholly inside a gap is never allocated: it reads as silence.
      - Nothing is allocated per chunk except new blocks.
      - Every block counts against the manager's cap. When a block won't fit, the recording
        fills the blocks it has, then stops with "memory-limit". Nothing already recorded is
        dropped.

    Alignment (by each chunk's frame: see feature 4's "Server protocol")
      - The first chunk of each endpoint is held until every endpoint has sent one (or
        alignmentWindowFrames have gone by, in case one never does). The earliest first frame
        becomes recording frame 0; the other endpoints start with leading silence, which isn't
        a dropout.
      - A chunk that starts later than expected: the gap is silence and a dropout
        { startFrame, numFrames } on that endpoint.
      - A chunk that overlaps what's already recorded, or goes backwards: the frames already
        recorded are dropped (overlaps.count and overlaps.frames count them).
      - A chunk without frame (an older server): appended back to back, and the endpoint and
        recording are marked isUnaligned.
      - More channels in a later chunk (a rebuild): the new channels are silent before it.
        Fewer: the missing ones record silence.
*/

/// The number of frames in each block of a recording
export const recordingBlockSize = 65536;

/// The frames per bucket of the min/max mip levels, finest first
export const recordingMipBucketSizes = Object.freeze ([256, 4096]);

/// The default memory limit for all of a manager's recordings
export const defaultRecordingCapBytes = 256 * 1024 * 1024;

/// The bytes one block of one channel takes: the samples and its min/max mip arrays
export const bytesPerRecordingBlock = 4 * (recordingBlockSize + 2 * (recordingBlockSize / 256) + 2 * (recordingBlockSize / 4096));

/// How far apart (in frames) the endpoints' first chunks can be before recording starts without the late ones
export const alignmentWindowFrames = 8192;

/// The most markers a recording keeps
export const maxRecordingMarkers = 10000;

/// The codes of Recording.stopReason
export const RecordingStopReason = Object.freeze ({
    user:           "user",
    memoryLimit:    "memory-limit",
    connectionLost: "connection-lost",
    discarded:      "discarded",
    disposed:       "disposed"
});

const defaultNotifyDelayMs = 16;

//==============================================================================
export class RecordingManager
{
    constructor ({ capBytes = defaultRecordingCapBytes, scheduleNotification } = {})
    {
        this.capBytes = capBytes;
        this.usedBytes = 0;
        this.recordings = [];
        this.recordingCounter = 0;
        this.scheduleNotification = scheduleNotification ?? (fn => setTimeout (fn, defaultNotifyDelayMs));
        this.notifier = new Notifier (this, this.scheduleNotification);
    }

    dispose()
    {
        for (const r of [...this.recordings])
            r.discard (RecordingStopReason.disposed);

        this.notifier.listeners.clear();
    }

    get remainingBytes()                { return Math.max (0, this.capBytes - this.usedBytes); }
    get isRecording()                   { return this.recordings.some (r => r.isRecording); }

    getRecording (id)                   { return this.recordings.find (r => r.id === id); }
    addListener (fn)                    { return this.notifier.add (fn); }

    setCapBytes (bytes)
    {
        if (! (Number.isFinite (bytes) && bytes >= 0))
            throw new Error ("The recording memory limit must be a number of bytes");

        this.capBytes = bytes;
        this.notifier.now();
    }

    start (options = {})
    {
        const recording = new Recording (this, options, ++this.recordingCounter);
        this.recordings.push (recording);
        recording.subscribe();
        this.notifier.now();
        return recording;
    }

    stopAll (reason = RecordingStopReason.user)
    {
        for (const r of this.recordings)
            r.stop (reason);
    }

    discardAll()
    {
        for (const r of [...this.recordings])
            r.discard();
    }

    handleConnectionChanged (connection)
    {
        for (const r of this.recordings)
            if (r.isRecording && ! r.isFor (connection))
                r.stop (RecordingStopReason.connectionLost);
    }

    stopRecordingsFor (connectionOrHub, reason = RecordingStopReason.connectionLost)
    {
        for (const r of this.recordings)
            if (r.isRecording && connectionOrHub && r.isFor (connectionOrHub))
                r.stop (reason);
    }

    //==============================================================================
    tryAllocate (bytes)
    {
        if (this.usedBytes + bytes > this.capBytes)
            return false;

        this.usedBytes += bytes;
        this.notifier.request();
        return true;
    }

    release (bytes)
    {
        this.usedBytes = Math.max (0, this.usedBytes - bytes);
    }

    removeRecording (recording)
    {
        const index = this.recordings.indexOf (recording);

        if (index >= 0)
            this.recordings.splice (index, 1);

        this.notifier.now();
    }
}

//==============================================================================
export class Recording
{
    constructor (manager, options, number)
    {
        const { endpoints, connection = null, sampleRate, name } = options;
        const hub = options.hub ?? connection?.hub;

        if (! Array.isArray (endpoints) || endpoints.length === 0)
            throw new Error ("A recording needs one or more audio endpoints");

        if (! hub || typeof hub.subscribe !== "function")
            throw new Error ("A recording needs a hub, or a connection with one");

        if (! (Number.isFinite (sampleRate) && sampleRate > 0))
            throw new Error ("A recording needs the patch's sample rate");

        this.manager = manager;
        this.id = number;
        this.connection = connection;
        this.hub = hub;
        this.sampleRate = sampleRate;
        this.startTime = Date.now();
        this.stopTime = null;
        this.name = name || `Recording ${number} – ${formatClockTime (new Date (this.startTime))}`;
        this.state = "recording";
        this.stopReason = null;
        this.usedBytes = 0;
        this.originFrame = undefined;
        this.isAligning = true;
        this.markers = [];
        this.droppedMarkerCount = 0;
        this.markerCounter = 0;
        this.eventUnsubscribers = [];
        this.notifier = new Notifier (this, manager.scheduleNotification);
        this.tracks = new Map();

        for (const e of endpoints)
        {
            const endpointID = typeof e === "string" ? e : e?.endpointID;

            if (typeof endpointID !== "string" || endpointID.length === 0)
                throw new Error ("Each recorded endpoint needs an endpointID");

            if (! this.tracks.has (endpointID))
                this.tracks.set (endpointID, new Track (endpointID));
        }
    }

    subscribe()
    {
        for (const track of this.tracks.values())
            track.unsubscribe = subscribeToFullAudio (this.hub, track.endpointID, chunk => this.handleChunk (track, chunk));
    }

    get isRecording()           { return this.state === "recording"; }
    get isDiscarded()           { return this.state === "discarded"; }
    get endpointIDs()           { return [...this.tracks.keys()]; }
    get durationSeconds()       { return this.numFrames / this.sampleRate; }
    get isUnaligned()           { return [...this.tracks.values()].some (t => t.isUnaligned); }

    get numFrames()
    {
        let n = 0;

        for (const t of this.tracks.values())
            n = Math.max (n, t.numFrames);

        return n;
    }

    get endpoints()             { return [...this.tracks.values()].map (t => t.getInfo()); }
    getEndpointInfo (endpointID) { return this.tracks.get (endpointID)?.getInfo(); }

    get dropouts()
    {
        const all = [];

        for (const t of this.tracks.values())
            for (const d of t.dropouts)
                all.push ({ endpointID: t.endpointID, startFrame: d.startFrame, numFrames: d.numFrames });

        return all.sort ((a, b) => a.startFrame - b.startFrame);
    }

    addListener (fn)            { return this.notifier.add (fn); }

    setName (name)
    {
        this.name = String (name);
        this.changed (true);
    }

    isFor (connectionOrHub)
    {
        if (! connectionOrHub)
            return false;

        if (this.connection && this.connection === connectionOrHub)
            return true;

        const ownHub = getRootHub (this.hub);
        return ownHub === getRootHub (connectionOrHub) || ownHub === getRootHub (connectionOrHub.hub);
    }

    //==============================================================================
    stop (reason = RecordingStopReason.user)
    {
        if (! this.isRecording)
            return;

        if (this.isAligning)
            this.finishAligning();

        this.state = "stopped";
        this.stopTime = Date.now();
        this.stopReason = { code: reason, message: getStopMessage (reason, this.manager) };

        for (const t of this.tracks.values())
        {
            try { t.unsubscribe?.(); }
            catch (e) { console.error (e); }

            t.unsubscribe = null;
        }

        for (const u of this.eventUnsubscribers.splice (0))
        {
            try { u(); }
            catch (e) { console.error (e); }
        }

        this.changed (true);
    }

    discard (reason = RecordingStopReason.discarded)
    {
        if (this.isDiscarded)
            return;

        this.stop (reason);
        this.state = "discarded";

        for (const t of this.tracks.values())
            t.channels = [];

        this.manager.release (this.usedBytes);
        this.usedBytes = 0;
        this.notifier.now();
        this.notifier.listeners.clear();
        this.manager.removeRecording (this);
    }

    //==============================================================================
    addMarker ({ kind = "user", label = "", endpointID, value, frame } = {})
    {
        if (this.isDiscarded)
            return null;

        const isApproximate = ! Number.isFinite (frame);
        const markerFrame = isApproximate ? this.getLatestFrame() : Math.max (0, Math.floor (frame));
        const last = this.markers[this.markers.length - 1];

        if (kind === "parameter" && isApproximate && last && last.kind === "parameter"
             && last.endpointID === endpointID && last.frame === markerFrame)
        {
            last.label = label;
            last.value = value;
            last.time = Date.now();
            this.changed (false);
            return last;
        }

        if (this.markers.length >= maxRecordingMarkers)
        {
            ++this.droppedMarkerCount;
            return null;
        }

        const marker = { id: ++this.markerCounter, kind, label, frame: markerFrame, isApproximate, time: Date.now() };

        if (endpointID !== undefined)   marker.endpointID = endpointID;
        if (value !== undefined)        marker.value = value;

        // keep them in frame order (user markers placed in the viewer can land anywhere)
        let index = this.markers.length;

        while (index > 0 && this.markers[index - 1].frame > markerFrame)
            --index;

        this.markers.splice (index, 0, marker);
        this.changed (false);
        return marker;
    }

    removeMarker (marker)
    {
        const index = this.markers.indexOf (marker);

        if (index >= 0)
        {
            this.markers.splice (index, 1);
            this.changed (true);
        }
    }

    watchEvents ({ events = [], parameters = [], formatLabel = defaultMarkerLabel } = {})
    {
        if (! this.isRecording)
            return () => {};

        const unsubscribers = [];

        for (const e of events)
        {
            const endpointID = typeof e === "string" ? e : e.endpointID;
            const kind = e.kind === "midi" ? "midi" : "event";
            const name = e.name ?? endpointID;

            unsubscribers.push (this.hub.subscribe (endpointID, (value) =>
            {
                if (this.isRecording)
                    this.addMarker ({ kind, label: formatLabel (kind, name, value), endpointID, value });
            }));
        }

        for (const p of parameters)
        {
            const endpointID = typeof p === "string" ? p : p.endpointID;
            const name = p.name ?? endpointID;
            let isSubscribing = true;

            unsubscribers.push (this.hub.subscribeParameter (endpointID, (value) =>
            {
                if (this.isRecording && ! isSubscribing)
                    this.addMarker ({ kind: "parameter", label: formatLabel ("parameter", name, value), endpointID, value });
            }));

            isSubscribing = false;
        }

        this.eventUnsubscribers.push (...unsubscribers);

        return () =>
        {
            for (const u of unsubscribers)
            {
                const index = this.eventUnsubscribers.indexOf (u);

                if (index >= 0)
                {
                    this.eventUnsubscribers.splice (index, 1);
                    u();
                }
            }
        };
    }

    //==============================================================================
    getSamples (endpointID, channel, startFrame, numFrames, out)
    {
        const result = out ?? new Float32Array (numFrames);
        const blocks = this.tracks.get (endpointID)?.channels[channel];
        const recorded = this.tracks.get (endpointID)?.numFrames ?? 0;

        result.fill (0, 0, numFrames);

        if (! blocks)
            return result;

        const first = Math.max (0, startFrame);
        const end = Math.min (recorded, startFrame + numFrames);

        for (let frame = first; frame < end;)
        {
            const blockIndex = Math.floor (frame / recordingBlockSize);
            const within = frame - blockIndex * recordingBlockSize;
            const n = Math.min (end - frame, recordingBlockSize - within);
            const block = blocks[blockIndex];

            if (block)
                result.set (block.samples.subarray (within, within + n), frame - startFrame);

            frame += n;
        }

        return result;
    }

    getMinMax (endpointID, channel, startFrame, endFrame, numBuckets, out)
    {
        const min = out?.min ?? new Float32Array (numBuckets);
        const max = out?.max ?? new Float32Array (numBuckets);
        const track = this.tracks.get (endpointID);
        const blocks = track?.channels[channel];
        const recorded = blocks ? track.numFrames : 0;
        const span = (endFrame - startFrame) / numBuckets;

        for (let i = 0; i < numBuckets; ++i)
        {
            const a = Math.floor (startFrame + i * span);
            const b = Math.max (a + 1, Math.floor (startFrame + (i + 1) * span));
            const lo = Math.max (0, a), hi = Math.min (recorded, b);

            accMin = Infinity;
            accMax = -Infinity;

            if (a < lo || b > hi)
                accumulate (0, 0);

            for (let frame = lo; frame < hi;)
            {
                const blockIndex = Math.floor (frame / recordingBlockSize);
                const blockStart = blockIndex * recordingBlockSize;
                const blockEnd = Math.min (hi, blockStart + recordingBlockSize);
                const block = blocks[blockIndex];

                if (block)
                    blockMinMax (block, frame - blockStart, blockEnd - blockStart);
                else
                    accumulate (0, 0);

                frame = blockEnd;
            }

            min[i] = accMin;
            max[i] = accMax;
        }

        return { min, max };
    }

    getWAVBlockSource (endpointID, { startFrame = 0, endFrame } = {})
    {
        const track = this.tracks.get (endpointID);

        if (! track)
            throw new Error (`"${endpointID}" isn't in this recording`);

        const total = this.numFrames;
        const start = Math.min (total, Math.max (0, Math.floor (startFrame)));
        const end = Math.min (total, Math.max (start, Math.floor (endFrame ?? total)));
        const numFrames = end - start;
        const numChannels = Math.max (1, track.numChannels);

        if (start % recordingBlockSize === 0)
        {
            const firstBlock = start / recordingBlockSize;

            return {
                numChannels, numFrames, blockSize: recordingBlockSize,
                getBlock: (channel, index) => track.channels[channel]?.[firstBlock + index]?.samples ?? getZeroBlock()
            };
        }

        return {
            numChannels, numFrames, blockSize: recordingBlockSize,
            getBlock: (channel, index) =>
            {
                const blockStart = index * recordingBlockSize;
                const length = Math.max (0, Math.min (recordingBlockSize, numFrames - blockStart));
                return this.getSamples (endpointID, channel, start + blockStart, length);
            }
        };
    }

    //==============================================================================
    getLatestFrame()
    {
        if (! this.isAligning)
            return this.numFrames;

        let first = Infinity, end = 0;

        for (const t of this.tracks.values())
        {
            if (t.pending.length > 0)
            {
                const last = t.pending[t.pending.length - 1];
                first = Math.min (first, t.pending[0].frame);
                end = Math.max (end, last.frame + last.numFrames);
            }
        }

        return Math.max (this.numFrames, first === Infinity ? 0 : end - first);
    }

    changed (immediately)
    {
        if (immediately)
        {
            this.notifier.now();
            this.manager.notifier.now();
        }
        else
        {
            this.notifier.request();
            this.manager.notifier.request();
        }
    }

    handleChunk (track, chunk)
    {
        if (! this.isRecording || chunk.numFrames === 0)
            return;

        if (! this.isAligning)
            return this.appendChunk (track, chunk);

        if (chunk.frame === undefined)
        {
            // an older server: this endpoint can't be aligned, so it doesn't wait
            track.isUnaligned = true;
            this.appendChunk (track, chunk);
        }
        else
        {
            track.pending.push (chunk);
        }

        if (this.isReadyToAlign())
            this.finishAligning();

        this.changed (false);
    }

    isReadyToAlign()
    {
        let first = Infinity, end = -Infinity, everyoneHasSent = true;

        for (const t of this.tracks.values())
        {
            if (t.pending.length > 0)
            {
                const last = t.pending[t.pending.length - 1];
                first = Math.min (first, t.pending[0].frame);
                end = Math.max (end, last.frame + last.numFrames);
            }
            else if (! t.isUnaligned)
            {
                everyoneHasSent = false;
            }
        }

        if (first === Infinity)
            return everyoneHasSent;     // all unaligned

        return everyoneHasSent || end - first >= alignmentWindowFrames;
    }

    finishAligning()
    {
        this.isAligning = false;

        for (const t of this.tracks.values())
            if (t.pending.length > 0)
                this.originFrame = Math.min (this.originFrame ?? Infinity, t.pending[0].frame);

        for (const t of this.tracks.values())
            for (const chunk of t.pending.splice (0))
                if (this.isRecording)
                    this.appendChunk (t, chunk);
    }

    appendChunk (track, chunk)
    {
        const n = chunk.numFrames;

        if (chunk.channels.length > track.numChannels)
            track.setNumChannels (chunk.channels.length);

        let dest;

        if (chunk.frame === undefined || track.isUnaligned || this.originFrame === undefined)
        {
            if (chunk.frame === undefined || this.originFrame === undefined)
                track.isUnaligned = true;

            dest = track.numFrames;
        }
        else
        {
            dest = chunk.frame - this.originFrame;
        }

        if (! track.hasData && chunk.frame !== undefined)
            track.firstServerFrame = chunk.frame;

        let skip = 0;

        if (dest < track.numFrames)
        {
            skip = Math.min (n, track.numFrames - dest);

            if (track.hasData)
            {
                ++track.overlapCount;
                track.overlapFrames += skip;
            }
            else
            {
                track.trimmedLeadingFrames += skip;
            }
        }
        else if (dest > track.numFrames)
        {
            const gap = dest - track.numFrames;

            if (track.hasData)
                track.dropouts.push ({ startFrame: track.numFrames, numFrames: gap });
            else
                track.leadingSilenceFrames += gap;

            track.fillSilence (dest);
        }

        track.hasData = true;

        if (skip < n && ! this.writeFrames (track, chunk, skip, n - skip))
            this.stop (RecordingStopReason.memoryLimit);
        else
            this.changed (false);
    }

    writeFrames (track, chunk, sourceOffset, count)
    {
        const numChannels = track.numChannels;
        let dest = track.numFrames;
        let done = 0;

        while (done < count)
        {
            const blockIndex = Math.floor (dest / recordingBlockSize);
            const within = dest - blockIndex * recordingBlockSize;
            const n = Math.min (count - done, recordingBlockSize - within);

            if (! this.ensureBlock (track, blockIndex))
            {
                track.numFrames = dest;
                return false;
            }

            const offset = sourceOffset + done;

            for (let ch = 0; ch < numChannels; ++ch)
            {
                const block = track.channels[ch][blockIndex];
                const source = chunk.channels[ch];

                if (source)
                {
                    const samples = block.samples;

                    for (let i = 0; i < n; ++i)
                        samples[within + i] = source[offset + i];
                }

                updateMips (block, within, within + n);
            }

            done += n;
            dest += n;
        }

        track.numFrames = dest;
        return true;
    }

    ensureBlock (track, blockIndex)
    {
        let missing = 0;

        for (const blocks of track.channels)
            if (! blocks[blockIndex])
                ++missing;

        if (missing === 0)
            return true;

        const bytes = missing * bytesPerRecordingBlock;

        if (! this.manager.tryAllocate (bytes))
            return false;

        this.usedBytes += bytes;

        for (const blocks of track.channels)
            if (! blocks[blockIndex])
                blocks[blockIndex] = createBlock();

        return true;
    }
}

//==============================================================================
class Track
{
    constructor (endpointID)
    {
        this.endpointID = endpointID;
        this.numChannels = 0;
        this.channels = [];         // channel -> sparse array of blocks (a hole is silence)
        this.numFrames = 0;
        this.hasData = false;
        this.isUnaligned = false;
        this.pending = [];
        this.firstServerFrame = undefined;
        this.leadingSilenceFrames = 0;
        this.trimmedLeadingFrames = 0;
        this.dropouts = [];
        this.overlapCount = 0;
        this.overlapFrames = 0;
        this.unsubscribe = null;
    }

    setNumChannels (n)
    {
        while (this.channels.length < n)
            this.channels.push ([]);

        this.numChannels = n;
    }

    /// Advances to toFrame through silence, keeping the mip levels of a part-filled block right
    fillSilence (toFrame)
    {
        const from = this.numFrames;
        const blockIndex = Math.floor (from / recordingBlockSize);
        const within = from - blockIndex * recordingBlockSize;
        const end = Math.min (recordingBlockSize, within + (toFrame - from));

        if (within > 0)
            for (const blocks of this.channels)
                if (blocks[blockIndex])
                    updateMips (blocks[blockIndex], within, end);

        this.numFrames = toFrame;
    }

    getInfo()
    {
        return {
            endpointID: this.endpointID,
            numChannels: this.numChannels,
            numFrames: this.numFrames,
            leadingSilenceFrames: this.leadingSilenceFrames,
            trimmedLeadingFrames: this.trimmedLeadingFrames,
            dropouts: this.dropouts.map (d => ({ ...d })),
            overlaps: { count: this.overlapCount, frames: this.overlapFrames },
            isUnaligned: this.isUnaligned,
            firstServerFrame: this.firstServerFrame
        };
    }
}

//==============================================================================
/// Calls its listeners with a target: now(), or coalesced through a scheduler with request()
class Notifier
{
    constructor (target, schedule)
    {
        this.target = target;
        this.schedule = schedule;
        this.listeners = new Set();
        this.isPending = false;
        this.flush = () => { if (this.isPending) this.now(); };
    }

    add (fn)
    {
        this.listeners.add (fn);
        return () => this.listeners.delete (fn);
    }

    request()
    {
        if (this.isPending || this.listeners.size === 0)
            return;

        this.isPending = true;
        this.schedule (this.flush);
    }

    now()
    {
        this.isPending = false;

        for (const l of [...this.listeners])
        {
            try { l (this.target); }
            catch (e) { console.error (e); }
        }
    }
}

//==============================================================================
const level0Size = recordingMipBucketSizes[0];
const level1Size = recordingMipBucketSizes[1];
const level0PerLevel1 = level1Size / level0Size;
let zeroBlock = null;
let accMin = Infinity, accMax = -Infinity;

function createBlock()
{
    return {
        samples: new Float32Array (recordingBlockSize),
        min: [new Float32Array (recordingBlockSize / level0Size), new Float32Array (recordingBlockSize / level1Size)],
        max: [new Float32Array (recordingBlockSize / level0Size), new Float32Array (recordingBlockSize / level1Size)]
    };
}

function getZeroBlock()
{
    zeroBlock ??= new Float32Array (recordingBlockSize);
    return zeroBlock;
}

/// Recomputes the mip buckets that frames [from, to) of a block fall in, from the start of
/// each bucket up to `to` (the block's frames beyond `to` aren't recorded yet)
function updateMips (block, from, to)
{
    const samples = block.samples, min0 = block.min[0], max0 = block.max[0];
    const firstBucket = Math.floor (from / level0Size), lastBucket = Math.floor ((to - 1) / level0Size);

    for (let b = firstBucket; b <= lastBucket; ++b)
    {
        const start = b * level0Size, end = Math.min (start + level0Size, to);
        let mn = samples[start], mx = mn;

        for (let i = start + 1; i < end; ++i)
        {
            const v = samples[i];

            if (v < mn) mn = v;
            else if (v > mx) mx = v;
        }

        min0[b] = mn;
        max0[b] = mx;
    }

    const min1 = block.min[1], max1 = block.max[1];
    const first1 = Math.floor (from / level1Size), last1 = Math.floor ((to - 1) / level1Size);

    for (let b = first1; b <= last1; ++b)
    {
        const start = b * level0PerLevel1;
        const end = Math.ceil (Math.min ((b + 1) * level1Size, to) / level0Size);
        let mn = min0[start], mx = max0[start];

        for (let i = start + 1; i < end; ++i)
        {
            if (min0[i] < mn) mn = min0[i];
            if (max0[i] > mx) mx = max0[i];
        }

        min1[b] = mn;
        max1[b] = mx;
    }
}

function accumulate (mn, mx)
{
    if (mn < accMin) accMin = mn;
    if (mx > accMax) accMax = mx;
}

function rawMinMax (samples, from, to)
{
    for (let i = from; i < to; ++i)
    {
        const v = samples[i];

        if (v < accMin) accMin = v;
        if (v > accMax) accMax = v;
    }
}

function level0MinMax (block, from, to)
{
    const first = Math.ceil (from / level0Size), last = Math.floor (to / level0Size);

    if (first >= last)
        return rawMinMax (block.samples, from, to);

    rawMinMax (block.samples, from, first * level0Size);

    for (let b = first; b < last; ++b)
        accumulate (block.min[0][b], block.max[0][b]);

    rawMinMax (block.samples, last * level0Size, to);
}

/// Accumulates the min/max of frames [from, to) of a block (all recorded), exactly
function blockMinMax (block, from, to)
{
    const first = Math.ceil (from / level1Size), last = Math.floor (to / level1Size);

    if (first >= last)
        return level0MinMax (block, from, to);

    level0MinMax (block, from, first * level1Size);

    for (let b = first; b < last; ++b)
        accumulate (block.min[1][b], block.max[1][b]);

    level0MinMax (block, last * level1Size, to);
}

function getRootHub (hubOrGroup)
{
    // a SubscriptionGroup has the hub it belongs to as .hub
    const inner = hubOrGroup?.hub;
    return inner && typeof inner.subscribe === "function" && typeof hubOrGroup.setActive === "function" ? inner : hubOrGroup;
}

function getStopMessage (reason, manager)
{
    switch (reason)
    {
        case RecordingStopReason.user:              return "Stopped";
        case RecordingStopReason.memoryLimit:       return `Stopped because the recordings reached the ${formatMegabytes (manager.capBytes)} memory limit`;
        case RecordingStopReason.connectionLost:    return "Stopped because the patch was unloaded or changed";
        case RecordingStopReason.discarded:         return "Discarded";
        case RecordingStopReason.disposed:          return "Stopped because the panel closed";
        default:                                    return String (reason);
    }
}

function formatMegabytes (bytes)
{
    return `${Math.round (bytes / (1024 * 1024))} MB`;
}

function formatClockTime (date)
{
    const pad = (n) => String (n).padStart (2, "0");
    return `${pad (date.getHours())}:${pad (date.getMinutes())}:${pad (date.getSeconds())}`;
}

/// The default label for markers made by watchEvents()
export function defaultMarkerLabel (kind, name, value)
{
    if (kind === "midi" && typeof value?.message === "number")
        return `${name}: ${getMIDIDescription (value.message)}`;

    if (typeof value === "number")
        return `${name} = ${Number.isInteger (value) ? value : Number (value.toPrecision (4))}`;

    if (typeof value === "string" || typeof value === "boolean")
        return `${name} = ${value}`;

    try
    {
        const text = JSON.stringify (value);
        return `${name}: ${text.length > 60 ? text.substring (0, 57) + "..." : text}`;
    }
    catch (e)
    {
        return name;
    }
}
