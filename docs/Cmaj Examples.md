## Examples

This section contains web-based versions of a few of the Cmajor example patches, to give you a quick feel for some of the things that Cmajor can do.

These are all exported using the command:

```
cmaj generate --target=webaudio-html <patch file> --output=<target folder>
```

..which emits a folder containing HTML/Javascript/WebAssembly that uses Web Audio and Web MIDI to run the patches.

(The Cmajor VScode extension also has a command to easily do this).

Our exporter uses the latest LLVM WebAssembly optimiser, so the DSP should be as efficient as is possible for WebAssembly. It achieves roughly half the performance of running the same patch in our native JIT engine.

Exporting to Javascript is just one of the ways you can deploy a patch. See our other documentation to learn about other export targets.
