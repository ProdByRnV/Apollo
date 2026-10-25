/*
    Playing Apollo from the computer keyboard (Phase 15, ADR-0111): which key
    plays which note, and how far the octave and the velocity move. Free of
    React and of the bridge, so the interface tests can import it.

    The layout is the one most music software uses. The middle row of letters
    is the white keys, from A; the row above it holds the black keys between
    them, where a piano has them; Z and X move down and up an octave; C and V
    play softer and harder. Keys are matched by their physical position
    (\`KeyboardEvent.code\`), not by the letter they print, so the piano is in
    the same place on a French or a German keyboard as on an American one.
*/

/** Semitones above the bottom C, by physical key. */
export const NOTE_KEYS: Readonly<Record<string, number>> = {
    KeyA: 0, KeyW: 1, KeyS: 2, KeyE: 3, KeyD: 4,
    KeyF: 5, KeyT: 6, KeyG: 7, KeyY: 8, KeyH: 9, KeyU: 10, KeyJ: 11,
    KeyK: 12, KeyO: 13, KeyL: 14, KeyP: 15, Semicolon: 16,
};

/** What each note key prints on an American keyboard, for the strip's labels. */
export const KEY_LABELS: Readonly<Record<string, string>> = {
    KeyA: 'A', KeyW: 'W', KeyS: 'S', KeyE: 'E', KeyD: 'D',
    KeyF: 'F', KeyT: 'T', KeyG: 'G', KeyY: 'Y', KeyH: 'H', KeyU: 'U', KeyJ: 'J',
    KeyK: 'K', KeyO: 'O', KeyL: 'L', KeyP: 'P', Semicolon: ';',
};

export const OCTAVE_DOWN = 'KeyZ';
export const OCTAVE_UP = 'KeyX';
export const SOFTER = 'KeyC';
export const HARDER = 'KeyV';

/** Middle C, where the A key sits at octave 0. */
export const MIDDLE_C = 60;

/** How far the octave moves: four down puts A on the lowest C a piano has
    near it, four up the highest. */
export const MIN_OCTAVE = -4;
export const MAX_OCTAVE = 4;

/** The velocities C and V step through. 100 to start: firm, with room above. */
export const VELOCITIES: readonly number[] = [20, 40, 60, 80, 100, 127];
export const DEFAULT_VELOCITY = 100;

/** The note a key plays at @p octave, or null for a key that plays none or a
    note past MIDI's range. */
export function noteForKey(code: string, octave: number): number | null {
    const semitones = NOTE_KEYS[code];
    if (semitones === undefined) return null;

    const note = MIDDLE_C + octave * 12 + semitones;
    return note >= 0 && note <= 127 ? note : null;
}

export function shiftOctave(octave: number, by: number): number {
    return Math.min(MAX_OCTAVE, Math.max(MIN_OCTAVE, octave + by));
}

/** The next velocity up or down the steps from @p velocity, held at the ends.
    A velocity between steps moves to the nearest step in that direction. */
export function stepVelocity(velocity: number, by: 1 | -1): number {
    if (by > 0) return VELOCITIES.find((step) => step > velocity) ?? VELOCITIES[VELOCITIES.length - 1]!;

    const lower = VELOCITIES.filter((step) => step < velocity);
    return lower.length > 0 ? lower[lower.length - 1]! : VELOCITIES[0]!;
}

const NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];

export function isBlack(note: number): boolean {
    return [1, 3, 6, 8, 10].includes(((note % 12) + 12) % 12);
}

export function noteName(note: number): string {
    return NAMES[((note % 12) + 12) % 12]!;
}

export interface StripKey {
    note: number;
    black: boolean;
    /** The computer key that plays it, or empty. */
    label: string;
    middleC: boolean;
}

/** The keys the strip draws at @p octave: two octaves and a top C, from the C
    the A key plays, each with the computer key that plays it. */
export function stripKeys(octave: number): StripKey[] {
    const bottom = MIDDLE_C + octave * 12;
    const labels = new Map<number, string>();

    for (const [code, semitones] of Object.entries(NOTE_KEYS)) labels.set(bottom + semitones, KEY_LABELS[code] ?? '');

    const keys: StripKey[] = [];

    for (let note = bottom; note <= bottom + 24; ++note) {
        if (note < 0 || note > 127) continue;
        keys.push({ note, black: isBlack(note), label: labels.get(note) ?? '', middleC: note === MIDDLE_C });
    }

    return keys;
}

/** How the octave reads: "Octave 0", "Octave +1", "Octave −2". */
export function octaveText(octave: number): string {
    if (octave === 0) return 'Octave 0';
    return `Octave ${octave > 0 ? '+' : '−'}${Math.abs(octave)}`;
}

/** Whether a key press belongs to whatever has focus rather than to the
    piano: typing in a field, or a key held with Ctrl, Alt or Cmd, which is a
    shortcut. Takes the focused element's tag and editability rather than the
    element, so it can be tested without a document. */
export function belongsElsewhere(
    event: { ctrlKey: boolean; altKey: boolean; metaKey: boolean },
    focused: { tagName: string; isContentEditable: boolean } | null,
): boolean {
    if (event.ctrlKey || event.altKey || event.metaKey) return true;
    if (focused === null) return false;

    const tag = focused.tagName.toUpperCase();
    return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || focused.isContentEditable;
}
