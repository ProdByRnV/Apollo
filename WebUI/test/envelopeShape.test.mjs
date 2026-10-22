/*
    The envelope editor's picture and edits.

    The anchor table is Tests/DSP/EnvelopeTests.cpp's: the engine is held to
    those values there and the page is held to them here, so the editor cannot
    draw a segment bending differently from the one that plays.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    MIN_SPAN,
    SUSTAIN_WIDTH,
    curveHandle,
    dragCurve,
    hitTest,
    levelAt,
    segmentShape,
    spanOf,
    stageHandle,
    vertices,
    withStage,
    withSustain,
} from '../src/params/envelopeShape.ts';

const near = (a, b, tolerance = 1e-6) => assert.ok(Math.abs(a - b) <= tolerance, `${a} is not ${b}`);

/** f at p = 1/8, 1/4, 1/2, 3/4, for each curve: EnvelopeTests.cpp. */
const ANCHORS = [
    [-1.0, [0.002776, 0.008652, 0.047426, 0.221200]],
    [-0.5, [0.023840, 0.058526, 0.182426, 0.444721]],
    [0.0, [0.125000, 0.250000, 0.500000, 0.750000]],
    [0.5, [0.329095, 0.555279, 0.817574, 0.941474]],
    [1.0, [0.528945, 0.778800, 0.952574, 0.991348]],
];

/** Envelope 1's defaults, normalised the way the registry's skew of 0.25 does. */
const skewed = (ms, max) => Math.pow(ms / max, 0.25);
const DEFAULT = {
    delay: 0,
    attack: skewed(5, 10000),
    hold: 0,
    decay: skewed(100, 10000),
    release: skewed(50, 10000),
    sustain: 1,
    curve: 0.5,
};

const pixels = ([x, level]) => [x * 100, (1 - level) * 100];
const toPixel = (vertex) => pixels([vertex.x, vertex.level]);

test('a segment bends as the engine bends it', () => {
    for (const [curve, values] of ANCHORS) {
        [0.125, 0.25, 0.5, 0.75].forEach((p, i) => near(segmentShape(p, curve), values[i], 1e-6));
        near(segmentShape(0, curve), 0, 0);
        near(segmentShape(1, curve), 1, 1e-12);
    }

    // Under the engine's threshold it is a straight line, not a division by
    // nearly nothing.
    assert.equal(segmentShape(0.3, 1e-5), 0.3);
});

test('the corners are a note held to its sustain and released', () => {
    const shape = { ...DEFAULT, delay: 0.2, hold: 0.1, sustain: 0.4 };
    const all = vertices(shape);

    assert.deepEqual(all.map((v) => v.level), [0, 0, 1, 1, 0.4, 0.4, 0]);
    for (let i = 1; i < all.length; ++i) assert.ok(all[i].x >= all[i - 1].x);

    near(all[1].x, 0.2);
    near(all[2].x - all[1].x, shape.attack);
    near(all[5].x - all[4].x, SUSTAIN_WIDTH);
    near(all[6].x - all[5].x, shape.release);
});

test('the line passes through every corner and bends between them', () => {
    const shape = { ...DEFAULT, delay: 0.2, hold: 0.1, sustain: 0.4, curve: -0.5 };

    for (const vertex of vertices(shape)) near(levelAt(shape, vertex.x), vertex.level);

    // Half way through the attack, on the curve.
    const [start, end] = [vertices(shape)[1], vertices(shape)[2]];
    near(levelAt(shape, (start.x + end.x) / 2), segmentShape(0.5, -0.5));

    // Half way through the decay, from 1 down to the sustain.
    const [top, bottom] = [vertices(shape)[3], vertices(shape)[4]];
    near(levelAt(shape, (top.x + bottom.x) / 2), 1 + (0.4 - 1) * segmentShape(0.5, -0.5));
});

test('the span fits the envelope with room to grow, and never shrinks to nothing', () => {
    assert.equal(spanOf({ ...DEFAULT, attack: 0, decay: 0, release: 0 }), MIN_SPAN);

    const long = { delay: 1, attack: 1, hold: 1, decay: 1, release: 1, sustain: 0.5, curve: 0 };
    assert.ok(spanOf(long) > vertices(long)[6].x);
});

test('handles stacked by a stage of no time: the earliest, or with Alt the latest', () => {
    const shape = { ...DEFAULT, hold: 0 };
    const [x, y] = toPixel(stageHandle(shape, 'attack'));

    assert.deepEqual(hitTest(shape, toPixel, x, y, 10), { kind: 'stage', stage: 'attack' });
    assert.deepEqual(hitTest(shape, toPixel, x, y, 10, true), { kind: 'stage', stage: 'hold' });

    // Pulled apart, each is its own.
    const held = { ...shape, hold: 0.3 };
    const [hx, hy] = toPixel(stageHandle(held, 'hold'));
    assert.deepEqual(hitTest(held, toPixel, hx, hy, 10), { kind: 'stage', stage: 'hold' });
    assert.deepEqual(hitTest(held, toPixel, x, y, 10, true), { kind: 'stage', stage: 'attack' });

    // Empty space is nothing.
    assert.equal(hitTest(shape, toPixel, 400, 50, 10), null);
});

test('the curve is grabbed half way along a segment that moves', () => {
    const shape = { ...DEFAULT, attack: 0.6, sustain: 0.3, decay: 0.6, release: 0.6 };

    for (const segment of ['attack', 'decay', 'release']) {
        const handle = curveHandle(shape, segment);
        const [x, y] = toPixel(handle);
        assert.deepEqual(hitTest(shape, toPixel, x, y, 10), { kind: 'curve', segment });
    }

    // Nothing to bend: a decay to a full sustain, a release from silence, a
    // stage of no time.
    assert.equal(curveHandle({ ...shape, sustain: 1 }, 'decay'), null);
    assert.equal(curveHandle({ ...shape, sustain: 0 }, 'release'), null);
    assert.equal(curveHandle({ ...shape, attack: 0 }, 'attack'), null);
});

test('dragging a curve up bows the line up, whichever way the segment runs', () => {
    const shape = { ...DEFAULT, curve: 0 };

    assert.equal(dragCurve(shape, 'attack', 0, 0.25).curve, 0.5);
    assert.equal(dragCurve(shape, 'decay', 0, 0.25).curve, -0.5);
    assert.equal(dragCurve(shape, 'release', 0, 0.25).curve, -0.5);

    // And a bowed-up attack really is above the straight line.
    const bent = dragCurve(shape, 'attack', 0, 0.25);
    const [from, to] = [vertices(bent)[1], vertices(bent)[2]];
    assert.ok(levelAt(bent, (from.x + to.x) / 2) > 0.5);

    assert.equal(dragCurve(shape, 'attack', 0.8, 1).curve, 1);
    assert.equal(dragCurve(shape, 'decay', 0.8, 1).curve, -1);
});

test('every edit stays in range', () => {
    assert.equal(withStage(DEFAULT, 'attack', 1.4).attack, 1);
    assert.equal(withStage(DEFAULT, 'release', -0.2).release, 0);
    assert.equal(withStage(DEFAULT, 'decay', Number.NaN).decay, 0);
    assert.equal(withSustain(DEFAULT, 2).sustain, 1);
    assert.equal(withSustain(DEFAULT, -1).sustain, 0);

    // Only the stage that was moved moves.
    const moved = withStage(DEFAULT, 'hold', 0.5);
    assert.deepEqual({ ...moved, hold: DEFAULT.hold }, DEFAULT);
});
