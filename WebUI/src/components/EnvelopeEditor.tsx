/*
    Draws an envelope, and is how it is shaped by hand (Phase 13f-2, PRD §15.1).

    A note held to its sustain and then released, left to right: delay, attack,
    hold, decay, sustain, release. The corners are handles, and the small
    diamond half way along each moving segment is the curve.

      - DRAG A CORNER sideways to change its stage's time. The decay's corner
        also moves up and down, which is the sustain level. Shift for fine.
      - CORNERS IN ONE PLACE: a stage of no time puts its corner on the one
        before it. A drag takes the earlier; Alt-drag takes the later, which is
        how a hold is pulled out from under the attack.
      - DRAG A DIAMOND up or down to bend the curve. There is one curve control,
        so every segment bends with it; up bows the segment you hold upwards.
      - DOUBLE-CLICK a handle to put what it controls back to its default.
      - FROM THE KEYBOARD, with the editor focused: [ and ] choose a handle,
        the arrows move it (Shift for fine), and Delete resets it.

    NOT A NEW CONTROL. Every handle is one of the knobs below it moved another
    way: the same parameters, the same gestures to the host, so a drag is
    automation, undo and MIDI Learn exactly as a knob turn is, and nothing here
    is stored anywhere a knob's value is not.

    WHAT IS DRAWN IS WHAT PLAYS, on a time axis that is not linear: see
    params/envelopeShape.ts for both.
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import { gesture } from '../bridge/bridge';
import type { ParameterDefinition } from '../bridge/protocol';
import { cssColour, fitCanvas, pointerOnCanvas } from '../canvas/draw';
import { useRedrawOnResize } from '../canvas/useRedrawOnResize';
import {
    TIMED_STAGES,
    curveHandle,
    dragCurve,
    hitTest,
    levelAt,
    spanOf,
    stageHandle,
    vertices,
} from '../params/envelopeShape';
import type { EnvelopeShape, Grab, TimedStage, Vertex } from '../params/envelopeShape';
import { formatPlain } from '../params/format';
import { toNormalized, toPlain } from '../params/mapping';
import {
    commitValue,
    definitionOf,
    setHeld,
    useParameterDefinition,
    useParameterValue,
    valueOf,
} from '../state/parameters';

const ASPECT = 0.42;

/** Padding inside the canvas, in CSS pixels, so a handle at an edge is still a
    whole handle and can be grabbed. */
const PAD = 8;

/** Room under the baseline for the stage letters. */
const FOOT = 11;

const POINT_RADIUS = 4.5;
const GRAB_RADIUS = 10;

/** CSS pixels of vertical drag that take the curve from one end to the other. */
const CURVE_DRAG_RANGE = 120;

/** Fine adjustment, as a knob's (Knob.tsx). */
const FINE = 0.2;

/** What [ and ] step through, in the order they sit. */
type Handle = TimedStage | 'curve';
const HANDLES: readonly Handle[] = [...TIMED_STAGES, 'curve'];

const STAGE_NAMES: Record<Handle, string> = {
    delay: 'Delay',
    attack: 'Attack',
    hold: 'Hold',
    decay: 'Decay',
    release: 'Release',
    curve: 'Curve',
};

/** The letters drawn under each stage, by its index in vertices(). */
const STAGE_LETTERS = ['D', 'A', 'H', 'D', 'S', 'R'];

const clamp = (value: number, low: number, high: number): number => Math.min(high, Math.max(low, value));

interface Geometry {
    width: number;
    height: number;
    span: number;
}

const xOf = (units: number, g: Geometry): number => PAD + (units / g.span) * (g.width - 2 * PAD);
const yOf = (level: number, g: Geometry): number => PAD + (1 - level) * (g.height - PAD - FOOT);

/** CSS pixels per width unit, and per unit of level. */
const pxPerUnit = (g: Geometry): number => (g.width - 2 * PAD) / g.span;
const pxPerLevel = (g: Geometry): number => g.height - PAD - FOOT;

/** The parameter ids a handle moves. */
function idsOf(envelope: number, handle: Handle): string[] {
    const p = `env${envelope}_`;
    if (handle === 'curve') return [`${p}curve`];
    if (handle === 'decay') return [`${p}decay`, `${p}sustain`];
    return [`${p}${handle}`];
}

