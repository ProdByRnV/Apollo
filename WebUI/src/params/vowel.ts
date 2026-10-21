/*
    Which vowel the formant filter's cutoff chooses.

    The engine's mapping, transcribed (Source/DSP/Filters/FormantFilter.cpp,
    `vowelPosition`): U at 160 Hz, then O, A, E and I an octave and a half apart.
    The page uses it only to name the vowel beside the control; the sound comes
    from the engine, so a disagreement here would mislabel a knob and nothing
    else. `WebUI/test/vowel.test.mjs` pins the two to the same numbers.
*/

export const VOWELS = ['U', 'O', 'A', 'E', 'I'] as const;

const FIRST_VOWEL_HZ = 160;
const OCTAVES_PER_VOWEL = 1.5;

/** 0 is U and 4 is I; the values between are the glides. */
export function vowelPosition(cutoffHz: number): number {
    if (!(cutoffHz > FIRST_VOWEL_HZ)) return 0;

    const position = Math.log2(cutoffHz / FIRST_VOWEL_HZ) / OCTAVES_PER_VOWEL;
    return Math.min(position, VOWELS.length - 1);
}

/** The vowel nearest the cutoff. */
export function vowelName(cutoffHz: number): string {
    return VOWELS[Math.round(vowelPosition(cutoffHz))] ?? VOWELS[0];
}
