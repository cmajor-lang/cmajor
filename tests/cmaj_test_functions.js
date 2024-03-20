//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     (C)2024 Cmajor Software Ltd
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     https://cmajor.dev
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88
//                                           ,88
//                                        888P"
//
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.


/*
    This file defines the set of standard unit-test functions that are built
    into the test suite.

    Any functions in here can be invoked inside a .cmajtest file by putting them
    on a line that starts with a double-hash. So for example:

    ## expectError ("2:11: error: Array elements cannot be references")

    will invoke the expectError() function declared below.

    You can run a .cmajtest file with the command line utility:

      $ cmaj test mytest.cmajtest

    The tests in this file are built into the app, but you can also write your
    own functions, using these as a guide to how to do that.
*/

'use strict';

//==============================================================================
/// This class can be used to parse a patch manifest to get a list of its
/// source files, and to resolve its externals.
class PatchManifest
{
    constructor (file)
    {
        this.manifestFile = file;
        this.manifest = JSON.parse (this.manifestFile.read());

        if (! this.manifest?.CmajorVersion)
            this.error = { severity: "error", fullDescription: "Not a valid manifest" };
    }

    createProgram()
    {
        const program = new Program();

        for (const sourceFile of this.getSourceFiles())
        {
            const error = program.parse (sourceFile);

            if (isError (error))
            {
                program.release();
                return error;
            }
        }

        return program;
    }

    getSourceFiles()
    {
        let list = [];

        if (Array.isArray (this.manifest.source))
        {
            for (let i = 0; i < this.manifest.source.length; ++i)
                list.push (this.manifestFile.getSibling (this.manifest.source[i]));
        }
        else
        {
            list.push (this.manifestFile.getSibling (this.manifest.source));
        }

        return list;
    }

    getExternals()
    {
        let externals = {};

        const externalDefs = this.manifest.externals;

        if (externalDefs)
            for (const [key, value] of Object.entries (externalDefs))
                externals[key] = this.replaceStringsWithAudioData (value);

        return externals;
    }

    replaceStringsWithAudioData (o)
    {
        if (! o)
            return o;

        if (typeof o == "string")
        {
            const audioFile = this.manifestFile.getSibling (o);
            const audioData = audioFile.readAudioData();

            if (typeof audioData == "object" && ! isError (audioData))
                return audioData;
        }

        if (Array.isArray (o))
        {
            for (let i = 0; i < o.size; ++i)
                o[i] =  this.replaceStringsWithAudioData (o[i]);
        }

        if (typeof o == "object")
        {
            for (let i in o)
                if (Object.prototype.hasOwnProperty.call (o, i))
                    o[i] =  this.replaceStringsWithAudioData (o[i]);
        }

        return o;
    }
}

//==============================================================================
/*  This test attempts to compile and run a processor, checking its output.

    The test expects to find a main processor which has an output stream (or an
    output event) that emits int32 values.

    It will then run the processor, reading frames (or events) from its output,
    and uses them in the following way:

    - if it encounters a 1, it continues rendering
    - if it encounters a 0, it stops and marks the test as having failed
    - when it encounters a -1, it stops the test

    This lets you write a processor that performs a rendering task and checks its
    own output, sending a stream of 1s if all is well, or a 0 if not.

    (Obviously if the code fails to compile or a processor can't be found, then
    the test fails)

    e.g.
    ## testProcessor()
*/
function testProcessor (expectedResult, options)
{
    let sampleCount = 100;
    let testSection = getCurrentTestSection();
    let sourceToCompile = testSection.source + testSection.globalSource;
    let program = new Program();
    let error = program.parse (sourceToCompile);

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let engine = createEngine (options);
    updateBuildSettings (engine, 44100, sampleCount, true, options);
    error = engine.load (program);

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let outputs = engine.getOutputEndpoints();
    let resultHandle = engine.getEndpointHandle (outputs[0].endpointID);

    error = engine.link();

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let performer = engine.createPerformer();
    performer.setBlockSize (sampleCount);
    error = performer.advance();

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let results = [];

    if (outputs[0].endpointType == "event")
    {
        let events = performer.getOutputEvents (resultHandle);

        for (let i = 0; i < events.length; ++i)
            results.push (events[i].event);
    }
    else if (outputs[0].endpointType == "stream")
    {
        results = performer.getOutputFrames (resultHandle);
    }
    else
    {
        testSection.reportFail ("Unsupported output endpoint type " + outputs[0].endpointType);
        return;
    }

    let successes = 0;
    let fails = 0;

    for (let i = 0; i < results.length; ++i)
    {
        const value = results[i];

        if (value < 0)
            break;

        if (value > 0)
            ++successes;
        else
            ++fails;
    }

    const outputResult = (successes > 0 && fails == 0);

    if (expectedResult === undefined || expectedResult)
    {
        if (outputResult)
            testSection.reportSuccess();
        else
            testSection.reportFail ("Processor test failed");
    }
    else
    {
        if (! outputResult)
            testSection.reportSuccess();
        else
            testSection.reportFail ("Processor test succeeded but we were expecting it to fail");
    }
}

