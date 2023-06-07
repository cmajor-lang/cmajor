//  //
//  //     ,ad888ba,                                88
//  //    d8"'    "8b
//  //   d8            88,dPba,,adPba,   ,adPPYba,  88      The Cmajor Language
//  //   88            88P'  "88"   "8a        '88  88
//  //   Y8,           88     88     88  ,adPPPP88  88      (c)2022 Sound Stacks Ltd
//  //    Y8a.   .a8P  88     88     88  88,   ,88  88      https://cmajor.dev
//  //     '"Y888Y"'   88     88     88  '"8bbP"Y8  88
//  //                                             ,88
//  //                                           888P"


export function getByte0 (message)     { return (message >> 16) & 0xff; }
export function getByte1 (message)     { return (message >> 8) & 0xff; }
export function getByte2 (message)     { return message & 0xff; }

function isVoiceMessage (message, type)     { return ((message >> 16) & 0xf0) == type; }
function get14BitValue (message)            { return getByte1 (message) | (getByte2 (message) << 7); }

export function getChannel0to15 (message)   { return getByte0 (message) & 0x0f; }
export function getChannel1to16 (message)   { return getChannel0to15 (message) + 1; }

export function getMessageSize (message)
{
    const mainGroupLengths = (3 << 0) | (3 << 2) | (3 << 4) | (3 << 6)
                           | (2 << 8) | (2 << 10) | (3 << 12);

    const lastGroupLengths = (1 <<  0) | (2 <<  2) | (3 <<  4) | (2 <<  6)
                           | (1 <<  8) | (1 << 10) | (1 << 12) | (1 << 14)
                           | (1 << 16) | (1 << 18) | (1 << 20) | (1 << 22)
                           | (1 << 24) | (1 << 26) | (1 << 28) | (1 << 30);

    const firstByte = getByte0 (message);
    const group = (firstByte >> 4) & 7;

    return (group != 7 ? (mainGroupLengths >> (2 * group))
                       : (lastGroupLengths >> (2 * (firstByte & 15)))) & 3;
}

export function isNoteOn  (message)                         { return isVoiceMessage (message, 0x90) && getVelocity (message) != 0; }
export function isNoteOff (message)                         { return isVoiceMessage (message, 0x80) || (isVoiceMessage (message, 0x90) && getVelocity (message) == 0); }

export function getNoteNumber (message)                     { return getByte1 (message); }
export function getVelocity (message)                       { return getByte2 (message); }

export function isProgramChange (message)                   { return isVoiceMessage (message, 0xc0); }
export function getProgramChangeNumber (message)            { return getByte1 (message); }
export function isPitchWheel (message)                      { return isVoiceMessage (message, 0xe0); }
export function getPitchWheelValue (message)                { return get14BitValue (message); }
export function isAftertouch (message)                      { return isVoiceMessage (message, 0xa0); }
export function getAfterTouchValue (message)                { return getByte2 (message); }
export function isChannelPressure (message)                 { return isVoiceMessage (message, 0xd0); }
export function getChannelPressureValue (message)           { return getByte1 (message); }
export function isController (message)                      { return isVoiceMessage (message, 0xb0); }
export function getControllerNumber (message)               { return getByte1 (message); }
export function getControllerValue (message)                { return getByte2 (message); }
export function isControllerNumber (message, number)        { return getByte1 (message) == number && isController (message); }
export function isAllNotesOff (message)                     { return isControllerNumber (message, 123); }
export function isAllSoundOff (message)                     { return isControllerNumber (message, 120); }
export function isQuarterFrame (message)                    { return getByte0 (message) == 0xf1; }
export function isClock (message)                           { return getByte0 (message) == 0xf8; }
export function isStart (message)                           { return getByte0 (message) == 0xfa; }
export function isContinue (message)                        { return getByte0 (message) == 0xfb; }
export function isStop (message)                            { return getByte0 (message) == 0xfc; }
export function isActiveSense (message)                     { return getByte0 (message) == 0xfe; }
export function isMetaEvent (message)                       { return getByte0 (message) == 0xff; }
export function isSongPositionPointer (message)             { return getByte0 (message) == 0xf2; }
export function getSongPositionPointerValue (message)       { return get14BitValue (message); }

