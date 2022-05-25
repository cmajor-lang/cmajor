# The Cmajor Patch Format

Cmajor patches are a format for bundling together code and other resources, so that they can be loaded into audio hosts such as DAWs, to provide instruments or effect plugin functionality.

------------------------------------------------------------------------------

A Cmajor "patch" is essentially a bundle of files, including:

- A main "manifest" file, with the suffix `.cmajorpatch`. This file describes the patch's properties and contains links to the other files in the patch
- One or more `.cmajor` source files containing the actual Cmajor code
- Some optional javascript control code for non-DSP housekeeping tasks
- Optionally some sub-folders containing resources such as HTML/CSS/Javascript GUI, audio files, etc.

## The `.cmajorpatch` manifest file

The `.cmajorpatch` file contains JSON to describe the properties of the patch.

For example, the `HelloWorld.cmajorpatch` contains:

```json
{
    "CmajorVersion":    1,
    "ID":               "dev.cmajor.examples.helloworld",
    "version":          "1.0",
    "name":             "Hello World",
    "description":      "The classic audio Hello World",
    "manufacturer":     "Sound Stacks Ltd",
    "category":         "generator",
    "isInstrument":     false,
    "source":           "HelloWorld.cmajor"
}
```

There are a few required properties that a patch must define:

- `CmajorVersion` - this is the version of Cmajor for which this patch was written
- `ID` - a universally unique ID for the patch, which should be in the form of a reverse-URL that includes the company name/website.
- `version` - a version number for your patch. This is just a string - there are no restrictions on its format.
- `name` - a human-readable name for your patch

Other optional (but recommended properties) include:

- `description` - a complete description that a host can display to its users
- `manufacturer` - the name of you or your company
- `category` - hosts will be give this string, but how they choose to interpret it will be host-dependent
- `isInstrument` - if specified, this marks the patch as being an instrument rather an effect. Some hosts may treat a plugin differently depending on this flag.

## Cmajor source files

The `source` property in the manifest tells the host which `.cmajor` files to compile for the Cmajor source code. This property can either be a single string containing a file path (relative to the folder containing the patch), or an array of string filenames if there are multiple files.

e.g.

```json
    "source":   [ "src/MainProcessor.cmajor",
                  "src/Utilities.cmajor" ],
```

All the source files will be loaded and linked together as a single unit, so they can freely refer to definitions in the other files without needing to explicitly import them.

## Selection of the patch's main processor

One of the processors defined in your source files will be used as the patch's top-level processor. To help the host decide which one to use, you should decorate it with the `[[ main ]]` attribute, e.g.

```cpp
processor HelloWorld  [[ main ]]
{
```

## Patch Parameters

Patches are designed to be used as audio plugins, and an audio plugin generally has a list of "parameters" that the host can read and write to, so that a host/DAW can record and play back automated changes to these parameters.

The list of parameters for a patch is determined by looking at the input endpoints of its main processor. If the endpoint meets the following criteria, it will be revealed to the host as a parameter:

- It must be a `value` or `event` endpoint. Streams cannot be used as parameters.
- Its type must have a single type which is a `float32`, `float64`, `int32`, `int64` or `bool`.
- It must have an annotation that declares a `name` (and will probably also declare some range properties).

```cpp
processor HelloWorld  [[ main ]]
{
    input stream float audioIn;  // Parameters cannot be streams, so this is ignored

    // This parameter is a float called "foo" and has the range 0.5 to 10
    input event float in1   [[ name: "foo", min: 0.5, max: 10.0 ]];

    // This parameter is an int called "foo2" between 1 and 99
    input value int32 in2   [[ name: "foo2", min: 1, max: 99 ]];
```

The following properties can be added to the endpoint annotation in order to give the host more information about the parameter:

- `name` (required) - the name to display to the user
- `min` - the minimum value for the parameter. If not specified, defaults to 0.0
- `max` - the maximum value for the parameter. If not specified, defaults to 1.0
- `init` - if specified, this value will be sent to the parameter when the patch is initialised
- `step` - the intervals to which the parameter value must "snap" when being changed
- `unit` - an optional string which the host will display as the units for this value. E.g. "%" or "dB"
- `group` - an optional string to hint at a parent group to which this parameter belongs. Some hosts may use this to organise parameters into on-screen groups.
- `text` - a formatting string which is used to print the value in a custom style.
    The string is preprocessed a bit like "printf", supporting the following format strings:
    - `%d` prints the parameter value as an integer
    - `%f` prints the parameter value as a floating point number
    - `%[digits]f` prints the parameter value with a maximum number of decimal places, e.g. `%2f` uses up to 2 decimal places
    - `%0[digits]f` prints the parameter value with exactly the specified number of decimal places
    - `%+d` or `%+f` prints the number with a `+` or `-` sign before it

    The `text` property can also contain a list of names separated by the pipe `|` character. In this mode, the names will be used as labels for the parameter values, and the host may choose to display them to the user in a drop-down menu or other list selector. For example `text: "low|med|high"` will map the strings "low", "med" and high to the value range 0, 1, 2. If a `max` range is specified then the values will be spread across the range, e.g. `max: 9` would map "low" to 0 -> 3, "med" to 3 -> 6, and "high" to 6 -> 9. If no `step` is provided, it will automatically be set based on the number of items.

