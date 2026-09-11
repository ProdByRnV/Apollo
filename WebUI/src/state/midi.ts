/*
    MIDI Learn state.

    The native side owns the mappings, exactly as it owns parameter values: this
    page never invents one and never assumes a request succeeded. Every command
    is answered with the whole mapping state, and that answer is what the page
    draws (UI_BINDINGS.md §1, §6).

    One store rather than React state at the top of the tree, for the same reason
    the parameter store exists: a mapping change affects a hundred and forty
    controls' badges and nothing else, and re-rendering the page to move a badge
    is the wrong shape. Unlike parameter values these change a few times a
    minute, so there is one listener set rather than one per id.
*/

import { useSyncExternalStore } from 'react';

import type { ControllerProfileEntry, MidiMappingEntry } from '../bridge/protocol';
import { midiLearnBegin, midiLearnCancel } from '../bridge/bridge';

export interface MidiMapping {
    controller: number;
    channel: number;
    min: number;
    max: number;
}

export interface MidiState {
    /** True while clicking a control assigns it rather than moving it. */
    mode: boolean;
    /** The id learn is armed on, or "". */
    learning: string;
    capacity: number;
    mappings: ReadonlyMap<string, MidiMapping>;
    profiles: readonly ControllerProfileEntry[];
}

let state: MidiState = {
    mode: false,
    learning: '',
    capacity: 0,
    mappings: new Map(),
    profiles: [],
};

const listeners = new Set<() => void>();

function set(next: Partial<MidiState>): void {
    state = { ...state, ...next };
    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function midiState(): MidiState {
    return state;
}

export function useMidi(): MidiState {
    return useSyncExternalStore(subscribe, midiState);
}

//==============================================================================

export function setMidiMode(on: boolean): void {
    // Leaving the mode with learn still armed would leave the engine waiting for
    // a control the user can no longer see they are choosing.
    if (!on && state.learning) midiLearnCancel();

    set({ mode: on });
}

export function applyMappings(
    mappings: MidiMappingEntry[],
    learning: string,
    capacity: number,
): void {
    const next = new Map<string, MidiMapping>();

    for (const entry of mappings) {
        next.set(entry.id, {
            controller: entry.controller,
            channel: entry.channel,
            min: entry.min,
            max: entry.max,
        });
    }

    set({ mappings: next, learning: learning || '', capacity });
}

export function applyProfiles(profiles: ControllerProfileEntry[]): void {
    set({ profiles });
}

/** Arms, or disarms, learn for one control. */
export function toggleLearn(id: string): void {
    if (state.learning === id) midiLearnCancel();
    else midiLearnBegin(id);
}
