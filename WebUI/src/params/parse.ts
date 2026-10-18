/*
    Reading a number back in — the inverse of `format.ts`.

    A control can be dragged, nudged and reset, and from 12c-2 it can also be
    told a value: right-click and Enter value, or press Enter on a focused knob.
    "Set the cutoff to 440 Hz" was otherwise a matter of dragging until the
    readout happened to say so.

    WHAT IS ACCEPTED is what the readout writes, plus the shorthand a person
    types. If a control shows "2.50 kHz", then "2.5 kHz", "2.5k" and "2500" all
    mean the same thing. If it shows "50 %", then "50" means fifty per cent, not
    half of one, because the number typed is read on the scale the user sees.

    WHAT IS REFUSED is any suffix this control does not use. "5 s" typed into a
    frequency is not a frequency, and guessing what was meant would set a value
    nobody asked for. A refusal returns null, and the field says so without
    changing anything.

    Plain values out, unclamped. Clamping is `toNormalized`'s job, and the
    readout shows where the value landed.

    No runtime imports, so the interface tests can load it directly (WebUI/test/).
*/

/** Each suffix a unit accepts, and what it multiplies the number by. The empty
    suffix is the unit itself. */
const SUFFIXES: Readonly<Record<string, Readonly<Record<string, number>>>> = {
    Hz: { '': 1, hz: 1, k: 1000, khz: 1000 },
    ms: { '': 1, ms: 1, s: 1000, sec: 1000 },
    dB: { '': 1, db: 1 },
    // The scale the readout uses: 0.5 is shown as "50 %", so 50 is typed.
    '%': { '': 0.01, '%': 0.01 },
    voices: { '': 1, v: 1, voice: 1, voices: 1 },
    st: { '': 1, st: 1, semi: 1, semis: 1, semitone: 1, semitones: 1 },
    // "c" is what the readout writes; "ct" is the DAW convention.
    cents: { '': 1, c: 1, ct: 1, cent: 1, cents: 1 },
    oct: { '': 1, oct: 1, octave: 1, octaves: 1 },
};

/** A number in ordinary or exponent notation, then whatever follows it. */
const PATTERN = /^([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*([a-z%]*)$/;

/** @returns the plain value `text` names on a control of `unit`, or null if it
    names none.

    @param unit The engine's unit token, as `ParameterDefinition.unit` carries
                it. An unknown token accepts a bare number and nothing else.
*/
export function parsePlain(unit: string, text: string): number | null {
    // Space is allowed between the number and its unit, where the readout puts
    // one, and nowhere else. Dropping it everywhere read "1 2" as twelve. A
    // decimal comma is accepted when it is the only separator, because a
    // European keyboard types one; with a point also present it is ambiguous,
    // and refused.
    let compact = text.trim().toLowerCase();

    if (compact.includes(',')) {
        if (compact.includes('.') || compact.indexOf(',') !== compact.lastIndexOf(',')) return null;
        compact = compact.replace(',', '.');
    }

    // The readout's own minus sign, should somebody paste one back.
    compact = compact.replace(/−/g, '-');

    const match = PATTERN.exec(compact);
    if (match === null) return null;

    const number = Number(match[1]);
    const suffix = match[2] ?? '';

    const multiplier = (SUFFIXES[unit] ?? { '': 1 })[suffix];
    if (multiplier === undefined) return null;

    const plain = number * multiplier;

    return Number.isFinite(plain) ? plain : null;
}
