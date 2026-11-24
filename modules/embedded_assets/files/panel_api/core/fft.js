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

/// The smallest and largest FFT sizes supported (both powers of two)
export const minFFTSize = 32;
export const maxFFTSize = 32768;

/// The level that −Infinity dB (and anything quieter) is clamped to
export const spectrumFloorDB = -200;

/*
    A small FFT and spectrum analyser, for the panel's spectrum display. No DOM, no third-party code.

    - fft (re, im, inverse) is an in-place iterative radix-2 complex FFT over Float32Array or
      Float64Array pairs, for power-of-two sizes from minFFTSize to maxFFTSize. The forward
      transform is unscaled (X[k] = Σ x[n]·e^(−2πikn/N)); the inverse one divides by N, so
      fft (re, im) then fft (re, im, true) gives the input back. The bit-reversal and twiddle
      tables are made once per size and cached.

    - getHannWindow (size) is the periodic Hann window (cached per size: don't modify it). Its
      coherent gain (sum / N) is exactly 0.5.

    - RealSpectrum (size) turns a real frame into a one-sided spectrum of size / 2 + 1 bins
      (0 = DC, size / 2 = Nyquist). It windows the frame and corrects for the window's coherent
      gain and for the one-sided spectrum, so the numbers are AMPLITUDES: a sine of amplitude A
      centred on a bin reads A at that bin (1.0 = 0 dBFS for a full-scale sine), and a DC offset
      of A reads A at bin 0. (A sine between two bins reads up to ~1.42 dB low: the Hann
      window's scalloping loss.) It preallocates its buffers, so it doesn't allocate per frame.

    - SpectrumAnalyser keeps the last fftSize samples of each channel in a ring, and update()
      computes the dB spectrum from them, with exponential averaging and peak hold:

        const analyser = new SpectrumAnalyser ({ fftSize: 4096, sampleRate: 48000, numChannels: 2,
                                                 averagingTime: 0.1, peakDecayDBPerSecond: 20 });
        analyser.addBlock ([left, right]);      // any number of frames, as often as they arrive
        analyser.update (dt);                   // e.g. from the frame scheduler
        draw (analyser.levelsDB, analyser.peaksDB, analyser.getBinFrequency (k));

      - addBlock() accepts blocks of any length (longer than fftSize too: only the last fftSize
        frames are kept); a block with fewer channels than numChannels counts the rest as silent,
        and one with more grows numChannels.
      - Channels: channelMode "average" (the default) averages the channels' power, so a mono
        signal on both channels reads the same as on one; a number picks that channel.
      - Averaging is done on power, across updates: averagingAlpha (0 < α ≤ 1, the weight of the
        newest frame, per update) or averagingTime (a time constant in seconds: α = 1 − e^(−dt/τ)).
        Both 0 (the default) means no averaging. reset() starts again from the next frame.
      - Peak hold: each bin's peak falls by peakDecayDBPerSecond × dt, but never below its level
        (Infinity = no hold, 0 = hold for ever until resetPeaks()).
      - update (dt) runs the FFT whether or not new samples have arrived (it returns whether
        any had); dt defaults to the time those new samples cover, at sampleRate.
      - levelsDB, peaksDB and currentDB (this update's unaveraged frame) are Float32Arrays of
        binCount values, clamped to floorDB; they're overwritten by each update(), so copy them
        to keep them.
*/

//==============================================================================
/// True if size is a power of two from minFFTSize to maxFFTSize
export function isValidFFTSize (size)
{
    return Number.isInteger (size) && size >= minFFTSize && size <= maxFFTSize && (size & (size - 1)) === 0;
}