/** Plain value to normalised, with the engine's own mapping. */
function normalise(id: string, plain: number): number {
    const definition = definitionOf(id);
    return definition ? toNormalized(definition, plain) : clamp(plain, 0, 1);
}

function plainText(definition: ParameterDefinition | undefined, normalised: number): string {
    return definition ? formatPlain(definition, toPlain(definition, normalised)) : '';
}

interface Drag {
    grab: Grab;
    ids: string[];
    startX: number;
    startY: number;
    start: EnvelopeShape;
    geometry: Geometry;
}

export function EnvelopeEditor({ envelope }: { envelope: number }): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);

    const p = `env${envelope}_`;

    // Seven subscriptions, in a fixed order: each re-renders this editor when
    // its own parameter moves, from a knob, the host or the matrix's owner.
    const delay = useParameterValue(`${p}delay`);
    const attack = useParameterValue(`${p}attack`);
    const hold = useParameterValue(`${p}hold`);
    const decay = useParameterValue(`${p}decay`);
    const sustain = useParameterValue(`${p}sustain`);
    const release = useParameterValue(`${p}release`);
    const curve = useParameterValue(`${p}curve`);

    const sustainDefinition = useParameterDefinition(`${p}sustain`);
    const curveDefinition = useParameterDefinition(`${p}curve`);

    const shape: EnvelopeShape = {
        delay,
        attack,
        hold,
        decay,
        release,
        sustain: sustainDefinition ? toPlain(sustainDefinition, sustain) : sustain,
        curve: curveDefinition ? toPlain(curveDefinition, curve) : 0,
    };

    const shapeRef = useRef(shape);
    shapeRef.current = shape;

    const [selected, setSelected] = useState<Handle>('attack');
    const selectedRef = useRef(selected);
    selectedRef.current = selected;

    const dragRef = useRef<Drag | null>(null);

    /** The span drawn last. Held while a hand is on a handle, so the picture
        does not rescale under it; refitted when the hand lets go. */
    const spanRef = useRef(spanOf(shape));

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');
        if (!root || !canvas || !context) return;

        const { width, height, ratio } = fitCanvas(root, canvas, ASPECT);
        const env = shapeRef.current;

        if (!dragRef.current) spanRef.current = spanOf(env);
        const g: Geometry = { width: width / ratio, height: height / ratio, span: spanRef.current };

        const grid = cssColour(root, '--scope-grid', '#2b3138');
        const line = cssColour(root, '--wave-line', '#f7ef8a');
        const muted = cssColour(root, '--text-dim', '#7c848c');
        const ground = cssColour(root, '--panel', '#181b1f');

        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, g.width, g.height);

        const corners = vertices(env);
        const end = corners[corners.length - 1]!;
        const base = Math.round(yOf(0, g)) + 0.5;

        // Grid: the baseline solid, full level and half dashed, and a faint
        // rule at every corner so the stages read as stages.
        context.strokeStyle = grid;
        context.lineWidth = 1;

        context.beginPath();
        context.moveTo(PAD, base);
        context.lineTo(g.width - PAD, base);
        context.stroke();

        context.setLineDash([2, 3]);
        for (const level of [0.5, 1]) {
            const y = Math.round(yOf(level, g)) + 0.5;
            context.beginPath();
            context.moveTo(PAD, y);
            context.lineTo(g.width - PAD, y);
            context.stroke();
        }

        for (const corner of corners.slice(1)) {
            const x = Math.round(xOf(corner.x, g)) + 0.5;
            context.beginPath();
            context.moveTo(x, PAD);
            context.lineTo(x, base);
            context.stroke();
        }
        context.setLineDash([]);

        // Each stage's letter under it, where there is room for one.
        context.fillStyle = muted;
        context.font = '8px ui-monospace, monospace';
        context.textAlign = 'center';
        context.textBaseline = 'top';

        for (let i = 1; i < corners.length; ++i) {
            const left = xOf(corners[i - 1]!.x, g);
            const right = xOf(corners[i]!.x, g);
            if (right - left >= 9) context.fillText(STAGE_LETTERS[i - 1]!, (left + right) / 2, base + 2);
        }

        // The envelope, one sample per CSS pixel, filled faintly to the base.
        const startX = xOf(0, g);
        const endX = xOf(end.x, g);

        context.beginPath();
        context.moveTo(startX, yOf(0, g));
        for (let x = startX; x <= endX; x += 1) {
            const units = ((x - PAD) / pxPerUnit(g));
            context.lineTo(x, yOf(levelAt(env, units), g));
        }
        context.lineTo(endX, yOf(0, g));

        context.strokeStyle = line;
        context.lineWidth = 1.6;
        context.lineJoin = 'round';
        context.stroke();

        context.closePath();
        context.fillStyle = line;
        context.globalAlpha = 0.08;
        context.fill();
        context.globalAlpha = 1;

        // The curve's diamonds, then the corners over them. A diamond is gold
        // when the curve is bent and grey when it is a straight line; selected
        // or not is told by size as well as colour (CLAUDE.md §24.2).
        const curveSelected = selectedRef.current === 'curve';

        for (const segment of ['attack', 'decay', 'release'] as const) {
            const handle = curveHandle(env, segment);
            if (!handle) continue;

            const x = xOf(handle.x, g);
            const y = yOf(handle.level, g);
            const r = curveSelected ? 4 : 3;

            context.beginPath();
            context.moveTo(x, y - r);
            context.lineTo(x + r, y);
            context.lineTo(x, y + r);
            context.lineTo(x - r, y);
            context.closePath();
            context.fillStyle = (env.curve !== 0 || curveSelected) ? line : muted;
            context.fill();
        }

        for (const stage of TIMED_STAGES) {
            const corner = stageHandle(env, stage);
            const x = xOf(corner.x, g);
            const y = yOf(corner.level, g);
            const isSelected = stage === selectedRef.current;

            context.beginPath();
            context.arc(x, y, isSelected ? POINT_RADIUS + 1 : POINT_RADIUS, 0, 2 * Math.PI);

            if (isSelected) {
                context.fillStyle = line;
                context.fill();
            } else {
                context.fillStyle = ground;
                context.fill();
                context.strokeStyle = line;
                context.lineWidth = 1.4;
                context.stroke();
            }
        }
    }, []);

    useEffect(draw, [draw, delay, attack, hold, decay, sustain, release, curve, selected]);
    useRedrawOnResize(rootRef, draw);

    //--------------------------------------------------------------------------
    // Editing. Every change goes through commitValue, as a knob's does, inside
    // a begin/end gesture so a host records it as one movement.

    /** Puts each of @p ids back to its default, as one gesture. */
    const resetIds = useCallback((ids: string[]) => {
        for (const id of ids) {
            const definition = definitionOf(id);
            if (!definition) continue;

            gesture(id, 'begin');
            commitValue(id, toNormalized(definition, definition.default));
            gesture(id, 'end');
        }
    }, []);

    useEffect(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        if (!root || !canvas) return;

        const pointer = (event: MouseEvent): ReturnType<typeof pointerOnCanvas> =>
            pointerOnCanvas(root, canvas, ASPECT, event);

        const hit = (event: MouseEvent, latest: boolean): Grab | null => {
            const at = pointer(event);
            const g: Geometry = { width: at.width, height: at.height, span: spanRef.current };
            const toPixel = (vertex: Vertex): [number, number] => [xOf(vertex.x, g), yOf(vertex.level, g)];
            return hitTest(shapeRef.current, toPixel, at.x, at.y, GRAB_RADIUS, latest);
        };

        const handleOf = (grab: Grab): Handle => (grab.kind === 'stage' ? grab.stage : 'curve');

        const onPointerDown = (event: PointerEvent): void => {
            if (event.button !== 0 || document.body.dataset.midiMode === 'true') return;

            const grab = hit(event, event.altKey);
            if (!grab) return;

            event.preventDefault();
            canvas.focus();
            canvas.setPointerCapture(event.pointerId);

            const ids = idsOf(envelope, handleOf(grab));
            for (const id of ids) {
                setHeld(id, true);
                gesture(id, 'begin');
            }

            const at = pointer(event);
            dragRef.current = {
                grab,
                ids,
                startX: at.x,
                startY: at.y,
                start: shapeRef.current,
                geometry: { width: at.width, height: at.height, span: spanRef.current },
            };

            setSelected(handleOf(grab));
        };

        const onPointerMove = (event: PointerEvent): void => {
            const drag = dragRef.current;
            if (!drag) return;

            const at = pointer(event);
            const scale = event.shiftKey ? FINE : 1;
            const across = (at.x - drag.startX) * scale;
            const up = (drag.startY - at.y) * scale;
            const { grab, start, geometry: g } = drag;

            if (grab.kind === 'curve') {
                const next = dragCurve(start, grab.segment, start.curve, up / CURVE_DRAG_RANGE);
                commitValue(`${p}curve`, normalise(`${p}curve`, next.curve));
                return;
            }

            commitValue(`${p}${grab.stage}`, start[grab.stage] + across / pxPerUnit(g));

            if (grab.stage === 'decay') {
                const level = clamp(start.sustain + up / pxPerLevel(g), 0, 1);
                commitValue(`${p}sustain`, normalise(`${p}sustain`, level));
            }
        };

        const onPointerUp = (event: PointerEvent): void => {
            const drag = dragRef.current;
            if (!drag) return;

            dragRef.current = null;
            if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);

            for (const id of drag.ids) {
                gesture(id, 'end');
                setHeld(id, false);
            }

            // Let go: refit the picture to what the envelope now is.
            draw();
        };

        const onDoubleClick = (event: MouseEvent): void => {
            const grab = hit(event, event.altKey);
            if (grab) resetIds(idsOf(envelope, handleOf(grab)));
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
    }, [draw, envelope, p, resetIds]);

    /** One keyboard step on @p id, as a knob's nudge: its own gesture. */
    const nudge = (id: string, normalised: number): void => {
        gesture(id, 'begin');
        commitValue(id, normalised);
        gesture(id, 'end');
    };

    const onKeyDown = (event: React.KeyboardEvent<HTMLCanvasElement>): void => {
        const index = HANDLES.indexOf(selected);
        const fine = event.shiftKey ? FINE : 1;
        const env = shapeRef.current;

        const across = (sign: number): void => {
            if (selected === 'curve') return;
            const id = `${p}${selected}`;
            nudge(id, valueOf(id) + sign * 0.02 * fine);
        };

        const vertical = (sign: number): void => {
            if (selected === 'decay') {
                nudge(`${p}sustain`, normalise(`${p}sustain`, clamp(env.sustain + sign * 0.05 * fine, 0, 1)));
            } else if (selected === 'curve') {
                nudge(`${p}curve`, normalise(`${p}curve`, clamp(env.curve + sign * 0.1 * fine, -1, 1)));
            }
        };

        switch (event.key) {
            case '[': setSelected(HANDLES[Math.max(0, index - 1)]!); break;
            case ']': setSelected(HANDLES[Math.min(HANDLES.length - 1, index + 1)]!); break;
            case 'ArrowLeft': across(-1); break;
            case 'ArrowRight': across(1); break;
            case 'ArrowUp': vertical(1); break;
            case 'ArrowDown': vertical(-1); break;
            case 'Delete':
            case 'Backspace': resetIds(idsOf(envelope, selected)); break;
            default:
                return;
        }

        event.preventDefault();
    };

    // What the selected handle holds, in the knobs' own words.
    const timeDefinition = useParameterDefinition(selected === 'curve' ? `${p}curve` : `${p}${selected}`);
    const selectedValue = selected === 'curve' ? curve : { delay, attack, hold, decay, release }[selected];

    let reading = `${STAGE_NAMES[selected]} ${plainText(timeDefinition, selectedValue)}`;
    if (selected === 'decay') reading += ` · Sustain ${plainText(sustainDefinition, sustain)}`;

    return (
        <div ref={rootRef} className="env-shape" data-envelope-shape={envelope}>
            <canvas
                ref={canvasRef}
                className="env-shape__canvas"
                tabIndex={0}
                role="application"
                aria-label={`Envelope ${envelope} shape`}
                title={'Drag a corner to change a time; the decay\'s also sets the sustain. '
                    + 'Drag a diamond to bend the curve. Alt-drag takes the later of two corners in one place. '
                    + 'Double-click to reset.'}
                aria-description={'Drag a corner to change its stage\'s time; the decay\'s corner also sets the '
                    + 'sustain. Drag a diamond to bend the curve. Double-click a handle to reset it. With the '
                    + 'shape focused, [ and ] choose a handle, the arrows move it and Delete resets it.'}
                onKeyDown={onKeyDown}
            />

            <div className="env-shape__caption">
                <span className="env-shape__reading" aria-live="polite">{reading}</span>
                {/* Stage letters are drawn; say what the axis is in words too. */}
                <span className="env-shape__note">time not to scale</span>
            </div>
        </div>
    );
}
