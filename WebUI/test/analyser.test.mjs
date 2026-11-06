/*
    The output analyser's arithmetic (18d): the axis, the names it reads out,
    and the peak line.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    ANALYSER_MAX_HZ,
    ANALYSER_MIN_HZ,
    PEAK_FALL_DB_PER_FRAME,
    formatHz,
    frequencyAt,
    frequencyOfPoint,
    holdPeaks,
    levelAt,
    noteOf,
    pointAt,
    positionOfHz,
} from '../src/params/analyser.ts';

const near = (actual, expected, tolerance, message) =>
    assert.ok(Math.abs(actual - expected) <= tolerance, `${message ?? ''} ${actual} against ${expected}`);

test('the axis is the engine\'s: 20 Hz to 20 kHz, every point the same ratio from the next', () => {
    near(frequencyOfPoint(0, 160), ANALYSER_MIN_HZ, 1e-9);
    near(frequencyOfPoint(159, 160), ANALYSER_MAX_HZ, 1e-6);

    const ratio = frequencyOfPoint(1, 160) / frequencyOfPoint(0, 160);
    near(frequencyOfPoint(101, 160) / frequencyOfPoint(100, 160), ratio, 1e-9);

    // 1 kHz is the same place whichever way it is asked.
    near(frequencyAt(positionOfHz(1000)), 1000, 1e-6);
    near(frequencyOfPoint(pointAt(positionOfHz(1000), 160), 160), 1000, 1e-6);
});

test('the axis is clamped at its ends', () => {
    assert.equal(positionOfHz(5), 0);
    assert.equal(positionOfHz(48000), 1);
    near(frequencyAt(-1), ANALYSER_MIN_HZ, 1e-9);
    near(frequencyAt(2), ANALYSER_MAX_HZ, 1e-6);
});

test('a frequency is named as the piano strip names its keys', () => {
    assert.equal(noteOf(440), 'A4');
    assert.equal(noteOf(261.6256), 'C4');
    assert.equal(noteOf(55), 'A1');
    assert.equal(noteOf(440 * Math.pow(2, 12 / 1200)), 'A4 +12 c');
    assert.equal(noteOf(440 * Math.pow(2, -30 / 1200)), 'A4 −30 c');
    assert.equal(noteOf(277.1826), 'C♯4');
    assert.equal(noteOf(0), '—');
    assert.equal(noteOf(Number.NaN), '—');
});

test('a frequency is written in the units a musician reads', () => {
    assert.equal(formatHz(440), '440 Hz');
    assert.equal(formatHz(1250), '1.25 kHz');
    assert.equal(formatHz(12500), '12.5 kHz');
    assert.equal(formatHz(-1), '—');
});

test('a level is read between the two nearest points', () => {
    const levels = [-60, -40, -20];
    assert.equal(levelAt(levels, 0), -60);
    assert.equal(levelAt(levels, 0.5), -50);
    assert.equal(levelAt(levels, 2), -20);
    assert.equal(levelAt(levels, 5), -20, 'clamped at the end');
    assert.ok(Number.isNaN(levelAt([], 0)));
});

test('the peak line rises with the level at once, and falls slowly after it', () => {
    let peaks = holdPeaks([-20, -50], null);
    assert.deepEqual(Array.from(peaks), [-20, -50]);

    peaks = holdPeaks([-80, -30], peaks);
    near(peaks[0], -20 - PEAK_FALL_DB_PER_FRAME, 1e-6, 'falls by a step');
    assert.equal(peaks[1], -30, 'rises at once');

    for (let f = 0; f < 1000; ++f) peaks = holdPeaks([-80, -30], peaks);
    assert.equal(peaks[0], -80, 'and stops at the live level');

    // A frame of another length starts again rather than mixing two axes.
    assert.equal(holdPeaks([1, 2, 3], peaks).length, 3);
});
