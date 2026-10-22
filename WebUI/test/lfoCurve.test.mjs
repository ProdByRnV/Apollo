/*
    The drawn LFO curve's arithmetic, and the editor's edits.

    The values here are the engine's (Tests/DSP/LfoCurveTests.cpp): if the page
    and the engine disagreed, the editor would draw a curve the LFO does not
    play. And every edit must leave a curve the engine will accept, because it
    refuses one that is not valid rather than repairing it.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    DEFAULT_LFO_CURVE,
    LFO_CURVE_MAX_POINTS,
    bendPoint,
    bendSegment,
    evaluate,
    forWire,
    insertPoint,
    movePoint,
    removePoint,
} from '../src/params/lfoCurve.ts';

/** The engine's validity rule (LfoCurve.cpp, isValid). */
function isValid(curve) {
    if (curve.length < 2 || curve.length > LFO_CURVE_MAX_POINTS) return false;
    let previous = 0;
    for (const [x, y, b] of curve) {
        if (![x, y, b].every(Number.isFinite)) return false;
        if (x < 0 || x > 1 || y < -1 || y > 1 || b < -1 || b > 1 || x < previous) return false;
        previous = x;
    }
    return curve[0][0] === 0 && curve[curve.length - 1][0] === 1;
}

const near = (a, b, tolerance = 1e-6) => assert.ok(Math.abs(a - b) <= tolerance, `${a} is not ${b}`);

test('the default is the triangle', () => {
    for (const p of [0, 0.1, 0.25, 0.5, 0.6, 0.9]) {
        near(evaluate(DEFAULT_LFO_CURVE, p), p < 0.5 ? 4 * p - 1 : 3 - 4 * p);
    }
    assert.ok(isValid(DEFAULT_LFO_CURVE));
});

test('a bend is the engine\'s bend', () => {
    assert.equal(bendSegment(0, 0.8), 0);
    assert.equal(bendSegment(1, -0.8), 1);
    near(bendSegment(0.5, 1), Math.pow(0.5, 0.25));
    near(bendSegment(0.5, -1), 0.0625);
});

test('two points at one position are a step at that position', () => {
    const square = [[0, 1, 0], [0.5, 1, 0], [0.5, -1, 0], [1, -1, 0]];
    assert.equal(evaluate(square, 0.4999), 1);
    assert.equal(evaluate(square, 0.5), -1);
    assert.equal(evaluate(square, 1), -1);
});

test('an end moves only up and down; a point stays between its neighbours', () => {
    const curve = [[0, 0, 0], [0.3, 0.5, 0], [0.6, -0.5, 0], [1, 0, 0]];

    const end = movePoint(curve, 0, 0.4, 2);
    assert.deepEqual(end[0], [0, 1, 0]);

    const squeezed = movePoint(curve, 1, 0.9, 0.2);
    assert.equal(squeezed[1][0], 0.6, 'a point passed its neighbour');
    assert.ok(isValid(squeezed));

    assert.ok(isValid(movePoint(curve, 2, -5, -5)));
});

test('points are added in order, up to the limit, and the ends cannot be removed', () => {
    const added = insertPoint(DEFAULT_LFO_CURVE, 0.75, 0.2);
    assert.equal(added.index, 2);
    assert.deepEqual(added.curve[2], [0.75, 0.2, 0]);
    assert.ok(isValid(added.curve));

    // At an existing point's position, after it: a step.
    const step = insertPoint(DEFAULT_LFO_CURVE, 0.5, -1);
    assert.equal(step.index, 2);
    assert.ok(isValid(step.curve));

    let full = DEFAULT_LFO_CURVE;
    while (full.length < LFO_CURVE_MAX_POINTS) full = insertPoint(full, Math.random(), 0).curve;
    assert.ok(isValid(full));
    assert.equal(insertPoint(full, 0.5, 0), null, 'a full curve took another point');

    assert.equal(removePoint(DEFAULT_LFO_CURVE, 0), null);
    assert.equal(removePoint(DEFAULT_LFO_CURVE, 2), null);
    assert.equal(removePoint(DEFAULT_LFO_CURVE, 1).length, 2);
});

test('a bend is clamped and snaps to straight near zero', () => {
    assert.equal(bendPoint(DEFAULT_LFO_CURVE, 0, 3)[0][2], 1);
    assert.equal(bendPoint(DEFAULT_LFO_CURVE, 0, 0.01)[0][2], 0);
    assert.equal(bendPoint(DEFAULT_LFO_CURVE, 2, 0.5), DEFAULT_LFO_CURVE, 'the last point has no segment to bend');
});

test('what goes on the wire is valid, with the ends exactly on the cycle', () => {
    const wire = forWire([[0.00001, 0.123456, 0.5], [0.3333333, -0.98765, 0], [0.99999, 0, 0]]);
    assert.deepEqual(wire[0], [0, 0.1235, 0.5]);
    assert.equal(wire[1][0], 0.3333);
    assert.equal(wire[2][0], 1);
    assert.ok(isValid(wire));
});
