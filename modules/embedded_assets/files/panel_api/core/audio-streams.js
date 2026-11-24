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

/// The number of frames in each chunk of an endpoint's full audio stream
export const fullAudioGranularity = 1024;

/// The EndpointDataHub options for the shared full audio stream
export const fullAudioStreamOptions = Object.freeze ({ granularity: fullAudioGranularity, fullAudioData: true });

/*
    The shared full-audio stream: an endpoint's actual samples (rather than the min/max that the
    meters use), for the spectrum analyser and the recorder.

    - Everything that wants samples subscribes through subscribeToFullAudio(), with the same
      options (fullAudioStreamOptions), so they share one EndpointDataHub feed and so ONE server
      listener per endpoint, however many of them are watching it.
    - Each chunk is fullAudioGranularity (1024) frames. The server sends it as
      { data: [[ch0 samples...], [ch1 samples...], ...] } (and a newer server adds frame: the
      index of the chunk's first frame). normaliseFullAudioMessage() turns that into
          { frame: number | undefined, numFrames, channels: Float32Array[] }
      converting each channel once: the result is cached per message, so every subscriber gets
      the same object. Treat it as read-only, and copy anything you keep.
    - frame is passed through when the server sends it and is undefined otherwise; with it, a
      gap between chunks (a dropout) can be detected.
    - Size: the client's FIFO for messages from the server is 64 KB, and a chunk of 8 channels ×
      1024 frames × 4 bytes is 32 KB of samples, so a chunk always fits with room to spare. Keep
      the granularity at 1024 unless that's checked again for more channels.

        const unsubscribe = subscribeToFullAudio (hub, "out", ({ frame, channels }) => analyser.addBlock (channels));

    (hub may be an EndpointDataHub or one of its SubscriptionGroups: anything with subscribe().)
*/

/// Subscribes listener ({ frame, numFrames, channels }) to an endpoint's full audio stream,
/// through an EndpointDataHub (or SubscriptionGroup). Messages without sample data are skipped.
/// Returns a function that unsubscribes.
export function subscribeToFullAudio (hub, endpointID, listener)
{
    return hub.subscribe (endpointID, (message) =>
    {
        const chunk = normaliseFullAudioMessage (message);

        if (chunk)
            listener (chunk);
    }, fullAudioStreamOptions);
}

/// Turns a full-audio-data message ({ data: [[...], ...], frame? }) into
/// { frame: number | undefined, numFrames, channels: Float32Array[] }, or null if it holds no
/// sample data. The result is cached per message object.
export function normaliseFullAudioMessage (message)
{
    if (message == null || typeof message !== "object")
        return null;

    const cached = normalisedMessages.get (message);

    if (cached !== undefined)
        return cached;

    let chunk = null;

    if (Array.isArray (message.data))
    {
        const channels = message.data.map (toFloat32Array);

        if (channels.every (c => c !== null))
        {
            const frame = Number.isFinite (message.frame) ? message.frame : undefined;
            const numFrames = channels.length > 0 ? Math.min (...channels.map (c => c.length)) : 0;
            chunk = { frame, numFrames, channels };
        }
    }

    normalisedMessages.set (message, chunk);
    return chunk;
}

//==============================================================================
const normalisedMessages = new WeakMap();

function toFloat32Array (samples)
{
    if (samples instanceof Float32Array)
        return samples;

    if (Array.isArray (samples) || ArrayBuffer.isView (samples))
        return Float32Array.from (samples, v => Number.isFinite (v) ? v : 0);

    return null;
}
