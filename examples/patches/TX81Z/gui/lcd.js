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
    A 16x2 character dot-matrix LCD, like the backlit HD44780-style display on the TX81Z.

    Each character cell is 5x8 dots: rows 0-6 hold the glyph and row 7 is where the underline
    cursor goes. Characters 0-7 are user-definable glyphs (the HD44780's CGRAM), which the GUI
    uses for bar graphs and waveform pictures.
*/

// Each glyph is 7 rows of 5 dots, top row first
const font =
{
    " ": "00000 00000 00000 00000 00000 00000 00000",
    "!": "00100 00100 00100 00100 00000 00000 00100",
    '"': "01010 01010 01010 00000 00000 00000 00000",
    "#": "01010 01010 11111 01010 11111 01010 01010",
    "$": "00100 01111 10100 01110 00101 11110 00100",
    "%": "11000 11001 00010 00100 01000 10011 00011",
    "&": "01100 10010 10100 01000 10101 10010 01101",
    "'": "01100 00100 01000 00000 00000 00000 00000",
    "(": "00010 00100 01000 01000 01000 00100 00010",
    ")": "01000 00100 00010 00010 00010 00100 01000",
    "*": "00000 00100 10101 01110 10101 00100 00000",
    "+": "00000 00100 00100 11111 00100 00100 00000",
    ",": "00000 00000 00000 00000 01100 00100 01000",
    "-": "00000 00000 00000 11111 00000 00000 00000",
    ".": "00000 00000 00000 00000 00000 01100 01100",
    "/": "00000 00001 00010 00100 01000 10000 00000",
    "0": "01110 10001 10011 10101 11001 10001 01110",
    "1": "00100 01100 00100 00100 00100 00100 01110",
    "2": "01110 10001 00001 00010 00100 01000 11111",
    "3": "11111 00010 00100 00010 00001 10001 01110",
    "4": "00010 00110 01010 10010 11111 00010 00010",
    "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110",
    "7": "11111 00001 00010 00100 01000 01000 01000",
    "8": "01110 10001 10001 01110 10001 10001 01110",
    "9": "01110 10001 10001 01111 00001 00010 01100",
    ":": "00000 01100 01100 00000 01100 01100 00000",
    ";": "00000 01100 01100 00000 01100 00100 01000",
    "<": "00010 00100 01000 10000 01000 00100 00010",
    "=": "00000 00000 11111 00000 11111 00000 00000",
    ">": "01000 00100 00010 00001 00010 00100 01000",
    "?": "01110 10001 00001 00010 00100 00000 00100",
    "@": "01110 10001 00001 01101 10101 10101 01110",
    "A": "01110 10001 10001 10001 11111 10001 10001",
    "B": "11110 10001 10001 11110 10001 10001 11110",
    "C": "01110 10001 10000 10000 10000 10001 01110",
    "D": "11100 10010 10001 10001 10001 10010 11100",
    "E": "11111 10000 10000 11110 10000 10000 11111",
    "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111",
    "H": "10001 10001 10001 11111 10001 10001 10001",
    "I": "01110 00100 00100 00100 00100 00100 01110",
    "J": "00111 00010 00010 00010 00010 10010 01100",
    "K": "10001 10010 10100 11000 10100 10010 10001",
    "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001",
    "N": "10001 10001 11001 10101 10011 10001 10001",
    "O": "01110 10001 10001 10001 10001 10001 01110",
    "P": "11110 10001 10001 11110 10000 10000 10000",
    "Q": "01110 10001 10001 10001 10101 10010 01101",
    "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110",
    "T": "11111 00100 00100 00100 00100 00100 00100",
    "U": "10001 10001 10001 10001 10001 10001 01110",
    "V": "10001 10001 10001 10001 10001 01010 00100",
    "W": "10001 10001 10001 10101 10101 10101 01010",
    "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 10001 01010 00100 00100 00100",
    "Z": "11111 00001 00010 00100 01000 10000 11111",
    "[": "01110 01000 01000 01000 01000 01000 01110",
    "\\":"00000 10000 01000 00100 00010 00001 00000",
    "]": "01110 00010 00010 00010 00010 00010 01110",
    "^": "00100 01010 10001 00000 00000 00000 00000",
    "_": "00000 00000 00000 00000 00000 00000 11111",
    "`": "01000 00100 00010 00000 00000 00000 00000",
    "a": "00000 00000 01110 00001 01111 10001 01111",
    "b": "10000 10000 10110 11001 10001 10001 11110",
    "c": "00000 00000 01110 10000 10000 10001 01110",
    "d": "00001 00001 01101 10011 10001 10001 01111",
    "e": "00000 00000 01110 10001 11111 10000 01110",
    "f": "00110 01001 01000 11100 01000 01000 01000",
    "g": "00000 01111 10001 10001 01111 00001 01110",
    "h": "10000 10000 10110 11001 10001 10001 10001",
    "i": "00100 00000 01100 00100 00100 00100 01110",
    "j": "00010 00000 00110 00010 00010 10010 01100",
    "k": "10000 10000 10010 10100 11000 10100 10010",
    "l": "01100 00100 00100 00100 00100 00100 01110",
    "m": "00000 00000 11010 10101 10101 10001 10001",
    "n": "00000 00000 10110 11001 10001 10001 10001",
    "o": "00000 00000 01110 10001 10001 10001 01110",
    "p": "00000 00000 11110 10001 11110 10000 10000",
    "q": "00000 00000 01101 10011 01111 00001 00001",
    "r": "00000 00000 10110 11001 10000 10000 10000",
    "s": "00000 00000 01110 10000 01110 00001 11110",
    "t": "01000 01000 11100 01000 01000 01001 00110",
    "u": "00000 00000 10001 10001 10001 10011 01101",
    "v": "00000 00000 10001 10001 10001 01010 00100",
    "w": "00000 00000 10001 10001 10101 10101 01010",
    "x": "00000 00000 10001 01010 00100 01010 10001",
    "y": "00000 00000 10001 10001 01111 00001 01110",
    "z": "00000 00000 11111 00010 00100 01000 11111",
    "{": "00010 00100 00100 01000 00100 00100 00010",
    "|": "00100 00100 00100 00100 00100 00100 00100",
    "}": "01000 00100 00100 00010 00100 00100 01000",
    "~": "00000 00000 00000 01101 10010 00000 00000",
};

const glyphs = new Map (Object.entries (font).map (([ch, rows]) =>
    [ch, [...rows.split (" ").map (r => parseInt (r, 2)), 0]]));

/** The characters that the LCD can show, e.g. for limiting what can be typed into a name. */
export function isDisplayableCharacter (ch)     { return glyphs.has (ch); }

//==============================================================================
export class DotMatrixLCD
{
    constructor (canvas, { columns = 16, rows = 2, dotPitch = 3.4, oversampling = 3 } = {})
    {
        this.canvas = canvas;
        this.columns = columns;
        this.rows = rows;
        this.lines = new Array (rows).fill ("");
        this.customGlyphs = new Array (8).fill (null).map (() => new Array (8).fill (0));
        this.cursor = null;
        this.cursorVisible = false;

        this.dotPitch = dotPitch;
        this.dotSize = dotPitch * 0.86;
        this.charGap = dotPitch;
        this.lineGap = dotPitch * 2;
        this.margin = dotPitch * 3;

        this.width  = 2 * this.margin + columns * 5 * dotPitch + (columns - 1) * this.charGap;
        this.height = 2 * this.margin + rows * 8 * dotPitch + (rows - 1) * this.lineGap;

        canvas.style.width  = this.width + "px";
        canvas.style.height = this.height + "px";

        // The whole GUI gets scaled up and down with CSS, so draw at a higher resolution
        // than the canvas's nominal size to keep the dots crisp
        const scale = oversampling * (window.devicePixelRatio || 1);
        canvas.width  = Math.ceil (this.width * scale);
        canvas.height = Math.ceil (this.height * scale);
        this.context = canvas.getContext ("2d");
        this.context.scale (scale, scale);

        this.paint();
    }

    dispose()
    {
        clearInterval (this.cursorTimer);
    }

    /** Sets one of the eight user-definable glyphs (character codes 0 to 7) to an array of 8 rows. */
    setCustomGlyph (index, rows)
    {
        this.customGlyphs[index] = rows.slice (0, 8);
    }

    setText (...lines)
    {
        for (let i = 0; i < this.rows; ++i)
            this.lines[i] = (lines[i] ?? "").padEnd (this.columns).substring (0, this.columns);

        this.paint();
    }

    /** Shows a blinking underline cursor, or hides it if `position` is null. */
    setCursor (position)
    {
        clearInterval (this.cursorTimer);
        this.cursor = position;
        this.cursorVisible = !! position;

        if (position)
        {
            this.cursorTimer = setInterval (() =>
            {
                this.cursorVisible = ! this.cursorVisible;
                this.paint();
            }, 450);
        }

        this.paint();
    }

    //==============================================================================
    getGlyph (ch)
    {
        const code = ch.charCodeAt (0);

        if (code < 8)
            return this.customGlyphs[code];

        return glyphs.get (ch) ?? glyphs.get ("?");
    }

    paint()
    {
        const c = this.context;
        const { width, height, dotPitch, dotSize } = this;

        // The backlight: brightest in the middle, falling off towards the edges
        const backlight = c.createRadialGradient (width / 2, height / 2, 10, width / 2, height / 2, width * 0.6);
        backlight.addColorStop (0, "#c4d84a");
        backlight.addColorStop (1, "#97b02a");
        c.fillStyle = backlight;
        c.fillRect (0, 0, width, height);

        const unlit = "rgba(70, 95, 10, 0.13)";
        const lit = "#1c2a0c";
        const shadow = "rgba(40, 60, 0, 0.22)";

        const forEachDot = (callback) =>
        {
            for (let row = 0; row < this.rows; ++row)
            {
                for (let col = 0; col < this.columns; ++col)
                {
                    const glyph = this.getGlyph (this.lines[row][col] ?? " ");
                    const showCursor = this.cursorVisible && this.cursor?.row === row && this.cursor?.column === col;
                    const x0 = this.margin + col * (5 * dotPitch + this.charGap);
                    const y0 = this.margin + row * (8 * dotPitch + this.lineGap);

                    for (let y = 0; y < 8; ++y)
                    {
                        const bits = (y === 7 && showCursor) ? 0x1f : glyph[y];

                        for (let x = 0; x < 5; ++x)
                            callback (x0 + x * dotPitch, y0 + y * dotPitch, (bits >> (4 - x)) & 1);
                    }
                }
            }
        };

        // The liquid crystal layer sits a little above the backlight, so lit dots cast a faint
        // shadow down and to the right; and the unlit dots are just about visible
        forEachDot ((x, y, on) =>
        {
            c.fillStyle = on ? shadow : unlit;
            c.fillRect (x + (on ? 0.7 : 0), y + (on ? 0.8 : 0), dotSize, dotSize);
        });

        c.fillStyle = lit;
        forEachDot ((x, y, on) => { if (on) c.fillRect (x, y, dotSize, dotSize); });
    }
}
