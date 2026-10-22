/*
    A DAHDSR envelope's picture, and the edits the envelope editor makes to it
    (Phase 13f-2, PRD §15.1).

    THE SHAPE IS THE ENGINE'S. Each segment travels from where it starts to
    where it ends along

        f(p) = (1 - e^(-k p)) / (1 - e^(-k)),   k = 6 * curve

    which is the closed form of what Source/DSP/Envelopes/Envelope.cpp steps
    incrementally. A curve under 1e-3 in k is a straight line there and here.
    Tests/DSP/EnvelopeTests.cpp holds the engine to a table of values of f and
    test/envelopeShape.test.mjs holds this file to the same table, so a picture
    that bent differently from the sound would fail one or the other.

    THE TIME AXIS IS NOT LINEAR, deliberately. Envelope times run from nothing
    to ten seconds; drawn to one scale, a 5 ms attack beside a 2 s release is a
    pixel nobody can grab. So each stage is as wide as its *normalised* value —
    the same skewed position its knob shows — which makes a pixel of drag move a
    time by the proportion a knob turn would, and keeps a short stage and a long
    one both visible. The caption gives the time itself.

    Everything below works on an EnvelopeShape: the five stage times as
    normalised values, and the sustain level and curve in their own units. The
    editor converts to and from parameters with the engine's metadata, so this
    file invents no range.
*/

export const TIMED_STAGES = ['delay', 'attack', 'hold', 'decay', 'release'] as const;
export type TimedStage = (typeof TIMED_STAGES)[number];

/** The segments whose level moves, which are the ones the curve bends. */
export type BentSegment = 'attack' | 'decay' | 'release';

export interface EnvelopeShape {
    /** Normalised 0 to 1, as the parameter holds it; also the stage's width. */
    delay: number;
    attack: number;
    hold: number;
    decay: number;
    release: number;
    /** Level, 0 to 1. */
    sustain: number;
    /** Tension, -1 to +1. */
    curve: number;
}

/** The engine's: Envelope.cpp, curveScale and linearThreshold. */
const CURVE_SCALE = 6;
const LINEAR_THRESHOLD = 1e-3;

/** The sustain has no time, so it is drawn a fixed width: enough to read as a
    plateau and to keep the decay's handle clear of the release's. */
export const SUSTAIN_WIDTH = 0.5;

/** The narrowest the picture's span gets, so a short envelope is drawn short
    rather than stretched across the whole editor. */
export const MIN_SPAN = 2.5;

/** Room past the release's end, so its handle can be dragged outwards before
    the picture is refitted. */
const HEADROOM = 1.15;

const clamp = (value: number, low: number, high: number): number =>
    Math.min(high, Math.max(low, Number.isFinite(value) ? value : low));

/** How far along its travel a segment is at progress @p p, for @p curve. */
export function segmentShape(p: number, curve: number): number {
    const progress = clamp(p, 0, 1);
    const k = clamp(curve, -1, 1) * CURVE_SCALE;

    if (Math.abs(k) < LINEAR_THRESHOLD) return progress;
    return (1 - Math.exp(-k * progress)) / (1 - Math.exp(-k));
}

//==============================================================================
// Layout, in width units: a stage at its maximum time is one unit wide.

export interface Vertex {
    x: number;
    level: number;
}

/** The seven corners of a note held to its sustain and then released:
    the start, then the end of the delay, attack, hold, decay, sustain and
    release. A stage of no time puts two corners in one place. */
export function vertices(shape: EnvelopeShape): Vertex[] {
    const sustain = clamp(shape.sustain, 0, 1);
    const out: Vertex[] = [{ x: 0, level: 0 }];

    let x = 0;
    const push = (width: number, level: number): void => {
        x += clamp(width, 0, 1);
        out.push({ x, level });
    };

    push(shape.delay, 0);
    push(shape.attack, 1);
    push(shape.hold, 1);
    push(shape.decay, sustain);
    x += SUSTAIN_WIDTH;
    out.push({ x, level: sustain });
    push(shape.release, 0);

    return out;
}

/** Index into vertices() of the corner each stage's handle sits on. */
const STAGE_VERTEX: Record<TimedStage, number> = { delay: 1, attack: 2, hold: 3, decay: 4, release: 6 };

/** Index into vertices() of the corner each bent segment starts at. */
const SEGMENT_START: Record<BentSegment, number> = { attack: 1, decay: 3, release: 5 };

