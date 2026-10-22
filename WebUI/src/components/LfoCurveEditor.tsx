/*
    Draws an LFO's shape, and is how it is drawn (Phase 13f-1, PRD §15.2).

    One cycle, left to right; the value, -1 at the bottom to +1 at the top. The
    points are handles, and the small diamond half way along each segment is its
    bend.

      - DRAG A POINT to move it. The two ends stay at the start and end of the
        cycle and move only up and down; every other point stays between its
        neighbours. Hold Shift to snap to the grid: sixteenths across, quarters
        of the range up and down.
      - DRAG A DIAMOND up or down to bend its segment. Up bows the line upwards
        whichever way the segment runs, because that is the direction the hand
        moved.
      - DOUBLE-CLICK EMPTY SPACE to add a point there; DOUBLE-CLICK A POINT to
        remove it. Two points at one position make a step.
      - FROM THE KEYBOARD, with the editor focused: [ and ] choose a point, the
        arrows move it (Shift for fine), Insert adds one after it and Delete
        removes it.

    WHAT IS DRAWN IS WHAT PLAYS. The curve is evaluated with params/lfoCurve.ts,
    a transcription of the arithmetic the engine renders its table from
    (LfoCurve.cpp), and a test holds the two to the same values. Every edit is a
    function there too. The page's copy is drawn while a hand is on it;
    otherwise it is the engine's echo (state/lfoCurves.ts).
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import { setLfoCurve } from '../bridge/bridge';
import type { LfoCurvePoint } from '../bridge/protocol';
import { cssColour, fitCanvas } from '../canvas/draw';
import { useRedrawOnResize } from '../canvas/useRedrawOnResize';
import {
    DEFAULT_LFO_CURVE,
    bendPoint,
    evaluate,
    forWire,
    insertPoint,
    movePoint,
    removePoint,
} from '../params/lfoCurve';
import type { Curve } from '../params/lfoCurve';
import { holdLfoCurve, setLocalLfoCurve, useLfoCurve } from '../state/lfoCurves';

const ASPECT = 0.42;

/** Padding inside the canvas, in CSS pixels, so a handle at an edge is still
    a whole handle and can be grabbed. */
const PAD = 8;

const POINT_RADIUS = 4.5;
const GRAB_RADIUS = 10;

/** CSS pixels of vertical drag that take a bend from one end to the other. */
const BEND_DRAG_RANGE = 120;

const SNAP_X = 1 / 16;
const SNAP_Y = 0.25;

const clamp = (value: number, low: number, high: number): number => Math.min(high, Math.max(low, value));

interface Geometry {
    width: number;
    height: number;
}

function xOf(position: number, g: Geometry): number {
    return PAD + position * (g.width - 2 * PAD);
}

function yOf(value: number, g: Geometry): number {
    return PAD + (1 - (value + 1) / 2) * (g.height - 2 * PAD);
}

function positionOf(x: number, g: Geometry): number {
    return clamp((x - PAD) / Math.max(1, g.width - 2 * PAD), 0, 1);
}

function valueOf(y: number, g: Geometry): number {
    return clamp(1 - 2 * ((y - PAD) / Math.max(1, g.height - 2 * PAD)), -1, 1);
}

/** Where a segment's bend handle sits: half way across it, on the curve. */
function bendHandle(curve: Curve, segment: number): [number, number] | null {
    const from = curve[segment];
    const to = curve[segment + 1];
    if (!from || !to || !(to[0] - from[0] > 0)) return null;

    const middle = (from[0] + to[0]) / 2;
    return [middle, evaluate(curve, middle)];
}

type Grab =
    | { kind: 'point'; index: number }
    | { kind: 'bend'; segment: number; startY: number; startBend: number };

function describePoint(curve: Curve, index: number): string {
    const point = curve[index];
    if (!point) return '';

    const value = point[1];
    return `point ${index + 1} of ${curve.length} · ${Math.round(point[0] * 100)}% · `
        + `${value >= 0 ? '+' : ''}${value.toFixed(2)}`;
}

