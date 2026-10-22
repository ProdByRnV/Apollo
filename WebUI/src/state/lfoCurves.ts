/*
    The four drawn LFO curves, as the engine last said they were (Phase 13f-1).

    The page's copy is a cache, like every other value it holds: the store in the
    processor is authoritative, and what is drawn is what it last sent. A curve
    being dragged is the one exception — while a hand is on it, the page draws
    its own copy and does not let an echo, which arrives a frame late, pull a
    point back under the pointer. That is the same rule a held knob follows.
*/

import { useSyncExternalStore } from 'react';

import type { LfoCurvePoint } from '../bridge/protocol';
import { DEFAULT_LFO_CURVE } from '../params/lfoCurve';

let curves: readonly (readonly LfoCurvePoint[])[] = [
    DEFAULT_LFO_CURVE, DEFAULT_LFO_CURVE, DEFAULT_LFO_CURVE, DEFAULT_LFO_CURVE,
];

const listeners = new Set<() => void>();
const held = new Set<number>();

function notify(): void {
    for (const listener of listeners) listener();
}

/** Takes the engine's four curves. A curve under the pointer keeps the page's
    copy until it is let go. */
export function applyLfoCurves(incoming: { lfo: number; points: LfoCurvePoint[] }[]): void {
    const next = curves.slice();

    for (const entry of incoming) {
        const index = entry.lfo - 1;
        if (index < 0 || index >= next.length || held.has(entry.lfo)) continue;
        next[index] = entry.points;
    }

    curves = next;
    notify();
}

/** The page's own edit, drawn at once while it travels to the engine. */
export function setLocalLfoCurve(lfo: number, points: readonly LfoCurvePoint[]): void {
    const index = lfo - 1;
    if (index < 0 || index >= curves.length) return;

    const next = curves.slice();
    next[index] = points;
    curves = next;
    notify();
}

export function holdLfoCurve(lfo: number, holding: boolean): void {
    if (holding) held.add(lfo);
    else held.delete(lfo);
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useLfoCurve(lfo: number): readonly LfoCurvePoint[] {
    return useSyncExternalStore(subscribe, () => curves[lfo - 1] ?? DEFAULT_LFO_CURVE);
}
