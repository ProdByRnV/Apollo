/*
    Which knobs a live routing actually moves.

    The matrix knows this and the knobs need it, and they are half a page apart
    in the tree. Passing it down would mean every knob re-rendering whenever any
    slot changed — a hundred and forty-three of them, thirty times a second while
    a depth rail is being dragged.

    So it is a store, and the hook a knob uses returns a **boolean**:
    `useSyncExternalStore` compares snapshots with `Object.is`, so a knob
    re-renders when its own answer flips and at no other time. Publishing is
    likewise a no-op unless the set genuinely changed, which during a drag is
    once — as the depth crosses zero.
*/

import { useSyncExternalStore } from 'react';

let lit: ReadonlySet<string> = new Set();
let assigned = 0;

const listeners = new Set<() => void>();

function sameSet(a: ReadonlySet<string>, b: ReadonlySet<string>): boolean {
    if (a.size !== b.size) return false;

    for (const value of a) if (!b.has(value)) return false;

    return true;
}

export function publishRouting(nextLit: ReadonlySet<string>, nextAssigned: number): void {
    if (nextAssigned === assigned && sameSet(nextLit, lit)) return;

    lit = nextLit;
    assigned = nextAssigned;

    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useIsModulated(id: string): boolean {
    return useSyncExternalStore(subscribe, () => lit.has(id));
}

export function useAssignedRoutings(): number {
    return useSyncExternalStore(subscribe, () => assigned);
}
