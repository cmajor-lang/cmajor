//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


/** Returns the first byte of a packed MIDI message.
 *  @param {number} message - the packed MIDI message (`(byte0 << 16) | (byte1 << 8) | byte2`)
 *  @returns {number} byte 0 (0–255)
 */
export function getByte0 (message)     { return (message >> 16) & 0xff; }

/** Returns the second byte of a packed MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {number} byte 1 (0–255)
 */
export function getByte1 (message)     { return (message >> 8) & 0xff; }

/** Returns the third byte of a packed MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {number} byte 2 (0–255)
 */
export function getByte2 (message)     { return message & 0xff; }

/** @param {number} message @param {number} type @returns {boolean} */
function isVoiceMessage (message, type)     { return ((message >> 16) & 0xf0) === type; }
/** @param {number} message @returns {number} */
function get14BitValue (message)            { return getByte1 (message) | (getByte2 (message) << 7); }

/** Returns the zero-based MIDI channel (0–15) from a packed MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {number} channel index 0–15
 */
export function getChannel0to15 (message)   { return getByte0 (message) & 0x0f; }

/** Returns the one-based MIDI channel (1–16) from a packed MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {number} channel number 1–16
 */
export function getChannel1to16 (message)   { return getChannel0to15 (message) + 1; }

/** Returns the number of data bytes used by the given MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {number} message size in bytes (1–3), or 0 for an empty/unknown message
 */
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

    return (group !== 7 ? (mainGroupLengths >> (2 * group))
                        : (lastGroupLengths >> (2 * (firstByte & 15)))) & 3;
}

/** Returns true if the message is a Note-On event with non-zero velocity.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isNoteOn  (message)                         { return isVoiceMessage (message, 0x90) && getVelocity (message) !== 0; }

/** Returns true if the message is a Note-Off event (status 0x80, or 0x90 with zero velocity).
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isNoteOff (message)                         { return isVoiceMessage (message, 0x80) || (isVoiceMessage (message, 0x90) && getVelocity (message) === 0); }

/** Returns the MIDI note number from a note message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getNoteNumber (message)                     { return getByte1 (message); }

/** Returns the velocity from a note message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getVelocity (message)                       { return getByte2 (message); }

/** Returns true if the message is a Program Change event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isProgramChange (message)                   { return isVoiceMessage (message, 0xc0); }

/** Returns the program number from a Program Change message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getProgramChangeNumber (message)            { return getByte1 (message); }

/** Returns true if the message is a Pitch Wheel event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isPitchWheel (message)                      { return isVoiceMessage (message, 0xe0); }

/** Returns the 14-bit pitch wheel value from a Pitch Wheel message (0–16383).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getPitchWheelValue (message)                { return get14BitValue (message); }

/** Returns true if the message is a Polyphonic Aftertouch (Key Pressure) event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isAftertouch (message)                      { return isVoiceMessage (message, 0xa0); }

/** Returns the aftertouch pressure value from a Polyphonic Aftertouch message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getAfterTouchValue (message)                { return getByte2 (message); }

/** Returns true if the message is a Channel Pressure (mono aftertouch) event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isChannelPressure (message)                 { return isVoiceMessage (message, 0xd0); }

/** Returns the pressure value from a Channel Pressure message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getChannelPressureValue (message)           { return getByte1 (message); }

/** Returns true if the message is a Control Change (CC) event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isController (message)                      { return isVoiceMessage (message, 0xb0); }

/** Returns the controller number from a Control Change message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getControllerNumber (message)               { return getByte1 (message); }

/** Returns the controller value from a Control Change message (0–127).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getControllerValue (message)                { return getByte2 (message); }

/** Returns true if the message is a Control Change event for the given controller number.
 *  @param {number} message - the packed MIDI message
 *  @param {number} number - the controller number to test against
 *  @returns {boolean}
 */
export function isControllerNumber (message, number)        { return getByte1 (message) === number && isController (message); }

