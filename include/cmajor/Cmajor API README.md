# Cmajor C++ API

This folder contains the public headers for using the Cmajor C++ API.

The API allows you to embed the Cmajor compiler and JIT engine into a native app or plugin, and to use it to build and run Cmajor code and/or patches.

- `include/cmajor/API` contains the main public classes that you'll use to interact with the compiler at a low level.
- `include/cmajor/helpers` provides C++ helper classes for higher-level tasks like loading and running patches.
- `include/cmajor/COM` contains the lowest-level COM classes to interoperate with the compiler. You shouldn't need to use these directly, as the C++ wrapper classes in `include/cmajor/API` provide a much easier-to-use layer that hides the COM details.

More documentation about how to use the API is available [here](https://cmajor.dev/docs/C++API).

Example projects demonstrating how to use the API can be found [in the examples folder](../../examples/native_apps/).
