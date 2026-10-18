/*
    The arrow-key arithmetic every group on the page shares.

    Run with Node's own test runner (`npm test`), which needs no dependency and
    reads the TypeScript module directly. CMake runs it as part of building the
    interface, so a regression here fails the build rather than reaching a user
    holding a keyboard.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { hasCommandModifier, rovingTarget, tabStopIndex } from '../src/components/roving.ts';

test('arrows move one place in a wrapping group', () => {
    assert.equal(rovingTarget('ArrowRight', 1, 4), 2);
    assert.equal(rovingTarget('ArrowDown', 1, 4), 2);
    assert.equal(rovingTarget('ArrowLeft', 1, 4), 0);
    assert.equal(rovingTarget('ArrowUp', 1, 4), 0);
});

test('a wrapping group comes round at both ends', () => {
    assert.equal(rovingTarget('ArrowRight', 3, 4), 0);
    assert.equal(rovingTarget('ArrowLeft', 0, 4), 3);
});

test('a list stops at its ends rather than wrapping', () => {
    const list = { orientation: 'vertical', wrap: false };

    assert.equal(rovingTarget('ArrowDown', 9, 10, list), 9);
    assert.equal(rovingTarget('ArrowUp', 0, 10, list), 0);
    assert.equal(rovingTarget('ArrowDown', 4, 10, list), 5);
});

test('Home and End reach the ends from anywhere', () => {
    assert.equal(rovingTarget('Home', 2, 5), 0);
    assert.equal(rovingTarget('End', 2, 5), 4);
    assert.equal(rovingTarget('Home', -1, 5), 0);
    assert.equal(rovingTarget('End', 99, 5), 4);
});

test('orientation leaves the other axis alone', () => {
    assert.equal(rovingTarget('ArrowUp', 1, 4, { orientation: 'horizontal' }), null);
    assert.equal(rovingTarget('ArrowDown', 1, 4, { orientation: 'horizontal' }), null);
    assert.equal(rovingTarget('ArrowLeft', 1, 4, { orientation: 'vertical' }), null);
    assert.equal(rovingTarget('ArrowRight', 1, 4, { orientation: 'vertical' }), null);
});

test('keys a group does not own are not claimed', () => {
    // Claiming Tab would trap focus in the group; claiming Enter or Space would
    // stop a button from being pressed.
    for (const key of ['Tab', 'Enter', ' ', 'Escape', 'a', 'Delete', 'PageDown']) {
        assert.equal(rovingTarget(key, 1, 4), null, key);
    }
});

test('focus from outside the group lands on an end, not nowhere', () => {
    assert.equal(rovingTarget('ArrowRight', -1, 4), 0);
    assert.equal(rovingTarget('ArrowLeft', -1, 4), 3);

    // An index from a list that has since been filtered shorter.
    assert.equal(rovingTarget('ArrowDown', 12, 3, { orientation: 'vertical', wrap: false }), 0);
    assert.equal(rovingTarget('ArrowRight', 0.5, 4), 0);
});

test('an empty or nonsensical group handles nothing', () => {
    assert.equal(rovingTarget('ArrowRight', 0, 0), null);
    assert.equal(rovingTarget('Home', 0, -3), null);
    assert.equal(rovingTarget('End', 0, Number.NaN), null);
});

test('a group of one stays where it is', () => {
    assert.equal(rovingTarget('ArrowRight', 0, 1), 0);
    assert.equal(rovingTarget('ArrowLeft', 0, 1), 0);
    assert.equal(rovingTarget('ArrowDown', 0, 1, { wrap: false }), 0);
});

test('the Tab stop is the selected member, or the first', () => {
    assert.equal(tabStopIndex(2, 4), 2);
    assert.equal(tabStopIndex(-1, 4), 0);
    assert.equal(tabStopIndex(4, 4), 0);
    assert.equal(tabStopIndex(Number.NaN, 4), 0);
    assert.equal(tabStopIndex(1.5, 4), 0);
});

test('only Alt, Control and Meta count as a command modifier', () => {
    const none = { altKey: false, ctrlKey: false, metaKey: false };

    assert.equal(hasCommandModifier(none), false);
    assert.equal(hasCommandModifier({ ...none, altKey: true }), true);
    assert.equal(hasCommandModifier({ ...none, ctrlKey: true }), true);
    assert.equal(hasCommandModifier({ ...none, metaKey: true }), true);
});
