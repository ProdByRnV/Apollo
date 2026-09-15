/*
    The preset library, as the browser sees it.

    The native side owns every one of these facts, the same way it owns
    parameter values and MIDI mappings: this page never decides that a preset
    was loaded, never decides that a save worked, and never edits its own copy
    of the list to match what it just asked for. It asks, and it draws the
    answer (UI_BINDINGS.md §1, §6).

    That matters most for the two things that can fail. A save can be refused
    because a preset of that name is already there; a load can be refused
    because the file went away while the list was on screen. A browser that
    optimistically moved its own highlight would then be showing a sound that is
    not playing.

    THE FILTERS LIVE HERE TOO, and they are the one part of this file the
    backend has no opinion about. Searching and narrowing by category or bank
    are operations on a list the page already has; sending them across the
    bridge would mean a round trip per keystroke to compute something the page
    can compute in a microsecond.
*/

import { useSyncExternalStore } from 'react';

import type { PresetEntry, PresetIndexMessage, PresetStatusMessage } from '../bridge/protocol';

/** What the interface knows about the library and the sound in front of it. */
export interface PresetState {
    entries: readonly PresetEntry[];
    /** Files carrying the extension that could not be read as presets. */
    unreadable: number;
    /** True when the scan stopped early. What is listed is still valid. */
    truncated: boolean;
    userMissing: boolean;
    factoryMissing: boolean;
    scanning: boolean;
    /** True once an index has arrived, so "empty" and "not asked yet" differ. */
    received: boolean;

    /** The id of the loaded preset, or 0 for a sound not from the library. */
    loaded: number;

    name: string;
    author: string;
    category: string;
    comment: string;

    /** The most recent status token from the engine, e.g. "ALREADY_EXISTS". */
    status: string;
    statusMessage: string;

    // Filters. Page-local: nothing here is ever sent.
    search: string;
    categoryFilter: string;
    bankFilter: string;
}

const initial: PresetState = {
    entries: [],
    unreadable: 0,
    truncated: false,
    userMissing: false,
    factoryMissing: false,
    scanning: false,
    received: false,
    loaded: 0,
    name: '',
    author: '',
    category: '',
    comment: '',
    status: '',
    statusMessage: '',
    search: '',
    categoryFilter: '',
    bankFilter: '',
};

let state: PresetState = initial;

const listeners = new Set<() => void>();

function set(next: Partial<PresetState>): void {
    state = { ...state, ...next };
    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function presetState(): PresetState {
    return state;
}

export function usePresets(): PresetState {
    return useSyncExternalStore(subscribe, presetState);
}

//==============================================================================

export function applyPresetIndex(message: PresetIndexMessage): void {
    set({
        entries: message.presets,
        unreadable: message.unreadable,
        truncated: message.truncated,
        userMissing: message.userMissing,
        factoryMissing: message.factoryMissing,
        scanning: message.scanning,
        received: true,
    });
}

export function applyPresetStatus(message: PresetStatusMessage): void {
    set({
        loaded: message.loaded,
        name: message.name,
        author: message.author,
        category: message.category,
        comment: message.comment,
        status: message.status,
        statusMessage: message.statusMessage,
    });
}

/** Clears a status the user has dealt with, without touching anything else.

    Used when a replace prompt is answered: the prompt is the rendering of a
    status, so dismissing it means forgetting the status rather than inventing a
    separate "prompt open" flag that could disagree with it.
*/
export function clearPresetStatus(): void {
    set({ status: '', statusMessage: '' });
}

export function setPresetSearch(search: string): void {
    set({ search });
}

export function setPresetCategoryFilter(categoryFilter: string): void {
    set({ categoryFilter });
}

export function setPresetBankFilter(bankFilter: string): void {
    set({ bankFilter });
}

//==============================================================================
// Derived views. Pure functions of the state above, so a component can call one
// during render without needing to keep the result anywhere.

/** @returns the entries matching the current search and filters, in index order.

    Index order rather than re-sorted here: the backend sorts before it numbers,
    so the order the page receives is already factory-then-user, by bank, by
    name. Sorting it again on the page would be a second opinion about the same
    question, and the two would eventually differ.
*/
export function filteredPresets(current: PresetState): readonly PresetEntry[] {
    const search = current.search.trim().toLowerCase();

    return current.entries.filter((entry) => {
        if (current.categoryFilter && entry.category !== current.categoryFilter) return false;
        if (current.bankFilter && entry.bank !== current.bankFilter) return false;

        if (!search) return true;

        // Name, author, category and bank all match, because all four are
        // things a user might half-remember about a sound they are looking for.
        return (
            entry.name.toLowerCase().includes(search)
            || entry.author.toLowerCase().includes(search)
            || entry.category.toLowerCase().includes(search)
            || entry.bank.toLowerCase().includes(search)
        );
    });
}

/** @returns every distinct non-empty value of one field, sorted for a menu. */
export function distinctValues(
    entries: readonly PresetEntry[],
    field: 'category' | 'bank',
): readonly string[] {
    const values = new Set<string>();

    for (const entry of entries) {
        const value = entry[field];
        if (value) values.add(value);
    }

    return [...values].sort((a, b) => a.localeCompare(b));
}
