/*
    Stepping through presets with ‹ and › (Phase 17).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { stepPreset } from '../src/params/presetStep.ts';

const list = [{ id: 4 }, { id: 9 }, { id: 2 }];

test('steps along the list as shown, not by id', () => {
    assert.equal(stepPreset(list, 4, 1)?.id, 9);
    assert.equal(stepPreset(list, 9, 1)?.id, 2);
    assert.equal(stepPreset(list, 9, -1)?.id, 4);
});

test('wraps at both ends', () => {
    assert.equal(stepPreset(list, 2, 1)?.id, 4);
    assert.equal(stepPreset(list, 4, -1)?.id, 2);
});

test('from a sound that is not listed, starts at an end', () => {
    assert.equal(stepPreset(list, 0, 1)?.id, 4);
    assert.equal(stepPreset(list, 77, -1)?.id, 2);
});

test('an empty list has nowhere to go', () => {
    assert.equal(stepPreset([], 4, 1), null);
});
