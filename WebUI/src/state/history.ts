/*
    What can be undone and redone, as the engine last said (Phase 18a).

    The history is the processor's; the page only shows it. Each name is the
    edit an undo or a redo would take back or put back, or empty when there is
    none.
*/

import { useSyncExternalStore } from 'react';

export interface History {
    undo: string;
    redo: string;
}

let current: History = { undo: '', redo: '' };

const listeners = new Set<() => void>();

export function applyHistory(undo: string, redo: string): void {
    current = {
        undo: typeof undo === 'string' ? undo : '',
        redo: typeof redo === 'string' ? redo : '',
    };

    for (const listener of listeners) listener();
}

/** The history as it stands, for code that is not rendering. */
export function currentHistory(): History {
    return current;
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useHistory(): History {
    return useSyncExternalStore(subscribe, () => current);
}
