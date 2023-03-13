# Cmaj Script Files

The `cmaj` command-line tool can be used to execute a javascript file to perform various Cmajor-related tasks.

To run a script, just invoke the `cmaj play` command with your .js file, e.g.

```shell
$ cmaj play my_script.js
```

There are various built-in javascript bindings available to let you programmatically build and run Cmajor code:

```javascript

//==============================================================================
// Many of the functions return errors when parsing, linking etc.
// Because these errors may contain complex items, they're represented by
// objects, not just strings.
// These helper functions make it easy to find out whether a return value
// is an error, and to dump it out if needed.

function isErrorOrWarning (response)
function isError (response)
function getErrorDescription (error) // converts an error object to a string description
function printError (error)

//==============================================================================
// To compile a Cmajor program, create one of these, and call parse() on it to
// add your source code. If it doesn't return any errors, you can pass it into
// Engine.load()
class Program
{
    constructor()
    release()

    parse (sourceCodeString)    // parses some code, returning an error if there is one
    reset()                     // clears the program object for re-use
    getSyntaxTree (moduleName)  // returns a JSON representation of the full syntax tree of the program
    getBinaryModule()           // returns a conpact binary representation of the program which can be
                                // used in place of the source code for faster loading and obfuscation
}

//==============================================================================
// This represents a Cmajor engine, which can be used to compile Cmajor programs.
// This is basically a binding around the C++ cmaj::Engine class, so for more
// details, see the notes for that class in the C++ API docs.
class Engine
{
    constructor (engineArgs)
    release()
    isValid()
    getBuildSettings()
    setBuildSettings (settings)
    load (program) // takes a Program object, as detailed below.
    unload()
    getInputEndpoints()
    getOutputEndpoints()
    link()
    isLoaded()
    isLinked()
    createPerformer()  // returns a new Performer object (see below) or an error
    getEndpointHandle (endpointID)
    getExternalVariables()
    setExternalVariable(fullyQualifiedName, value)
    getAvailableCodeGenTargetTypes()
    generateCode (target, options)
}

//==============================================================================
// This object is created by Engine.createPerformer() and is used to
// render a processor.
// See also the C++ class cmaj::Performer in the C++ API docs, as this
// object is a wrapper around that class.
class Performer
{
    constructor (performerID)
    release()
    setBlockSize (frames)
    advance()
    getOutputFrames (h)
    getOutputEvents (h)
    getOutputValue (h)
    setInputFrames (h, d)
    setInputValue (h, d, f)
    addInputEvent (h, d)
    getXRuns()
    calculateRenderPerformance (bs, f)
}

//==============================================================================
// This helper class just provides access to the filesystem
class File
{
    constructor (path)          // Creates a file from a path string
    path                        // A property containing the file path as a string

    exists()                    // True if the file exists
    isFolder()                  // True if the file is a folder
    getModificationTime()       // Get the last modification time of the file
    parent()                    // Returns a new File object representing the parent folder
    getChild (relativePath)     // Returns a new File object by appenting this relative path
    getSibling (relativePath)   // Returns a new File object for a sibling file with the given path

    // Reads the content of the file and returns it as a string (if it's valid UTF8) or
    // an array of bytes if not. Returns an error object on failure.
    read()

    // reads an audio file and returns it as an object containing
    // fields for rate, length, and channel data as arrays of floats
    readAudioData (annotations)

    // attempts to overwrite this file with the given string or array of bytes,
    // returning an error on failure
    overwrite (newContent)

    // Scans this folder for children and returns the list as an array of File objects.
    // If shouldFindFolders is true, it only looks for folders, if false, only looks
    // for files. If recursive is true, it's recursive. The wildcard parameter is an
    // optional simple wildcard expression to use.
    findChildren (shouldFindFolders, recursive, wildcard)
}

//==============================================================================
// These functions allow you to use timers, following the standard javascript timer API:

function setInterval (callback, milliseconds)
function setTimeout (callback, milliseconds)
function clearInterval (timerID)
```
