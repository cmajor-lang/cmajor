# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Cmajor is a C-family programming language specifically designed for writing fast, portable audio software and DSP signal processing code. The language aims to match C/C++ performance while being more portable across processor architectures (CPU, DSP, GPU, TPU) and easier to learn.

## Build Commands

### Initial Setup
```bash
# Clone with submodules (required for 3rd party libraries and LLVM)
git clone --recurse-submodules <repository-url>
```

### Platform-Specific Builds

**macOS:**
```bash
cmake -Bbuild -GXcode .
# Open the generated Xcode project in build/ directory
```

**Windows:**
```bash
cmake -Bbuild -G"Visual Studio 16 2019" .
# Use -DBUILD_PLUGIN=OFF for Windows/arm64
# Only Release and RelWithDebugInfo builds supported due to LLVM runtime requirements
```

**Linux:**
```bash
cmake -Bbuild -GNinja -DBUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release .
cd build
ninja
```

### Build Options
- `BUILD_CMAJ=ON/OFF` - Command line tool (default: ON)
- `BUILD_CMAJ_LIB=ON/OFF` - Shared library (default: ON)
- `BUILD_PLUGIN=ON/OFF` - Plugin build (default: OFF, requires JUCE_PATH)
- `BUILD_EXAMPLES=ON/OFF` - Example projects (default: ON)

### Running Tests
The test runner is part of the command line tool. Use:
```bash
./cmaj test <test-file>
```

## Architecture Overview

### Core Modules (`modules/`)

**Compiler (`modules/compiler/`):**
- **AST**: Abstract syntax tree representation, parsing, and manipulation
- **Backends**: Code generation for different targets:
  - `CPlusPlus/`: C++ code generation
  - `LLVM/`: LLVM IR generation for native performance
  - `WebAssembly/`: WASM compilation and JavaScript class generation
- **Passes**: Analysis and transformation passes (type resolution, name resolution, etc.)
- **Transformations**: Code transformations for optimization and target-specific adaptations

**Core Libraries:**
- **Playback** (`modules/playback/`): Audio playback infrastructure and patch players
- **Scripting** (`modules/scripting/`): JavaScript engine integration and test runner
- **Server** (`modules/server/`): HTTP server for patch development workflows
- **Plugin** (`modules/plugin/`): Audio plugin wrappers (CLAP support)

