/*
    Typing a value into a control: what is understood and what is refused.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { parsePlain } from '../src/params/parse.ts';

test('what the readout writes is read back as the same value', () => {
    // format.ts output for representative values of every unit.
    assert.equal(parsePlain('Hz', '2.50 kHz'), 2500);
    assert.equal(parsePlain('Hz', '440.0 Hz'), 440);
    assert.equal(parsePlain('ms', '1.50 s'), 1500);
    assert.equal(parsePlain('ms', '220 ms'), 220);
    assert.equal(parsePlain('dB', '+3.0 dB'), 3);
    assert.equal(parsePlain('dB', '-12.5 dB'), -12.5);
    assert.equal(parsePlain('%', '50 %'), 0.5);
    assert.equal(parsePlain('%', '-25 %'), -0.25);
    assert.equal(parsePlain('voices', '7'), 7);
    assert.equal(parsePlain('st', '+12 st'), 12);
    assert.equal(parsePlain('cents', '-7 c'), -7);
    assert.equal(parsePlain('oct', '-1 oct'), -1);
    assert.equal(parsePlain('oct', '0.50 oct'), 0.5);
    assert.equal(parsePlain('', '0.71'), 0.71);
});

test('the shorthand a person types', () => {
    assert.equal(parsePlain('Hz', '2.5k'), 2500);
    assert.equal(parsePlain('Hz', '2500'), 2500);
    assert.equal(parsePlain('Hz', '1KHZ'), 1000);
    assert.equal(parsePlain('ms', '2s'), 2000);
    assert.equal(parsePlain('ms', '.5 sec'), 500);
    assert.equal(parsePlain('%', '50'), 0.5);
    assert.equal(parsePlain('%', '100%'), 1);
    assert.equal(parsePlain('cents', '5 ct'), 5);
    assert.equal(parsePlain('st', '-7 semitones'), -7);
    assert.equal(parsePlain('Hz', '  440  '), 440);
    assert.equal(parsePlain('Hz', '4.4e2'), 440);
});

test('a decimal comma is accepted when it is the only separator', () => {
    assert.equal(parsePlain('Hz', '2,5k'), 2500);
    assert.equal(parsePlain('dB', '-1,5'), -1.5);
    assert.equal(parsePlain('Hz', '1,000.5'), null);
    assert.equal(parsePlain('Hz', '1,0,0'), null);
});

test('the readout\'s minus sign pastes back', () => {
    assert.equal(parsePlain('dB', '−6.0 dB'), -6);
});

test('a suffix the control does not use is refused, not guessed', () => {
    assert.equal(parsePlain('Hz', '5 s'), null);
    assert.equal(parsePlain('ms', '440 Hz'), null);
    assert.equal(parsePlain('dB', '50 %'), null);
    assert.equal(parsePlain('%', '3 dB'), null);
    assert.equal(parsePlain('', '3 Hz'), null);
    assert.equal(parsePlain('unknown-unit', '3 x'), null);
});

test('things that are not numbers are refused', () => {
    for (const text of ['', '   ', 'abc', '-', '.', '+', '1..2', '--1', '1 2', 'Infinity', 'NaN', '1e999', '0x10', '½']) {
        assert.equal(parsePlain('Hz', text), null, JSON.stringify(text));
    }
});

test('an unknown unit still accepts a bare number', () => {
    assert.equal(parsePlain('unknown-unit', '3'), 3);
});
