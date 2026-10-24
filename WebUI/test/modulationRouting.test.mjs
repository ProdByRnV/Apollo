/*
    The page's half of drag-and-drop modulation (Phase 13h-4): which routings
    land on which control, what a tag says, and how far a dragged tag moves
    a depth. Which slot a drop takes is the engine's, and tested there
    (Tests/UI/ModulationDropTests.cpp).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    SOURCE_COUNT,
    SOURCE_TAGS,
    depthText,
    draggedDepth,
    liveSlotCount,
    routingsByControl,
    sameRoutings,
    tagTitle,
    visibleTags,
} from '../src/params/modulationRouting.ts';

// What the engine's metadata says, in part: 12 is Filter 1 Cutoff, 6 Osc 1
// Level; 1 (All Pitch) and 16 (Amplitude) are no single control.
const controls = new Map([[12, 'filter1_cutoff'], [6, 'osc1_level']]);
const controlFor = (destination) => controls.get(destination);

const empty = { source: 0, destination: 0, depth: 0 };

test('every source has a tag, two characters, in the engine\'s order', () => {
    assert.equal(SOURCE_COUNT, 20, 'dsp::ModSource::count');
    assert.equal(SOURCE_TAGS[0], '');
    assert.equal(SOURCE_TAGS[5], 'L1');
    assert.equal(SOURCE_TAGS[19], 'M4');

    for (const tag of SOURCE_TAGS.slice(1)) assert.equal(tag.length, 2, tag);
    assert.equal(new Set(SOURCE_TAGS).size, SOURCE_TAGS.length, 'no two alike');
});

test('routings are grouped by the control they move, in slot order', () => {
    const slots = [
        { source: 5, destination: 12, depth: 0.25 },
        empty,
        { source: 1, destination: 6, depth: -0.5 },
        { source: 9, destination: 12, depth: 0 },
        { source: 2, destination: 1, depth: 0.4 },   // All Pitch: no control
        { source: 3, destination: 0, depth: 0.4 },   // half made
        { source: 0, destination: 12, depth: 0.4 },  // half made
        { source: 16.0000001, destination: 11.9999999, depth: 0.1 }, // floats from the store
    ];

    const result = routingsByControl(slots, controlFor);

    assert.deepEqual([...result.keys()].sort(), ['filter1_cutoff', 'osc1_level']);

    const cutoff = result.get('filter1_cutoff');
    assert.deepEqual(cutoff.map((routing) => routing.slot), [1, 4, 8]);
    assert.deepEqual(cutoff.map((routing) => routing.source), [5, 9, 16]);
    assert.deepEqual(cutoff.map((routing) => routing.live), [true, false, true],
                     'a slot with no depth is kept, to be dragged up from nothing');

    assert.equal(result.get('osc1_level')[0].depth, -0.5);

    assert.equal(liveSlotCount(slots), 4, 'every slot that moves something, a control of its own or not');
});

test('a routing list compares by what is drawn', () => {
    const a = [{ slot: 1, source: 5, depth: 0.25, live: true }];

    assert.ok(sameRoutings(a, [{ ...a[0] }]));
    assert.ok(!sameRoutings(a, [{ ...a[0], depth: 0.3 }]));
    assert.ok(!sameRoutings(a, [{ ...a[0], slot: 2 }]));
    assert.ok(!sameRoutings(a, []));
});

test('depths read as the matrix reads them', () => {
    assert.equal(depthText(0.25), '+25%');
    assert.equal(depthText(-0.6), '-60%');
    assert.equal(depthText(0), '0%');
    assert.equal(depthText(0.004), '0%');
    assert.equal(depthText(-0.004), '0%', 'never "-0%"');
    assert.equal(depthText(1), '+100%');
});

test('a dragged tag moves the depth as far as a knob moves its value', () => {
    assert.equal(draggedDepth(0, 95, false), 1, 'half the travel is the whole positive half');
    assert.equal(draggedDepth(0, -95, false), -1);
    assert.equal(draggedDepth(0.25, 0, false), 0.25);
    assert.ok(Math.abs(draggedDepth(0, 95, true) - 0.2) < 1e-12, 'a fifth as far with Shift');
    assert.equal(draggedDepth(0.9, 500, false), 1, 'held at the ends');
    assert.equal(draggedDepth(-0.9, -500, false), -1);
});

test('a control shows four tags, or three and a count', () => {
    const routings = (n) => Array.from({ length: n }, (_, i) => ({ slot: i + 1, source: 1, depth: 0.1, live: true }));

    assert.deepEqual(visibleTags(routings(4)).hidden, 0);
    assert.equal(visibleTags(routings(4)).shown.length, 4);
    assert.equal(visibleTags(routings(5)).shown.length, 3);
    assert.equal(visibleTags(routings(5)).hidden, 2);
    assert.equal(visibleTags([]).shown.length, 0);
});

test('a tag says what it does in words', () => {
    const title = tagTitle('LFO 1', 'Filter 1 Cutoff', { slot: 3, source: 5, depth: -0.4, live: true });
    assert.match(title, /^LFO 1 → Filter 1 Cutoff, -40%, slot 3\./);

    const silent = tagTitle('LFO 1', 'Filter 1 Cutoff', { slot: 3, source: 5, depth: 0, live: false });
    assert.match(silent, /no depth yet/);
});