//==============================================================================
/*  This test passes if the code compiles with the same error provided as an
    argument.

    The chunk of code that is provided is implicitly wrapped in a namespace before
    being compiled, so you can write compact tests that just contain a bare list
    of functions (it's normally a syntax error to declare a function at global scope).
    If you want to

    ## expectError ("2:11: error: Array elements cannot be references")

    If you perform this test with no arguments, i.e:

    ## expectError

    ..then it will re-write this line in your test file, adding the actual error
    encountered, which makes it easy to get the correct error string into the test
    file without needing to copy-paste it from the compiler output manually.
*/
function expectError (expectedError, options)
{
    let testSection = getCurrentTestSection();

    let sourceToCompile;

    if (options != null && options.doNotWrapInTestNamespace)
        sourceToCompile = testSection.source + testSection.globalSource;
    else
        sourceToCompile = "namespace tests { " + testSection.source + " }\n"
                            + "processor TestProcessor { output stream float32 out; void main() { advance(); } }\n"
                            + testSection.globalSource;

    let program = new Program();
    let error = program.parse (sourceToCompile);
    let newErrorLine = getErrorReportString (error);

    if (! isError (error))
    {
        let engine = createEngine (options);
        updateBuildSettings (engine, 44100, 1024, false, options);

        error = engine.load (program);
        newErrorLine += getErrorReportString (error);

        if (! isError (error))
            newErrorLine += getErrorReportString (engine.link());
    }

    if (newErrorLine.length == 0)
    {
        testSection.reportFail ("Failed to fail");
        return;
    }

    if (expectedError == null || expectedError.length == 0)
    {
        testSection.reportSuccess();
        testSection.logMessage ("Updating error text to '" + newErrorLine + "'");

        if (options == null)
            testSection.updateTestHeader ("## expectError (\"" + newErrorLine.replace (/\"/g, "\\\"") + "\")");
        else
            testSection.updateTestHeader ("## expectError (\"" + newErrorLine.replace (/\"/g, "\\\"") + "\", " + JSON.stringify (options) + ")");
    }
    else if (newErrorLine == expectedError)
    {
        testSection.reportSuccess();
    }
    else
    {
        testSection.logCompilerError (error);
        testSection.reportFail ("error mismatch");

        testSection.logMessage ("Expecting " + expectedError);
        testSection.logMessage ("Got       " + newErrorLine);
    }
}

//==============================================================================
/*
    This test takes the code provided, and finds all the functions declared which
    return a bool and take no arguments. It then attempts to call each one, expecting
    them all to return true. If any return false, the test fails and it reports
    which of the functions failed.

    The chunk of code that is provided is implicitly wrapped in a namespace before
    being compiled, so you can write compact tests that just contain a bare list
    of functions (it's a syntax error to declare a function at global scope).

    ## testFunction()
*/
function testFunction (options)
{
    let testSection = getCurrentTestSection();
    let sourceToCompile = "namespace tests { " + testSection.source + " }" + testSection.globalSource;
    let program = new Program();
    let error = program.parse (sourceToCompile);

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let functions = findTestFunctions (program);

    if (functions.length == 0)
    {
        testSection.reportFail ("No test functions found");
        return;
    }

    let callAllTestFunctions = "";

    for (let i = 0; i < functions.length; i++)
        callAllTestFunctions += "        result <- (tests::" + functions[i] + "() ? 1 : 0); advance();\n";

    let processorSource = "processor FunctionTester [[main]]\n"
                        + "{\n"
                        + "    output value int result;\n"
                        + "    void main() {\n"
                        + callAllTestFunctions
                        + "        result <- -1;\n"
                        + "        advance();\n"
                        + "    }\n"
                        + "}\n";

    error = program.parse (processorSource);

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let engine = createEngine (options);
    updateBuildSettings (engine, 44100, 1, ! options?.failOnWarnings, options);
    error = engine.load (program);

    if (isError (error) || (options?.failOnWarnings && isErrorOrWarning (error)))
    {
        testSection.reportFail (error);
        return;
    }

    let outputs = engine.getOutputEndpoints();
    let resultHandle = engine.getEndpointHandle (outputs[0].endpointID);

    error = engine.link();

    if (isError (error) || (options?.failOnWarnings && isErrorOrWarning (error)))
    {
        testSection.reportFail (error);
        return;
    }

    let failingTests = [];
    let performer = engine.createPerformer();

    for (let i = 0; i < functions.length + 1; i++)
    {
        performer.setBlockSize (1);

        let error = performer.advance();

        if (isError (error))
        {
            testSection.reportFail (error);
            return;
        }

        let result = performer.getOutputValue (resultHandle);

        if (Number.isInteger (result))
        {
            if (result == 0)
                failingTests.push (functions[i]);
        }
        else
        {
            testSection.reportFail (result);
        }
    }

    if (failingTests.length != 0)
    {
        testSection.reportFail (failingTests.length + " functions failed: " + failingTests.join (", "));
        return;
    }

    testSection.reportSuccess();
}

