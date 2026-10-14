# Cmajor C++ API

This folder contains the public headers for using the Cmajor C++ API.

The API allows you to embed the Cmajor compiler and JIT engine into a native app or plugin, and to use it to build and run Cmajor code and/or patches.

- `include/cmajor/API` contains the main public classes that you'll use to interact with the compiler at a low level.
- `include/cmajor/helpers` provides C++ helper classes for higher-level tasks like loading and running patches.
- `include/cmajor/COM` contains the lowest-level COM classes to interoperate with the compiler. You shouldn't need to use these directly, as the C++ wrapper classes in `include/cmajor/API` provide a much easier-to-use layer that hides the COM details.

These headers depend on the [CHOC](https://github.com/Tracktion/choc) library. They need no extra setup: if no copy of CHOC is on your include path, they use the copy bundled in `include/choc`.

If your project already uses its own copy of CHOC, put the folder containing its `choc` directory on your include path (or, with CMake, define a `choc::choc` target before adding Cmajor). The headers then use your copy instead of the bundled one, so only one version of CHOC is compiled into your project. The headers decide this once, by checking for `choc/platform/choc_Platform.h`, so they use either your copy or the bundled one throughout and never mix the two.

More documentation about how to use the API is available [here](https://cmajor.dev/docs/C++API).

Example projects demonstrating how to use the API can be found [in the examples folder](../../examples/native_apps/).
