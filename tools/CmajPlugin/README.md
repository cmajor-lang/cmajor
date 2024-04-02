# CMaj Plugin

## How to build
Cmaj Plugin requires JUCE to build. Copy a JUCE distribution (https://juce.com/download/) into this CmajPlugin directory and add this line to CMakeLists.txt before `juce_add_plugin`:
```
add_subdirectory(JUCE)
```

### Building on macOS
1) Ensure you have CMake installed on your system (https://formulae.brew.sh/formula/cmake)  
2) In a shell execute this, substituting in the current Cmajor version of this repo:
```
cmake . -DCMAJ_VERSION={version}
make .
```

### Building with VSCode
1) Copy the .vscode.example directory in this CmajPlugin directory to .vscode:
```
cp -r .vscode.example .vscode
```
2) Ensure that the CMAJ_VERSION under `cmake.configureSettings` in settings.json matches the version of Cmajor you have checked out in this repo
3) Open this CmajPlugin directory in VSCode. Navigate to the 'Run and Debug' panel on the left and press the play button next to Launch CmajPlugin