//==============================================================================
/**
    This test just attempts to compile the code, failing if there are any errors.

    If it is given the optional argument `false` then it doesn't attempt to link
    the code, just to parse and load it into an engine.

    ## testCompile()
*/
function testCompile (testLink, options)
{
    if (testLink == null)
        testLink = true;

    let testSection = getCurrentTestSection();
    let timingInfo = {};
    let engine = buildEngineWithLoadedProgram (testSection, options, timingInfo);
    let error;

    if (isError (engine, options))
    {
        testSection.reportFail (engine);
        return;
    }

    let inputs = engine.getInputEndpoints();
    let outputs = engine.getOutputEndpoints();

    for (let i = 0; i < inputs.length; i++)
        engine.getEndpointHandle (inputs[i].endpointID);

    for (let i = 0; i < outputs.length; ++i)
        engine.getEndpointHandle (outputs[i].endpointID);

    if (testLink)
    {
        error = engine.link();

        if (isError (error, options))
        {
            testSection.reportFail (error);
            return;
        }
    }

    engine.unload();
    error = buildEngineWithLoadedProgram (testSection, options, timingInfo, engine);

    if (isError (error, options))
    {
        testSection.reportFail (error);
        return;
    }

    if (testLink)
    {
        error = engine.link();

        if (isError (error, options))
        {
            testSection.reportFail (error);
            return;
        }
    }

    testSection.reportSuccess();
}

//==============================================================================
/* This test checks whether the console output matches what was expected

    e.g.
    ## testConsole ("hello_world")

*/
function testConsole (expectedConsoleMsg, options)
{
    let testSection = getCurrentTestSection();
    let sourceToCompile = testSection.source + testSection.globalSource;
    let program = new Program();
    let error = program.parse (sourceToCompile);

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let engine = createEngine (options);
    updateBuildSettings (engine, 44100, 1024, true, options);
    error = engine.load (program);

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let outputs = engine.getOutputEndpoints();
    let consoleIndex = -1;

    if (outputs)
        for (let i = 0; i < outputs.length; ++i)
            if (outputs[i].endpointID == "console")
                consoleIndex = i;

    if (consoleIndex < 0)
        testSection.reportFail ("no console output stream found");

    let consoleHandle = engine.getEndpointHandle ("console");

    if (isError (consoleHandle))
    {
        testSection.reportFail (consoleHandle);
        return;
    }

    error = engine.link();

    if (isError (error))
    {
        testSection.reportFail (error);
        return;
    }

    let performer = engine.createPerformer();

    let framesToRender = 1024;
    let framesPerBlock = 64;
    let consoleMsg = "";

    while (framesToRender > 0)
    {
        performer.setBlockSize (framesPerBlock);
        performer.advance();

        let result = performer.getOutputEvents (consoleHandle);

        if (result)
        {
            for (let i = 0; i < result.length; i++)
            {
                if (typeof result[i].event == "object")
                    consoleMsg += JSON.stringify (result[i].event);
                else
                    consoleMsg += result[i].event;
            }
        }

        framesToRender -= framesPerBlock;
    }

    if (! expectedConsoleMsg || expectedConsoleMsg.length == 0)
    {
        testSection.reportSuccess();
        testSection.logMessage ("Updating console text to '" + consoleMsg + "'");
        testSection.updateTestHeader ("## testConsole (" + JSON.stringify (consoleMsg) + ")");
    }
    else if (consoleMsg == expectedConsoleMsg)
    {
        testSection.reportSuccess();
    }
    else
    {
        testSection.reportFail ("console mismatch");
        testSection.logMessage ("Expecting " + expectedConsoleMsg);
        testSection.logMessage ("Got       " + consoleMsg);
    }
}


//==============================================================================
/*
    This test builds a processor and renders a given amount of data through it,
    measuring and reporting its performance.

    e.g.
    ## performanceTest ({ frequency:44100, minBlockSize:4, maxBlockSize: 1024, samplesToRender:100000 })
    ## performanceTest ({ frequency:44100, minBlockSize:4, maxBlockSize: 1024, samplesToRender:100000, patch: "testPatch.cmajorpatch" })
*/

