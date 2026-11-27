# Dynamic EQ

A six-band floating-band dynamic equaliser. Each band is an EQ filter whose gain moves with
the level of the signal in its own frequency range, so it can tame a harsh resonance only
when it appears, or lift a range only when it's played.

## Features

- **Six bands**, each a low shelf, bell or high shelf, with frequency, static gain and Q.
- **Per-band dynamics**: threshold, range, ratio, attack and release. When the band's
  sidechain level rises above the threshold, its gain moves by up to `range` dB:
  a negative range cuts (compression), a positive range boosts (upward expansion).
  The static gain and the dynamic gain add together.
- **Sidechain per band**: *Split* listens only to the band's frequency range (a unity-peak
  band-pass for bells, a low/high-pass at the corner for shelves); *Wide* listens to the
  full-band input. Every band detects from the unprocessed input, so the bands don't interact.
- **Mid/side per band**: each band can process Stereo, Mid only or Side only.
- **Solo**: hear what a band's detector is listening to.
- **Low cut / high cut** filters (12 or 24 dB/oct Butterworth) and output gain.
- **Analyser**: a spectrum of the input and/or output, overlaid on the EQ curves. *Flat*
  shows the levels as measured, so white noise and impulses look flat; *Music* tilts the
  display by +3dB/octave around 1kHz, so pink noise (and the average spectrum of most
  music) looks flat instead. These settings are kept in the patch's stored state.

## The GUI

The display shows the spectrum behind the EQ curves. For each band it draws:

- the **static curve**, with a light fill,
- the **dynamic zone** that the dynamics can move it through (from the static gain to the
  static gain plus range), with a dotted outline and a diamond-shaped range handle,
- the **live curve**, which moves as the dynamics act (with a white dot on the range line
  showing the current gain),
- and for the selected band, its **sidechain filter** as a dashed line.

The white curve is the total live response, including the low/high cut filters.

| On the display | |
| - | - |
| Drag a numbered handle | Frequency and gain |
| Alt/Option-drag a handle, or drag the diamond | Dynamic range |
| Mouse wheel | Q (of the hovered or selected band) |
| Double-click a handle | Turn the band, or low/high cut, on or off |
| Shift while dragging | Fine adjustment |

Below the display, the band tabs select a band (click the LED to turn it on or off) and
show each band's gain change. The selected band's panel has its options, knobs, and a
detector meter showing the sidechain level and gain change; drag in the meter to set the
threshold. Knobs reset to their default on double-click and respond to the mouse wheel.

## How it works

- `DynamicEQ.cmajor` is the top-level graph.
- `dsp/Equaliser.cmajor` holds the EQ. The bands are Simper/Cytomic trapezoidal
  state-variable filters (bells use `k / A` so boosts and cuts have the same bandwidth). The
  audio is processed in the mid/side domain, and each band has separate mid and side
  coefficients, which is how a band can affect only the mid or side. The gain computer has
  a 6dB soft knee and runs every 16 frames, at which point the coefficients are updated;
  parameter changes are smoothed, and turning a band off fades its gain to 0dB, so nothing clicks.
- `dsp/SpectrumAnalyser.cmajor` runs a 4096-point FFT about 30 times a second. The pre
  and post EQ signals are analysed together in one complex FFT (one as the real part, one
  as the imaginary part) and separated afterwards. It sends 256 log-spaced levels from
  20Hz to 20kHz, so the GUI doesn't have to handle thousands of bins. Where a level covers
  several bins it's a power mean of them, a compromise that keeps white noise close to
  flat without making tones read too low (see `binCombiningExponent`).
- `ui/index.js` is the GUI. It calculates the filter responses exactly, with the same
  equations as the DSP, so the curves it draws are the real response.

The patch also sends each band's gain change and sidechain level to the GUI 30 times a second.

## Tests

`tests/DynamicEQ.cmajtest` checks static and dynamic gains, ratio and range, mid/side
isolation, the cut filters and output gain, the analyser's levels and pre/post
separation, and that white noise and impulses give a flat spectrum. It also checks that the DSP's response matches the GUI's curve calculations.
To run them:

```
cd tests
cmaj test DynamicEQ.cmajtest
```