/** How many width units the picture spans: the envelope plus room to grow. */
export function spanOf(shape: EnvelopeShape): number {
    const all = vertices(shape);
    return Math.max(MIN_SPAN, all[all.length - 1]!.x * HEADROOM);
}

/** The level at @p x width units in. */
export function levelAt(shape: EnvelopeShape, x: number): number {
    const all = vertices(shape);
    const curve = clamp(shape.curve, -1, 1);

    for (let i = 1; i < all.length; ++i) {
        const from = all[i - 1]!;
        const to = all[i]!;
        if (x > to.x) continue;

        const width = to.x - from.x;
        if (!(width > 0)) return to.level;

        return from.level + (to.level - from.level) * segmentShape((x - from.x) / width, curve);
    }

    return 0;
}

export function stageHandle(shape: EnvelopeShape, stage: TimedStage): Vertex {
    return vertices(shape)[STAGE_VERTEX[stage]]!;
}

/** Where the curve's handle on @p segment sits — half way through it, on the
    line — or null when the segment is not there to bend: no time, or no
    change of level (a decay to a full sustain, a release from silence). */
export function curveHandle(shape: EnvelopeShape, segment: BentSegment): Vertex | null {
    const all = vertices(shape);
    const from = all[SEGMENT_START[segment]]!;
    const to = all[SEGMENT_START[segment] + 1]!;

    if (!(to.x - from.x > 0) || from.level === to.level) return null;

    const x = (from.x + to.x) / 2;
    return { x, level: levelAt(shape, x) };
}

//==============================================================================
// Grabbing.

export type Grab =
    | { kind: 'stage'; stage: TimedStage }
    | { kind: 'curve'; segment: BentSegment };

/**
    The handle under (@p px, @p py), or null.

    @param toPixel  where a vertex is drawn, in the same pixels as px and py
    @param latest   which of several handles in one place to take. Stages of
                    no time stack their handles: with no hold, the hold's sits
                    on the attack's. A plain grab takes the earliest, because
                    lengthening the attack is the edit people make; with
                    @p latest (Alt held) it takes the last, which is how a hold
                    is pulled out from under the attack.
*/
export function hitTest(
    shape: EnvelopeShape,
    toPixel: (vertex: Vertex) => [number, number],
    px: number,
    py: number,
    radius: number,
    latest = false,
): Grab | null {
    const distance = (vertex: Vertex): number => {
        const [x, y] = toPixel(vertex);
        return Math.hypot(px - x, py - y);
    };

    // Stage handles first: a corner is what most grabs are for. Within half a
    // pixel of the nearest counts as the same place.
    const stages = TIMED_STAGES.map((stage) => ({ stage, d: distance(stageHandle(shape, stage)) }));
    const nearest = Math.min(...stages.map((s) => s.d));

    if (nearest < radius) {
        const stack = stages.filter((s) => s.d <= nearest + 0.5);
        return { kind: 'stage', stage: (latest ? stack[stack.length - 1]! : stack[0]!).stage };
    }

    let best: Grab | null = null;
    let bestDistance = radius;

    for (const segment of ['attack', 'decay', 'release'] as const) {
        const handle = curveHandle(shape, segment);
        if (!handle) continue;

        const d = distance(handle);
        if (d < bestDistance) {
            bestDistance = d;
            best = { kind: 'curve', segment };
        }
    }

    return best;
}

//==============================================================================
// Edits. Each returns a new shape with every value in its range.

export function withStage(shape: EnvelopeShape, stage: TimedStage, normalised: number): EnvelopeShape {
    return { ...shape, [stage]: clamp(normalised, 0, 1) };
}

export function withSustain(shape: EnvelopeShape, level: number): EnvelopeShape {
    return { ...shape, sustain: clamp(level, 0, 1) };
}

export function withCurve(shape: EnvelopeShape, curve: number): EnvelopeShape {
    return { ...shape, curve: clamp(curve, -1, 1) };
}

/**
    The curve after dragging @p segment's handle upwards by @p travel, where 1
    is the whole height of the curve's range.

    Up bows the line upwards, because that is the direction the hand moved. On
    the rising attack that is positive tension — quick off the mark; on a
    falling decay or release, the same tension drops quickly and so bows
    downwards, and up is negative.

    There is one curve control (PRD §15.1), so every segment bends together.
*/
export function dragCurve(shape: EnvelopeShape, segment: BentSegment, startCurve: number, travel: number): EnvelopeShape {
    const direction = segment === 'attack' ? 1 : -1;
    return withCurve(shape, startCurve + direction * travel * 2);
}
