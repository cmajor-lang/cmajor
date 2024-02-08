
/*
   Our manifest file declares this file as being the "worker" for our
   patch, meaning that it will be executed when the patch is created.

   Because everything in this script runs on the message thread, you
   should just do any setup you need to register some timers and events
   to respond to things that happen to the patch.

   In this example, we just launch a set of timers which will send events
   to our patch to trigger note playback.
*/

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

// The default function exported by this module will be called to run your
// worker process.
//
// The PatchConnection argument is an object for your worker code to use to
// control your patch. It's the same as the PatchConnection used in GUI code,
// and provides a whole API for attaching listeners and exchanging data with
// the patch.
export default function runWorker (patchConnection)
{
    // kick off some timers to trigger the notes of a little tune...
    // Obviously this isn't going to be sample-accurate like it would
    // be to trigger notes within a Cmajor processor, but it
    // illustrates how you might send non-time-critical events from
    // javascript..

    let time = 0;

    for (const note of notesToPlay)
    {
        setTimeout (() => { patchConnection.sendEventOrValue ("noteToPlay", note.pitch); }, time);
        time += note.length * 150;
    }

    setTimeout (() => { patchConnection.sendEventOrValue ("noteToPlay", 0); }, time);
}
