//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

/*
    The patch's parameters, as the GUI sees them: what each one is called on the panel and on
    the LCD, and how its value is written on the LCD.

    The min/max/step/init values here are only fallbacks: when the patch's status arrives,
    updateFromStatus() replaces them with the real annotations from TX81Z.cmajor.
*/

export const operatorCount = 4;

const signed = (v) => (v > 0 ? "+" : "") + v;

//==============================================================================
// lcd:   the parameter's name on the LCD, at most 12 characters (the LCD's top line
//        has "OPn " in front of it for an operator parameter)
// label: the name printed on the panel

const globalParameters =
[
    { id: "volumeIn",      lcd: "Master Volume", label: "VOLUME",    min: -48, max: 0,  step: 1, init: -6,  format: v => v + "dB" },
    { id: "pitchOffsetIn", lcd: "Transpose",     label: "TRANSPOSE", min: -24, max: 24, step: 1, init: -12, format: v => signed (v), bipolar: true },
    { id: "algorithmIn",   lcd: "Algorithm",     label: "ALGORITHM", min: 1,   max: 8,  step: 1, init: 2,   format: v => "ALG " + v },
];

const operatorParameterTemplates =
[
    { suffix: "WaveshapeIn",       lcd: "Waveform",     label: "WAVE",     min: 1, max: 8,    step: 1, isWave: true, format: v => "W" + v },
    { suffix: "FreqMultiplierIn",  lcd: "Freq Ratio",   label: "RATIO",    min: 0, max: 16,   step: 0,    format: v => v.toFixed (2) },
    { suffix: "FixedIn",           lcd: "Fixed Freq",   label: "FIXED",    min: 0, max: 1000, step: 0,    format: formatFixedFrequency },
    { suffix: "FeedbackIn",        lcd: "Feedback",     label: "FEEDBACK", min: 0, max: 7,    step: 1 },
    { suffix: "KVSIn",             lcd: "Key Vel Sens", label: "KVS",      min: 0, max: 7,    step: 1 },
    { suffix: "RateScalingIn",     lcd: "Rate Scaling", label: "RATE SCL", min: 0, max: 3,    step: 1 },
    { suffix: "LevelIn",           lcd: "Out Level",    label: "LEVEL",    min: 0, max: 99,   step: 1 },
    { suffix: "AttackRateIn",      lcd: "Attack Rate",  label: "AR",       min: 0, max: 31,   step: 1 },
    { suffix: "DecayRate1In",      lcd: "Decay1 Rate",  label: "D1R",      min: 0, max: 31,   step: 1 },
    { suffix: "DecayLevelIn",      lcd: "Decay1 Level", label: "D1L",      min: 0, max: 15,   step: 1 },
    { suffix: "DecayRate2In",      lcd: "Decay2 Rate",  label: "D2R",      min: 0, max: 31,   step: 1 },
    { suffix: "ReleaseRateIn",     lcd: "Release Rate", label: "RR",       min: 1, max: 15,   step: 1 },
];

function formatFixedFrequency (v)
{
    // The patch adds this to the operator's ratio-based frequency, so zero means "not fixed"
    if (v < 0.05)   return "OFF";
    if (v < 100)    return v.toFixed (1) + "Hz";

    return Math.round (v) + "Hz";
}

//==============================================================================
export function getOperatorParameterID (operator, suffix)
{
    return "osc" + operator + suffix;
}

function createParameters()
{
    const params = globalParameters.map (p => ({ ...p, lcdTitle: p.lcd }));

    for (let op = 1; op <= operatorCount; ++op)
        for (const t of operatorParameterTemplates)
            params.push ({ ...t,
                           id: getOperatorParameterID (op, t.suffix),
                           operator: op,
                           lcdTitle: "OP" + op + " " + t.lcd,
                           init: t.min });

    for (const p of params)
        p.value = p.init;

    return params;
}

export const parameters = createParameters();
export const parametersByID = new Map (parameters.map (p => [p.id, p]));

//==============================================================================
/** Copies the real ranges from the patch's endpoint annotations into our parameter list. */
export function updateFromStatus (status)
{
    for (const input of status?.details?.inputs ?? [])
    {
        const param = parametersByID.get (input.endpointID);
        const annotation = input.annotation;

        if (param && annotation)
        {
            param.min  = annotation.min  ?? param.min;
            param.max  = annotation.max  ?? param.max;
            param.step = annotation.step ?? param.step;
            param.init = annotation.init ?? param.init;
        }
    }
}

export function isStepped (param)             { return param.step > 0; }

export function constrainValue (param, value)
{
    value = Math.min (param.max, Math.max (param.min, value));

    if (isStepped (param))
        value = param.min + Math.round ((value - param.min) / param.step) * param.step;

    return value;
}

export function toNormalised (param, value)   { return param.max > param.min ? (value - param.min) / (param.max - param.min) : 0; }
export function fromNormalised (param, norm)  { return constrainValue (param, param.min + norm * (param.max - param.min)); }

export function formatValue (param, value)
{
    if (param.format)
        return param.format (value);

    return isStepped (param) ? String (Math.round (value)) : value.toFixed (2);
}

//==============================================================================
/** A copy of TX81Z::getWaveshape() from TX81Z.cmajor, used to draw the waveforms. */
export function getWaveshape (waveshape, phase)
{
    const sinOfPhase  = (p) => Math.sin (p * Math.PI * 2);
    const sinOf2Phase = (p) => Math.sin (p * Math.PI * 4);

    phase = phase - Math.floor (phase);

    switch (waveshape)
    {
        case 1:     return sinOfPhase (phase);

        case 2:     if (phase < 0.25)   return sinOfPhase (phase - 0.25) + 1;
                    if (phase < 0.5)    return sinOfPhase (phase + 0.25) + 1;
                    if (phase < 0.75)   return sinOfPhase (phase - 0.25) - 1;
                                        return sinOfPhase (phase + 0.25) - 1;

        case 3:     return phase < 0.5 ? sinOfPhase (phase) : 0;

        case 4:     if (phase < 0.25)   return sinOfPhase (phase - 0.25) + 1;
                    if (phase < 0.5)    return sinOfPhase (phase + 0.25) + 1;
                                        return 0;

        case 5:     return phase < 0.5 ? sinOfPhase (2 * phase) : 0;

        case 6:     if (phase < 0.125)  return sinOf2Phase (phase - 0.125) + 1;
                    if (phase < 0.25)   return sinOf2Phase (phase + 0.125) + 1;
                    if (phase < 0.375)  return sinOf2Phase (phase - 0.125) - 1;
                    if (phase < 0.5)    return sinOf2Phase (phase + 0.125) - 1;
                                        return 0;

        case 7:     if (phase < 0.25)   return  sinOf2Phase (phase);
                    if (phase < 0.5)    return -sinOf2Phase (phase);
                                        return 0;

        case 8:     if (phase < 0.125)  return 1 + sinOf2Phase (phase - 0.125);
                    if (phase < 0.25)   return 1 + sinOf2Phase (phase + 0.125);
                    if (phase < 0.375)  return 1 - sinOf2Phase (phase - 0.125);
                    if (phase < 0.5)    return 1 - sinOf2Phase (phase + 0.125);
                                        return 0;

        default:    return 0;
    }
}
