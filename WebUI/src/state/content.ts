/*
    The user's own content, as the browsers see it (Phase 13g): the wavetable
    and noise libraries, what each slot holds, and what last happened to it.

    THE ENGINE OWNS ALL OF IT, as it owns presets. Choosing a row sends a
    number and waits; the slot changes when the engine says it did, which may
    be never — a file can turn out not to be a wavetable — and the message
    that says so is shown where the user is looking.

    The search box is the one thing here the engine has no opinion about: it
    narrows a list the page already has (filterContent).
*/

import { useSyncExternalStore } from 'react';

import type {
    ContentEntry,
    ContentIndexMessage,
    ContentSlot,
    ContentSlotState,
    ContentSlotsMessage,
} from '../bridge/protocol';

export interface SlotEvent {
    status: string;
    message: string;
}

export interface ContentState {
    wavetables: readonly ContentEntry[];
    noise: readonly ContentEntry[];
    scanning: boolean;
    truncated: boolean;
    received: boolean;

    slots: Readonly<Record<ContentSlot, ContentSlotState>>;

    /** The last thing that happened to each slot, until something else does. */
    events: Readonly<Partial<Record<ContentSlot, SlotEvent>>>;
}

const emptySlot = (slot: ContentSlot): ContentSlotState => ({
    slot,
    loaded: false,
    name: '',
    origin: '',
    frames: 0,
    seconds: 0,
    entry: 0,
    loading: false,
    loadingName: '',
});

const initial: ContentState = {
    wavetables: [],
    noise: [],
    scanning: false,
    truncated: false,
    received: false,
    slots: { osc1: emptySlot('osc1'), osc2: emptySlot('osc2'), noise: emptySlot('noise') },
    events: {},
};

let state: ContentState = initial;
const listeners = new Set<() => void>();

function set(next: Partial<ContentState>): void {
    state = { ...state, ...next };
    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function contentState(): ContentState {
    return state;
}

export function useContent(): ContentState {
    return useSyncExternalStore(subscribe, contentState);
}

//==============================================================================

export function applyContentIndex(message: ContentIndexMessage): void {
    set({
        wavetables: message.wavetables,
        noise: message.noise,
        scanning: message.scanning,
        truncated: message.truncated,
        received: true,
    });
}

/** Events that say nothing worth keeping on screen: the state of the slot
    already says it. */
const QUIET = new Set(['CURRENT', 'RESTORED', 'LOADING']);

export function applyContentSlots(message: ContentSlotsMessage): void {
    const slots = { ...state.slots };

    for (const slot of message.slots) {
        if (slot.slot === 'osc1' || slot.slot === 'osc2' || slot.slot === 'noise') slots[slot.slot] = slot;
    }

    const events = { ...state.events };
    const target = message.slot as ContentSlot;

    if (target in slots) {
        if (QUIET.has(message.status)) {
            // A new load clears the last one's message; the rest leave it.
            if (message.status === 'LOADING') delete events[target];
        } else {
            events[target] = { status: message.status, message: message.statusMessage };
        }
    }

    set({ slots, events });
}

//==============================================================================

/** @returns @p entries whose name or bank contains every word of @p search,
    ignoring case, in their order. An empty search is everything. */
export function filterContent(entries: readonly ContentEntry[], search: string): ContentEntry[] {
    const words = search.toLowerCase().split(/\s+/).filter((word) => word !== '');
    if (words.length === 0) return [...entries];

    return entries.filter((entry) => {
        const text = `${entry.name} ${entry.bank}`.toLowerCase();
        return words.every((word) => text.includes(word));
    });
}

/** @returns how a slot's content is described in a line: its name, then its
    size, then where it came from. */
export function describeSlot(slot: ContentSlotState): string {
    if (slot.loading) return `Loading ${slot.loadingName}…`;
    if (!slot.loaded) return '';

    const size = slot.slot === 'noise'
        ? `${slot.seconds.toFixed(2)} s`
        : `${slot.frames} frame${slot.frames === 1 ? '' : 's'}`;

    const origin = slot.origin === 'library' ? 'your library'
        : slot.origin === 'factory' ? 'factory library'
            : 'a file';

    return `${slot.name} · ${size} · from ${origin}`;
}

/** @returns a library entry's size as the list shows it. */
export function describeEntry(entry: ContentEntry): string {
    if (entry.problem) return entry.problem;
    if (entry.frames !== undefined) return `${entry.frames} frame${entry.frames === 1 ? '' : 's'}`;
    if (entry.seconds !== undefined) return `${entry.seconds.toFixed(2)} s`;
    return '';
}
