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
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

/*
    A WAV (RIFF/WAVE) file writer, for saving recordings. No DOM, no third-party code.

    Formats ("format" option):
      "pcm16" | "pcm24" | "pcm32"   integer PCM. Samples are clipped to [-1, 1] and scaled
                                    symmetrically by 2^(bits-1) - 1, then rounded to the nearest
                                    integer, so +1 -> 32767 and -1 -> -32767 (the most negative
                                    code, -32768, is never written). Readers divide by 2^(bits-1),
                                    so a full-scale sample reads back at 1 - 2^-(bits-1).
                                    NaN is written as 0.
      "float32"                     IEEE float, copied bit for bit (no clipping: out-of-range
                                    values, infinities and NaNs are kept).

    Header layout follows Microsoft's rules (mmreg.h / "Multiple channel audio data and WAVE files"):
      - WAVE_FORMAT_EXTENSIBLE (0xFFFE, 40-byte fmt) when there are more than 2 channels, or the
        samples are integers deeper than 16 bits (pcm24, pcm32). Its sub-format GUID is
        KSDATAFORMAT_SUBTYPE_PCM or _IEEE_FLOAT, wValidBitsPerSample = the container size, and
        dwChannelMask comes from getChannelMask().
      - otherwise WAVE_FORMAT_PCM (1, 16-byte fmt) for pcm16, or WAVE_FORMAT_IEEE_FLOAT
        (3, 18-byte fmt with cbSize = 0) for float32.
      - every non-PCM file (float32, plain or extensible) has a "fact" chunk holding the frame count.
      - the data chunk is padded to an even length (only possible with pcm24).
    The file must fit the RIFF limit (its RIFF size field is 32 bits): bigger ones throw a
    WAVTooLargeError before anything is allocated. RF64 isn't supported.

    API:
      encodeWAV (channels, sampleRate, { format, dither })  -> ArrayBuffer
          channels: Float32Array[], one per channel, all the same length.

      encodeWAVFromBlocks (source, sampleRate, { format, dither })  -> ArrayBuffer
          The same file, from audio held in blocks (as the recorder keeps it), written straight
          into one preallocated buffer without first joining the blocks. source is a WAVBlockSource:
            {
              numChannels,              // >= 1
              numFrames,                // total frames to write (per channel)
              blockSize,                // frames per block; block i holds frames [i*blockSize, (i+1)*blockSize)
              getBlock (channel, index) // -> Float32Array holding at least the frames of block `index`
                                        //    that are < numFrames (the last block may be shorter
                                        //    than blockSize, or longer: extra frames are ignored)
            }
          blockSourceFromArrays (blocksPerChannel, blockSize, numFrames?) makes one from arrays of
          blocks, e.g. [[ch0block0, ch0block1, ...], [ch1block0, ...]].

      generateWAVParts (source, sampleRate, { format, dither })  -> generator of Uint8Array
          The same bytes again, as the header followed by one part per block (and a pad byte if
          needed), so the whole file never has to be in one buffer: new Blob ([...generateWAVParts (...)],
          { type: "audio/wav" }) gives a Blob that HostBridge.saveFile() accepts.

      getWAVFileSize (numChannels, numFrames, format)  -> bytes (throws WAVTooLargeError if > the limit)
      getWAVHeaderSize (numChannels, format)           -> bytes before the first sample
      getChannelMask (numChannels)                      -> dwChannelMask used for extensible files

    Options:
      format    one of wavFormats (default "pcm16")
      dither    true adds TPDF dither of +-1 LSB (the sum of two uniform randoms) before rounding.
                pcm16 only (ignored for other formats); off by default. random (optional) replaces
                Math.random, for repeatable tests.
*/

export const wavFormats = ["pcm16", "pcm24", "pcm32", "float32"];

/// The largest file the RIFF format can describe: its size field (the file's length minus 8) is a uint32
export const maxWAVFileSize = 0xffffffff + 8;

export class WAVTooLargeError extends Error
{
    constructor (fileSize)
    {
        super (`This recording would make a ${(fileSize / 2 ** 30).toFixed (2)} GB WAV file, but WAV files can't be bigger than 4 GB. `
                 + "Save a shorter selection, fewer channels or a smaller sample format.");
        this.name = "WAVTooLargeError";
        this.fileSize = fileSize;
    }
}

const formatTag = { pcm: 1, float: 3, extensible: 0xfffe };

/// The speaker masks of the common layouts (mono, stereo, 3.0, quad, 5.0, 5.1, 6.1, 7.1). Other
/// channel counts get 0, which means "not assigned to speakers".
const channelMasks = [0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3f, 0x13f, 0x63f];

export function getChannelMask (numChannels)
{
    return channelMasks[numChannels] ?? 0;
}

