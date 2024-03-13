## CompuFart

Thanks to [Alex Fink](https://github.com/alexmfink) for this example.

CompuFart is a fart sound synthesizer that generates sound by physically modeling wind passing through an asshole. This version is programmed in Cmajor.

### How to Use

You can use the provided fart synthesizer in your DAW or with a handful of tools provide by Cmajor Software. Alternatively, you can use the physical model in your own Cmajor patch!

### Building your own Fart Engine

The quickest way to use the physical model is to use the `processor` named `Terrance` (the phsyical model) along with the input and output interface `processor`s, `TerranceInputInterface` and `TerranceOutputInterface`. The interface `processor`s provide a quick way to get meaningful and useful parameters and audio into and out of the model. The model should oscillate when sufficient pressure is provided to the artificial sphincter.

The provided mono synth, `CompuFartSynth` shows one possible way to construct a digital instrument with the fart engine.

### Notes

* The model is not currently (pitch) tuned. However, using typical pitch control inputs (keyboard, bend) should provide relative pitch control.
* There is currently no guarantee of compatibility or consistency between different versions of the model, synth, or other patches and code. If you wish to preserve a particular sound, it is recommended that you record it and make note of the parameter values and the version and git commit SHA. Of course, you can also fork the code.
