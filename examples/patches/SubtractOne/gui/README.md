
## subtract.one

### Developing / Running

First, install the dependencies: `npm install`

The `package.json` contains scripts for running a local server, for use whilst developing (it also runs React in development mode), that watches the source files and hot reloads on change. There are also scripts for building minified production bundles. These are `npm run start` and `npm run build` respectively.

There are variants of each script for different versions of the app:

- full web app w/ original web audio engine: `npm run start` (i.e no suffix)
- full web app w/ cmaj wasm audio engine: `npm run start:cmaj-wasm`
- cmajor patch view (w/ stripped down app): `npm run start:cmaj-patch`

To build a compiled app for use in a patch (just the central controls from the synth UI + patch connection plumbing, with all javascript dependencies bundled / minified etc):
  - `npm run build:cmaj-patch`
  - this will generate a bunch of files in the `build` directory
     - we don't actually need all of them, everything other than `index.html`, and the `images` and `static` folder can be removed
     - `build` is ignored by our .gitignore file, so move the file manually to `stripped-build`

The built wasm target needs to be served from a web server to get around cross-origin security constraints, as a result the build artifacts are not commited to the repo, and the easiest thing to do is run it in development mode via `npm run start:cmaj-wasm`. We could explore embedding the resources and creating a blob url at runtime to workaround the restrictions.

When changing the patch, regenerate the wasm wrapper and commit the changes:

`cmaj generate --target=wasm /path/to/repo/examples/SubtractOne/SubtractOne.cmajorpatch --output=/path/to/repo/examples/SubtractOne/gui/public/wasm/SubtractOne.js`

### Release notes [from Twitter](https://twitter.com/juliussohn/status/1275830840317095936)

🛠 Dev Stack

- React
- Redux
- Styled Components
- Web Audio API
- Web MIDI API
- Tonal JS (the only audio related library - just used for note name conversion)
- @framer Motion
- Bitly (for sharing patches - the original links are to long)

🎛 Audio Stack

- 3 Voltage Controlled Oscillators
   - 4 Waveforms
   - Independently pitchable
- Lowpass filter
   - Filter resonance
- Filter ADSR Envelope Generator
- Amplitude ADSR Evelope Generator
- 8 Presets
- Phase synced Oscilloscope
- Glide/Portamento

Julius Elias Sohn
@juliussohn