/*
    The names the user gives the four macros (Phase 16c, ADR-0115), as rules
    the page can apply without asking: what a person reads for an unnamed
    macro, what is kept of what they typed, and how the names reach the lists
    of modulation sources.

    The engine is authoritative. `cleanMacroName` is a transcription of
    state::MacroNameStore::clean so the field shows what will be kept, but what
    the page shows afterwards is what the engine echoes back.
*/

/** dsp::ModSource numbers Macro 1 as 16, and the other three follow it. */
export const FIRST_MACRO_SOURCE = 16;
export const MACRO_COUNT = 4;

/** What the engine takes, until it says otherwise in its first message. */
export const DEFAULT_MACRO_NAME_LIMIT = 16;

/** "Macro 1" to "Macro 4", for a macro numbered from 1. */
export function defaultMacroName(macro: number): string {
    return `Macro ${macro}`;
}

/** What a person reads for a macro: its name, or "Macro N" when it has none. */
export function macroLabel(macro: number, stored: string): string {
    return stored !== '' ? stored : defaultMacroName(macro);
}

/** Every run of whitespace or control characters becomes one space, and the
    ends are trimmed: a name is one line, whatever was pasted. */
export function cleanMacroName(text: string): string {
    return text.replace(/[\s\u0000-\u001f\u007f-\u009f]+/g, ' ').trim();
}

/** At most @p limit characters — characters, not UTF-16 units, as the engine
    counts them, so an accented letter or an emoji costs one. */
export function limitMacroName(text: string, limit: number): string {
    const characters = Array.from(text);
    return characters.length > limit ? characters.slice(0, limit).join('') : text;
}

/** What to send for what was typed: cleaned, and empty when it is the macro's
    own default, which is how a name is taken away. */
export function macroNameToSend(macro: number, typed: string): string {
    const cleaned = cleanMacroName(typed);
    return cleaned === defaultMacroName(macro) ? '' : cleaned;
}

/** The macro a modulation source is, from 1, or null for any other source. */
export function macroOfSource(source: number): number | null {
    const macro = source - FIRST_MACRO_SOURCE + 1;
    return macro >= 1 && macro <= MACRO_COUNT ? macro : null;
}

/** The modulation sources' names with the macros' own in place of
    "Macro 1" to "Macro 4". The same length as @p table, so a stepped
    parameter's labels still line up with its steps. */
export function withMacroNames(table: readonly string[], names: readonly string[]): string[] {
    return table.map((label, source) => {
        const macro = macroOfSource(source);
        return macro === null ? label : macroLabel(macro, names[macro - 1] ?? '');
    });
}
