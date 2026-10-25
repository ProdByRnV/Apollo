/*
    Whether this window may play notes (Phase 15): true in the standalone, which
    the engine says in the parameter metadata, and false in a DAW, which has its
    own keyboard. A store because App learns it and the piano strip reads it.
*/

import { useSyncExternalStore } from 'react';

let enabled = false;

const listeners = new Set<() => void>();

export function setNotePlaying(next: boolean): void {
    if (next === enabled) return;

    enabled = next;
    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useNotePlaying(): boolean {
    return useSyncExternalStore(subscribe, () => enabled);
}