function getFormatInfo (numChannels, format)
{
    if (! wavFormats.includes (format))
        throw new Error (`Unknown WAV format "${format}": expected one of ${wavFormats.join (", ")}`);

    if (! (Number.isInteger (numChannels) && numChannels > 0 && numChannels <= 65535))
        throw new Error ("A WAV file needs between 1 and 65535 channels");

    const isFloat = format === "float32";
    const bitsPerSample = isFloat ? 32 : Number (format.substring (3));
    const bytesPerSample = bitsPerSample / 8;
    const isExtensible = numChannels > 2 || (! isFloat && bitsPerSample > 16);
    const fmtSize = isExtensible ? 40 : (isFloat ? 18 : 16);
    const hasFact = isFloat;
    const headerSize = 12 + (8 + fmtSize) + (hasFact ? 12 : 0) + 8;

    return { isFloat, bitsPerSample, bytesPerSample, isExtensible, fmtSize, hasFact, headerSize,
             blockAlign: bytesPerSample * numChannels };
}

export function getWAVHeaderSize (numChannels, format = "pcm16")
{
    return getFormatInfo (numChannels, format).headerSize;
}

function getSizes (numChannels, numFrames, format)
{
    const info = getFormatInfo (numChannels, format);

    if (! (Number.isInteger (numFrames) && numFrames >= 0))
        throw new Error ("The number of frames must be a whole number >= 0");

    const dataSize = numFrames * info.blockAlign;
    const fileSize = info.headerSize + dataSize + (dataSize & 1);

    if (fileSize > maxWAVFileSize)
        throw new WAVTooLargeError (fileSize);

    return { ...info, numChannels, numFrames, dataSize, fileSize };
}

/// The size in bytes of the file encodeWAV() would make. Throws a WAVTooLargeError if it's over the limit.
export function getWAVFileSize (numChannels, numFrames, format = "pcm16")
{
    return getSizes (numChannels, numFrames, format).fileSize;
}

//==============================================================================
export function encodeWAV (channels, sampleRate, options = {})
{
    if (! Array.isArray (channels) || channels.length === 0)
        throw new Error ("encodeWAV needs an array of one or more Float32Array channels");

    const numFrames = channels[0].length;

    if (channels.some (c => c.length !== numFrames))
        throw new Error ("encodeWAV: all the channels must be the same length");

    return encodeWAVFromBlocks ({ numChannels: channels.length, numFrames, blockSize: Math.max (1, numFrames),
                                  getBlock: (channel) => channels[channel] }, sampleRate, options);
}

export function encodeWAVFromBlocks (source, sampleRate, options = {})
{
    const sizes = getSizes (source.numChannels, source.numFrames, options.format ?? "pcm16");
    const buffer = new ArrayBuffer (sizes.fileSize);
    const view = new DataView (buffer);
    const writer = createSampleWriter (sizes, options);

    writeHeader (view, sizes, sampleRate);
    let offset = sizes.headerSize;

    forEachBlock (source, (blocks, start, count) =>
    {
        writer (view, offset, blocks, start, count);
        offset += count * sizes.blockAlign;
    });

    // (the pad byte, if any, is already 0)
    return buffer;
}

export function* generateWAVParts (source, sampleRate, options = {})
{
    const sizes = getSizes (source.numChannels, source.numFrames, options.format ?? "pcm16");
    const writer = createSampleWriter (sizes, options);
    const header = new Uint8Array (sizes.headerSize);

    writeHeader (new DataView (header.buffer), sizes, sampleRate);
    yield header;

    const parts = [];
    forEachBlock (source, (blocks, start, count) => parts.push ([blocks, start, count]));

    for (const [blocks, start, count] of parts)
    {
        const part = new Uint8Array (count * sizes.blockAlign);
        writer (new DataView (part.buffer), 0, blocks, start, count);
        yield part;
    }

    if (sizes.dataSize & 1)
        yield new Uint8Array (1);
}

/// Makes a WAVBlockSource from arrays of blocks: blocksPerChannel[channel][blockIndex] is a Float32Array.
/// numFrames defaults to the length of the first channel's blocks added up.
export function blockSourceFromArrays (blocksPerChannel, blockSize, numFrames)
{
    return {
        numChannels: blocksPerChannel.length,
        numFrames: numFrames ?? blocksPerChannel[0].reduce ((total, b) => total + b.length, 0),
        blockSize,
        getBlock: (channel, index) => blocksPerChannel[channel][index]
    };
}

//==============================================================================
function writeFourCC (view, offset, text)
{
    for (let i = 0; i < 4; ++i)
        view.setUint8 (offset + i, text.charCodeAt (i));
}

// KSDATAFORMAT_SUBTYPE_PCM is 00000001-0000-0010-8000-00aa00389b71, and _IEEE_FLOAT is the same
// with 3 at the start: as a GUID struct, a little-endian uint32 tag followed by these 12 bytes
const subFormatGUIDTail = [0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71];

