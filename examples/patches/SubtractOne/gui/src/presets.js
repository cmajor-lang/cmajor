const presets = [
    {
        //0 - white

        general: {
            octave: 0,
            glide: 0,
        },

        vco: [
            {
                pitch: 0,
                gain: 1,
                octave: 0,
                semitones: 0,
                detune: 0,
                type: "sawtooth",
            },
            {
                pitch: 0,
                type: "sawtooth",
                gain: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
            },
            {
                pitch: 0,
                type: "sawtooth",
                gain: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
            },
        ],

        envelope: {
            attack: 0,
            decay: 0,
            release: 0,
            sustain: 100,
        },
        filter: {
            frequency: 1512.7153011703033,
            resonance: 0,
        },
        filterEnvelope: {
            sustain: 100,
            intensity: 0,
            attack: 0,
            decay: 0,
            release: 0,
        },
    },
    {
        //1 - yellow

        vco: [
            {
                pitch: 0,
                type: "sawtooth",
                gain: 1,
                octave: 0,
                semitones: 0,
                detune: 0,
            },
            {
                pitch: 0,
                type: "sawtooth",
                octave: 0,
                detune: 10,
                semitones: 7,
                gain: 1,
            },
            {
                pitch: 0,
                octave: 0,
                semitones: 0,
                type: "square",
                detune: 2.142857142857146,
                gain: 0.31363636363636366,
            },
        ],
        general: {
            octave: -1,
            glide: 0.08035714285714286,
        },
        filter: {
            resonance: 1,
            frequency: 899.9248073623645,
        },
        filterEnvelope: {
            attack: 0.2,
            release: 0.5,
            sustain: 48.035714285714285,
            decay: 0.95,
            intensity: 36.60714285714286,
        },
        envelope: {
            decay: 0.2,
            sustain: 100,
            release: 0.8321428571428571,
            attack: 0.01499999999999996,
        },
    },
    //2 - orange
    {
        meta: {
            presetName: "Bell",
        },
        power: {
            active: true,
        },
        envelope: {
            sustain: 0,
            attack: 0.006428571428571418,
            decay: 0.7821428571428571,
            release: 0.7392857142857142,
        },
        general: {
            octave: 2,
            glide: 0,
        },
        filterEnvelope: {
            attack: 0.010714285714285714,
            intensity: 100,
            decay: 0.46785714285714286,
            release: 0.3964285714285714,
            sustain: 40.714285714285715,
        },
        vco: [
            {
                pitch: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
                gain: 0.8136363636363636,
                type: "square",
            },
            {
                pitch: 0,
                octave: 0,
                type: "triangle",
                detune: 11.25,
                semitones: 7,
                gain: 0.6363636363636365,
            },
            {
                pitch: 0,
                octave: 0,
                gain: 1,
                semitones: 12,
                detune: 18.214285714285722,
                type: "square",
            },
        ],
        filter: {
            resonance: 0,
            frequency: 765.4388234673022,
        },
    },
    //4 - pink
    {
        meta: {
            presetName: "Troy",
        },
        power: {
            active: true,
        },
        general: {
            glide: 0,
            octave: -1,
        },
        filter: {
            resonance: 0,
            frequency: 514.4382313196404,
        },
        filterEnvelope: {
            sustain: 0,
            intensity: 54.10714285714286,
            release: 0.45,
            attack: 0.48214285714285693,
            decay: 0.6749999999999999,
        },
        envelope: {
            decay: 0,
            release: 0,
            sustain: 100,
            attack: 0.36428571428571427,
        },
        vco: [
            {
                pitch: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
                type: "sawtooth",
                gain: 1,
            },
            {
                pitch: 0,
                octave: 0,
                detune: 0,
                type: "sine",
                semitones: -12,
                gain: 0.6727272727272726,
            },
            {
                pitch: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
                type: "sine",
                gain: 0,
            },
        ],
    }, // 5 = purple
    {
        power: {
            active: true,
        },
        envelope: {
            attack: 2,
            decay: 2,
            sustain: 54.46428571428571,
            release: 2,
        },
        filter: {
            resonance: 2.142857142857143,
            frequency: 100,
        },
        filterEnvelope: {
            sustain: 100,
            decay: 0,
            attack: 2,
            release: 1.7571428571428571,
            intensity: 50.35714285714286,
        },
        vco: [
            {
                pitch: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
                type: "sine",
                gain: 0.75,
            },
            {
                pitch: 0,
                octave: 0,
                gain: 0.7954545454545454,
                type: "triangle",
                detune: 12.32142857142857,
                semitones: 3,
            },
            {
                pitch: 0,
                octave: 0,
                type: "square",
                gain: 0.4681818181818182,
                detune: -7.5,
                semitones: 7,
            },
        ],
        general: {
            glide: 0.41785714285714287,
            octave: 1,
        },
    }, // 6 - blue
    {
        meta: {
            presetName: "WahWah",
        },
        general: {
            octave: -2,
            glide: 0.13,
            currentTime: 0,
        },
        vco: [
            {
                pitch: 0,
                gain: 1,
                octave: 0,
                semitones: 0,
                detune: 0,
                type: "sawtooth",
            },
            {
                pitch: 0,
                type: "triangle",
                gain: 0.7227272727272727,
                octave: 0,
                semitones: 12,
                detune: 0,
            },
            {
                pitch: 0,
                type: "square",
                gain: 0,
                octave: 0,
                semitones: 0,
                detune: 0,
            },
        ],
        envelope: {
            attack: 0.16926666150378772,
            decay: 6.520572293428623,
            sustain: 100,
            release: 0.25118864315095835,
        },
        filterEnvelope: {
            sustain: 28,
            intensity: 36.428571428571416,
            attack: 0.5531681197617235,
            decay: 7.56,
            release: 0.12589254117941692,
        },
        filter: {
            frequency: 213.40171133990876,
            resonance: 95.71428571428572,
        },
    },
    // 7 -teal
    {
        power: { active: true },
        general: {
            octave: 0,
            glide: 0,
        },
        vco: [
            {
                pitch: 0,
                type: "square",
                gain: 1,
                octave: 0,
                semitones: 0,
                detune: 10.714285714285694,
            },
            {
                pitch: 0,
                type: "square",
                gain: 0.6590909090909092,
                octave: 0,
                semitones: -7,
                detune: 0.5357142857142847,
            },
            {
                pitch: 0,
                type: "square",
                gain: 0.7181818181818183,
                octave: 0,
                semitones: 7,
                detune: 10.17857142857143,
            },
        ],
        envelope: {
            attack: 0.005094984382828271,
            decay: 2.357537269587174,
            sustain: 26.071428571428573,
            release: 0.5179474679231217,
        },
        filterEnvelope: {
            attack: 0.001,
            decay: 0.9699386857056398,
            sustain: 4.821428571428571,
            release: 0.0048496934285282,
            intensity: 70.71428571428572,
        },
        filter: {
            frequency: 571.4458476045286,
            resonance: 65.89285714285714,
        },
    },
    {
        // aua - green

        general: {
            octave: -1,
            glide: 0.24107142857142827,
        },
        vco: [
            {
                pitch: 0,
                type: "triangle",
                gain: 0.6500000000000001,
                octave: 0,
                semitones: 5,
                detune: 0,
            },
            {
                pitch: 0,
                type: "square",
                gain: 0.8818181818181818,
                octave: 0,
                semitones: -7,
                detune: -15,
            },
            {
                pitch: 0,
                type: "sawtooth",
                gain: 0.9318181818181818,
                octave: 0,
                semitones: 0,
                detune: 24.10714285714286,
            },
        ],
        envelope: {
            attack: 0.003789408531634213,
            decay: 4.941822455971212,
            sustain: 0,
            release: 2.308105833452399,
        },
        filterEnvelope: {
            attack: 0.3377313908791014,
            decay: 0.5179474679231217,
            sustain: 24.107142857142858,
            release: 9.676410537094561,
            intensity: -55,
        },
        filter: {
            frequency: 874.7405861451177,
            resonance: 90,
        },
    },
];

export default presets;