export function getChromaticScaleIndex (note)               { return (note % 12) & 0xf; }
export function getOctaveNumber (note, octaveForMiddleC)    { return ((Math.floor (note / 12) + (octaveForMiddleC ? octaveForMiddleC : 3)) & 0xff) - 5; }
export function getNoteName (note)                          { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithSharps (note)                { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithFlats (note)                 { const names = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }
export function getNoteNameWithOctaveNumber (note)          { return getNoteName (note) + getOctaveNumber (note); }
export function isNatural (note)                            { const nats = [true, false, true, false, true, true, false, true, false, true, false, true]; return nats[getChromaticScaleIndex (note)]; }
export function isAccidental (note)                         { return ! isNatural (note); }

export function printHexMIDIData (message)
{
    const numBytes = getMessageSize (message);

    if (numBytes == 0)
        return "[empty]";

    let s = "";

    for (let i = 0; i < numBytes; ++i)
    {
        if (i != 0)  s += ' ';

        const byte = message >> (16 - 8 * i) & 0xff;
        s += "0123456789abcdef"[byte >> 4];
        s += "0123456789abcdef"[byte & 15];
    }

    return s;
}

export function getMIDIDescription (message)
{
    const channelText = " Channel " + getChannel1to16 (message);
    function getNote (m)   { const s = getNoteNameWithOctaveNumber (getNoteNumber (message)); return s.length < 4 ? s + " " : s; };

    if (isNoteOn (message))                return "Note-On:  "   + getNote (message) + channelText + "  Velocity " + getVelocity (message);
    if (isNoteOff (message))               return "Note-Off: "   + getNote (message) + channelText + "  Velocity " + getVelocity (message);
    if (isAftertouch (message))            return "Aftertouch: " + getNote (message) + channelText +  ": " + getAfterTouchValue (message);
    if (isPitchWheel (message))            return "Pitch wheel: " + getPitchWheelValue (message) + ' ' + channelText;
    if (isChannelPressure (message))       return "Channel pressure: " + getChannelPressureValue (message) + ' ' + channelText;
    if (isController (message))            return "Controller:" + channelText + ": " + getControllerName (getControllerNumber (message)) + " = " + getControllerValue (message);
    if (isProgramChange (message))         return "Program change: " + getProgramChangeNumber (message) + ' ' + channelText;
    if (isAllNotesOff (message))           return "All notes off:" + channelText;
    if (isAllSoundOff (message))           return "All sound off:" + channelText;
    if (isQuarterFrame (message))          return "Quarter-frame";
    if (isClock (message))                 return "Clock";
    if (isStart (message))                 return "Start";
    if (isContinue (message))              return "Continue";
    if (isStop (message))                  return "Stop";
    if (isMetaEvent (message))             return "Meta-event: type " + getByte1 (message);
    if (isSongPositionPointer (message))   return "Song Position: " + getSongPositionPointerValue (message);

    return printHexMIDIData (message);
}

export function getControllerName (controllerNumber)
{
    if (controllerNumber < 128)
    {
        const controllerNames = [
            "Bank Select",                  "Modulation Wheel (coarse)",      "Breath controller (coarse)",       undefined,
            "Foot Pedal (coarse)",          "Portamento Time (coarse)",       "Data Entry (coarse)",              "Volume (coarse)",
            "Balance (coarse)",             undefined,                        "Pan position (coarse)",            "Expression (coarse)",
            "Effect Control 1 (coarse)",    "Effect Control 2 (coarse)",      undefined,                          undefined,
            "General Purpose Slider 1",     "General Purpose Slider 2",       "General Purpose Slider 3",         "General Purpose Slider 4",
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            "Bank Select (fine)",           "Modulation Wheel (fine)",        "Breath controller (fine)",         undefined,
            "Foot Pedal (fine)",            "Portamento Time (fine)",         "Data Entry (fine)",                "Volume (fine)",
            "Balance (fine)",               undefined,                        "Pan position (fine)",              "Expression (fine)",
            "Effect Control 1 (fine)",      "Effect Control 2 (fine)",        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            "Hold Pedal",                   "Portamento",                     "Sustenuto Pedal",                  "Soft Pedal",
            "Legato Pedal",                 "Hold 2 Pedal",                   "Sound Variation",                  "Sound Timbre",
            "Sound Release Time",           "Sound Attack Time",              "Sound Brightness",                 "Sound Control 6",
            "Sound Control 7",              "Sound Control 8",                "Sound Control 9",                  "Sound Control 10",
            "General Purpose Button 1",     "General Purpose Button 2",       "General Purpose Button 3",         "General Purpose Button 4",
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          "Reverb Level",
            "Tremolo Level",                "Chorus Level",                   "Celeste Level",                    "Phaser Level",
            "Data Button increment",        "Data Button decrement",          "Non-registered Parameter (fine)",  "Non-registered Parameter (coarse)",
            "Registered Parameter (fine)",  "Registered Parameter (coarse)",  undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            undefined,                      undefined,                        undefined,                          undefined,
            "All Sound Off",                "All Controllers Off",            "Local Keyboard",                   "All Notes Off",
            "Omni Mode Off",                "Omni Mode On",                   "Mono Operation",                   "Poly Operation"
        ];

        const name = controllerNames[controllerNumber];

        if (name)
            return name;
    }

    return controllerNumber.toString();
}
