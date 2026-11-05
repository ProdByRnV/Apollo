/*
    The interface size (18b): how large the page draws for a size chosen, in a
    window of a given size.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { MINIMUM_LAYOUT, scaleLabel, scaleTitle, zoomFor } from '../src/params/interfaceScale.ts';

test('a size is drawn as chosen where the window has room for it', () => {
    assert.equal(zoomFor(100, 1278, 700), 1);
    assert.equal(zoomFor(150, 2560, 1400), 1.5);
    assert.equal(zoomFor(200, 3840, 2100), 2);
});

test('a smaller size is always drawn as chosen: it only gives the layout more room', () => {
    assert.equal(zoomFor(75, 1278, 700), 0.75);
    assert.equal(zoomFor(85, 900, 560), 0.85);
    assert.equal(zoomFor(75, 400, 300), 0.75);
});

test('a larger size is drawn only as large as leaves the layout its minimum', () => {
    // The laptop this was built on: 1278 by about 700 at 100%.
    const zoom = zoomFor(150, 1278, 700);
    assert.equal(zoom, 700 / MINIMUM_LAYOUT.height);
    assert.ok(1278 / zoom >= MINIMUM_LAYOUT.width && 700 / zoom >= MINIMUM_LAYOUT.height);

    // Wide and short: the height decides. Narrow and tall: the width does.
    assert.equal(zoomFor(200, 3000, 840), 1.5);
    assert.equal(zoomFor(200, 1350, 2000), 1.5);

    // Never below what was there before it was made larger.
    assert.equal(zoomFor(150, 800, 500), 1);
});

test('at every size, in every window, the masthead stays on the window', () => {
    for (const scale of [75, 85, 100, 110, 125, 150, 175, 200]) {
        for (let width = MINIMUM_LAYOUT.width; width <= 4000; width += 61) {
            for (let height = MINIMUM_LAYOUT.height; height <= 2200; height += 53) {
                const zoom = zoomFor(scale, width, height);

                assert.ok(zoom > 0 && zoom <= scale / 100 + 1e-9, `${scale}% in ${width}x${height}`);
                assert.ok(width / zoom >= MINIMUM_LAYOUT.width - 1e-6
                    && height / zoom >= MINIMUM_LAYOUT.height - 1e-6, `${scale}% in ${width}x${height}`);
            }
        }
    }
});

test('nonsense is drawn at 100%', () => {
    for (const scale of [Number.NaN, 0, -150, Number.POSITIVE_INFINITY]) assert.equal(zoomFor(scale, 1920, 1080), 1);
    assert.equal(zoomFor(150, 0, 0), 1, 'a window that reports nothing is not a reason to grow');
});

test('the control says when the window is holding a size back', () => {
    assert.equal(scaleLabel(125), '125%');
    assert.equal(scaleTitle(125, 1.25), 'Interface size: 125%');
    assert.equal(scaleTitle(75, 0.75), 'Interface size: 75%');
    assert.equal(scaleTitle(150, 1.25), 'Interface size: 150%, drawn at 125% until the window is larger');
});
