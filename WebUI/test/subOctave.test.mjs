/*
    The sub oscillator's octave (Phase 16b): −2, −1 and 0 shown as one choice
    over the two parameters underneath, `sub_octave` and `sub_octave_up`. That
    the engine plays the sum is tested there (Tests/Engine/SourceControlsTests.cpp).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { SUB_OCTAVES, shownSubOctave, subOctaveTargets } from '../src/params/subOctave.ts';

test('the three octaves are offered lowest first', () => {
    assert.deepEqual([...SUB_OCTAVES], [-2, -1, 0]);
});

test('each octave sets the two parameters to values that play it', () => {
    for (const octave of SUB_OCTAVES) {
        const targets = subOctaveTargets(octave);
        assert.equal(shownSubOctave(targets.octave, targets.up), octave, `octave ${octave}`);
    }
});

test('-1 without the switch is how every sound before it was saved', () => {
    assert.deepEqual(subOctaveTargets(-1), { octave: 1, up: 0 });
    assert.equal(shownSubOctave(1, 0), -1);
});

test('only 0 turns the switch on', () => {
    assert.equal(subOctaveTargets(-2).up, 0);
    assert.equal(subOctaveTargets(-1).up, 0);
    assert.equal(subOctaveTargets(0).up, 1);
});

test('what a host can set and the page would not is shown as what plays', () => {
    // −2 raised is −1, as the engine plays it.
    assert.equal(shownSubOctave(0, 1), -1);

    // Values between the steps, as automation can pass through, round as the
    // engine reads them.
    assert.equal(shownSubOctave(0.4, 0.6), -1);
    assert.equal(shownSubOctave(0.6, 0.4), -1);
    assert.equal(shownSubOctave(0.9, 0.9), 0);
});
