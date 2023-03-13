#### Cmajor javascript API

This folder contains a set of javascript modules which are made available patch GUI code to import and use.

To import one of the modules, use a path starting with `/cmaj_api/`, e.g.

```js
import { getCmajorVersion } from "/cmaj_api/cmaj-version.js"
import * as midi from "/cmaj_api/cmaj-midi-helpers.js"

console.log (`Cmajor version: ${getCmajorVersion()}`);
console.log ("MIDI message: " + midi.getMIDIDescription (0x924030));
```
