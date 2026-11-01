/*
    The sub oscillator's octave, as the page shows it: −2, −1 or 0.

    TWO PARAMETERS UNDERNEATH (Phase 16b, ADR-0114). `sub_octave` is the one
    1.0 shipped, −2 or −1, and its range cannot grow: a DAW's automation of it
    stores a position along the range, and a third value would move −1 to
    somewhere else. So octave 0 is −1 raised by `sub_octave_up`, a switch of its
    own, and the engine and this page both read the sum.

    Pure functions of normalised values, so they can be tested without a page.
*/

/** The octaves offered, lowest first. */
export const SUB_OCTAVES = [-2, -1, 0] as const;

export type SubOctave = (typeof SUB_OCTAVES)[number];

/** @returns the octave the sub plays for the two parameters' normalised
    values. A host can set −2 with the switch on, which is −1, so the sum is
    clamped rather than assumed to be one of the three the page would set. */
export function shownSubOctave(octaveNormalized: number, upNormalized: number): SubOctave {
    const octave = Math.round(octaveNormalized) === 0 ? -2 : -1;
    const up = Math.round(upNormalized) === 1 ? 1 : 0;
    const sum = octave + up;

    return (sum < -2 ? -2 : (sum > 0 ? 0 : sum)) as SubOctave;
}

/** @returns the normalised values that make the sub play @p octave. */
export function subOctaveTargets(octave: SubOctave): { octave: number; up: number } {
    return {
        octave: octave === -2 ? 0 : 1,
        up: octave === 0 ? 1 : 0,
    };
}
