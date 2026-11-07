/*
    How a value is written beside its control.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { formatPlain } from '../src/params/format.ts';

const plainNumber = { unit: '', step: 0 };

test('a value that rounds to nothing is written without a sign', () => {
    // An envelope bend of -0.5 over a curve of +0.5 plays a hair under zero
    // after the parameter's float round trip, which must read 0.00 (18f).
    assert.equal(formatPlain(plainNumber, -1e-8), '0.00');
    assert.equal(formatPlain(plainNumber, -0), '0.00');
    assert.equal(formatPlain(plainNumber, -0.004), '0.00');

    // A value that does not round to nothing keeps its sign.
    assert.equal(formatPlain(plainNumber, -0.006), '-0.01');
    assert.equal(formatPlain(plainNumber, -0.67), '-0.67');
    assert.equal(formatPlain(plainNumber, 0.5), '0.50');
});
