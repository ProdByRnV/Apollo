/*
    The page's half of the user's content (Phase 13g): what the browsers show
    about a slot and a library entry, how the search narrows a list, and which
    events stay on a slot's line.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    applyContentSlots,
    contentState,
    describeEntry,
    describeSlot,
    filterContent,
} from '../src/state/content.ts';

const entries = [
    { id: 1, name: 'Growl', bank: 'Basses', factory: false, frames: 64, problem: '' },
    { id: 2, name: 'Air Pad', bank: '', factory: false, frames: 4, problem: '' },
    { id: 3, name: 'Growl Wide', bank: 'Basses', factory: true, frames: 256, problem: '' },
    { id: 4, name: 'Ragged', bank: '', factory: false, frames: 0, problem: 'A wavetable must be a whole number of 2048-sample frames.' },
];

const slot = (overrides) => ({
    slot: 'osc1', loaded: false, name: '', origin: '', frames: 0, seconds: 0,
    entry: 0, loading: false, loadingName: '', ...overrides,
});

test('the search matches every word against name and bank, ignoring case', () => {
    assert.deepEqual(filterContent(entries, '').map((e) => e.id), [1, 2, 3, 4]);
    assert.deepEqual(filterContent(entries, '  ').map((e) => e.id), [1, 2, 3, 4]);
    assert.deepEqual(filterContent(entries, 'growl').map((e) => e.id), [1, 3]);
    assert.deepEqual(filterContent(entries, 'basses wide').map((e) => e.id), [3]);
    assert.deepEqual(filterContent(entries, 'AIR').map((e) => e.id), [2]);
    assert.deepEqual(filterContent(entries, 'nothing').map((e) => e.id), []);
});

test('a slot is described by name, size and where it came from', () => {
    assert.equal(describeSlot(slot({})), '');
    assert.equal(describeSlot(slot({ loaded: true, name: 'Growl', frames: 64, origin: 'library' })),
        'Growl · 64 frames · from your library');
    assert.equal(describeSlot(slot({ loaded: true, name: 'One', frames: 1, origin: 'file' })),
        'One · 1 frame · from a file');
    assert.equal(describeSlot(slot({ slot: 'noise', loaded: true, name: 'Breath', seconds: 0.5, origin: 'factory' })),
        'Breath · 0.50 s · from factory library');
    assert.equal(describeSlot(slot({ loaded: true, name: 'Old', loading: true, loadingName: 'New' })),
        'Loading New…', 'a load in progress is what the line says');
});

test('an entry shows its size, or why it will not load', () => {
    assert.equal(describeEntry(entries[0]), '64 frames');
    assert.equal(describeEntry({ ...entries[0], frames: 1 }), '1 frame');
    assert.equal(describeEntry({ id: 9, name: 'b', bank: '', factory: false, seconds: 1.234, problem: '' }), '1.23 s');
    assert.equal(describeEntry(entries[3]), entries[3].problem);
});

test('a failure stays on its slot until something else happens; a new load clears it', () => {
    const message = (status, slotName, text) => ({
        type: 'contentSlots', version: 1, status, slot: slotName, statusMessage: text,
        slots: [slot({ slot: 'osc1' }), slot({ slot: 'osc2' }), slot({ slot: 'noise' })],
    });

    applyContentSlots(message('LOAD_FAILED', 'osc2', 'Ragged: not whole frames.'));
    assert.deepEqual(contentState().events.osc2, { status: 'LOAD_FAILED', message: 'Ragged: not whole frames.' });

    // Something happening elsewhere leaves it.
    applyContentSlots(message('CURRENT', '', ''));
    applyContentSlots(message('LOADED', 'osc1', 'Growl is playing on oscillator 1.'));
    assert.equal(contentState().events.osc2?.status, 'LOAD_FAILED');
    assert.equal(contentState().events.osc1?.status, 'LOADED');

    // A new load on that slot clears it, and a restore says nothing of its own.
    applyContentSlots(message('LOADING', 'osc2', 'Loading Growl.'));
    assert.equal(contentState().events.osc2, undefined);

    applyContentSlots(message('RESTORED', 'osc2', ''));
    assert.equal(contentState().events.osc2, undefined);
});