/** Returns true if the message is an All Notes Off controller event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isAllNotesOff (message)                     { return isControllerNumber (message, 123); }

/** Returns true if the message is an All Sound Off controller event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isAllSoundOff (message)                     { return isControllerNumber (message, 120); }

/** Returns true if the message is a MIDI Time Code Quarter Frame event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isQuarterFrame (message)                    { return getByte0 (message) === 0xf1; }

/** Returns true if the message is a MIDI Clock event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isClock (message)                           { return getByte0 (message) === 0xf8; }

/** Returns true if the message is a MIDI Start event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isStart (message)                           { return getByte0 (message) === 0xfa; }

/** Returns true if the message is a MIDI Continue event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isContinue (message)                        { return getByte0 (message) === 0xfb; }

/** Returns true if the message is a MIDI Stop event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isStop (message)                            { return getByte0 (message) === 0xfc; }

/** Returns true if the message is a MIDI Active Sensing event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isActiveSense (message)                     { return getByte0 (message) === 0xfe; }

/** Returns true if the message is a MIDI Meta Event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isMetaEvent (message)                       { return getByte0 (message) === 0xff; }

/** Returns true if the message is a Song Position Pointer event.
 *  @param {number} message - the packed MIDI message
 *  @returns {boolean}
 */
export function isSongPositionPointer (message)             { return getByte0 (message) === 0xf2; }

/** Returns the song position pointer value from a Song Position Pointer message (0–16383).
 *  @param {number} message - the packed MIDI message
 *  @returns {number}
 */
export function getSongPositionPointerValue (message)       { return get14BitValue (message); }

/** Returns the chromatic scale index (0–11) for the given MIDI note number, where 0 = C.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {number}
 */
export function getChromaticScaleIndex (note)               { return (note % 12) & 0xf; }

/** Returns the octave number for the given MIDI note number.
 *  @param {number} note - MIDI note number (0–127)
 *  @param {number} [octaveForMiddleC] - octave number to assign to middle C (default 3)
 *  @returns {number}
 */
export function getOctaveNumber (note, octaveForMiddleC)    { return ((Math.floor (note / 12) + (octaveForMiddleC ? octaveForMiddleC : 3)) & 0xff) - 5; }

/** Returns the note name (using sharps where applicable) for the given MIDI note number.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {string} e.g. `"C"`, `"C#"`, `"D"`, …
 */
export function getNoteName (note)                          { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }

/** Returns the note name using sharp notation for the given MIDI note number.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {string} e.g. `"C#"`, `"G#"`, …
 */
export function getNoteNameWithSharps (note)                { const names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }

/** Returns the note name using flat notation for the given MIDI note number.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {string} e.g. `"Db"`, `"Gb"`, …
 */
export function getNoteNameWithFlats (note)                 { const names = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]; return names[getChromaticScaleIndex (note)]; }

/** Returns the note name with octave number for the given MIDI note number, e.g. `"C4"`.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {string}
 */
export function getNoteNameWithOctaveNumber (note)          { return getNoteName (note) + getOctaveNumber (note); }

/** Returns true if the given MIDI note number is a natural (white) key.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {boolean}
 */
export function isNatural (note)                            { const nats = [true, false, true, false, true, true, false, true, false, true, false, true]; return nats[getChromaticScaleIndex (note)]; }

/** Returns true if the given MIDI note number is an accidental (black) key.
 *  @param {number} note - MIDI note number (0–127)
 *  @returns {boolean}
 */
export function isAccidental (note)                         { return ! isNatural (note); }

/** Returns a hex string representation of the bytes in the given MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {string} e.g. `"90 3c 64"`, or `"[empty]"` for an empty message
 */
export function printHexMIDIData (message)
{
    const numBytes = getMessageSize (message);

    if (numBytes === 0)
        return "[empty]";

    let s = "";

    for (let i = 0; i < numBytes; ++i)
    {
        if (i !== 0)  s += ' ';

        const byte = message >> (16 - 8 * i) & 0xff;
        s += "0123456789abcdef"[byte >> 4];
        s += "0123456789abcdef"[byte & 15];
    }

    return s;
}

/** Returns a human-readable description of the given MIDI message.
 *  @param {number} message - the packed MIDI message
 *  @returns {string}
 */
export function getMIDIDescription (message)
{
    const channelText = " Channel " + getChannel1to16 (message);
    const getNote = /** @param {number} m @returns {string} */ (m) => { const s = getNoteNameWithOctaveNumber (getNoteNumber (m)); return s.length < 4 ? s + " " : s; };

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

/** Returns the name of the given MIDI controller number as a human-readable string.
 *  Returns the controller number as a string if there is no standard name for it.
 *  @param {number} controllerNumber - MIDI controller number (0–127)
 *  @returns {string}
 */
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