function writeHeader (view, sizes, sampleRate)
{
    if (! (Number.isInteger (sampleRate) && sampleRate > 0 && sampleRate <= 0xffffffff))
        throw new Error ("The WAV sample rate must be a whole number of Hz");

    const baseTag = sizes.isFloat ? formatTag.float : formatTag.pcm;

    writeFourCC (view, 0, "RIFF");
    view.setUint32 (4, sizes.fileSize - 8, true);
    writeFourCC (view, 8, "WAVE");

    writeFourCC (view, 12, "fmt ");
    view.setUint32 (16, sizes.fmtSize, true);
    view.setUint16 (20, sizes.isExtensible ? formatTag.extensible : baseTag, true);
    view.setUint16 (22, sizes.numChannels, true);
    view.setUint32 (24, sampleRate, true);
    view.setUint32 (28, sampleRate * sizes.blockAlign, true);
    view.setUint16 (32, sizes.blockAlign, true);
    view.setUint16 (34, sizes.bitsPerSample, true);

    let offset = 36;

    if (sizes.isExtensible)
    {
        view.setUint16 (36, 22, true);                                      // cbSize
        view.setUint16 (38, sizes.bitsPerSample, true);                     // wValidBitsPerSample
        view.setUint32 (40, getChannelMask (sizes.numChannels), true);      // dwChannelMask
        view.setUint16 (44, baseTag, true);                                 // SubFormat GUID
        subFormatGUIDTail.forEach ((b, i) => view.setUint8 (46 + i, b));
        offset = 60;
    }
    else if (sizes.fmtSize === 18)
    {
        view.setUint16 (36, 0, true);                                       // cbSize
        offset = 38;
    }

    if (sizes.hasFact)
    {
        writeFourCC (view, offset, "fact");
        view.setUint32 (offset + 4, 4, true);
        view.setUint32 (offset + 8, sizes.numFrames, true);
        offset += 12;
    }

    writeFourCC (view, offset, "data");
    view.setUint32 (offset + 4, sizes.dataSize, true);
}

/// Calls fn (blocksForEachChannel, startFrameInBlock, numFrames) for each block in turn
function forEachBlock (source, fn)
{
    const { numChannels, numFrames, blockSize } = source;

    if (! (Number.isInteger (blockSize) && blockSize > 0))
        throw new Error ("A WAV block source needs a blockSize > 0");

    for (let index = 0, start = 0; start < numFrames; ++index, start += blockSize)
    {
        const count = Math.min (blockSize, numFrames - start);
        const blocks = [];

        for (let channel = 0; channel < numChannels; ++channel)
        {
            const block = source.getBlock (channel, index);

            if (! (block instanceof Float32Array) || block.length < count)
                throw new Error (`WAV block source: block ${index} of channel ${channel} should be a Float32Array of at least ${count} frames`);

            blocks.push (block);
        }

        fn (blocks, 0, count);
    }
}

/// Returns writer (view, byteOffset, blocks, start, count), which interleaves frames
/// [start, start + count) of the blocks into the view
function createSampleWriter (sizes, options)
{
    const { numChannels, bytesPerSample, blockAlign } = sizes;

    if (sizes.isFloat)
    {
        return (view, byteOffset, blocks, start, count) =>
        {
            for (let channel = 0; channel < numChannels; ++channel)
            {
                // (reading the bits as integers keeps NaN payloads exact)
                const bits = new Uint32Array (blocks[channel].buffer, blocks[channel].byteOffset, blocks[channel].length);

                for (let i = 0, p = byteOffset + channel * 4; i < count; ++i, p += blockAlign)
                    view.setUint32 (p, bits[start + i], true);
            }
        };
    }

    const scale = 2 ** (sizes.bitsPerSample - 1) - 1;
    const useDither = !! options.dither && sizes.bitsPerSample === 16;
    const random = options.random ?? Math.random;

    const quantise = (x) =>
    {
        if (! (x === x))
            return 0;

        let v = (x > 1 ? 1 : (x < -1 ? -1 : x)) * scale;

        if (useDither)
            v += random() - random();

        v = Math.round (v);
        return v > scale ? scale : (v < -scale ? -scale : v);
    };

    return (view, byteOffset, blocks, start, count) =>
    {
        for (let channel = 0; channel < numChannels; ++channel)
        {
            const block = blocks[channel];
            let p = byteOffset + channel * bytesPerSample;

            if (bytesPerSample === 2)
            {
                for (let i = 0; i < count; ++i, p += blockAlign)
                    view.setInt16 (p, quantise (block[start + i]), true);
            }
            else if (bytesPerSample === 3)
            {
                for (let i = 0; i < count; ++i, p += blockAlign)
                {
                    const v = quantise (block[start + i]);
                    view.setUint16 (p, v & 0xffff, true);
                    view.setInt8 (p + 2, v >> 16);
                }
            }
            else
            {
                for (let i = 0; i < count; ++i, p += blockAlign)
                    view.setInt32 (p, quantise (block[start + i]), true);
            }
        }
    };
}
