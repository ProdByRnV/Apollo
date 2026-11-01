/*
    One list of every wavetable an oscillator can play (Phase 16d): what is in
    it, which one is playing, and how the arrows step through it. That the
    engine loads them is tested there (Tests/Resources/ExtraTableTests.cpp).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { filterChoices, playingKey, stepChoice, tableChoices } from '../src/params/wavetableChoices.ts';

const CLASSIC = ['SWEEP', 'PULSE', 'FORMANT', 'FOLD', 'USER'];
const BUILT_IN = [
    { table: 1, name: 'Analog', frames: 16 },
    { table: 2, name: 'Sync', frames: 16 },
];
const LIBRARY = [
    { id: 1, name: 'Growl', bank: 'Basses', factory: false, frames: 64, problem: '' },
    { id: 2, name: 'Broken', bank: '', factory: false, frames: 0, problem: 'Not a wavetable.' },
    { id: 3, name: 'Air', bank: 'Pads', factory: true, frames: 8, problem: '' },
];

const choices = tableChoices(CLASSIC, BUILT_IN, LIBRARY);

test('the built-ins come first, the four then the extra ones, and USER is not a table', () => {
    assert.deepEqual(choices.map((c) => c.name),
                     ['SWEEP', 'PULSE', 'FORMANT', 'FOLD', 'ANALOG', 'SYNC', 'Growl', 'Broken', 'Air']);
    assert.deepEqual(choices.map((c) => c.group),
                     ['builtin', 'builtin', 'builtin', 'builtin', 'builtin', 'builtin', 'library', 'library', 'library']);
});

test('each is reached the way it has to be', () => {
    assert.deepEqual(choices[3].action, { kind: 'parameter', choice: 3 });
    assert.deepEqual(choices[4].action, { kind: 'builtin', table: 1 });
    assert.deepEqual(choices[6].action, { kind: 'library', entry: 1 });
});

test('what is playing follows the parameter, then the slot', () => {
    const empty = { loaded: false, entry: 0, builtIn: 0 };

    assert.equal(playingKey(2, { loaded: true, entry: 1, builtIn: 0 }), 'p2', 'a built-in wins over a loaded slot');
    assert.equal(playingKey(4, { loaded: true, entry: 0, builtIn: 2 }), 'b2');
    assert.equal(playingKey(4, { loaded: true, entry: 3, builtIn: 0 }), 'e3');
    assert.equal(playingKey(4, { loaded: true, entry: 0, builtIn: 0 }), null, 'a file from elsewhere is not listed');
    assert.equal(playingKey(4, empty), null);
});

test('the arrows step past what cannot load, and wrap', () => {
    assert.equal(stepChoice(choices, 'p0', 1)?.key, 'p1');
    assert.equal(stepChoice(choices, 'e1', 1)?.key, 'e3', 'the broken entry is skipped');
    assert.equal(stepChoice(choices, 'e3', 1)?.key, 'p0', 'past the end is the start');
    assert.equal(stepChoice(choices, 'p0', -1)?.key, 'e3', 'before the start is the end');
    assert.equal(stepChoice(choices, null, 1)?.key, 'p0');
    assert.equal(stepChoice(choices, null, -1)?.key, 'e3');
    assert.equal(stepChoice([], null, 1), null);
});

test('searching narrows by name or bank', () => {
    assert.deepEqual(filterChoices(choices, 'pads').map((c) => c.key), ['e3']);
    assert.deepEqual(filterChoices(choices, 'sy').map((c) => c.key), ['b2']);
    assert.equal(filterChoices(choices, '  ').length, choices.length);
});
