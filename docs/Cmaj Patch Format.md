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

## Patch GUIs

### Specifying a custom GUI for a patch

To add a custom GUI to your patch, your `.cmajorpatch` file should include a `view` property, e.g.

```json
  "view": {
    "src":       "patch_gui/index.js",
    "width":     800,
    "height":    700,
    "resizable": false
  }
```

The `view` property should contain a `src` property providing a relative URL to a javascript module. This module should have a default export, which must be a function returning a DOM Element that can be used as the GUI element for the patch. The exported function will be passed a `PatchConnection` object which it can use for communication with the patch instance that it controls.

### GUI Communication

Patch GUIs communicate with the runtime by passing serialised message structures back and forth over a communication channel. The specific implementation details are encapsulated inside a `PatchConnection` object, which is made available in the global scope by the runtime.

The `PatchConnection` API has methods for sending data to, and requesting data from, the runtime, and allows for setting up functions which the runtime will invoke when various aspects of the patch change (i.e in response to previous actions from the GUI, from external changes in a DAW, or because of recompilation).

Below is a brief overview of the available functionality:

_(note: for all methods below, `endpointID` is the endpoint name, as specified in the cmajor source code, as a string)_

#### GUI-to-Runtime

##### Actions
- `sendEventOrValue (endpointID, value, optionalNumFrames)`
  - Notify the runtime that a given input endpoint (either an `event` or `value`) should change to the given value
  - The value will typically be a primitive number, or boolean, but it can also be a javascript object
    - e.g. to update an endpoint of type `std::timeline::Tempo`, send a value like `{ bpm: 120.0 }` and the runtime will take care of converting it to a `std::timeline::Tempo`
  - In the case of a `value` endpoint, an optional frame count can be specified, which the runtime will use to smoothly interpolate between the current value and the target value
  - If the runtime accepts the change, it will asynchronously notify the client by invoking the `onParameterEndpointChanged` callback
  - Typically this will be invoked multiple times when changing a UI control like a slider, bookended by calls to the gesture actions described below
- `sendParameterGestureStart (endpointID)`
- `sendParameterGestureEnd (endpointID)`
   - These are used by host applications when recording automation, indicating that the user is holding a given parameter. The gesture calls must always be matched (a `GestureEnd` action must eventually follow a `GestureStart` action).

##### Requests
- `requestStatusUpdate()`
  - Request the current manifest and endpoint details
  - The runtime will asynchronously call back to `onPatchStatusChanged`
- `requestEndpointValue (endpointID)`
  - Request the current value for a given input endpoint
  - The runtime will asynchronously call back to `onParameterEndpointChanged`

#### Runtime-to-GUI
- `onPatchStatusChanged (errorMessage, patchManifest, inputsList, outputsList)`
  - This will be called in response to a `requestStatusUpdate()` request, or following each recompile
  - `errorMessage` will contain any output from an unsuccessful compile run, or otherwise be empty
  - `patchManifest` is the same information specified in the `cmajorpatch` file
  - `inputsList` and `outputsList` are arrays of javascript object representations of the `cmaj::EndpointDetails` structures described in the C++ API docs. See [`EndpointDetails::toJSON()`](https://github.com/SoundStacks/cmajor/blob/main/include/cmajor/API/cmaj_Endpoints.h#L290) for the specific implementation details
- `onSampleRateChanged (newSampleRate)`
  - This will be called following changes to the sample rate within the host, or following each recompile
- `onParameterEndpointChanged (endpointID, newValue)`
  - This will be called following a change to an input endpoint parameter (i.e. after processing a `sendEventOrValue` call, or from an external change via the host), or following a `requestEndpointValue` request
- `onOutputEvent (endpointID, newValue)`
  - The value here can be a scalar value (a primitive number or boolean), an array, or a javascript object. If the endpoint type is an aggregate / `struct`, the runtime will convert it to a javascript object, i.e there will be a key for each field name in the struct

#### Typical use cases

- Dedicated UI for a single patch
  - i.e has fixed custom UI controls laid out by hand, which represent specific endpoints
  - Here, the UI code will end up with some hardcoded endpoint IDs, and is mostly concerned with initiating, and reacting to, changes of input endpoints, and additionally reacting to output events. Information from endpoint annotations may be used for various bits of UI logic, such as limiting the input range on the client side
- Generic UI for use with multiple patches
  - i.e something like the default patch player UI, where controls are programatically laid out using type and annotation information
  - Here, much more is done in the `onPatchStatusChanged` callback, typically including tearing down the whole UI and starting again using the various bits of information about the patch (e.g. the title from the manifest, input endpoint annotations, etc) to decide what to render

Regardless of the specific use case, the app will typically setup callbacks on the given `PatchConnection` instance early in its bootstrapping for manipulating UI state when aspects of the patch change, and then request the initial state from the runtime. i.e:

```js
export default async function createPatchView (connection)
{
    // This is the entry point for a patch GUI.
    //
    // A host (the command line patch player, or other environments) will call
    // it to get a patch's view. Ultimately, a DOM element must be returned to
    // the caller for it to append to its document. However, as the function is
    // `async`, it is possible to perform various asyncronous tasks, such as
    // fetching remote resources for use in the view, before doing so.
    //
    // When using libraries such as React, this is where the call to
    // `ReactDOM.createRoot` would go rendering into a container component
    // before returning.
    const container = document.createElement ("div");

    connection.onPatchStatusChanged = (errorMessage, patchManifest, inputList, outputList) =>
    {
        // following a status update, it is typical to request the value
        // of all input parameters, and update the UI accordingly. e.g:
        const inputParameters = inputList.filter (e => e.purpose === "parameter");
        inputParameters.forEach (({ endpointID, annotation }) =>
        {
            // ...update any UI state dependent on annotation data etc

            // further request current value
            connection.requestEndpointValue (endpointID);
        });
    };

    connection.onSampleRateChanged = (newSampleRate) =>
    {
        // ...update any UI state that needs the sample rate information
    };

    connection.onParameterEndpointChanged = (endpointID, newValue) =>
    {
        // ...update UI state for endpoint, typically some kind of knob, slider, etc
    };

    connection.onOutputEvent = (endpointID, newValue) =>
    {
        // ...update UI state for endpoint, typically some kind of visualisation
    };

    // initial request for state
    connection.requestStatusUpdate();

    return container;
}
```

It is worth noting that the runtime should be considered the source of truth for patch state, as parameters can change from outside the GUI (i.e. via automation in a DAW). Therefore UI controls should only really update in reaction to patch connection callbacks.

_Note: The patch player and plugin provide access to the dev tools / inspector of the web view via the context menu presented when right-clicking on the window. This can be used for debugging, and inspecting the source of the default Patch player_

#### Known limitations

- Currently, `PatchConnection` is effectively a singleton / the app is limited to a single instance. This is due to the current mechanism used by the runtime to invoke callbacks, and it will only notify the most recently created instance about changes. However, multiple instances can be used to communicate from the GUI to the runtime. More sophisticated abstractions can be built client-side to work around these limitations, allowing for multiple listeners etc.
- The annotations are currently the raw annotations as written in the cmajor source file, and therefore do not contain the default properties picked by the C++ runtime
- The patch player and plugins will only process input values for parameter and timeline related input endpoints

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