function performanceTest (options)
{
    let testSection = getCurrentTestSection();

    if (getEngineName() == "webview" || getEngineName() == "webview-binaryen")
    {
        testSection.reportUnsupported ("engine type " + getEngineName() + " not supported");
        return;
    }

    let timingInfo = {};
    let engine = buildEngineWithLoadedProgram (testSection, options, timingInfo);

    if (isError (engine, options))
    {
        testSection.reportFail (engine);
        return;
    }

    if (engine.getBuildSettings().optimisationLevel == 0)
    {
        testSection.reportUnsupported ("Test disabled for --O0 as it will take too long");
        return;
    }

    let inputEndpoints = engine.getInputEndpoints();

    for (let i = 0; i < inputEndpoints.length; i++)
        inputEndpoints[i].handle = engine.getEndpointHandle (inputEndpoints[i].endpointID);

    let outputEndpoints = engine.getOutputEndpoints();

    for (let i = 0; i < outputEndpoints.length; i++)
        outputEndpoints[i].handle = engine.getEndpointHandle (outputEndpoints[i].endpointID);

    timingInfo.linkTime = engine.link ();

    if (isError (timingInfo.linkTime, options))
    {
        if (timingInfo.linkTime.message == "Language feature not yet implemented: cpp performer on windows!")
            testSection.reportUnsupported (timingInfo.linkTime);
        else
            testSection.reportFail (timingInfo.linkTime);

        return;
    }

    let totalTime = timingInfo.loadTime  + timingInfo.linkTime;

    if (timingInfo.parseTime != undefined)
    {
        totalTime += timingInfo.parseTime;
        testSection.logMessage ("Parse time: " + Math.round (timingInfo.parseTime * 1000) + " ms");
    }

    testSection.logMessage ("Load time : " + Math.round (timingInfo.loadTime * 1000) + " ms");
    testSection.logMessage ("Link time : " + Math.round (timingInfo.linkTime * 1000) + " ms");
    testSection.logMessage ("Total     : " + Math.round (totalTime * 1000) + " ms");

    let performer = engine.createPerformer();
    let blockSize = options.minBlockSize;
    let inputFrames = [];

    for (let i = 0; i < options.maxBlockSize; i++)
        inputFrames[i] = i / options.maxBlockSize;

    while (blockSize <= options.maxBlockSize)
    {
        performer.setBlockSize (blockSize);

        for (let i = 0; i < inputEndpoints.length; i++)
        {
            if (inputEndpoints[i].endpointType == "stream")
                performer.setInputFrames (inputEndpoints[i].handle,
                                          inputFrames.slice (0, blockSize));
        }

        let runtime = performer.calculateRenderPerformance (blockSize, options.samplesToRender);
        let framesPerSec = options.samplesToRender / runtime;
        let utilisation = 100.0 * options.frequency / framesPerSec;

        testSection.logMessage ("Block size " + blockSize + ", runtime " + runtime + ", frames/sec = "
                                 + framesPerSec.toFixed(0) + " utilisation = " + utilisation.toFixed (2));
        blockSize *= 2;
    }

    testSection.reportSuccess();
}

//==============================================================================
/*
    This test takes the filename of a .cmajorpatch and tries to build it, failing
    if there are any errors. It doesn't use any code from the block in the test
    file.

    e.g.
    ## testPatch ("../../../cmajor/examples/patches/Freeverb/Freeverb.cmajorpatch")
*/
function testPatch (file, expectedError)
{
    const testSection = getCurrentTestSection();
    const absolutePath = testSection.getAbsolutePath (file);
    const error = loadAndTestPatch (absolutePath, 44100, 128);

    let newErrorLine = getErrorReportString (error);

    if (expectedError == null)
    {
        if (newErrorLine.length == 0)
        {
            testSection.reportSuccess();
            return;
        }

        testSection.logMessage (error);
        testSection.reportFail ("unexpected error");
    }
    else
    {
        newErrorLine = newErrorLine.replace (absolutePath, file);

        if (expectedError == newErrorLine)
        {
            testSection.reportSuccess();
            return;
        }

        if (expectedError.length == 0)
        {
            testSection.reportSuccess();
            testSection.logMessage ("Updating error text to '" + newErrorLine + "'");
            testSection.updateTestHeader ("## testPatch (\"" + file + "\", \"" + newErrorLine.replace (/\"/g, "\\\"") + "\")");
        }
        else
        {
            testSection.logMessage (error);
            testSection.reportFail ("error mismatch");

            testSection.logMessage ("Expecting " + expectedError);
            testSection.logMessage ("Got       " + newErrorLine);
        }
    }
}

//==============================================================================
/*
    This test loads helper files containing input and output data that should
    be fed into a processor. It can also run tests on a patch by specifying a patch
    to build

    e.g.
    ## runScript ({ sampleRate:44100, blockSize:32, samplesToRender:1000, subDir:"foo" })
    ## runScript ({ sampleRate:44100, blockSize:32, samplesToRender:1000, subDir:"foo", patch: "path/to/patch.cmajorpatch" })

*/

