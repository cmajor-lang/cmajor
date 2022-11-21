# The Cmajor native API

This document covers the API you'll need to use if you want to embed the Cmajor JIT engine into a native C++ app.

The Cmajor repository contains a set of C++ header-only helper classes which allow you to build an app that can use the Cmajor JIT engine to compile and run code.

The two main folders to look at for this are:

- `include/cmajor/API` - contains the basic API objects
- `include/cmajor/helpers` - provides many optional utilities for tasks such as dealing with the patch format, plugin formats, background compilation, audio/MIDI handling, etc

(There is also a lower-level COM-based API in `include/cmajor/COM` which you can use if you like, but it's probably better to ignore them and stick to the API wrapper classes that expose the same things as idiomatic C++ classes).

There are some example projects demonstrating simple use of the C++ API: see the [examples/native_apps](https://github.com/SoundStacks/cmajor/tree/main/examples/native_apps) folder.

## Main API classes

### `cmaj::Engine`

The `Engine` class acts like a compiler - you create an Engine, give it a Cmajor program, and it compiles it into a `Performer` object that is used to actually run the program.

The general sequence of events is:
1. Create an Engine with `cmaj::Engine::create()`
2. If you want to apply any build settings (e.g. sample rate, block size, optimisation level), use `cmaj::Engine::setBuildSettings()` to do so
3. Create a `cmaj::Program` object containing your code, and pass it into the `Engine::load()` method.
4. If the `load()` completed without any compile errors, you can now find out about the program's endpoints and external variables using `Engine::getInputEndpoints()` and `Engine::getExternalVariables()`
5. If your program has any external variables, you'll need to provide values for them with the `Engine::setExternalVariable()` method.
6. Next step is linking the program using `Engine::link()`
7. If `link()` succeeded without any errors, you can now call `Engine::createPerformer()` method to create one or more performer instances, and these can be used to actually run the code.

### `cmaj::Performer`

A Performer is an object containing a compiled, ready-to-run instance of a Cmajor program. An Engine can create many performer instances for a program, and they can all be run independently.

The class provides methods to:
- write data and events to the program's input endpoints
- read data and events from the program's output endpoints
- synchronously render the next 'n' frames
- get status information like over/underrun counts, runtime errors, etc

Most of these methods are designed to be called synchronously on a real-time thread such as an audio thread, and are very low-level. If you're building a system where you have different threads handling things like audio, MIDI and other events, helper classes are provided that add thread-safe and realtime-safe abstractions around this very basic API.

### `cmaj::Program`

A `Program` is simply a collection of source files that have been parsed ready for compiling. To create one, simply get a `cmaj::Program` object and call its `parse()` method for each file or code chunk you want to build. When parsing, it'll emit error messages for any obvious syntactic errors, but most compiler errors will be detected later on when it's being built by the engine.

Once you've create and populated your `Program` object, you'll want to pass it to `cmaj::Engine::load()` to start the build process.

### `cmaj::DiagnosticMessageList`

When parsing, loading or linking code, lots of methods take a reference to a `DiagnosticMessageList` as a parameter, and will add any error and warning messages to the list.

The list can contain both errors, warnings and notes, and the class provides functions for printing, iterating and otherwise probing the messages.

### `cmaj::BuildSettings`

The `BuildSettings` class holds a selection of compile options that can be given to an Engine before asking it to build a program. This includes things like sample-rate, block size, optimisation level, buffer sizes, etc.

### `cmaj::EndpointDetails`, `cmaj::EndpointDetailsList`

Your app will need to enumerate and communicate with the performer's input/output endpoints, so these classes provide lots of functionality to help with that task.

As well the name and type information for an endpoint, the `EndpointDetails` class provides its annotations, and has heuristic methods for working out whether it should be treated as a parameter, or an audio or MIDI input, etc.

## Helper classes

### `cmaj::AudioMIDIPerformer`

This helper owns and manages a `cmaj::Performer`, providing a simple `process()` function that can be called with audio/MIDI buffers in the way that a plugin or other traditional C++ audio processor might look.

As well as taking care of the audio and MIDI i/o, it has lock-free FIFOs to allow other threads to safely inject events and value changes while it's running. It also allows the caller to attach a callback for handling output event data.

To use this class
1. Create yourself a suitable `Engine`, add your code to it and link it.
2. Then create a `AudioMIDIPerformer::Builder` object with your engine, and use the builder's methods to set the appropriate audio i/o channel mappings.
3. Call `Builder::createPerfomer()` to get an `AudioMIDIPerformer` object which you can then use for playback.

### `cmaj::PatchManifest`

This class can parse and interrogate a .cmajorpatch JSON file.

It also provides a set of functors for finding and reading file content, so that you can create custom patches that load their resources from virtual files rather than the normal filesystem.

### `cmaj::Patch`

A high-level helper object which can asynchronously load, build and process a patch.

This class can be given a `PatchManifest` to load, and will take care of running a background thread to do the compilation. It has all the heuristics necessary to decide which endpoints should be treated as audio, MIDI or parameters, and to interact with the underlying performer in a plugin-like style that's appropriate for most use-cases of a patch.

### `cmaj::JUCEPluginBase` and `cmaj::JUCEPluginFormat`

These JUCE-based helper classes are provided to allow you to create `juce::AudioPluginInstance` objects for Cmajor patches, and thus build (or host) them as VST/AU/AAX plugins.

If you create a `cmaj::JUCEPluginBase<cmaj::JUCEPluginType_DynamicJIT>`, it provides a plugin which can dynamically load different Cmajor patches. It does this by allowing patches to be dragged-and-dropped onto its GUI, and it will dynamically load and re-JIT them. This is flexible, but sadly because most hosts can't cope when plugins dynamically change their parameters, or i/o configuration, there are a few compromises involved in the implementation of this class.

If you want to build a plugin that loads a particular patch and doesn't change it, you can use `cmaj::JUCEPluginBase<cmaj::JUCEPluginType_SinglePatchJIT>` instead. This still uses the JIT engine and can also live-reload changes to the patch when running, but it takes a patch on construction, pre-loads it, and makes sure that the host has the correct i/o and parameter list ready at the start, which plays much more nicely with the way most hosts work. In this mode, it doesn't allow you to drag-and-drop onto the GUI to load different patches.

The `cmaj::JUCEPluginFormat` is a `juce::AudioPluginFormat` class which can scan for and load Cmajor patches as JUCE plugins. It uses the `cmaj::JUCEPluginBase<cmaj::JUCEPluginType_SinglePatchJIT>` wrapper, and if you're writing a JUCE-based host that you want to load Cmajor patches, this would be the class to give to your `juce::AudioPluginFormatManager`.

### `cmaj::GeneratedCppEngine`

This templated class lets you create a `cmaj::Engine` object around the code-generated C++ classes that are emitted by the `cmaj` tool's code-generator.

When you use the `cmaj` tool to code-generate some C++ from a Cmajor patch, the output is a bare-bones, dependency-free C++ class that contains static constants and rendering functions. By wrapping this in a `GeneratedCppEngine`, it can be used in the same way as the JIT engine, so you can easily wrap it into a `cmaj::Patch` or use a `cmaj::GeneratedPlugin` to create a JUCE plugin from it.

Note that rather than dealing with this class directly, you should call the `cmaj::createEngineForGeneratedCppProgram()` function, which will cleanly return a `cmaj::Engine` object.
