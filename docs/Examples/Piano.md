## Piano

This example is found in the piano folder in the example patches section. There, you'll find `Piano.cmajorpatch` and `Piano.cmajor` as well as 5 audio files:

```
Piano/Piano.cmajorpatch
Piano/Piano.cmajor
Piano/piano_36.ogg
Piano/piano_48.ogg
Piano/piano_60.ogg
Piano/piano_72.ogg
Piano/piano_84.ogg
```

Have a look inside the `Piano.cmajorpatch` and `Piano.cmajor` files. `Piano.cmajorpatch` is the description of the patch, with links to the resources it needs, and `Piano.cmajor` contains the actual audio DSP code.

### Code walkthrough

This is a *very* low-fi piano, but it shows how to connect together various standard library helpers into a multi-voice MIDI synthesiser that plays back audio samples loaded from the patch resources.

#### The main graph: `Piano`

If you look in the `Piano.cmajor` file, near the top you'll see this declaration:

```
graph Piano  [[ main ]]
{
```

A `.cmajor` file can contain many graph declarations, so the `[[ main ]]` annotation tells the compiler that this is the one we want our patch to play.

Inside our `Piano` graph, the first thing to be declared are its input and output endpoints:

```cpp
    output stream float out;
    input event std::midi::Message midiIn;
    input gain.volume;
```

The output stream `out` is where the audio data will emerge.

The `midiIn` input will receive incoming MIDI events with the type `std::midi::Message`

The `input gain.volume` declaration exports an input parameter from a node in the graph called `gain`. This will appear as a parameter on our patch's GUI, and when you turn the dial the values will arrive at this input.

Next, the graph's nodes are declared:

```cpp
    node gain = std::levels::SmoothedGain (float, 0.1f);
    node voices = Voice[numVoices];
```

Then, all the connections between the nodes and the graph's inputs and outputs are listed:

```cpp
    connection midiIn
                -> std::midi::MPEConverter
                -> std::voices::VoiceAllocator(numVoices)
                -> voices
                -> gain.in;

    connection gain -> out;
```

So in this graph, the incoming MIDI events are sent into a `std::midi::MPEConverter` node. This is a standard library helper object which converts raw MIDI events to a stream of more usable note event objects.

From there, these note on/off events are sent to another standard library object, a `VoiceAllocator` which sends them on to our array of `Voice` objects.

Next, all the outputs from our voice array are funnelled into our `gain` node. This node uses another helper object `std::levels::SmoothedGain` which applies a dynamic gain to the siganl that flows through it, and damps-down the rate at which this gain can change, to avoid audible glitches.

Finally, the output of our `gain` node is sent to the graph's main output stream, and this is what we'll hear when we play the patch.

#### A simple voice

Below the `Piano` declaration you'll see the `Voice` graph's defintion:

```
graph Voice
{
    output stream float out;
    input event (std::notes::NoteOn, std::notes::NoteOff) eventIn;
```

Each `Voice` has an audio output stream, and takes input events which tell it when a note is started and stopped.

```
node
{
    envelope     = std::envelopes::FixedASR (0.0f, 0.3f);
    attenuator   = std::levels::DynamicGain (float);
    noteSelector = NoteSelector;
    samplePlayer = std::audio_data::SamplePlayer (PianoSample::source);
}

connection
{
    eventIn -> noteSelector;
    noteSelector.content -> samplePlayer.content;
    noteSelector.speedRatio -> samplePlayer.speedRatio;
    samplePlayer -> attenuator.in;
    eventIn -> envelope -> attenuator.gain;
    attenuator -> out;
}

```

The `Voice` uses a `std::envelopes::FixedASR` to generate a simple envelope, and feeds this envelope into a `std::levels::DynamicGain` node to apply it to the signal.

The actual sample playback is provided by a `std::audio_data::SamplePlayer` node.

To glue things together, it uses a `NoteSelector` processor which we'll look at in a moment.

Incoming note on/off events are sent both to the `NoteSelector` and the `FixedASR` so that they can be used to trigger the envelope, and also to select and trigger the correct note sample.

#### Triggering samples

The `NoteSelector` processor is where we decide which of our piano audio samples we're going to use to play a note:

```
processor NoteSelector
{
    input event std::notes::NoteOn eventIn;
    output event PianoSample::source content;
    output event float speedRatio;
```

This is a `processor` rather than a `graph`. It takes any incoming note-on events (it doesn't need note-offs), and emits events which can be used to tell a `std::audio_data::SamplePlayer` what data to play, and how the rate at which to play it.

Most `processor` declarations need a `main()` function, but in cases like this where only events (and not streams) are processed, then it's optional. In this case all the processor needs to do is to react to the incoming note-on events:

```
    event eventIn (std::notes::NoteOn e)
    {
        let sample = selectBestSampleForNote (int (e.pitch));

        speedRatio <- float32 (std::notes::getSpeedRatioBetween (float32 (sample.rootNote), e.pitch));
        content <- sample.source;
    }
```

When a note-on arrives, it picks which of its samples is nearest in pitch, and works out a pitch-change ratio that will get it to the target pitch, and then sends events to its outputs which will make a downstream `std::audio_data::SamplePlayer` start playing the data.

#### Getting audio sample data into the patch

The final part of our piano involves getting a set of piano audio samples into the patch as data that we can play.

```
    struct PianoSample
    {
        std::audio_data::Mono source;
        int rootNote;
    }

    external PianoSample[5] samples;
```

The `external` keyword is used to declare a constant variable whose data is supplied by the runtime. In this case, we're declaring an array of 5 objects called `samples`. We've put this inside a namespace called `piano` so its fully-qualified name is `piano::samples`.

If you look inside the `Piano.cmajorpatch` file, you'll see where the 5 values are provided for this array:

```json
    "externals":
    {
        "piano::samples" : [ { "source": "piano_36.ogg", "rootNote": 36 },
                             { "source": "piano_48.ogg", "rootNote": 48 },
                             { "source": "piano_60.ogg", "rootNote": 60 },
                             { "source": "piano_72.ogg", "rootNote": 72 },
                             { "source": "piano_84.ogg", "rootNote": 84 } ]
    }
```

When loading our patch, this tells the runtime to try to convert this JSON value into something it can use as the value of the `piano::samples` external variable. The runtime is smart enough that when it sees a string like "piano_36.ogg" in a place where it needs some audio data, it'll look for a resource file in the patch with that name and load it if possible.