function runScript (options)
{
    let testSection = getCurrentTestSection();

    if (options.subDir == null)
        options.subDir = ".";

    if (options.maxDiffDb == null)
        options.maxDiffDb = -100;

    let timingInfo = {};

    let engine = buildEngineWithLoadedProgram (testSection, options, timingInfo);

    if (isError (engine, options))
    {
        testSection.reportFail (engine);
        return;
    }

    let inputEndpoints = engine.getInputEndpoints();
    let outputEndpoints = engine.getOutputEndpoints();

    for (let i = 0; i < inputEndpoints.length; i++)
    {
        inputEndpoints[i].handle = engine.getEndpointHandle (inputEndpoints[i].endpointID);

        if (inputEndpoints[i].endpointType == "stream")
        {
            let expectedStreamFilename = options.subDir + "/" + inputEndpoints[i].endpointID + ".wav";
            let inputData = testSection.readStreamData (expectedStreamFilename);

            if (isError (inputData))
            {
                if (options.skipMissing == null)
                {
                    testSection.reportFail ("Failed to read input stream " + expectedStreamFilename);
                    return;
                }

                inputData.data = Array (options.samplesToRender);
            }

            inputEndpoints[i].frames = inputData;
        }
        else if (inputEndpoints[i].endpointType == "value")
        {
            let expectedStreamFilename = options.subDir + "/" + inputEndpoints[i].endpointID + ".json";
            let inputData = testSection.readEventData (expectedStreamFilename);

            if (isError (inputData))
            {
                if (options.skipMissing == null)
                {
                    testSection.reportFail ("Failed to read input value data " + expectedStreamFilename);
                    return;
                }
            }

            if (! validateInputData (expectedStreamFilename, inputData, testSection, "value"))
                return;

            inputEndpoints[i].values = inputData;
            inputEndpoints[i].nextValue = 0;
        }
        else if (inputEndpoints[i].endpointType == "event")
        {
            let expectedStreamFilename = options.subDir + "/" + inputEndpoints[i].endpointID + ".json";
            let inputData = testSection.readEventData (expectedStreamFilename);

            if (isError (inputData))
            {
                if (inputEndpoints[i].purpose == "midi in")
                {
                    expectedStreamFilename = options.subDir + "/" + inputEndpoints[i].endpointID + ".mid";
                    inputData = testSection.readMidiData (expectedStreamFilename);
                }

                if (isError (inputData))
                {
                    if (options.skipMissing == null)
                    {
                        testSection.reportFail ("Failed to read input event data " + expectedStreamFilename);
                        return;
                    }
                }
            }

            if (! validateInputData (expectedStreamFilename, inputData, testSection, "event"))
                return;

            inputEndpoints[i].events = inputData;
            inputEndpoints[i].nextEvent = 0;
        }
    }

    for (let i = 0; i < outputEndpoints.length; i++)
    {
        outputEndpoints[i].handle = engine.getEndpointHandle (outputEndpoints[i].endpointID);

        if (outputEndpoints[i].endpointType == "stream")
            outputEndpoints[i].frames = { "sampleRate": options.frequency, "frameCount": 0, "data": []};
        else if (outputEndpoints[i].endpointType == "value")
            outputEndpoints[i].values = [];
        else if (outputEndpoints[i].endpointType == "event")
            outputEndpoints[i].events = [];
    }

    timingInfo.linkTime = engine.link();

    if (isError (timingInfo.linkTime, options))
    {
        if (timingInfo.linkTime.message == "Language feature not yet implemented: cpp performer on windows!")
            testSection.reportUnsupported (timingInfo.linkTime);
        else
            testSection.reportFail (timingInfo.linkTime);

        return;
    }

    let performer = engine.createPerformer();

    let outstandingSamples = options.samplesToRender;
    let framesRendered = 0;

    let eventsToApply = [];
    let valuesToApply = [];

    for (let i = 0; i < inputEndpoints.length; i++)
    {
        const input = inputEndpoints[i];

        if (input.purpose == "parameter" && input.annotation.init !== undefined)
        {
            if (input.endpointType == "event")
                eventsToApply.push ({ handle: input.handle,
                                      event: input.annotation.init });
            else
                valuesToApply.push ({ handle: input.handle,
                                      value: input.annotation.init,
                                      frameCount: 0});
        }
    }

    while (outstandingSamples > 0)
    {
        let samplesThisBlock = (options.blockSize < outstandingSamples) ? options.blockSize : outstandingSamples;

        for (let i = 0; i < inputEndpoints.length; i++)
        {
            if (inputEndpoints[i].endpointType == "event")
            {
                let arrayLength = inputEndpoints[i].events.length;

                while (inputEndpoints[i].nextEvent < arrayLength && inputEndpoints[i].events[inputEndpoints[i].nextEvent].frameOffset == framesRendered)
                {
                    eventsToApply.push ({ handle: inputEndpoints[i].handle,
                                          event: inputEndpoints[i].events[inputEndpoints[i].nextEvent].event });

                    inputEndpoints[i].nextEvent++;
                }

                if (inputEndpoints[i].nextEvent < arrayLength)
                {
                    let framesTillNextEvent = inputEndpoints[i].events[inputEndpoints[i].nextEvent].frameOffset - framesRendered;

                    if (framesTillNextEvent < samplesThisBlock)
                        samplesThisBlock = framesTillNextEvent;
                }
            }
            else if (inputEndpoints[i].endpointType == "value")
            {
                let arrayLength = inputEndpoints[i].values.length;

                while (inputEndpoints[i].nextValue < arrayLength && inputEndpoints[i].values[inputEndpoints[i].nextValue].frameOffset == framesRendered)
                {
                    valuesToApply.push ({ handle: inputEndpoints[i].handle,
                                          value: inputEndpoints[i].values[inputEndpoints[i].nextValue].value,
                                          frameCount: inputEndpoints[i].values[inputEndpoints[i].nextValue].framesToReachValue});

                    inputEndpoints[i].nextValue++;
                }

                if (inputEndpoints[i].nextValue < arrayLength)
                {
                    let framesTillNextValue = inputEndpoints[i].values[inputEndpoints[i].nextValue].frameOffset - framesRendered;

                    if (framesTillNextValue < samplesThisBlock)
                        samplesThisBlock = framesTillNextValue;
                }
            }
        }

        performer.setBlockSize (samplesThisBlock);

        for (let i = 0; i < eventsToApply.length; i++)
            performer.addInputEvent (eventsToApply[i].handle, eventsToApply[i].event);

        for (let i = 0; i < valuesToApply.length; i++)
            performer.setInputValue (valuesToApply[i].handle, valuesToApply[i].value, valuesToApply[i].frameCount);

        eventsToApply = [];
        valuesToApply = [];

        for (let i = 0; i < inputEndpoints.length; i++)
        {
            if (inputEndpoints[i].endpointType == "stream")
            {
                performer.setInputFrames (inputEndpoints[i].handle,
                                          inputEndpoints[i].frames.data.slice (framesRendered,
                                                                               framesRendered + samplesThisBlock));
            }
        }

        performer.advance();

        for (let i = 0; i < outputEndpoints.length; i++)
        {
            if (outputEndpoints[i].endpointType == "stream")
            {
                let outDataThisBlock = performer.getOutputFrames (outputEndpoints[i].handle);
                Array.prototype.push.apply (outputEndpoints[i].frames.data, outDataThisBlock);
                outputEndpoints[i].frames.frameCount += samplesThisBlock;
            }
            else if (outputEndpoints[i].endpointType == "value")
            {
                let outValue = performer.getOutputValue (outputEndpoints[i].handle);
                let value = { frameOffset: framesRendered,
                              value: outValue};
                outputEndpoints[i].values.push (value);
            }
            else if (outputEndpoints[i].endpointType == "event")
            {
                let outEvents = performer.getOutputEvents (outputEndpoints[i].handle);

                for (let n = 0; n < outEvents.length; n++)
                {
                    outEvents[n].frameOffset += framesRendered;
                    outputEndpoints[i].events.push (outEvents[n]);
                }
            }
        }

        outstandingSamples -= samplesThisBlock;
        framesRendered += samplesThisBlock;
    }

    let overruns = performer.getXRuns();
    let expectedOverruns = 0;

    if (options.expectedOverruns)
        expectedOverruns = options.expectedOverruns;

    if (overruns != expectedOverruns)
    {
        testSection.reportFail ("Expected " + expectedOverruns + " overruns, got " + overruns);
        return;
    }

    for (let i = 0; i < outputEndpoints.length; i++)
    {
        if (outputEndpoints[i].endpointType == "stream")
        {
            let expectedStreamFilename = options.subDir + "/expectedOutput-" + outputEndpoints[i].endpointID + ".wav";

            // testSection.logMessage ("Got output data:" + JSON.stringify (outputEndpoints[i].frames));

            let expectedData = testSection.readStreamData (expectedStreamFilename);

            if (isError (expectedData))
            {
                testSection.logMessage ("Can't find output file " + expectedStreamFilename + " - write it");
                testSection.writeStreamData (expectedStreamFilename, outputEndpoints[i].frames);
            }
            else
            {
                // testSection.logMessage ("Got expectedData data:" + JSON.stringify (expectedData));

                let result = streamDataComparison (outputEndpoints[i].frames, expectedData, options.maxDiffDb);

                if (result != null)
                {
                    testSection.reportFail (outputEndpoints[i].endpointID + ": Comparison failed: " + result);
                    return;
                }
            }
        }
        else if (outputEndpoints[i].endpointType == "value")
        {
            let expectedEventFilename = options.subDir + "/expectedOutput-" + outputEndpoints[i].endpointID + ".json";
            let expectedData = testSection.readEventData (expectedEventFilename);

            if (isError (expectedData))
            {
                testSection.logMessage ("Can't find output file " + expectedEventFilename + " - write it");
                testSection.writeEventData (expectedEventFilename, (outputEndpoints[i].values));
            }
            else
            {
                let result = eventDataComparison (outputEndpoints[i].values, expectedData);

                if (result != null)
                {
                    testSection.reportFail ("Comparison failed: " + result);
                    return;
                }
            }
        }
        else if (outputEndpoints[i].endpointType == "event")
        {
            let expectedEventFilename = options.subDir + "/expectedOutput-" + outputEndpoints[i].endpointID + ".json";
            let expectedData = testSection.readEventData (expectedEventFilename);

            if (isError (expectedData))
            {
                testSection.logMessage ("Can't find output file " + expectedEventFilename + " - write it");
                testSection.writeEventData (expectedEventFilename, (outputEndpoints[i].events));
            }
            else
            {
                let result = eventDataComparison (outputEndpoints[i].events, expectedData);

                if (result != null)
                {
                    testSection.reportFail (outputEndpoints[i].endpointID + ": Comparison failed: " + result);
                    return;
                }
            }
        }

    }

    if (options.displayTimings)
    {
        let totalTime = timingInfo.loadTime  + timingInfo.linkTime;

        if (timingInfo.parseTime != undefined)
        {
            totalTime += timingInfo.parseTime;
            testSection.logMessage ("Parse time: " + Math.round (timingInfo.parseTime * 1000) + " ms");
        }

        testSection.logMessage ("Load time : " + Math.round (timingInfo.loadTime * 1000) + " ms");
        testSection.logMessage ("Link time : " + Math.round (timingInfo.linkTime * 1000) + " ms");
        testSection.logMessage ("Total     : " + Math.round (totalTime * 1000) + " ms");
    }

    testSection.reportSuccess();
}


