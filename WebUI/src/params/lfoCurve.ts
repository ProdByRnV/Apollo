/*
    A drawn LFO curve's arithmetic, and the edits the editor makes to one.

    The first half is a transcription of Source/DSP/LFO/LfoCurve.cpp, and it has
    to stay one: the editor draws a curve with these functions and the engine
    plays it with those, so if they disagreed the picture would not be the
    sound. The same bend, and the same rule that of two points at one position
    the later one wins. test/lfoCurve.test.mjs holds the two to the same values.

    The second half is every change the editor can make, as functions of a
    curve, so that what a drag or a key does can be tested without a canvas.
    Each one keeps the curve valid by the engine's rules, because the engine
    refuses one that is not.
*/

import type { LfoCurvePoint } from '../bridge/protocol';

export type Curve = readonly LfoCurvePoint[];

export const LFO_CURVE_MAX_POINTS = 32;

/** The engine's default: the built-in triangle. */
export const DEFAULT_LFO_CURVE: Curve = [[0, -1, 0], [0.5, 1, 0], [1, -1, 0]];

export function bendSegment(u: number, bend: number): number {
    if (bend === 0 || u <= 0 || u >= 1) return u;
    return Math.pow(u, Math.pow(4, -bend));
}

export function evaluate(curve: Curve, position: number): number {
    const last = curve.length - 1;
    if (last < 1) return 0;
    if (position >= 1) return curve[last]![1];

    let segment = 0;
    while (segment + 1 < last && curve[segment + 1]![0] <= position) ++segment;

    const [x0, y0, bend] = curve[segment]!;
    const [x1, y1] = curve[segment + 1]!;
    const width = x1 - x0;

    if (!(width > 0)) return y1;

    const u = Math.max(0, (position - x0) / width);
    return y0 + (y1 - y0) * bendSegment(u, bend);
}

//==============================================================================

const clamp = (value: number, low: number, high: number): number => Math.min(high, Math.max(low, value));

const copy = (curve: Curve): LfoCurvePoint[] => curve.map((p) => [p[0], p[1], p[2]] as LfoCurvePoint);

/** Moves point @p index towards (@p position, @p value). The ends keep their
    places in the cycle; every other point stays between its neighbours, which
    it may share a position with (a step). */
export function movePoint(curve: Curve, index: number, position: number, value: number): Curve {
    const next = copy(curve);
    const last = next.length - 1;
    const point = next[index];
    if (!point) return curve;

    if (index === 0) point[0] = 0;
    else if (index === last) point[0] = 1;
    else point[0] = clamp(position, next[index - 1]![0], next[index + 1]![0]);

    point[1] = clamp(value, -1, 1);
    return next;
}

/** Adds a straight point at (@p position, @p value), after any already at
    that position. @returns null when the curve is full. */
export function insertPoint(curve: Curve, position: number, value: number): { curve: Curve; index: number } | null {
    if (curve.length >= LFO_CURVE_MAX_POINTS) return null;

    const at = clamp(position, 0, 1);
    let index = 1;
    while (index < curve.length - 1 && curve[index]![0] <= at) ++index;

    const next = copy(curve);
    next.splice(index, 0, [at, clamp(value, -1, 1), 0]);
    return { curve: next, index };
}

/** Removes point @p index. @returns null for an end, which a curve cannot lose. */
export function removePoint(curve: Curve, index: number): Curve | null {
    if (index <= 0 || index >= curve.length - 1) return null;
    return curve.filter((_, i) => i !== index);
}

/** Sets the bend of segment @p segment, near-zero snapping to straight. */
export function bendPoint(curve: Curve, segment: number, bend: number): Curve {
    const next = copy(curve);
    const point = next[segment];
    if (!point || segment >= next.length - 1) return curve;

    const clamped = clamp(bend, -1, 1);
    point[2] = Math.abs(clamped) < 0.02 ? 0 : clamped;
    return next;
}

/** Rounds to ten-thousandths for the wire, with the ends exactly on the
    cycle's start and end, which rounding must never move. */
export function forWire(curve: Curve): LfoCurvePoint[] {
    const round = (value: number): number => Math.round(value * 10000) / 10000;
    const next = curve.map(([x, y, b]) => [round(x), round(y), round(b)] as LfoCurvePoint);

    if (next.length > 0) next[0]![0] = 0;
    if (next.length > 1) next[next.length - 1]![0] = 1;

    return next;
}