/// In-place complex FFT of re/im (Float32Array or Float64Array, the same power-of-two length).
/// The forward transform is unscaled; inverse = true runs the inverse, scaled by 1 / N.
export function fft (re, im, inverse = false)
{
    const size = re.length;

    if (im.length !== size)
        throw new Error ("fft: re and im must be the same length");

    const tables = getFFTTables (size);
    const bitReversed = tables.bitReversed;

    for (let i = 0; i < size; ++i)
    {
        const j = bitReversed[i];

        if (j > i)
        {
            let t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    const cosTable = tables.cos, sinTable = tables.sin;
    const sign = inverse ? -1 : 1;

    for (let blockSize = 2; blockSize <= size; blockSize <<= 1)
    {
        const half = blockSize >> 1;
        const tableStep = size / blockSize;

        for (let start = 0; start < size; start += blockSize)
        {
            for (let j = 0, k = 0; j < half; ++j, k += tableStep)
            {
                // w = e^(∓2πik/N) = c ∓ i·s
                const c = cosTable[k], s = sign * sinTable[k];
                const a = start + j, b = a + half;
                const br = re[b], bi = im[b];
                const tr = br * c + bi * s;
                const ti = bi * c - br * s;

                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }

    if (inverse)
    {
        const scale = 1 / size;

        for (let i = 0; i < size; ++i)
        {
            re[i] *= scale;
            im[i] *= scale;
        }
    }
}

/// The periodic Hann window of the given size, cached: don't modify it
export function getHannWindow (size)
{
    checkSize (size);
    let window = hannWindows.get (size);

    if (! window)
    {
        window = new Float64Array (size);

        for (let i = 0; i < size; ++i)
            window[i] = 0.5 - 0.5 * Math.cos (2 * Math.PI * i / size);

        hannWindows.set (size, window);
    }

    return window;
}

/// A window's coherent gain: its sum divided by its length (0.5 for Hann)
export function getCoherentGain (window)
{
    let sum = 0;

    for (let i = 0; i < window.length; ++i)
        sum += window[i];

    return sum / window.length;
}

/// Converts an amplitude to dB, clamped to floorDB (so 0 and −Infinity give floorDB)
export function amplitudeToDB (amplitude, floorDB = spectrumFloorDB)
{
    const db = 20 * Math.log10 (amplitude);
    return db > floorDB ? db : floorDB;
}

/// Converts a power (amplitude squared) to dB, clamped to floorDB
export function powerToDB (power, floorDB = spectrumFloorDB)
{
    const db = 10 * Math.log10 (power);
    return db > floorDB ? db : floorDB;
}

//==============================================================================
/// The windowed, gain-corrected one-sided spectrum of a real frame (see the comment at the top)
export class RealSpectrum
{
    constructor (size, { window = "hann" } = {})
    {
        checkSize (size);
        this.size = size;
        this.binCount = size / 2 + 1;
        this.window = window === "hann" ? getHannWindow (size) : null;    // null = rectangular
        this.re = new Float64Array (size);
        this.im = new Float64Array (size);

        const coherentGain = this.window ? getCoherentGain (this.window) : 1;
        this.edgeScale = 1 / (size * coherentGain);     // DC and Nyquist
        this.binScale = 2 * this.edgeScale;             // the rest: one-sided, so doubled
    }

    /// Windows and transforms a frame of size samples, read from input starting at offset (wrapping
    /// round, so a ring buffer can be passed with its oldest sample's index). Leaves the raw
    /// spectrum in this.re / this.im.
    transform (input, offset = 0)
    {
        const { size, re, im, window } = this;
        const length = input.length;

        if (length < size)
            throw new Error ("RealSpectrum: input is shorter than the frame size");

        let index = offset % length;

        for (let i = 0; i < size; ++i)
        {
            re[i] = window ? input[index] * window[i] : input[index];
            im[i] = 0;

            if (++index === length)
                index = 0;
        }

        fft (re, im);
    }

    /// The amplitude of each of binCount bins (into output, which is returned; allocated if not given)
    getAmplitudes (input, output = new Float64Array (this.binCount), offset = 0)
    {
        this.getPowers (input, output, offset);

        for (let k = 0; k < this.binCount; ++k)
            output[k] = Math.sqrt (output[k]);

        return output;
    }

    /// The power (amplitude squared) of each of binCount bins (into output, which is returned)
    getPowers (input, output = new Float64Array (this.binCount), offset = 0)
    {
        this.transform (input, offset);
        return this.addPowers (output, 1, true);
    }

    /// Adds the last transform's bin powers × weight to output (or writes them, if overwrite)
    addPowers (output, weight = 1, overwrite = false)
    {
        const { re, im, binCount, size } = this;

        for (let k = 0; k < binCount; ++k)
        {
            const scale = (k === 0 || k === size / 2) ? this.edgeScale : this.binScale;
            const p = (re[k] * re[k] + im[k] * im[k]) * scale * scale * weight;
            output[k] = overwrite ? p : output[k] + p;
        }

        return output;
    }
}

//==============================================================================
/// A multi-channel spectrum analyser with averaging and peak hold (see the comment at the top)
export class SpectrumAnalyser
{
    constructor ({ fftSize = 4096, sampleRate = 48000, numChannels = 1, channelMode = "average",
                   averagingTime = 0, averagingAlpha = 0, peakDecayDBPerSecond = 20,
                   floorDB = spectrumFloorDB } = {})
    {
        this.sampleRate = sampleRate;
        this.channelMode = channelMode;
        this.averagingTime = averagingTime;
        this.averagingAlpha = averagingAlpha;
        this.peakDecayDBPerSecond = peakDecayDBPerSecond;
        this.floorDB = floorDB;
        this.numChannels = 0;
        this.rings = [];
        this.setFFTSize (fftSize);
        this.setNumChannels (Math.max (1, numChannels));
    }

    /// Changes the FFT size (reallocates, and resets everything)
    setFFTSize (fftSize)
    {
        checkSize (fftSize);
        this.fftSize = fftSize;
        this.binCount = fftSize / 2 + 1;
        this.spectrum = new RealSpectrum (fftSize);
        this.framePower = new Float64Array (this.binCount);
        this.averagePower = new Float64Array (this.binCount);
        this.currentDB = new Float32Array (this.binCount);
        this.levelsDB = new Float32Array (this.binCount);
        this.peaksDB = new Float32Array (this.binCount);
        this.rings = this.rings.map (() => new Float32Array (fftSize));
        this.reset();
    }

    /// Changes the number of channels kept (a new channel starts silent)
    setNumChannels (numChannels)
    {
        while (this.rings.length < numChannels)
            this.rings.push (new Float32Array (this.fftSize));

        this.rings.length = numChannels;
        this.numChannels = numChannels;
    }

    /// Forgets the samples, the average and the peaks
    reset()
    {
        for (const ring of this.rings)
            ring.fill (0);

        this.writePosition = 0;
        this.numFramesKept = 0;          // how many of the ring's frames are real (up to fftSize)
        this.framesSinceUpdate = 0;
        this.hasAverage = false;
        this.currentDB.fill (this.floorDB);
        this.levelsDB.fill (this.floorDB);
        this.resetPeaks();
    }

    /// Drops the held peaks (they start again from the levels)
    resetPeaks()
    {
        this.peaksDB.fill (this.floorDB);
    }

    /// True once fftSize frames have been added since the last reset
    get isFull()                                { return this.numFramesKept >= this.fftSize; }

    /// The centre frequency of bin k, in Hz
    getBinFrequency (k)                         { return k * this.sampleRate / this.fftSize; }

    /// The (fractional) bin index of a frequency in Hz
    getBinForFrequency (frequency)              { return frequency * this.fftSize / this.sampleRate; }

    //==============================================================================
    /// Adds a block of frames: channels is an array of per-channel sample arrays (Float32Array
    /// or plain arrays). numFrames defaults to the first channel's length.
    addBlock (channels, numFrames = channels[0]?.length ?? 0)
    {
        if (channels.length > this.numChannels)
            this.setNumChannels (channels.length);

        if (numFrames <= 0)
            return;

        const size = this.fftSize;
        const skip = Math.max (0, numFrames - size);    // only the last fftSize frames matter
        const framesToWrite = numFrames - skip;

        for (let c = 0; c < this.numChannels; ++c)
        {
            const ring = this.rings[c];
            const source = channels[c];
            let position = this.writePosition;

            for (let i = skip; i < numFrames; ++i)
            {
                const v = source ? source[i] : 0;
                ring[position] = Number.isFinite (v) ? v : 0;

                if (++position === size)
                    position = 0;
            }
        }

        this.writePosition = (this.writePosition + framesToWrite) % size;
        this.numFramesKept = Math.min (size, this.numFramesKept + numFrames);
        this.framesSinceUpdate += numFrames;
    }

    /// Computes the spectrum of the last fftSize frames into currentDB, levelsDB and peaksDB.
    /// dt (seconds) drives the averaging time constant and the peak decay; it defaults to the
    /// time covered by the frames added since the last update. Returns true if there were any.
    update (dt = this.framesSinceUpdate / this.sampleRate)
    {
        const hadNewFrames = this.framesSinceUpdate > 0;
        this.framesSinceUpdate = 0;
        this.computeFramePower();

        const { binCount, framePower, averagePower, currentDB, levelsDB, peaksDB, floorDB } = this;
        const alpha = this.hasAverage ? this.getAveragingAlpha (dt) : 1;
        const peakDrop = Number.isFinite (this.peakDecayDBPerSecond) ? Math.max (0, this.peakDecayDBPerSecond * dt) : Infinity;
        this.hasAverage = true;

        for (let k = 0; k < binCount; ++k)
        {
            averagePower[k] += alpha * (framePower[k] - averagePower[k]);
            currentDB[k] = powerToDB (framePower[k], floorDB);

            const level = powerToDB (averagePower[k], floorDB);
            levelsDB[k] = level;

            const peak = peaksDB[k] - peakDrop;
            peaksDB[k] = peak > level ? peak : level;
        }

        return hadNewFrames;
    }

    /// The weight given to the newest frame by an update with this dt (1 = no averaging)
    getAveragingAlpha (dt)
    {
        if (this.averagingAlpha > 0)
            return Math.min (1, this.averagingAlpha);

        if (this.averagingTime > 0)
            return dt > 0 ? 1 - Math.exp (-dt / this.averagingTime) : 0;

        return 1;
    }

    //==============================================================================
    computeFramePower()
    {
        const { framePower, spectrum, rings, numChannels } = this;
        const oldest = this.writePosition;     // the ring is full-length, so the oldest frame is where the next one goes

        if (typeof this.channelMode === "number")
        {
            const ring = rings[this.channelMode];

            if (ring)
                spectrum.getPowers (ring, framePower, oldest);
            else
                framePower.fill (0);

            return;
        }

        framePower.fill (0);

        for (let c = 0; c < numChannels; ++c)
        {
            spectrum.transform (rings[c], oldest);
            spectrum.addPowers (framePower, 1 / numChannels);
        }
    }
}

//==============================================================================
const fftTables = new Map();
const hannWindows = new Map();

function checkSize (size)
{
    if (! isValidFFTSize (size))
        throw new Error (`FFT size must be a power of two from ${minFFTSize} to ${maxFFTSize} (not ${size})`);
}

function getFFTTables (size)
{
    let tables = fftTables.get (size);

    if (tables)
        return tables;

    checkSize (size);

    const bits = Math.log2 (size);
    const bitReversed = new Uint32Array (size);

    for (let i = 0; i < size; ++i)
    {
        let r = 0;

        for (let b = 0, v = i; b < bits; ++b, v >>= 1)
            r = (r << 1) | (v & 1);

        bitReversed[i] = r;
    }

    const half = size / 2;
    const cos = new Float64Array (half), sin = new Float64Array (half);

    for (let k = 0; k < half; ++k)
    {
        const angle = 2 * Math.PI * k / size;
        cos[k] = Math.cos (angle);
        sin[k] = Math.sin (angle);
    }

    tables = { bitReversed, cos, sin };
    fftTables.set (size, tables);
    return tables;
}
