import * as midi from "@tonaljs/midi";
import { transform } from "framer-motion";

import { Event, setupGlobalKeyboardAndMIDIListeners } from "./midi-input-utils.js";
import { pressNote, updateOscilloscope } from "../actions/actions.js";
import { makeStore } from "../store";
import Root from "../App";

export function makeEnvironment () {
    const store = makeStore ({
        stateOverrides: {},
        middleware: [],
    });

    const engine = new NativeWebAudioSynthEngine ({
        audioContext: new AudioContext(),
        initialState: store.getState().state,
        updateOscilloscope: (dataArray) => {
            store.dispatch (updateOscilloscope (dataArray));
        },
        onPlayNote: (midiNote) => {
            store.dispatch (pressNote (midiNote));
        },
    });

    store.subscribe (() => engine.update (store.getState().state));

    setupGlobalKeyboardAndMIDIListeners ((msg) => engine.onMIDIMessage (msg));

    return {
        Root,
        store,
    };
}

const transposeFrequencyByCents = (frequency, cents) => {
    const freq = frequency * Math.pow(2, cents / 1200);
    return freq;
};
const transposeFrequencyBySemitones = (frequency, semitones) => {
    const a = Math.pow(2, 1 / 12);
    return frequency * Math.pow(a, semitones);
};

class NativeWebAudioSynthEngine {
    constructor ({
        audioContext,
        updateOscilloscope,
        onPlayNote,
        initialState,
    }) {
        this.audioCtx = audioContext;
        this.onPlayNote = onPlayNote;
        this.state = initialState;

        this.lastNote = initialState.keyboard.note.midi;
        this.lastVelocity = 0;

        this.vco = [];
        this.vcoGain = [];

        this.gain = this.audioCtx.createGain();
        this.envelope = this.audioCtx.createGain();
        this.envelope.gain.value = 0;

        this.biquadFilter = this.audioCtx.createBiquadFilter();
        this.biquadFilter.type = "lowpass";
        this.biquadFilter.frequency.value = this.state.filter.frequency;
        this.biquadFilter.gain.value = 0;
        this.biquadFilter.Q.value = this.state.filter.resonance / 4;

        this.compressor = this.audioCtx.createDynamicsCompressor();

        this.analyzer = this.audioCtx.createAnalyser();
        this.analyzer.fftSize = this.state.oscilloscope.fftSize;
        this.analyzerBufferLength = this.analyzer.frequencyBinCount;
        const dataArray = new Float32Array(this.analyzer.fftSize);

        this.biquadFilter.connect(this.envelope);
        this.envelope.connect(this.analyzer);

        this.analyzer.connect(this.gain);

        this.gain.connect(this.compressor);
        this.compressor.connect(this.audioCtx.destination);

        this.pressedNotes = [];

        setInterval(() => {
            this.analyzer.getFloatTimeDomainData(dataArray);
            updateOscilloscope(dataArray, this.audioCtx.currentTime);
        }, 1000 / 60);
    }

    onMIDIMessage (data) {
        const status = data[0] & 0xF0;
        switch (status) {
            case Event.NOTE_ON:
                const velocity = transform (data[2], [1, 127], [0, 1]);
                this.playNote (data[1], velocity);
                break;
            case Event.NOTE_OFF:
                this.releaseNote (data[1]);
                break;
            default:
                return null;
        }
    }

    playNote(midiNote, velocity = 1) {
        const ctx = this.audioCtx;
        this.lastNote = midiNote;
        this.lastVelocity = velocity;

        this.triggerEnvelope(velocity);

        if (this.pressedNotes.indexOf(midiNote) === -1)
            this.pressedNotes.push(midiNote); // dont add key if already in

        this.onPlayNote (midiNote);

        const baseFrequency = this.getBaseFrequency(midiNote, this.state.general.octave);

        this.vco.forEach((vco, i) => {
            const frequency = this.getVCOFrequency(i, baseFrequency);

            if (this.envelope.gain.value > 0.1 && this.state.general.glide) {
                vco.frequency.cancelScheduledValues(ctx.currentTime);
                vco.frequency.setValueAtTime(vco.frequency.value, ctx.currentTime);
                vco.frequency.linearRampToValueAtTime(
                    frequency,
                    ctx.currentTime + this.state.general.glide
                );
            } else {
                vco.frequency.setValueAtTime(frequency, ctx.currentTime);
            }
        });
    }

    releaseNote(midiNote) {
        const keyIndex = this.pressedNotes.indexOf(midiNote);

        //remove released note
        this.pressedNotes = this.pressedNotes.filter(function (value, index, arr) {
            return index !== keyIndex;
        });

        if (this.pressedNotes.length) {
            this.playNote(
                this.pressedNotes[this.pressedNotes.length - 1],
                this.lastVelocity
            );
        } else {
            this.releaseEnvelope();
        }
    }

    getBaseFrequency(midiNote, octave) {
        return midi.midiToFreq (midiNote + (octave * 12));
    }

    getVCOFrequency(i, baseFrequency) {
        const fineTuned = transposeFrequencyBySemitones(
            baseFrequency,
            this.state.vco[i].semitones
        );
        const detuned = transposeFrequencyByCents(
            fineTuned,
            this.state.vco[i].detune
        );
        return detuned;
    }

    getTimeConstant(time) {
        return Math.log(time + 1) / Math.log(100);
    }

    powerOn() {
        this.state.vco.forEach((vco, i) => {
            this.startVCO(i);
        });
    }

