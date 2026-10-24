/*
    Which routings land on which knob.

    The matrix knows this and the knobs need it, and they are half a page apart
    in the tree. Passing it down would mean every knob re-rendering whenever any
    slot changed — a hundred and forty-three of them, thirty times a second while
    a depth rail is being dragged.

    So it is a store, and a knob reads only its own entry. `useSyncExternalStore`
    compares snapshots with `Object.is`, so an entry keeps its identity until
    something about that knob's routings changes, and a knob re-renders then and
    at no other time. Since Phase 13h-4 the entry is the routings themselves,
    not only whether there are any: a knob draws a tag for each, and dragging one
    is how its depth is changed on the control (ADR-0108).
*/

import { useCallback, useSyncExternalStore } from 'react';

import { sameRoutings } from '../params/modulationRouting';
import type { ControlRouting } from '../params/modulationRouting';

const NONE: readonly ControlRouting[] = [];

let byControl: ReadonlyMap<string, readonly ControlRouting[]> = new Map();
let assigned = 0;

const listeners = new Set<() => void>();

/** Replaces the routings. Entries that did not change keep their identity, so
    only the knobs whose routings moved are told. */
export function publishRouting(next: ReadonlyMap<string, readonly ControlRouting[]>, nextAssigned: number): void {
    let changed = nextAssigned !== assigned || next.size !== byControl.size;
    const merged = new Map<string, readonly ControlRouting[]>();

    for (const [id, routings] of next) {
        const previous = byControl.get(id);

        if (previous !== undefined && sameRoutings(previous, routings)) {
            merged.set(id, previous);
        } else {
            merged.set(id, routings);
            changed = true;
        }
    }

    if (!changed) return;

    byControl = merged;
    assigned = nextAssigned;

    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

/** Every routing on one control, for a menu that is not rendering. */
export function routingsOf(id: string): readonly ControlRouting[] {
    return byControl.get(id) ?? NONE;
}

export function useRoutingsOf(id: string): readonly ControlRouting[] {
    const snapshot = useCallback(() => routingsOf(id), [id]);
    return useSyncExternalStore(subscribe, snapshot);
}

/** True when a routing is actually moving the control: a boolean, so a knob
    re-renders when its answer flips and not while a depth is dragged. */
export function useIsModulated(id: string): boolean {
    const snapshot = useCallback(() => routingsOf(id).some((routing) => routing.live), [id]);
    return useSyncExternalStore(subscribe, snapshot);
}

export function useAssignedRoutings(): number {
    return useSyncExternalStore(subscribe, () => assigned);
}