//==============================================================================
//==============================================================================
//
// The remainder of this file just consists of helper functions used by the code above
//

function buildEngineWithLoadedProgram (testSection, options, timingInfo, engine)
{
    if (options == undefined)
        options = {};

    if (engine == undefined)
        engine = createEngine (options);

    updateBuildSettings (engine, options.frequency, options.blockSize, false, options);

    let program;

    if (options.patch != null)
    {
        let patch = new PatchManifest (new File (testSection.getAbsolutePath (options.patch)));

        if (isError (patch.error))
            return patch.error;

        program = patch.createProgram();

        if (isError (program))
            return program;

        timingInfo.loadTime = engine.load (program, patch.getExternals());

        if (isError (timingInfo.loadTime))
            return timingInfo.loadTime;
    }
    else
    {
        program = new Program();
        let parseResult = program.parse (testSection.source + testSection.globalSource);

        if (isError (parseResult, options))
            return parseResult;

        timingInfo.parseTime = parseResult;

        let externalFilename = options.subDir + "/externals.json";
        let externalData = testSection.readEventData (externalFilename);

        timingInfo.loadTime = engine.load (program, externalData);

        if (isError (timingInfo.loadTime, options))
            return timingInfo.loadTime;
    }

    return engine;
}


function createEngine (options)
{
    let engineOptions = options?.engine;

    if (engineOptions == null)
        engineOptions = getDefaultEngineOptions();

    return new Engine (engineOptions);
}

