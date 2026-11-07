/*
    The library of drawn shapes as the page holds it (Phase 18g).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { groupShapes } from '../src/state/lfoShapes.ts';

const line = [[0, -1, 0], [1, 1, 0]];

test('the menu groups the shapes and keeps the number the engine loads each by', () => {
    const { apollo, yours } = groupShapes([
        { name: 'Pluck', builtIn: true, points: line },
        { name: 'Swell', builtIn: true, points: line },
        { name: 'Arp 2', builtIn: false, points: line },
        { name: 'Wobble', builtIn: false, points: line },
    ]);

    assert.deepEqual(apollo, [{ number: 1, name: 'Pluck' }, { number: 2, name: 'Swell' }]);
    assert.deepEqual(yours, [{ number: 3, name: 'Arp 2' }, { number: 4, name: 'Wobble' }]);
});

test('an empty library is two empty groups', () => {
    assert.deepEqual(groupShapes([]), { apollo: [], yours: [] });
});
