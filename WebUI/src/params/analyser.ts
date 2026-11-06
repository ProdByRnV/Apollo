/*
    The arithmetic behind the output analyser (18d): where a point sits on the
    frequency axis, what a frequency is called as a note, and the peak line.

    Kept out of the component so the tests can reach it, as the equaliser's
    curve is (eqCurve.ts).
*/

/** The axis the engine sends its levels on: 20 Hz to 20 kHz, logarithmic,
    every point the same ratio from the next (Engine/SpectrumFrame.h). */
export const ANALYSER_MIN_HZ = 20;
export const ANALYSER_MAX_HZ = 20000;

/** The tilt the engine applies, in dB per octave about 1 kHz. Said on the panel,
    because a level read off a tilted display is not the level a meter shows. */
export const ANALYSER_TILT_DB_PER_OCTAVE = 4.5;

/** How far the peak line falls each frame, in dB: twelve a second at thirty
    frames, a quarter of the speed of the live line, so a note that has gone is
    still there to read for a few seconds. */
export const PEAK_FALL_DB_PER_FRAME = 0.4;

/** @returns the frequency, in Hz, of point @p index of @p count. */
export function frequencyOfPoint(index: number, count: number): number {
    if (count < 2) return ANALYSER_MIN_HZ;

    const t = Math.min(1, Math.max(0, index / (count - 1)));
    return ANALYSER_MIN_HZ * Math.pow(ANALYSER_MAX_HZ / ANALYSER_MIN_HZ, t);
}

/** @returns the fractional point index at a fraction @p t across the axis. */
export function pointAt(t: number, count: number): number {
    return Math.min(1, Math.max(0, t)) * (count - 1);
}

/** @returns the frequency at a fraction @p t across the axis, in Hz. */
export function frequencyAt(t: number): number {
    const clamped = Math.min(1, Math.max(0, t));
    return ANALYSER_MIN_HZ * Math.pow(ANALYSER_MAX_HZ / ANALYSER_MIN_HZ, clamped);
}

/** @returns the fraction across the axis at which @p hz is drawn. */
export function positionOfHz(hz: number): number {
    const clamped = Math.min(ANALYSER_MAX_HZ, Math.max(ANALYSER_MIN_HZ, hz));
    return Math.log(clamped / ANALYSER_MIN_HZ) / Math.log(ANALYSER_MAX_HZ / ANALYSER_MIN_HZ);
}

/** @returns @p hz as the panel writes it: "440 Hz", "1.25 kHz". */
export function formatHz(hz: number): string {
    if (!Number.isFinite(hz) || hz <= 0) return '—';
    if (hz < 1000) return `${Math.round(hz)} Hz`;
    if (hz < 10000) return `${(hz / 1000).toFixed(2)} kHz`;
    return `${(hz / 1000).toFixed(1)} kHz`;
}

const NOTE_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];

/** @returns the nearest note to @p hz and how far from it, in cents:
    "A4", "C♯3 +12 c". A4 is 440 Hz and middle C is C4, as on the piano strip. */
export function noteOf(hz: number): string {
    if (!Number.isFinite(hz) || hz <= 0) return '—';

    const midi = 69 + 12 * Math.log2(hz / 440);
    const nearest = Math.round(midi);
    const cents = Math.round((midi - nearest) * 100);
    const name = `${NOTE_NAMES[((nearest % 12) + 12) % 12]}${Math.floor(nearest / 12) - 1}`;

    if (cents === 0) return name;
    return `${name} ${cents > 0 ? '+' : '−'}${Math.abs(cents)} c`;
}

/** @returns @p levels read at a fractional point, between the two nearest. */
export function levelAt(levels: ArrayLike<number>, point: number): number {
    if (levels.length === 0) return Number.NaN;

    const below = Math.min(levels.length - 1, Math.max(0, Math.floor(point)));
    const above = Math.min(levels.length - 1, below + 1);
    const fraction = Math.min(1, Math.max(0, point - below));
    const low = levels[below] ?? Number.NaN;
    const high = levels[above] ?? low;

    return low + (high - low) * fraction;
}

/** Holds each point's highest recent level in @p peaks, falling by
    PEAK_FALL_DB_PER_FRAME a frame towards the live level. Returns @p peaks,
    replaced by a copy of @p levels when the two differ in length. */
export function holdPeaks(levels: ArrayLike<number>, peaks: Float32Array | null): Float32Array {
    if (peaks === null || peaks.length !== levels.length) return Float32Array.from(levels);

    for (let i = 0; i < levels.length; ++i) {
        const level = levels[i] ?? -Infinity;
        const held = peaks[i] ?? -Infinity;
        peaks[i] = level >= held ? level : Math.max(level, held - PEAK_FALL_DB_PER_FRAME);
    }

    return peaks;
}