### Tools (`tools/`)
- **command/**: Main CLI tool (`cmaj`) with subcommands for compilation, testing, rendering
- **CmajDLL/**: Shared library exports for C API
- **CmajPlugin/**: Audio plugin implementations
- **wasm_compiler/**: WebAssembly compiler tools

### Examples and Patches
- **examples/patches/**: Sample Cmajor programs demonstrating language features
- **examples/native_apps/**: C++ applications showing API usage
- **standard_library/**: Built-in Cmajor modules for common DSP tasks

### Third-Party Dependencies
- **LLVM**: Pre-built libraries in `3rdParty/llvm/release/` for different platforms
- **Boost**: Header-only libraries for various utilities
- **JUCE**: Audio framework (external dependency for plugin builds)
- **Graphviz**: Graph visualization for patch diagrams

## Development Workflow

### Language Files
- `.cmajor`: Main language source files
- `.cmajorpatch`: Patch definition files with metadata
- `.cmajtest`: Test specification files

### Key Components to Understand
- **Processors**: Core Cmajor concept - stateful audio processing units
- **Graphs**: Networks of connected processors
- **Endpoints**: Input/output points for audio, MIDI, and parameters
- **Patches**: Complete audio programs with UI and metadata

### Testing
Tests are organized in `tests/`:
- `language_tests/`: Core language feature tests
- `integration_tests/`: End-to-end testing
- `performance_tests/`: Benchmarking

Use the test runner: `cmaj test path/to/test.cmajtest`

### Code Generation
The compiler supports multiple backend targets:
- Native performance via LLVM
- Web deployment via WebAssembly
- C++ code for integration into existing codebases

### Plugin Development
Audio plugins can be built using the CLAP wrapper. Requires setting `JUCE_PATH` and enabling `BUILD_PLUGIN`.

## Cmajor Language Reference

### Core Language Concepts

**Processors**: Stateful audio processing units that are the fundamental building blocks
**Graphs**: Networks of connected processors with routing connections
**Nodes**: Processor instances within a graph (declared with `node`)
**Endpoints**: Input/output points for streams, events, and values

### Processor Built-in Properties

**Sample Rate Access:**
```cmajor
processor.frequency    // Sample rate in Hz (e.g., 44100.0)
processor.period       // Period of one sample = 1/frequency
```

**Usage Examples:**
```cmajor
// Calculate phase increment for oscillator
let phaseDelta = frequency * processor.period * twoPi;

// Convert time to frames
let framesPerSecond = int(processor.frequency);

// Frequency-dependent calculations
phase += freq / float(processor.frequency);
```

### Standard Library: std::notes

**Event Structures** (all use `float32` for precise pitch representation):

```cmajor
std::notes::NoteOn {
    int32 channel;      // Voice allocation channel ID
    float32 pitch;      // MIDI note number (0-127, middle C = 60)
    float32 velocity;   // Key velocity (0-1)
}

std::notes::NoteOff {
    int32 channel;      // Matching channel ID from NoteOn
    float32 pitch;      // MIDI note number
    float32 velocity;   // Release velocity (0-1)
}

std::notes::PitchBend {
    int32 channel;      // Matching channel ID
    float32 bendSemitones;  // Semitones to bend (+/-)
}
```

**Event Handler Examples:**
```cmajor
event eventIn (std::notes::NoteOn e)
{
    frequency = std::notes::noteToFrequency(e.pitch);  // Use e.pitch
    velocity = e.velocity;
    isActive = true;
}

event eventIn (std::notes::NoteOff e)
{
    if (e.channel == currentChannel)  // Match by channel
        isActive = false;
}
```

**Utility Functions:**
```cmajor
// Convert MIDI note to frequency (A=440Hz tuning)
float32 std::notes::noteToFrequency(float32 midiNote)

// Convert frequency to MIDI note
float32 std::notes::frequencyToNote(float32 frequency)
```

### Voice Allocation Patterns

**Array Connections** (connecting to processor arrays):
```cmajor
node voices = MyVoice[8];                    // Array of 8 voice processors
node voiceAllocator = std::voices::VoiceAllocator(8);

connection {
    // Connect allocator output to voice array input endpoint
    voiceAllocator.voiceEventOut -> voices.eventIn;  // Correct syntax

    // Connect voice array output to graph output
    voices -> audioOut;
}
```

**Parameter Broadcasting** (connecting values to all voices):
```cmajor
// Parameters defined at graph level
input value float cutoff [[ name: "Cutoff" ]];

connection {
    // Broadcasts to all voices in the array
    cutoff -> voices.cutoff;
}
```

### Audio Processing Patterns

**Main Loop Structure:**
```cmajor
void main()  // Processor entry point (not run()!)
{
    loop {
        // Process one sample
        let input = audioIn;
        let output = processAudio(input);
        audioOut <- output;
        advance();  // Move to next sample
    }
}
```

**Vector Operations** (proper broadcasting):
```cmajor
// Scalar to vector multiplication requires explicit broadcast
let input = audioIn;           // float<2>
let gain = gainParameter;      // float
let output = input * float<2>(gain);  // Explicit vector broadcast

// Or use the scalar directly if supported
let output = input * gain;     // May work in newer versions
```

**Channel Layout Examples:**
```cmajor
// Mono processor
input stream float audioIn;     // Single channel
output stream float audioOut;

// Stereo processor
input stream float<2> audioIn;  // Left/right channels
output stream float<2> audioOut;

// Multi-channel
input stream float<8> audioIn;  // 8-channel input
```

### Common Patterns and Best Practices

**Oscillator Phase Management:**
```cmajor
float phase;

void main() {
    loop {
        let sample = sin(phase * twoPi);
        audioOut <- sample;

        // Proper phase increment using processor.frequency
        phase += frequency / float(processor.frequency);

        // Keep phase in valid range
        while (phase >= 1.0f)
            phase -= 1.0f;

        advance();
    }
}
```

**MIDI Event Processing:**
```cmajor
// Accept multiple event types
input event (std::notes::NoteOn, std::notes::NoteOff, std::notes::PitchBend) eventIn;

// Handler for each type
event eventIn (std::notes::NoteOn e) {
    noteFrequency = std::notes::noteToFrequency(e.pitch);  // Use e.pitch!
    noteVelocity = e.velocity;
    isNoteActive = true;
}

event eventIn (std::notes::PitchBend e) {
    bendAmount = e.bendSemitones;
    currentFrequency = noteFrequency * pow(2.0f, bendAmount / 12.0f);
}
```

**Graph Connections:**
```cmajor
graph MyInstrument [[ main ]] {
    input event std::midi::Message midiIn;
    output stream float<2> audioOut;

    // Node declarations
    node voices = MyVoice[8];
    node voiceAllocator = std::voices::VoiceAllocator(8);

    connection {
        // MIDI processing chain
        midiIn -> std::midi::MPEConverter -> voiceAllocator;

        // Voice allocation to voices
        voiceAllocator.voiceEventOut -> voices.eventIn;

        // Audio output
        voices -> audioOut;
    }
}
```

## Coding Standards

### File Naming Conventions
- **Source files**: Use `cmaj_` prefix for all C++ source files (`.cpp`, `.h`)
  - Examples: `cmaj_Parser.cpp`, `cmaj_AudioEngine.h`, `cmaj_PatchLoader.cpp`
- **Language files**: Use `.cmajor` extension for Cmajor source code
- **Test files**: Use `.cmajtest` extension for test specifications
- **Patch files**: Use `.cmajorpatch` extension for patch definitions

### Code Style

The repository follows the CHOC library C++ coding style. Key guidelines:

**Naming Conventions:**
- **Classes/Structs**: PascalCase (`AudioEngine`, `PatchLoader`, `MessageLoop`)
- **Functions/Methods**: camelCase (`createHexString`, `noteNumberToFrequency`, `getNumElements`)
- **Variables**: camelCase (`numElements`, `textToSearch`, `sourceData`)
- **Constants**: snake_case (`A440_frequency`, `A440_noteNumber`)
- **Namespaces**: snake_case (`choc::text`, `choc::midi`, `cmaj::compiler`)
- **Template Parameters**: PascalCase (`ElementType`, `StorageType`)
- **Boolean Methods**: Use `is/has` prefix (`isVoid()`, `isEmpty()`, `isPrimitive()`)
- **Getter Methods**: Use `get` prefix (`getElementType()`, `getControllerName()`)

**Formatting:**
- **Indentation**: 4 spaces (no tabs)
- **Braces**: Opening brace on same line for functions/control structures, new line for classes
- **Include Guards**: Use `#ifndef`/`#define`/`#endif` pattern with `CMAJ_FILENAME_HEADER_INCLUDED`
- **Parameters**: Multi-line parameters aligned under first parameter

**File Organization:**
1. File header with license
2. Include guards
3. System includes, then project includes
4. Namespace declarations
5. Forward declarations
6. Public interface
7. Implementation detail separator (`//==============================================================================`)
8. Implementation (for header-only)

**Comments:**
- Use `///` for documentation comments
- Use `//` for implementation comments
- ASCII art separators for major sections
- Clean namespace closing: `} // namespace cmaj::compiler`

**Modern C++:**
- Use `[[nodiscard]]` for functions returning values
- Use `constexpr` for compile-time constants
- Prefer RAII patterns and smart pointers
- Use `if constexpr` for template metaprogramming

**Additional Style Guidelines:**
- **Logical NOT operator**: Always leave a space after the logical not operator `!`
  - Good: `if (! condition)`, `if (! ptr)`
  - Bad: `if(!condition)`, `if(! condition)`
- **Variable declarations**: Follow "almost-always-auto" style for local variables
  - Good: `auto result = functionCall();`, `auto& element = container[0];`
  - Bad: `std::string result = functionCall();` (when type is obvious from context)
- **Empty string returns**: Prefer `return {};` over `return "";` for empty std::string
  - Good: `return {};`
  - Bad: `return "";`
- **Spacing after closing braces**: Leave a blank line after a closing brace if the next line is a full statement
  - Good: `} // end of block\n\nstatement();`
  - Bad: `} // end of block\nstatement();`
- **Simple if statements**: For short, simple one-line if statements, prefer to avoid braces
  - Good: `if (condition) doSomething();`
  - Bad: `if (condition) { doSomething(); }` (for simple cases)
- **Member variable names**: Must NOT end with an underscore
  - Good: `memberVariable`, `isEnabled`, `currentState`
  - Bad: `memberVariable_`, `isEnabled_`, `currentState_`
- **Function call and declaration spacing**: 
  - **WITH parameters/arguments**: ALWAYS leave a space before opening parenthesis
  - **WITHOUT parameters/arguments (empty parentheses)**: NEVER leave a space before opening parenthesis
  - **This applies to ALL function calls, method calls, constructors, destructors, and function declarations**
  - Good examples:
    - Function calls: `functionCall (param1, param2)`, `functionCall()`
    - Method calls: `object.method (arg)`, `object.method()`
    - Constructors: `MyClass (param)`, `MyClass()`
    - Function declarations: `void myFunction (int param)`, `void otherFunction()`
    - Destructors: `~MyClass()` (always no parameters)
  - Bad examples:
    - `functionCall(param1, param2)` ❌ (missing space with parameters)
    - `functionCall ()` ❌ (unwanted space with no parameters)
    - `void myFunction(int param)` ❌ (missing space in declaration)
    - `void otherFunction ()` ❌ (unwanted space with no parameters)
- **Multi-line function declarations**: When function declarations span multiple lines, ensure proper spacing
  - Always include a blank line after the closing brace of the function
  - Always include a blank line (or a comment-only line) before the function declaration
  - Good:
    ```cpp
    // Comment describing the function
    bool TestHost::verifyProperties(const TestResult::PluginInfo& discovered,
                                   const PatchGenerator::PatchProperties& expected,
                                   std::string& errorMessage)
    {
        // implementation
        return result;
    }

    void TestHost::nextFunction()
    ```
  - Bad:
    ```cpp
    bool TestHost::verifyProperties(const TestResult::PluginInfo& discovered,
                                   const PatchGenerator::PatchProperties& expected,
                                   std::string& errorMessage)
    {
        return result;
    }
    void TestHost::nextFunction()
    ```