function updateBuildSettings (engine, defaultFrequency, defaultBlockSize, ignoreWarnings, options)
{
    let buildSettings = engine.getBuildSettings();

    buildSettings.frequency      = defaultFrequency;
    buildSettings.maxBlockSize   = defaultBlockSize;
    buildSettings.ignoreWarnings = ignoreWarnings;

    if (options)
    {
        if (options.maxFrequency !== undefined)       buildSettings.maxFrequency = options.maxFrequency;
        if (options.frequency !== undefined)          buildSettings.frequency = options.frequency;
        if (options.blockSize !== undefined)          buildSettings.blockSize = options.blockSize;
        if (options.eventBufferSize !== undefined)    buildSettings.eventBufferSize = options.eventBufferSize;
        if (options.maxStateSize !== undefined)       buildSettings.maxStateSize = options.maxStateSize;
        if (options.maxStackSize !== undefined)       buildSettings.maxStackSize = options.maxStackSize;
        if (options.sessionID !== undefined)          buildSettings.sessionID = options.sessionID;
        if (options.optimisationLevel !== undefined)  buildSettings.optimisationLevel = options.optimisationLevel;
        if (options.mainProcessor !== undefined)      buildSettings.mainProcessor = options.mainProcessor;
    }

    engine.setBuildSettings (buildSettings);
}

