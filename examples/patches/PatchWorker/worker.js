
/*
   Our manifest file declares this file as being the "worker" for our
   patch, meaning that it will be executed when the patch is created.

   Because everything in this script runs on the message thread, you
   should just do any setup you need to register some timers and events
   to respond to things that happen to the patch.

   In this example, we just launch a set of timers which will send events
   to our patch to trigger note playback.
*/


function sendAudioSampleDataToPatch (patchConnection, audioData)
{
    // Our Cmajor processor has an event to receive chunks of sample data
    // which it builds up into a sample to play, so we'll dispatch a series
    // of events with chunks of our sample in it..

    // The audio data arrives as an array of per-frame arrays, which we need
    // to interleave into a single mono array of floats, because that's
    // what the processor is expecting.
    const toMono = (frames) =>
    {
        let array = [];

        for (let i = 0; i < frames.length; ++i)
            array.push (frames[i][0]);

        return array;
    }

    for (let i = 0; i < audioData.frames.length; i += 512)
    {
        const numFrames = Math.min (512, audioData.frames.length - i);

        const chunk = {
            frames: toMono (audioData.frames.slice (i, i + numFrames)),
            startPos: i,
            numFrames: numFrames,
            sampleRate: audioData.sampleRate,
            sourceRoot: 72
        };

        // send the event with a timeout of 1000ms so that it'll retry for
        // a while if the patch's FIFO is busy
        patchConnection.sendEventOrValue ("sampleData", chunk, -1, 1000);
    }
}

function setTimersForNoteEvents (patchConnection)
{
    const notesToPlay = [
        { pitch: 79,  length: 1 },
        { pitch: 77,  length: 1 },
        { pitch: 69,  length: 2 },
        { pitch: 71,  length: 2 },
        { pitch: 76,  length: 1 },
        { pitch: 74,  length: 1 },
        { pitch: 65,  length: 2 },
        { pitch: 67,  length: 2 },
        { pitch: 74,  length: 1 },
        { pitch: 72,  length: 1 },
        { pitch: 64,  length: 2 },
        { pitch: 67,  length: 2 },
        { pitch: 72,  length: 4 }
    ];

    // Then kick off some timers to trigger the notes of a little tune...
    // Obviously this isn't going to be sample-accurate like it would
    // be to trigger notes within a Cmajor processor, but it illustrates
    // how you can send non-time-critical events from javascript..
    let time = 10;

    for (const note of notesToPlay)
    {
        setTimeout (() => { patchConnection.sendEventOrValue ("noteToPlay", note.pitch); }, time);
        time += note.length * 150;
    }

    setTimeout (() => { patchConnection.sendEventOrValue ("noteToPlay", 0); }, time);
}

function playTuneWithSample (patchConnection, audioData)
{
    // First, give the patch the sample we want to play..
    sendAudioSampleDataToPatch (patchConnection, audioData);

    // ..then send it some notes to play..
    setTimersForNoteEvents (patchConnection);
}

// The default function exported by this module will be called to run your
// worker process.
//
// The PatchConnection argument is an object for your worker code to use to
// control your patch. It's the same as the PatchConnection used in GUI code,
// and provides a whole API for attaching listeners and exchanging data with
// the patch.
export default function runWorker (patchConnection)
{
    // In this example, we'll start by asynchronously asking to read an
    // audio file that's in our resource bundle. When this file data is
    // ready, we'll pass it to our playTuneWithSample() function with
    // which will send it to our cmajor processor
    patchConnection.readResourceAsAudioData ("/resources/piano_72.ogg")
        .then (audioData => playTuneWithSample (patchConnection, audioData),
               error => console.log ("Failed to read audio file: " + error));
}
