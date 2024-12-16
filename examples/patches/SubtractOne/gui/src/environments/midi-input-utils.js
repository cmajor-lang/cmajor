export const Event = {
    NOTE_ON: 0x90,
    NOTE_OFF: 0x80,
};

export async function setupGlobalKeyboardAndMIDIListeners (onMessage) {
    const toMIDIMessage = ({status, channel, byte1, byte2}) => {
        return Uint8Array.of (
            ((status & 0xF0) | channel),
            byte1,
            byte2
        );
    };

    window.addEventListener ("keydown", (e) => {
        const midiCode = toMIDINote (e.key);

        if (midiCode === undefined || e.repeat) return;

        onMessage (toMIDIMessage ({
            status: Event.NOTE_ON,
            channel: 0,
            byte1: midiCode,
            byte2: 127,
        }));
    });

    window.addEventListener ("keyup", (e) => {
        const midiCode = toMIDINote (e.key);

        if (midiCode === undefined) return;

        onMessage (toMIDIMessage ({
            status: Event.NOTE_OFF,
            channel: 0,
            byte1: midiCode,
            byte2: 0,
        }));
    });

    try {
        if (! navigator.requestMIDIAccess)
            throw new Error ("Web MIDI API not supported.");

        const midiAccess = await navigator.requestMIDIAccess();

        for (const input of midiAccess.inputs.values()) {
            input.onmidimessage = ({data}) => {
                onMessage (data);
            };
        }
    } catch (e) {
        console.warn (`Could not access your MIDI devices: ${e}`);
    }
}

function toMIDINote (key) {
    switch (key.toLowerCase()) {
        case "a":
            return 48;
        case "w":
            return 49;
        case "s":
            return 50;
        case "e":
            return 51;
        case "d":
            return 52;
        case "f":
            return 53;
        case "t":
            return 54;
        case "g":
            return 55;
        case "y":
            return 56;
        case "h":
            return 57;
        case "u":
            return 58;
        case "j":
            return 59;
        case "k":
            return 60;
        case "o":
            return 61;
        case "l":
            return 62;
        case "p":
            return 63;
        case ";":
            return 64;
        case "Dead":
            return 65;
        default:
            return undefined;
    }
}
