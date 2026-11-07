/*
    The library of drawn LFO shapes, as the engine last listed it, and what came
    of the last save (Phase 18g).

    The list is the engine's: a shape is loaded by its position in it, from 1,
    and the page never knows where a shape lives. It is asked for when an
    editor that shows it appears and after a save, which is when it can have
    changed from here; a file dropped into the folder appears the next time.
*/

import { useSyncExternalStore } from 'react';

import type { LfoCurvePoint } from '../bridge/protocol';

export interface LfoShapeEntry {
    name: string;
    builtIn: boolean;
    points: LfoCurvePoint[];
}

export interface LfoShapeStatus {
    code: string;
    message: string;
    name: string;
}

export interface LfoShapeLibrary {
    shapes: readonly LfoShapeEntry[];
    unreadable: number;
    maxNameLength: number;
    /** What the last save or folder request came to; null until there is one. */
    status: LfoShapeStatus | null;
}

let library: LfoShapeLibrary = { shapes: [], unreadable: 0, maxNameLength: 40, status: null };

const listeners = new Set<() => void>();

function notify(): void {
    for (const listener of listeners) listener();
}

export function applyLfoShapes(shapes: LfoShapeEntry[], unreadable: number, maxNameLength: number): void {
    library = { ...library, shapes, unreadable, maxNameLength };
    notify();
}

export function applyLfoShapeStatus(status: LfoShapeStatus | null): void {
    library = { ...library, status };
    notify();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useLfoShapes(): LfoShapeLibrary {
    return useSyncExternalStore(subscribe, () => library);
}

/** The shapes in the two groups the menu shows, each with the number the
    engine loads it by. */
export function groupShapes(shapes: readonly LfoShapeEntry[]): {
    apollo: { number: number; name: string }[];
    yours: { number: number; name: string }[];
} {
    const apollo: { number: number; name: string }[] = [];
    const yours: { number: number; name: string }[] = [];

    shapes.forEach((shape, index) => {
        (shape.builtIn ? apollo : yours).push({ number: index + 1, name: shape.name });
    });

    return { apollo, yours };
}
