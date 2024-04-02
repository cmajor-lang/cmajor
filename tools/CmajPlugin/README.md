# CMaj Plugin

## How to build
Cmaj Plugin requires JUCE to build. Copy a JUCE distribution (https://juce.com/download/) into this project directory and add this line to CMakeLists.txt before `juce_add_plugin`:
```
add_subdirectory(JUCE)
```

### Building on macOS
In a shell execute this, substituting in the current Cmajor version of this repo:
```
cmake . -DCMAJ_VERSION={version}
make .
```