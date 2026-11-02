/*
    Stepping through presets with ‹ and › (Phase 17).

    Through the list as the browser is showing it — the search and the two
    filters applied — so choosing a category and stepping auditions that
    category. Each press loads, as choosing a row does; it is a button, not an
    arrow key in the list, which only moves (Presets.tsx).
*/

export interface Steppable {
    id: number;
}

/** The preset @p direction (+1 or -1) along @p entries from the one loaded,
    wrapping at the ends; from a sound not in the list, +1 is the first and -1
    the last. Null for an empty list. */
export function stepPreset<T extends Steppable>(entries: readonly T[], loaded: number, direction: 1 | -1): T | null {
    if (entries.length === 0) return null;

    const at = entries.findIndex((entry) => entry.id === loaded);

    if (at < 0) return (direction > 0 ? entries[0] : entries[entries.length - 1]) ?? null;

    return entries[(at + direction + entries.length) % entries.length] ?? null;
}