function getErrorReportString (error)
{
    if (! isErrorOrWarning (error))
        return "";

    if (Array.isArray (error))
    {
        let locationLines = [];

        for (let i = 0; i < error.length; ++i)
            locationLines.push (error[i].fullDescription);

        return locationLines.join (" //// ");
    }

    if (error.fullDescription != null)
        return error.fullDescription;
}

// helper function to get a list of suitable boolean test functions
// from a Program object
function findTestFunctions (program)
{
    let result = [];
    let syntaxTree = program.getSyntaxTree ("tests");

    if (syntaxTree && syntaxTree.name == "tests")
    {
        for (let i = 0; i < syntaxTree.functions.length; ++i)
        {
            const func = syntaxTree.functions[i];

            if (func.returnType.OBJECT == "PrimitiveType"
                 && func.returnType.type == "boolean"
                 && func.parameters.length == 0)
            {
                result.push (func.name);
            }
        }
    }

    return result;
}

function streamDataCompareValue (comparisonStats, expectedValue, actualValue, frame, channel)
{
    if (Math.abs (expectedValue) > comparisonStats.maxValue)
        comparisonStats.maxValue = Math.abs (expectedValue);

    let diff = Math.abs (expectedValue - actualValue);

    if (diff > comparisonStats.maxDiff)
    {
        comparisonStats.maxDiff = diff;
        comparisonStats.diffFrame = frame;
        comparisonStats.diffChannel = channel;
    }
}

function streamDataComparison (data, expectedData, maxDiffDb)
{
    if (data.sampleRate != expectedData.sampleRate)
        return "Sample rate mismatch - expected " + expectedData.sampleRate + ", got " + data.sampleRate;

    if (data.frameCount != expectedData.frameCount)
        return "Frame count mismatch - expected " + expectedData.frameCount + ", got " + data.frameCount;

    let comparisonStats = { "maxDiff": 0, "maxValue":0, "diffFrame":0, "diffChannel":0 };

    for (let i = 0; i < expectedData.frameCount; i++)
    {
        let expectedFrame = expectedData.data[i];
        let dataFrame = data.data[i];

        // Convert all data to be array based to simplify vector<1> and primitive stream comparison
        if (expectedFrame.length == null)
            expectedFrame = [ expectedFrame ];

        if (dataFrame.length == null)
            dataFrame = [ dataFrame ];

        if (expectedFrame.length != dataFrame.length)
            return "Channel count mimatch at frame " + i + ", expected " + expectedFrame.length + ", got " + dataFrame.length;

        for (let channel = 0; channel < expectedFrame.length; channel++)
            streamDataCompareValue (comparisonStats, expectedFrame[channel], dataFrame[channel], i, channel);
    }

    let diffDb = 20.0 * Math.log10 (comparisonStats.maxDiff / comparisonStats.maxValue);

    if (diffDb > maxDiffDb)
        return "Max db diff exceeded: diff of " + diffDb + " detected. maxDiffDb allowed:" + maxDiffDb + ", maxDiff:" + comparisonStats.maxDiff + ", maxValue:" + comparisonStats.maxValue + ", diffFrame:" + comparisonStats.diffFrame + ", diffChannel:" + comparisonStats.diffChannel;

    return null;
}

function removeObjNameValues (data)
{
    delete data["_objectName"];

    for (let key in data)
        if (typeof data[key] == "object")
            removeObjNameValues (data[key]);
}

function eventDataComparison (data, expectedData)
{
    if (data.length != expectedData.length)
        return "Different number of events - expected " + expectedData.length + ", got " + data.length;

    removeObjNameValues (data);
    removeObjNameValues (expectedData);

    for (let i = 0; i < data.length; i++)
    {
        if (data[i].frameOffset != expectedData[i].frameOffset)
            return "Event " + i + " has different frame offset - expected " + expectedData[i].frameOffset + ", got " + data[i].frameOffset;

        let expectedValue = JSON.stringify (expectedData[i].value);
        let dataValue = JSON.stringify (data[i].value);

        if (dataValue != null && dataValue !== expectedValue)
            return "Event " + i + " has different event data - expected " + expectedValue + ", got " + dataValue;
    }

    return null;
}

// helper function to valid input data for event or value inputs
function validateInputData (inputName, inputData, testSection, type)
{
    for (let i = 0; i < inputData.length; i++)
    {
        if (inputData[i].frameOffset == undefined)
        {
            testSection.reportFail (inputName + ": Failed validation, missing frameOffset attribute for item " + i);
            return false;
        }

        if (type == "value")
        {
            if (inputData[i].value == undefined)
            {
                testSection.reportFail (inputName + ": Failed validation, missing event attribute for item " + i);
                return false;
            }

            if (inputData[i].framesToReachValue == undefined)
            {
                testSection.reportFail (inputName + ": Failed validation, missing framesToReachValue attribute for item " + i);
                return false;
            }
        }
    }

    return true;
}
