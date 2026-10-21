/*
    The formant filter's vowel, named the way the engine chooses it.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { vowelName, vowelPosition } from '../src/params/vowel.ts';

test('each vowel sits where the engine puts it', () => {
    // FormantFilter.cpp: U at 160 Hz, an octave and a half apart.
    assert.equal(vowelName(160), 'U');
    assert.equal(vowelName(160 * 2 ** 1.5), 'O');
    assert.equal(vowelName(1280), 'A');
    assert.equal(vowelName(160 * 2 ** 4.5), 'E');
    assert.equal(vowelName(10240), 'I');
});

test('the ends hold, and nonsense lands on the first vowel', () => {
    assert.equal(vowelName(20), 'U');
    assert.equal(vowelName(20000), 'I');
    assert.equal(vowelPosition(20000), 4);
    assert.equal(vowelName(Number.NaN), 'U');
});

test('between two vowels, the nearer one is named', () => {
    assert.equal(vowelPosition(1280), 2);
    assert.equal(vowelName(1280 * 2 ** 0.7), 'A');
    assert.equal(vowelName(1280 * 2 ** 0.8), 'E');
});