export function LfoCurveEditor({ lfo }: { lfo: number }): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);

    const curve = useLfoCurve(lfo);
    const curveRef = useRef(curve);
    curveRef.current = curve;

    const [selected, setSelected] = useState(0);
    const selectedRef = useRef(selected);
    selectedRef.current = selected;

    const grabRef = useRef<Grab | null>(null);
    const pendingRef = useRef<number | null>(null);

    /** Draws an edit at once and sends it, at most once a frame. */
    const commit = useCallback((next: Curve) => {
        const rounded = forWire(next);

        setLocalLfoCurve(lfo, rounded);

        if (pendingRef.current !== null) cancelAnimationFrame(pendingRef.current);
        pendingRef.current = requestAnimationFrame(() => {
            pendingRef.current = null;
            setLfoCurve(lfo, rounded);
        });
    }, [lfo]);

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');
        if (!root || !canvas || !context) return;

        const size = fitCanvas(root, canvas, ASPECT);
        const { width, height, ratio } = size;
        const g = { width: width / ratio, height: height / ratio };
        const points = curveRef.current;

        const grid = cssColour(root, '--scope-grid', '#2b3138');
        const line = cssColour(root, '--wave-line', '#f7ef8a');
        const muted = cssColour(root, '--text-dim', '#7c848c');
        const ground = cssColour(root, '--panel', '#181b1f');

        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, g.width, g.height);

        // Grid: quarters of the cycle, and the centre line drawn solid, because
        // it is the line a bipolar shape is read against.
        context.strokeStyle = grid;
        context.lineWidth = 1;

        for (let i = 1; i < 4; ++i) {
            const x = Math.round(xOf(i / 4, g)) + 0.5;
            context.beginPath();
            context.moveTo(x, PAD);
            context.lineTo(x, g.height - PAD);
            context.stroke();
        }

        const zero = Math.round(yOf(0, g)) + 0.5;
        context.beginPath();
        context.moveTo(PAD, zero);
        context.lineTo(g.width - PAD, zero);
        context.stroke();

        // The curve, one sample per CSS pixel, filled faintly to the centre.
        const span = g.width - 2 * PAD;
        context.beginPath();

        for (let i = 0; i <= span; ++i) {
            const position = i / span;
            const x = xOf(position, g);
            const y = yOf(evaluate(points, position), g);
            if (i === 0) context.moveTo(x, y);
            else context.lineTo(x, y);
        }

        context.strokeStyle = line;
        context.lineWidth = 1.6;
        context.lineJoin = 'round';
        context.stroke();

        context.lineTo(xOf(1, g), zero);
        context.lineTo(xOf(0, g), zero);
        context.closePath();
        context.fillStyle = line;
        context.globalAlpha = 0.08;
        context.fill();
        context.globalAlpha = 1;

        // Bend handles, then points over them.
        for (let s = 0; s + 1 < points.length; ++s) {
            const handle = bendHandle(points, s);
            if (!handle) continue;

            const x = xOf(handle[0], g);
            const y = yOf(handle[1], g);
            const r = 3;

            context.beginPath();
            context.moveTo(x, y - r);
            context.lineTo(x + r, y);
            context.lineTo(x, y + r);
            context.lineTo(x - r, y);
            context.closePath();
            context.fillStyle = (points[s]![2] !== 0) ? line : muted;
            context.fill();
        }

        points.forEach((point, index) => {
            const x = xOf(point[0], g);
            const y = yOf(point[1], g);

            context.beginPath();
            context.arc(x, y, index === selectedRef.current ? POINT_RADIUS + 1 : POINT_RADIUS, 0, 2 * Math.PI);

            // Selected is filled, the others outlined: told apart by shape as
            // well as colour (CLAUDE.md §24.2).
            if (index === selectedRef.current) {
                context.fillStyle = line;
                context.fill();
            } else {
                context.fillStyle = ground;
                context.fill();
                context.strokeStyle = line;
                context.lineWidth = 1.4;
                context.stroke();
            }
        });
    }, []);

    useEffect(draw, [draw, curve, selected]);
    useRedrawOnResize(rootRef, draw);

    // A curve with fewer points than the selection pointed at, after a load.
    useEffect(() => {
        if (selected >= curve.length) setSelected(Math.max(0, curve.length - 1));
    }, [curve, selected]);

    //--------------------------------------------------------------------------
    // Editing.

    const geometry = (): Geometry & { left: number; top: number } => {
        const box = canvasRef.current!.getBoundingClientRect();
        return { width: box.width, height: box.height, left: box.left, top: box.top };
    };

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;

        const hit = (event: PointerEvent | MouseEvent): Grab | null => {
            const g = geometry();
            const px = event.clientX - g.left;
            const py = event.clientY - g.top;
            const points = curveRef.current;

            let best: Grab | null = null;
            let bestDistance = GRAB_RADIUS;

            points.forEach((point, index) => {
                const distance = Math.hypot(px - xOf(point[0], g), py - yOf(point[1], g));
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = { kind: 'point', index };
                }
            });

            if (best) return best;

            for (let s = 0; s + 1 < points.length; ++s) {
                const handle = bendHandle(points, s);
                if (!handle) continue;

                const distance = Math.hypot(px - xOf(handle[0], g), py - yOf(handle[1], g));
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = { kind: 'bend', segment: s, startY: event.clientY, startBend: points[s]![2] };
                }
            }

            return best;
        };

        const onPointerDown = (event: PointerEvent): void => {
            if (event.button !== 0 || document.body.dataset.midiMode === 'true') return;

            const grabbed = hit(event);
            if (!grabbed) return;

            event.preventDefault();
            canvas.focus();
            canvas.setPointerCapture(event.pointerId);

            grabRef.current = grabbed;
            holdLfoCurve(lfo, true);

            if (grabbed.kind === 'point') setSelected(grabbed.index);
        };

        const onPointerMove = (event: PointerEvent): void => {
            const grabbed = grabRef.current;
            if (!grabbed) return;

            const g = geometry();
            const points = curveRef.current;

            if (grabbed.kind === 'point') {
                let position = positionOf(event.clientX - g.left, g);
                let value = valueOf(event.clientY - g.top, g);

                if (event.shiftKey) {
                    position = Math.round(position / SNAP_X) * SNAP_X;
                    value = Math.round(value / SNAP_Y) * SNAP_Y;
                }

                commit(movePoint(points, grabbed.index, position, value));
                return;
            }

            // Up bows the line upwards. For a rising segment that is a positive
            // bend (it gets most of the way early); for a falling one, negative.
            const from = points[grabbed.segment];
            const to = points[grabbed.segment + 1];
            if (!from || !to) return;

            const direction = to[1] < from[1] ? -1 : 1;
            const travel = (grabbed.startY - event.clientY) / BEND_DRAG_RANGE * 2;
            const bend = clamp(grabbed.startBend + direction * travel, -1, 1);

            commit(bendPoint(points, grabbed.segment, bend));
        };

        const onPointerUp = (event: PointerEvent): void => {
            if (!grabRef.current) return;

            grabRef.current = null;
            if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);

            // Let go: the engine's copy is the one drawn from here on, and the
            // last edit is sent now rather than a frame later.
            holdLfoCurve(lfo, false);
            if (pendingRef.current !== null) {
                cancelAnimationFrame(pendingRef.current);
                pendingRef.current = null;
            }
            setLfoCurve(lfo, curveRef.current.map((p) => [...p] as LfoCurvePoint));
        };

        const onDoubleClick = (event: MouseEvent): void => {
            const grabbed = hit(event);
            const points = curveRef.current;

            if (grabbed?.kind === 'point') {
                const next = removePoint(points, grabbed.index);
                if (next) {
                    commit(next);
                    setSelected(Math.max(0, grabbed.index - 1));
                }
                return;
            }

            if (grabbed) return;

            const g = geometry();
            const result = insertPoint(points,
                positionOf(event.clientX - g.left, g), valueOf(event.clientY - g.top, g));

            if (result) {
                commit(result.curve);
                setSelected(result.index);
            }
        };

        canvas.addEventListener('pointerdown', onPointerDown);
        canvas.addEventListener('pointermove', onPointerMove);
        canvas.addEventListener('pointerup', onPointerUp);
        canvas.addEventListener('pointercancel', onPointerUp);
        canvas.addEventListener('dblclick', onDoubleClick);

        return () => {
            canvas.removeEventListener('pointerdown', onPointerDown);
            canvas.removeEventListener('pointermove', onPointerMove);
            canvas.removeEventListener('pointerup', onPointerUp);
            canvas.removeEventListener('pointercancel', onPointerUp);
            canvas.removeEventListener('dblclick', onDoubleClick);
        };
    }, [commit, lfo]);

    const onKeyDown = (event: React.KeyboardEvent<HTMLCanvasElement>): void => {
        const points = curveRef.current;
        const index = Math.min(selected, points.length - 1);
        const point = points[index];
        if (!point) return;

        const step = event.shiftKey ? 0.01 : 0.05;
        let next: Curve | null = null;

        switch (event.key) {
            case '[': setSelected(Math.max(0, index - 1)); break;
            case ']': setSelected(Math.min(points.length - 1, index + 1)); break;
            case 'ArrowLeft': next = movePoint(points, index, point[0] - step, point[1]); break;
            case 'ArrowRight': next = movePoint(points, index, point[0] + step, point[1]); break;
            case 'ArrowUp': next = movePoint(points, index, point[0], point[1] + 2 * step); break;
            case 'ArrowDown': next = movePoint(points, index, point[0], point[1] - 2 * step); break;
            case 'Insert': {
                const after = points[Math.min(index + 1, points.length - 1)]!;
                const position = index < points.length - 1 ? (point[0] + after[0]) / 2 : point[0];
                const result = insertPoint(points, position, evaluate(points, position));
                if (result) {
                    commit(result.curve);
                    setSelected(result.index);
                }
                break;
            }
            case 'Delete':
            case 'Backspace': {
                const result = removePoint(points, index);
                if (result) {
                    commit(result);
                    setSelected(Math.max(0, index - 1));
                }
                break;
            }
            default:
                return;
        }

        event.preventDefault();
        if (next) commit(next);
    };

    const reset = (): void => {
        commit(DEFAULT_LFO_CURVE);
        setSelected(0);
    };

    return (
        <div ref={rootRef} className="lfo-curve" data-lfo-curve={lfo}>
            <canvas
                ref={canvasRef}
                className="lfo-curve__canvas"
                tabIndex={0}
                role="application"
                aria-label={`LFO ${lfo} drawn shape`}
                aria-description={'Drag a point to move it and a diamond to bend its segment. '
                    + 'Double-click to add or remove a point. With the shape focused, [ and ] choose a point, '
                    + 'the arrows move it, Insert adds one and Delete removes it.'}
                onKeyDown={onKeyDown}
            />

            <div className="lfo-curve__caption">
                <span className="lfo-curve__reading" aria-live="polite">{describePoint(curve, Math.min(selected, curve.length - 1))}</span>
                <button type="button" className="lfo-curve__reset" onClick={reset}>Reset</button>
            </div>
        </div>
    );
}
