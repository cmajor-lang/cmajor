# The Cmajor Language

Cmajor is a programming language for writing fast, portable audio software.

You've heard of C, C++, C#, objective-C... well, C*major* is a C-family language designed specifically for writing DSP signal processing code.

## Project goals

We're aiming to improve on the current status-quo for audio development in quite a few ways:

- To match (and often beat) the performance of traditional C/C++
- To make the same code portable across diverse processor architectures (CPU, DSP, GPU, TPU etc)
- To offer enough power and flexibility to satify professional audio tech industry users
- To speed-up commercial product cycles by enabling sound-designers to be more independent from the instrument platforms
- To attract students and beginners by being vastly easier to learn than C/C++

Our main documentation site is at [cmajor.dev](https://cmajor.dev).

The [Cmajor VScode extension](https://marketplace.visualstudio.com/items?itemName=SoundStacks.cmajor) offers a one-click install process to get you up-and-running, or see the [Quick Start Guide](https://cmajor.dev/docs/GettingStarted) for other options, like how to use the command-line tools.

If you want to learn about the nitty-gritty of the Cmajor language, the [language guide](https://cmajor.dev/docs/LanguageReference) offers a deep dive. To see some examples of the code, try the [examples](./examples/patches) folder.

## How to build

To build Cmajor requires a host with suitable compilers, and with support for Cmake. For MacOS, a recent XCode is required, whilst Windows requires VS2019. Linux builds have been successfully built using both clang and gcc v8 and above.

When cloning the Cmajor repository, you need to ensure you have also pulled the submodules, as there are a number for third party libraries and some pre-built LLVM libraries.

### Building on MacOS

To build on MacOS, from the top level directory in a shell execute:

```
> cmake -Bbuild -GXcode .
```

You can then open the XCode project in the build directory to build the tooling

### Building on Windows

On Windows, use cmake to build a suitable project to be opened with visual studio. From the top level directory in a shell execute:

```
> cmake -Bbuild -G"Visual Studio 16 2019" .
```

The resulting visual studio project can be opened and the tooling built. On Windows/arm64, building the plugin is not supported for now, and this can be disabled by specifying `-DBUILD_PLUGIN=OFF` when running cmake

Only `Release` and `RelWithDebugInfo` builds of the windows project are currently supported, as the pre-built LLVM libraries use the release runtime. If you find you need a Debug build, the simplest way is to re-build the LLVM library as a debug build. Be warned, this will take some time and the resulting LLVM libraries are rather large. The LLVM build script can be run to achieve this specifying `--build-type=Debug`

### Building on Linux

On linux platforms, simply running cmake with your generator of choice will produce a suitable build. We find ninja works well for this project:

```
> cmake -Bbuild -GNinja -DBUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release .
> cd build
> ninja
```

----

All content is copyright 2023 [Cmajor Software Ltd](https://cmajor.dev) unless marked otherwise.
