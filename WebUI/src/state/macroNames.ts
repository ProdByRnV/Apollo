/*
    The macros' names, as the engine last said they were (Phase 16c).

    A cache, like every other value the page holds: the store in the processor
    is authoritative, and what is shown is what it last sent. An empty string is
    a macro with no name, shown as "Macro N".
*/

import { useMemo, useSyncExternalStore } from 'react';

import { LABELS } from '../params/labels';
import { DEFAULT_MACRO_NAME_LIMIT, MACRO_COUNT, withMacroDestinationNames, withMacroNames } from '../params/macroNames';

interface MacroNames {
    names: readonly string[];
    maxLength: number;
}

let current: MacroNames = {
    names: Array.from({ length: MACRO_COUNT }, () => ''),
    maxLength: DEFAULT_MACRO_NAME_LIMIT,
};

const listeners = new Set<() => void>();

function notify(): void {
    for (const listener of listeners) listener();
}

/** Takes the engine's four names, and the longest it will accept. */
export function applyMacroNames(names: string[], maxLength: number): void {
    current = {
        names: Array.from({ length: MACRO_COUNT }, (_, index) => {
            const name = names[index];
            return typeof name === 'string' ? name : '';
        }),
        maxLength: Number.isInteger(maxLength) && maxLength > 0 ? maxLength : current.maxLength,
    };
    notify();
}

/** The page's own rename, shown at once while it travels to the engine. */
export function setLocalMacroName(macro: number, name: string): void {
    const index = macro - 1;
    if (index < 0 || index >= MACRO_COUNT) return;

    const names = current.names.slice();
    names[index] = name;
    current = { ...current, names };
    notify();
}

/** The names as they stand, for code that is not rendering. */
export function currentMacroNames(): MacroNames {
    return current;
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useMacroNames(): MacroNames {
    return useSyncExternalStore(subscribe, () => current);
}

/** The modulation sources' names, with the macros' own. */
export function useSourceLabels(): readonly string[] {
    const { names } = useMacroNames();
    return useMemo(() => withMacroNames(LABELS.modSource ?? [], names), [names]);
}

/** The destinations, with each macro by the name it was given (18h). */
export function useDestinationLabels(): readonly string[] {
    const { names } = useMacroNames();
    return useMemo(() => withMacroDestinationNames(LABELS.modDestination ?? [], names), [names]);
}