    powerOff(length) {
        this.state.vco.forEach((vco, i) => {
            this.stopVCO(i, length);
        });
    }

    startVCO(index) {
        const ctx = this.audioCtx;

        const frequency = this.getVCOFrequency(
            index,
            this.getBaseFrequency(this.lastNote, this.state.general.octave)
        );

        this.vco[index] = ctx.createOscillator();
        this.vco[index].type = this.state.vco[index].type;
        this.vco[index].frequency.setValueAtTime(frequency, ctx.currentTime); // value in hertz

        this.vcoGain[index] = ctx.createGain();
        this.vcoGain[index].gain.setValueAtTime(
            this.state.vco[index].gain,
            ctx.currentTime
        );

        // connect
        this.vco[index].connect(this.vcoGain[index]);
        this.vcoGain[index].connect(this.biquadFilter);

        this.vco[index].start();
    }

    stopVCO(index, time = 0) {
        const ctx = this.audioCtx;
        this.vco[index].stop(ctx.currentTime + time + this.state.envelope.release);
    }

    triggerEnvelope(velocity = 1) {
        const ctx = this.audioCtx;
        const { envelope, filterEnvelope, filter } = this.state;
        const startTime = ctx.currentTime;
        const startFrequency = this.biquadFilter.frequency.value;

        this.envelope.gain.cancelScheduledValues(0);
        this.envelope.gain.setValueAtTime(this.envelope.gain.value, startTime);

        this.biquadFilter.frequency.cancelScheduledValues(0);
        this.biquadFilter.frequency.setValueAtTime(startFrequency, startTime);

        if (envelope.attack === 0) {
            this.envelope.gain.linearRampToValueAtTime(velocity, startTime);
        } else {
            this.envelope.gain.linearRampToValueAtTime(
                velocity,
                startTime + envelope.attack
            );
        }

        const minv = Math.log(filter.frequency);
        const maxv = Math.log(20000);
        const scale = (maxv - minv) / 100;

        const attackFrequency = Math.exp(filterEnvelope.intensity * scale + minv); //scale * filterEnvelope.intensity
        const sustainFrequency = Math.exp(
            filterEnvelope.intensity * (filterEnvelope.sustain / 100) * scale + minv
        );

        this.biquadFilter.frequency.exponentialRampToValueAtTime(
            attackFrequency,
            startTime + filterEnvelope.attack
        );
        this.biquadFilter.frequency.setValueAtTime(
            attackFrequency,
            startTime + filterEnvelope.attack
        );
        this.biquadFilter.frequency.setTargetAtTime(
            sustainFrequency,
            startTime + filterEnvelope.attack,
            this.getTimeConstant(filterEnvelope.decay)
        );

        this.envelope.gain.setValueAtTime(velocity, startTime + envelope.attack);
        this.envelope.gain.setTargetAtTime(
            (envelope.sustain / 100) * velocity,
            startTime + envelope.attack,
            this.getTimeConstant(envelope.decay)
        );
    }

    releaseEnvelope() {
        const { envelope, filter, filterEnvelope } = this.state;
        const ctx = this.audioCtx;
        const startTime = ctx.currentTime;
        const startFrequency = this.biquadFilter.frequency.value;

        this.envelope.gain.cancelScheduledValues(startTime);
        this.envelope.gain.setValueAtTime(this.envelope.gain.value, startTime);

        this.biquadFilter.frequency.cancelScheduledValues(startTime);
        this.biquadFilter.frequency.setValueAtTime(startFrequency, startTime);

        const releaseConstant =
            envelope.release > 0 ? this.getTimeConstant(envelope.release) : 0.0001;

        this.biquadFilter.frequency.exponentialRampToValueAtTime(
            filter.frequency,
            startTime + filterEnvelope.release
        );

        this.envelope.gain.setTargetAtTime(0, startTime, releaseConstant);
    }

    update(nextState) {
        const ctx = this.audioCtx;

        // update VCOs
        this.vco.forEach((vco, i) => {
            if (nextState.vco[i].type !== this.state.vco[i].type)
                vco.type = nextState.vco[i].type;

            if (
                nextState.general.octave !== this.state.general.octave ||
                nextState.vco[i].semitones !== this.state.vco[i].semitones ||
                nextState.vco[i].detune !== this.state.vco[i].detune
            )
                vco.frequency.setValueAtTime(
                    this.getVCOFrequency(
                        i,
                        this.getBaseFrequency(
                            this.lastNote,
                            nextState.general.octave
                        )
                    ),
                    ctx.currentTime
                );

            if (nextState.vco[i].gain !== this.state.vco[i].gain)
                this.vcoGain[i].gain.setValueAtTime(
                    nextState.vco[i].gain,
                    ctx.currentTime
                );
        });

        // power
        if (nextState.power.active && !this.state.power.active) {
            this.powerOn();
        } else if (!nextState.power.active && this.state.power.active) {
            this.powerOff();
        }

        if (nextState.amp.gain !== this.state.amp.gain)
            this.gain.gain.setValueAtTime(nextState.amp.gain, ctx.currentTime);
        if (nextState.filter.frequency !== this.state.filter.frequency)
            this.biquadFilter.frequency.setValueAtTime(
                nextState.filter.frequency,
                ctx.currentTime
            );
        if (nextState.filter.resonance !== this.state.filter.resonance)
            this.biquadFilter.Q.setValueAtTime(
                nextState.filter.resonance / 4,
                ctx.currentTime
            );

        this.state = nextState;
    }
}