## External variable data

Cmajor code can declare `external` variables whose values are supplied by the runtime environment when the code is loaded. In a patch, you should add entries in the manifest file to supply the data or the resource file that should be loaded into these variables.

e.g. In the "Piano" example, `Piano.cmajor` declares an external variable which is an array of 5 `PianoSample` objects:

```cpp
    namespace piano
    {
        struct PianoSample
        {
            std::audio_data::Mono source;
            int rootNote;
        }

        external PianoSample[5] samples;
```

The `Piano.cmajorpatch` has an "externals" property which gives the runtime a JSON value that should be loaded into the `piano::samples` variable:

```json
    "externals": {
        "piano::samples": [ { "source": "piano_36.ogg",  "rootNote": 36 },
                            { "source": "piano_48.ogg",  "rootNote": 48 },
                            { "source": "piano_60.ogg",  "rootNote": 60 },
                            { "source": "piano_72.ogg",  "rootNote": 72 },
                            { "source": "piano_84.ogg",  "rootNote": 84 } ]
    }
```

The runtime will attempt to coerce these JSON values to fit the data type of the target variable.

So in the example above, the `samples` variable has the type `PianoSample[5]`. The JSON value is an array containing 5 objects. So it then attempts to convert each of these objects into a `PianoSample` value.

Simple types like integers, floats, bools and strings are converted as you'd expect, and when objects are probided, the JSON objects should have members with the same names as the Cmajor struct members.

The runtime also supports loading audio data from files, as is done in this example. When attempting to convert a string to some other kind of target object, the runtime will check whether the string is actually the name of an audio resource file in the patch, and if so will load it. It'll then attempt to copy the audio frames and sample rate into the target struct type.

In the example above, the audio files are being loaded into variables whose type is `std::audio_data::Mono`. This is a struct from the Cmajor standard library, which looks like this:

```cpp
    struct Mono
    {
        float[] frames;
        float64 sampleRate;
    }
```

Because it has a member called "sampleRate" the runtime will put the audio file's sample rate into this value. And because it has an array of floats (and the source file is also mono), the audio data will be loaded into this array.

The standard library provides a set of helper structs like this that you can use to load data from audio files, but they're not special - the runtime simply uses duck-typing to decide whether a struct might be able to contain audio data, so you can use your own types too.

It can also load audio file data directly into a raw array, so this would also compile:

```
    processor MyProcessor
    {
        external float[] audioData;
```

and

```json
    "externals": {
        "MyProcessor::audioData" : "piano_36.ogg",
    }
```

..but since there's nowhere to put the file's sample rate, that information will be discarded.

## Patch GUIs

### Specifying a custom GUI for a patch

To add a custom GUI to your patch, your `.cmajorpatch` file should include a `view` property, e.g.

```json
  "view": {
    "html":      "patch_gui/index.html",
    "width":     800,
    "height":    700,
    "resizable": false
  }
```

The `view` property should contain a `html` property providing a relative URL to a HTML index page that can be displayed in an embedded browser view in the host.

### GUI-to-patch Communication

** TODO **

## Playing a Patch

A patch is just a folder containing some files that describe its DSP and GUI. To run one, you need a host that can load it. Some of the ways you can load and run a patch include:

### Using the command-line tool

Run the `cmaj` console app, giving it the location of the patch file to play:
```
% cmaj play my_patches/MyGreatPatch.cmajorpatch
```
This will launch it in a window, and use the default audio and MIDI i/o devices on your machine to play it. If you modify any of the patch's files while it's running, the app should detect this and recompile it for you.
While this is an easy way to run a patch, it's very limited in terms of what you can do with its inputs and outputs.

### Using the Cmajor loader plugin

In your favourite DAW, just load the Cmajor plugin and point it at the patch that you want to run. This way, you can run a patch anywhere that you can run a standard VST or AU plugin, so you can set up the audio and MIDI input and output however you like. It will also recompile the patch if you change any code while it's running, so is handy for development.

Something to watch out for when running the JIT plugin is that most DAWs don't expect VST/AU plugins to dynamically change their audio i/o or parameter lists while they're running, because this almost never happens in "normal" plugins. But while you're live-editing a Cmajor patch, the i/o and parameters may change whenever you change its endpoints, and this means that some DAWs may fail to respond correctly to these changes, or even crash.

### Using the online playground

(Coming soon!)

When we finish building it (!), the cmajor.dev online playground will let you build and run a patch in your browser. This will be by far the simplest way to get things going without any setup required, but performance in a browser (using WASM) will always be slower than running the code natively on your machine.

### Building a native VST or AudioUnit from a patch

The `cmaj` tool supports code-generation of a JUCE C++ project that can be used to natively compile a VST/AudioUnit/AAX plugin for a patch. The resulting code doesn't do any JIT compilation, it simply translates the Cmajor code to pure C++ so that it can be built statically.

To use this feature, run the command line app in `generate` mode, e.g.

```
% cmaj generate --target=plugin
                --output=[path to a target folder for the project]
                --jucePath=[path to your JUCE folder]
                MyAmazingPatch.cmajorpatch
```

It will create a folder containing some source files and a JUCE cmake project. This can be built as you would any other C++ cmake project, to produce VST/AU/AAX/standalone binaries.
