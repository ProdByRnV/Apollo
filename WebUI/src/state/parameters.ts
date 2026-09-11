/*
    The parameter store.

    Deliberately not React state. Values arrive from the engine at 30 Hz and a
    hundred and forty-three of them can change at once when a project loads; a
    `useState` at the top of the tree would re-render the whole page on every
    echo. This is an external store with **one listener set per parameter id**,
    read through `useSyncExternalStore`, so a knob re-renders when its own value
    moves and at no other time.

    THE NATIVE SIDE OWNS THE TRUTH. Nothing here invents a range, a default or a
    step; every one of those comes from the metadata the engine sent
    (UI_BINDINGS.md §16). What the page owns is which module a control belongs
    to, what a discrete position is called, and how a number is written out.
*/

import { useCallback, useSyncExternalStore } from 'react';

import type { ParameterDefinition } from '../bridge/protocol';
import { setParameter } from '../bridge/bridge';
import { toPlain } from '../params/mapping';

const definitions = new Map<string, ParameterDefinition>();
const values = new Map<string, number>();

const valueListeners = new Map<string, Set<() => void>>();
const metadataListeners = new Set<() => void>();

/** Bumped whenever the definition set changes, so `useSyncExternalStore` has a
    stable snapshot to compare rather than a Map identity that never changes.
*/
let metadataVersion = 0;

/** Ids under the pointer right now.

    A control being dragged is not overwritten by the echo of what it just sent:
    the value would arrive a frame late and pull the knob backwards under the
    hand.
*/
const held = new Set<string>();

function notify(id: string): void {
    const listeners = valueListeners.get(id);
    if (listeners) for (const listener of listeners) listener();
}

//==============================================================================
// Writes, from the bridge or from a control.

export function setDefinitions(incoming: ParameterDefinition[]): void {
    definitions.clear();

    for (const definition of incoming) definitions.set(definition.id, definition);

    metadataVersion += 1;
    for (const listener of metadataListeners) listener();
}

/** Applies a value that came from the engine. */
export function applyValue(id: string, normalized: number): void {
    if (values.get(id) === normalized) return;

    values.set(id, normalized);
    notify(id);
}

export function applySnapshot(snapshot: Record<string, number>): void {
    for (const id of Object.keys(snapshot)) {
        const value = snapshot[id];
        if (value !== undefined) applyValue(id, value);
    }
}

/** Applies a value locally *and* sends it.

    Applied here as well as sent so the control tracks the hand at pointer rate
    rather than at the engine's coalesced echo rate. The echo still arrives and
    still wins; this only removes the lag before it does.
*/
export function commitValue(id: string, normalized: number): void {
    const clamped = Math.min(1, Math.max(0, normalized));

    applyValue(id, clamped);
    setParameter(id, clamped);
}

export function setHeld(id: string, isHeld: boolean): void {
    if (isHeld) held.add(id);
    else held.delete(id);
}

export function isHeld(id: string): boolean {
    return held.has(id);
}

//==============================================================================
// Reads.

export function definitionOf(id: string): ParameterDefinition | undefined {
    return definitions.get(id);
}

export function valueOf(id: string): number {
    return values.get(id) ?? 0;
}

export function plainOf(id: string): number {
    const definition = definitions.get(id);
    return definition ? toPlain(definition, valueOf(id)) : 0;
}

export function definitionCount(): number {
    return definitions.size;
}

/** Every registered id, in registry order. */
export function allParameterIds(): string[] {
    return [...definitions.keys()];
}

//==============================================================================
// React bindings.

function subscribeToValue(id: string, listener: () => void): () => void {
    let listeners = valueListeners.get(id);

    if (listeners === undefined) {
        listeners = new Set();
        valueListeners.set(id, listeners);
    }

    listeners.add(listener);

    return () => {
        const current = valueListeners.get(id);
        if (current === undefined) return;

        current.delete(listener);
        if (current.size === 0) valueListeners.delete(id);
    };
}

function subscribeToMetadata(listener: () => void): () => void {
    metadataListeners.add(listener);
    return () => { metadataListeners.delete(listener); };
}

/** The normalised value of one parameter, re-rendering only its own control. */
export function useParameterValue(id: string): number {
    const subscribe = useCallback(
        (listener: () => void) => subscribeToValue(id, listener),
        [id],
    );

    return useSyncExternalStore(subscribe, () => valueOf(id));
}

/** One parameter's definition, or undefined before metadata has arrived. */
export function useParameterDefinition(id: string): ParameterDefinition | undefined {
    return useSyncExternalStore(subscribeToMetadata, () => definitions.get(id));
}

/** Changes when the definition set is replaced, and at no other time. */
export function useMetadataVersion(): number {
    return useSyncExternalStore(subscribeToMetadata, () => metadataVersion);
}
