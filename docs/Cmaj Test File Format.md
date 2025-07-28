# Cmaj Unit Test Files

The test file format allows you to create test files which run multiple scripted tests of Cmajor code.

Javascript can be used to write custom functions that can perform kind of test, but a set of common helper functions are also provided.

A typical test might load a Cmaj program, check for compile errors, pass some inputs into it and check that the expected outputs are emitted.

## Running Tests

To run a test file, you can use the command-line tool:

```shell
$ cmaj test my_tests/TestFile1.cmajtest
```

You can also provide a folder instead of a filename, and it will scan for all the tests within that folder, and print out a set of total results for all of them.

The command-line switches include:

```
--singleThread      Use a single thread to run the tests
--threads=n         Run with the given number of threads, defaults to the available cores
--runDisabled       Run all tests including any marked disabled
--testToRun=n       Only run the specified test number in the test files
--xmlOutput=file    Generate a JUNIT compatible xml file containing the test results
--iterations=n      How many times to repeat the tests
```

Run `cmaj --help` for more command-line option information.

## File Format

A test file is split into blocks which are handled as separate tests.

Blocks are separated by a line starting with the characters `##`, followed by a javascript function call which performs a test function on the remaining content of the block that follows it.

e.g.

```
// All the text before the first delimiter line is parsed as javascript, and is available globally to all the tests. This is where you'd put any shared helper functions that your tests need to use.

## testFunction()

...The chunk of text in each section is provided to the test function that is called above...

## expectError ("...")

...Likewise, this chunk of text is passed to the "expectError" function...

```
Any content at the start of the file, up to the first `##` line, is parsed as Javascript. This is where you can define custom test functions which can be used multiple times within the file.

## Special Section Delimiters

### `## global`

The special delimiter `## global` is used to declare a chunk of code which will be prefixed onto all the other tests in the file. So if you're running many tests which all share a set of types or functions, you can avoid repeated code by putting the common definitions in a `global` section.

You can also specify a file or directory to the global command (e.g. `## global ("test.cmajor")`) which will pull in the given file or directory of source into the global space for this test file, allowing tests to be written for a library of cmajor utilities

### `## disabled [test...]`

To quickly disable a test, insert the token `disabled` in front of the test directive (anything following `disabled` on the line is ignored), and the test will be counted as disabled in the results.

## Built-in Test Functions

There's a built-in library of standard test functions, and you can have a closer look at their implementations in [cmaj_test_functions.js](https://github.com/cmajor-lang/cmajor/blob/main/tests/cmaj_test_functions.js).

The main functions include:

### `## testFunction`

This wraps the block of code in a dummy namespace, and finds all the functions which take no parameters and return a `bool`. Each of these is called, and if any of them return `false` it is counted as a failure.

e.g.

```
## testFunction()

bool test1()    { return 1 + 1 == 2; }  // this will pass
bool test2()    { return 1 + 2 == 3; }  // this will fail
int notATest() {}  // this function will be ignored since it doesn't return a bool
```

### `## testProcessor()`

This function will compile the block of code, and instantiate its main processor. The processor is expected to provide an output (either a `stream` or an `event` type) which emits `int` values.

The processor will then be run, and is expected to emit a stream of either 1 or 0 values, and then a -1 to indicate that the test is finished. If any 0 values are emitted, the test is registered as a fail, but if they're all ones, it's a pass.

e.g.

```
## testProcessor()

processor P
{
    output stream int out;

    void main()
    {
        out <- 1; advance();  // this is a pass
        out <- 0; advance();  // sending a zero will cause a fail to be logged

        out <- -1; advance(); // always send a -1 at the end to stop the test
    }
}
```

### `## testCompile()`

This simply checks that the code compiles without any compiler errors.

### `## testConsole()`

This instantiates a main processor from the code chunk that using the same system as `testProcessor()`. The output of the processor is ignored, but the content that was written to the console is checked against an expected value that is provided. e.g.

```
## testConsole ("hello world")

processor P
{
    output stream int out;
    void main()  { console <- "hello " <- "world"; out <- -1; advance(); }
}
```

### `## expectError ("<expected error message>")`

This wraps the chunk of code in a dummy namespace (to allow you to easily write free functions without any boilerplate) and attempts to compile it. If the compiler error matches the one specified in the test directive, it's a pass. If there's no error, or the error doesn't match, it's a fail.

```
## expectError ("2:9: error: Cannot find symbol 'XX'")

void f (XX& x) {}
```

## `## runScript()`

This method compiles and runs a block of code, and checks that the processor outputs match stored 'golden data' files. The result of the test is succesful if the outputs match the golden data files. Output events and values are stored in json files, whilst output streams are stored as .wav files.

Additional files contain input data for the processor, again, json files for events and values, and .wav files for input streams.

### runScript options

| Options | mandatory? | Description |
| - | - | - |
| frequency | Y | The sample rate for the session |
| samplesToRender | Y | How many frames to render |
| blockSize | Y | Specifies the maximum block size for the render |
| subDir | N | Specifies a relative subdirectory where the golden data is located |
| patch | N | Specifies a relative path to a patch to use for the test. This allows testing to be performed on a patch rather on a processor specified within the test file |
| mainProcessor | N | When specifying a patch, this allows the main processor to be overridden, allowing testing of components within the patch |
| maxDiffDb | N | Specifies the maximum difference in stream value which is acceptable. Defaults to -100db |

