/*
    A modulation source on its way to a control (Phase 13h-4, ADR-0108).

    Two ways to carry one. Dragged, it follows the pointer and lands where the
    button comes up. Picked up — a click on a source, or Enter on it — it is
    held until a control is clicked or given Enter, or Escape puts it down,
    which is the way that works from the keyboard and on a trackpad that makes
    dragging a chore.

    Knobs read two answers, both small: which source is in the air, so every
    control that can take it can say so, and whether it is over this one. The
    pointer position is read only by the layer that draws the source under it.
*/

import { useCallback, useSyncExternalStore } from 'react';

export type CarryMode = 'drag' | 'pick';

export interface Carried {
    source: number;
    mode: CarryMode;
    /** The control under the pointer that would take it, or null. */
    over: string | null;
    x: number;
    y: number;
}

let carried: Carried | null = null;

const listeners = new Set<() => void>();

function notify(): void {
    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function beginCarry(source: number, mode: CarryMode, x: number, y: number): void {
    carried = { source, mode, over: null, x, y };
    notify();
}

export function moveCarry(x: number, y: number, over: string | null): void {
    if (carried === null) return;
    if (carried.x === x && carried.y === y && carried.over === over) return;

    carried = { ...carried, x, y, over };
    notify();
}

/** Puts the source down. @returns what was being carried. */
export function endCarry(): Carried | null {
    const was = carried;
    if (was === null) return null;

    carried = null;
    notify();
    return was;
}

export function currentCarry(): Carried | null {
    return carried;
}

/** The source in the air, or 0. */
export function useCarriedSource(): number {
    return useSyncExternalStore(subscribe, () => carried?.source ?? 0);
}

/** True while the source in the air is over control @p id. */
export function useCarriedOver(id: string): boolean {
    const snapshot = useCallback(() => carried !== null && carried.over === id, [id]);
    return useSyncExternalStore(subscribe, snapshot);
}

export function useCarry(): Carried | null {
    return useSyncExternalStore(subscribe, () => carried);
}
