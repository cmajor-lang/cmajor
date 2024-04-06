# Cmajor javascript API

This folder contains a set of javascript modules which are provided for a patch's GUI code to use.

These are available via the `PatchConnection` object that is passed to your GUI or worker module when it is being created. The `PatchConnection` object has a `utilities` property containing various javascript objects for the modules in the API. To see what's available, have a look at the class in `cmajor/javascript/cmaj_api/cmaj-patch-connection.js`, and the various files in the `cmajor/javascript/cmaj_api` folder.

```js
// In your GUI module:
export default function createPatchView (patchConnection)
{
    console.log (`Cmajor version: ${patchConnection.getCmajorVersion()}`);
    console.log (`MIDI message: ${patchConnection.midi.getMIDIDescription (0x924030)}`);

    const keyboard = new patchConnection.utilities.PianoKeyboard();
}
```
