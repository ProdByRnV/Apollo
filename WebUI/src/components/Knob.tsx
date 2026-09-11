/*
    270 degrees of travel, drag-vertical.

    Fine adjustment on shift, because the alternative — a control you cannot
    place accurately — is the single most common complaint about software
    synthesisers.

    The dial is an SVG the component re-renders; at 30 Hz that is two path
    strings and a text node, which React handles without complaint. The canvases
    elsewhere on the page are the ones that must not go through React, and they
    do not (state/telemetry.ts).
*/

import { useRef, useState } from 'react';

import { formatPlain } from '../params/format';
import { stepCount, toNormalized, toPlain } from '../params/mapping';
import {
    commitValue,
    setHeld,
    useParameterDefinition,
    useParameterValue,
    valueOf,
} from '../state/parameters';
import { gesture } from '../bridge/bridge';
import { useIsModulated } from '../state/modulation';
import { useMidiChrome } from './midiChrome';

const START_ANGLE = -135;
const END_ANGLE = 135;
const RADIUS = 18;
const CENTRE = 23;

/** Full travel in pixels of vertical drag. */
const DRAG_RANGE = 190;

function polar(radius: number, degrees: number): [number, number] {
    const radians = ((degrees - 90) * Math.PI) / 180;
    return [CENTRE + radius * Math.cos(radians), CENTRE + radius * Math.sin(radians)];
}

function arcPath(from: number, to: number): string {
    if (Math.abs(to - from) < 0.15) return '';

    const [x0, y0] = polar(RADIUS, from);
    const [x1, y1] = polar(RADIUS, to);
    const large = Math.abs(to - from) > 180 ? 1 : 0;
    const sweep = to > from ? 1 : 0;

    return `M ${x0.toFixed(2)} ${y0.toFixed(2)}`
        + ` A ${RADIUS} ${RADIUS} 0 ${large} ${sweep} ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

export interface KnobProps {
    id: string;
    label: string;
}

export function Knob({ id, label }: KnobProps): JSX.Element | null {
    const definition = useParameterDefinition(id);
    const normalized = useParameterValue(id);

    const rootRef = useRef<HTMLDivElement>(null);
    const drag = useRef({ from: 0, value: 0 });

    // React state rather than a ref, and the distinction is not academic: the
    // held flag is *rendered*, and a ref mutated on pointer-up re-renders
    // nothing, so the control stayed lit after the hand left it. The hand-built
    // page wrote the attribute directly and could not have this problem; this is
    // the shape of mistake a migration to a declarative tree invites.
    const [dragging, setDragging] = useState(false);

    const chrome = useMidiChrome(id, definition?.name ?? id);

    // Green means motion here exactly as it does everywhere else in the
    // interface: a knob lights when a routing is actually moving it (§24.2).
    const modulated = useIsModulated(id);

    if (!definition) return null;

    // A parameter whose range crosses zero fills outward from the centre, so a
    // detune of zero looks like a centred control rather than a quarter turn.
    const bipolar = definition.min < 0 && definition.max > 0;
    const originAngle = bipolar
        ? START_ANGLE + (END_ANGLE - START_ANGLE) * toNormalized(definition, 0)
        : START_ANGLE;

    const plain = toPlain(definition, normalized);
    const angle = START_ANGLE + (END_ANGLE - START_ANGLE) * normalized;
    const text = formatPlain(definition, plain);

    const [innerX, innerY] = polar(6, angle);
    const [outerX, outerY] = polar(11.5, angle);

    const onPointerDown = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (event.button !== 0) return;

        rootRef.current?.setPointerCapture(event.pointerId);

        drag.current = { from: event.clientY, value: valueOf(id) };
        setDragging(true);
        setHeld(id, true);

        gesture(id, 'begin');
        event.preventDefault();
    };

    const onPointerMove = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (!dragging) return;

        const scale = event.shiftKey ? 0.2 : 1;
        const delta = ((drag.current.from - event.clientY) / DRAG_RANGE) * scale;

        commitValue(id, drag.current.value + delta);
    };

    const release = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (!dragging) return;

        setDragging(false);
        setHeld(id, false);

        try {
            rootRef.current?.releasePointerCapture(event.pointerId);
        } catch {
            // Already released; nothing to undo.
        }

        gesture(id, 'end');
    };

    const nudge = (next: number): void => {
        gesture(id, 'begin');
        commitValue(id, next);
        gesture(id, 'end');
    };

    // A stepped parameter moves one position per press; a continuous one moves
    // in hundredths, which is fine enough to be useful and coarse enough to
    // cross the range without holding the key down for a minute.
    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        const steps = stepCount(definition);
        const unit = steps > 1 ? 1 / (steps - 1) : 0.01;
        const current = valueOf(id);

        let next: number;

        switch (event.key) {
            case 'ArrowUp':
            case 'ArrowRight': next = current + unit * (event.shiftKey ? 0.2 : 1); break;
            case 'ArrowDown':
            case 'ArrowLeft': next = current - unit * (event.shiftKey ? 0.2 : 1); break;
            case 'PageUp': next = current + unit * 10; break;
            case 'PageDown': next = current - unit * 10; break;
            case 'Home': next = 0; break;
            case 'End': next = 1; break;

            // The keyboard equivalent of double-clicking. Without it the extremes
            // are reachable from the keyboard but the default is not, which
            // leaves anyone not using a mouse unable to undo a nudge
            // (CLAUDE.md §39).
            case 'Delete':
            case 'Backspace': next = toNormalized(definition, definition.default); break;

            default: return;
        }

        event.preventDefault();
        nudge(next);
    };

    return (
        <div
            ref={rootRef}
            className="knob"
            tabIndex={0}
            role="slider"
            aria-label={definition.name}
            aria-valuemin={definition.min}
            aria-valuemax={definition.max}
            aria-valuenow={Number(plain.toFixed(4))}
            aria-valuetext={text}
            title={chrome.title}
            data-midi-id={id}
            data-midi={chrome.state}
            data-modulated={modulated ? 'true' : 'false'}
            data-held={dragging ? 'true' : 'false'}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={release}
            onPointerCancel={release}
            onDoubleClick={() => nudge(toNormalized(definition, definition.default))}
            onKeyDown={onKeyDown}
        >
            <svg className="knob__dial" viewBox="0 0 46 46" aria-hidden="true">
                <path className="knob__track" d={arcPath(START_ANGLE, END_ANGLE)} />
                <path
                    className="knob__arc"
                    d={arcPath(Math.min(originAngle, angle), Math.max(originAngle, angle))}
                />
                <circle className="knob__cap" cx={CENTRE} cy={CENTRE} r={11.5} />
                <line
                    className="knob__pointer"
                    x1={innerX.toFixed(2)}
                    y1={innerY.toFixed(2)}
                    x2={outerX.toFixed(2)}
                    y2={outerY.toFixed(2)}
                />
            </svg>

            <div className="knob__value">{text}</div>
            <div className="knob__label">{label}</div>

            <span
                className="midi-badge"
                aria-hidden="true"
                hidden={chrome.state === undefined}
                data-state={chrome.state}
            >
                {chrome.badge}
            </span>
        </div>
    );
}
