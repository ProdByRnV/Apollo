/*
    Every wavetable an oscillator can play, as one list (Phase 16d, ADR-0116):
    the four the Wavetable parameter names, the tables Apollo ships beyond them,
    and the user's library.

    They are reached three different ways underneath, and the list is what
    hides that. The first four set `oscN_wavetable`, which a host automates and
    which cannot grow (ADR-0114). The rest load into the oscillator's own slot,
    which USER plays: Apollo's extra tables by their number, the library's
    entries by theirs.

    What is playing follows from the parameter and the slot together, never
    from what was last clicked: the engine says what changed (ADR-0063).
*/

/** `oscN_wavetable`'s choice that plays the oscillator's own slot. */
export const USER_TABLE_CHOICE = 4;

export type TableAction =
    | { kind: 'parameter'; choice: number }
    | { kind: 'builtin'; table: number }
    | { kind: 'library'; entry: number };

export interface TableChoice {
    /** Unique within the list, and stable while the index is. */
    key: string;
    name: string;
    group: 'builtin' | 'library';
    /** The bank, for a library entry. */
    bank: string;
    frames: number;
    factory: boolean;
    /** Why it cannot be loaded, or "". */
    problem: string;
    action: TableAction;
}

export interface BuiltInTable {
    table: number;
    name: string;
    frames: number;
}

export interface LibraryTable {
    id: number;
    name: string;
    bank: string;
    factory: boolean;
    frames?: number;
    problem: string;
}

/** The four the parameter names have this many frames. */
const CLASSIC_FRAMES = 16;

/** The whole list, built-ins first. @p classic is the parameter's labels, of
    which the first four are tables and the fifth is USER. */
export function tableChoices(
    classic: readonly string[],
    builtIn: readonly BuiltInTable[],
    library: readonly LibraryTable[],
): TableChoice[] {
    const choices: TableChoice[] = [];

    classic.slice(0, USER_TABLE_CHOICE).forEach((name, choice) => {
        choices.push({
            key: `p${choice}`, name, group: 'builtin', bank: '', frames: CLASSIC_FRAMES, factory: true, problem: '',
            action: { kind: 'parameter', choice },
        });
    });

    for (const table of builtIn) {
        choices.push({
            key: `b${table.table}`, name: table.name.toUpperCase(), group: 'builtin', bank: '',
            frames: table.frames, factory: true, problem: '',
            action: { kind: 'builtin', table: table.table },
        });
    }

    for (const entry of library) {
        choices.push({
            key: `e${entry.id}`, name: entry.name, group: 'library', bank: entry.bank,
            frames: entry.frames ?? 0, factory: entry.factory, problem: entry.problem,
            action: { kind: 'library', entry: entry.id },
        });
    }

    return choices;
}

export interface SlotView {
    loaded: boolean;
    /** The library entry it came from, or 0. */
    entry: number;
    /** Which of Apollo's extra tables it is, or 0. */
    builtIn: number;
}

/** The key of the choice that is playing, or null when it is none in the list:
    a file from somewhere else, or USER with nothing loaded. */
export function playingKey(choice: number, slot: SlotView): string | null {
    if (choice < USER_TABLE_CHOICE) return `p${choice}`;
    if (!slot.loaded) return null;
    if (slot.builtIn > 0) return `b${slot.builtIn}`;
    if (slot.entry > 0) return `e${slot.entry}`;
    return null;
}

/** The choice @p direction (+1 or -1) along the list from @p key, skipping
    what cannot be loaded and wrapping at the ends. From nothing, +1 is the
    first and -1 the last. */
export function stepChoice(choices: readonly TableChoice[], key: string | null, direction: 1 | -1): TableChoice | null {
    const usable = choices.filter((choice) => choice.problem === '');
    if (usable.length === 0) return null;

    const at = usable.findIndex((choice) => choice.key === key);

    if (at < 0) return direction > 0 ? usable[0] ?? null : usable[usable.length - 1] ?? null;

    return usable[(at + direction + usable.length) % usable.length] ?? null;
}

/** @p choices whose name or bank contains every word of @p search, ignoring
    case. */
export function filterChoices(choices: readonly TableChoice[], search: string): TableChoice[] {
    const words = search.toLowerCase().split(/\s+/).filter((word) => word !== '');
    if (words.length === 0) return [...choices];

    return choices.filter((choice) => {
        const text = `${choice.name} ${choice.bank}`.toLowerCase();
        return words.every((word) => text.includes(word));
    });
}