### input event file format

For each processor event input endpoint, a corresponding file named after the endpoint with a .json extension is expected to be found in the golden data directory. For example, if the processor is:

```C++
processor Test [[ main ]]
{
    input event float in;
    input value float valueIn;
    output event float out;
}
```

The golden data directory is expected to contain a file `in.json` which will contain the input events to be supplied to the input endpoint.

The json file will contain an array of objects, and each object will contain a `frameOffset` and `event` attribute. The `frameOffset` specifies which frame number the event should be sent, whilst the `event` attribute contains the value to be sent. For example, the above `in.json` could contain:

```
[
    {
        "frameOffset" : 10,
        "event" : 1.0
    },
    {
        "frameOffset" : 100,
        "event" : 5.0
    }
]
```

Indicating that at frame 10, the processor will receive an input float of `1.0` and at frame 100, it will receive `5.0`

The array of frameOffsets must be monotonically increasing.

### input value file format

Similar to input events, input value endpoints will appear in a similarly named `.json` file. The format is similar, containing an array of objects, which have a `frameOffset` field indicating when the value is to be submitted. For values, the object has a `value` attribute specifying the value to be sent, but in addition, a `framesToReachValue` attribute which specifies the number of frames the value is smoothed over.

In the above example, we could have a `valueIn.json` file containing:

```
[
    {
        "frameOffset": 100,
        "value": 0.4,
        "framesToReachValue": 100
    },
    {
        "frameOffset": 150,
        "value": 1.0,
        "framesToReachValue": 10
    }
]
```

This example specifies that at frame 100, the value starts ramping towards 0.4, with a ramp time of 100 frames. The value will smoothly increase linearly between frame 100 and 200 towards 0.4.

### input stream file format

For input streams, a `.wav` file is used, with the total length corresponding to the test frame count, and with the channel count corresponding to the vector size.

### output file formats

Output golden data files will be created automatically on first run - if the files do not exist, the outputs are assumed to be correct and are persisted in the output directory for future comparisons. The output files are prefixed with `expectedOutput-`

If we consider the test below:

```C++

## runScript ({ frequency:1000, blockSize:32, samplesToRender:1000, subDir:"gain"})

processor Gain [[ main ]]
{
    input stream float<2> in;
    input value float gain;
    output stream float<2> out;
}
```

We would expect there to be:

1) A stereo wav file `gain/in.wav` containing 1000 samples of data

2) A `gain/gain.json` file containing an array of value objects specifying the values for the gain endpoint.

3) A stereo wav file `gain/expectedOutput-out.wav` which will contain 1000 samples.


### stream output comparison

It is expected that on different architectures, and with different optimisation levels, that the output streams may not match exactly across runs. This is due to differences is code generation (such as fused multiply add) used by the runtime on different processor and optimisation combinations. Because of this, we do not expect output streams to exactly match the golden data.

To solve this, the comparison with output stream values and the golden data uses the following algorithm. The maximum frame difference is calculcated across the output frames, and the largest absolute expected frame value is calculated.

The db of the max difference relative to the largest absolute value is calculated, and this value is used to define how accurate the output is compared to the expected output. By default, the maximum accepted error is -100db, and this can be overridden by specifying the `maxDiffDb` value


## `## testPatch()`

This will attempt to load and compile a patch from a filename provided. Any compile errors will be registered as a failure.
e.g.

```
## testPatch ("../../../my_patches/example_patch.cmajor_patch")
```

## Auto-updating Expected Results

For tests like `expectError` or `testConsole`, the directive contains a string which is the expected outcome of the test. To easily update these, the tool will automatically insert the correct result and re-save the test file if you write the directive without any arguments.

For example, if you create a test file which looks like this:

```
## expectError

void f (XX& x) {}
```

and then run the test tool on it, it will rewrite the test file to look like this:

```
## expectError ("2:9: error: Cannot find symbol 'XX'")

void f (XX& x) {}
```

## Creating Custom Test Functions

You can write your own javascript test functions at the start of the test file, and invoke them on a `##` line.

If writing your own tests, it's probably helpful to look at how the standard ones are implemented, in [cmaj_test_functions.js](https://github.com/cmajor-lang/cmajor/blob/main/tests/cmaj_test_functions.js).

The underlying javascript API used for managing the test status looks like this:

```javascript
// A TestSection object provides functions that the current test can call. You get
// this object by calling the global function getCurrentTestSection()

class TestSection
{
    // logs a message to the console
    logMessage (message)

    // adds a failure to the test results, with a message
    reportFail (message)
    // adds a success to the test results, with a message
    reportSuccess (message)
    // adds a disabled test to the results, with a message
    reportDisabled (message)
    // notes that a test is unsupported on the current platform
    reportUnsupported (message)
    // logs an internal compiler error
    logCompilerError (error)

    // writes some stream data to a wav file
    writeStreamData (filename, streamData)
    // writes some event data to a JSON file
    writeEventData (filename, eventData)
    // reads some stream data from a wav file
    readStreamData (filename)
    // reads some event data from a JSON file
    readEventData (filename)

    // replaces the text of the current test invocation line with a new string
    updateTestHeader (newHeaderText)

    // converts a relative path from the test file to an absolute system path
    getAbsolutePath (relativePath)
}

function getCurrentTestSection()
function getDefaultEngineOptions()
function getEngineName()
```

The script also has access to the javascript bindings to create and render Cmajor processors.
