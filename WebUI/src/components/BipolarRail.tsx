/*
    The matrix depth control.

    Fills outward from the centre, so the sign of a routing is a direction rather
    than a minus sign to be noticed.
*/

import { useRef, useState } from 'react';

import { toNormalized, toPlain } from '../params/mapping';
import {
    commitValue,
    setHeld,
    useParameterDefinition,
    useParameterValue,
    valueOf,
} from '../state/parameters';
import { gesture } from '../bridge/bridge';

export interface BipolarRailProps {
    id: string;
}

export function BipolarRail({ id }: BipolarRailProps): JSX.Element | null {
    const definition = useParameterDefinition(id);
    const normalized = useParameterValue(id);

    const railRef = useRef<HTMLDivElement>(null);
    const drag = useRef({ from: 0, value: 0 });

    // React state rather than a ref, and the distinction is not academic: the
    // held flag is *rendered*, and a ref mutated on pointer-up re-renders
    // nothing, so the control stayed lit after the hand left it. The hand-built
    // page wrote the attribute directly and could not have this problem; this is
    // the shape of mistake a migration to a declarative tree invites.
    const [dragging, setDragging] = useState(false);

    if (!definition) return null;

    const plain = toPlain(definition, normalized);
    const fraction = Math.min(1, Math.max(0, normalized));
    const negative = fraction < 0.5;
    const text = `${plain > 0 ? '+' : ''}${(plain * 100).toFixed(0)} %`;

    const onPointerDown = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (event.button !== 0) return;

        railRef.current?.setPointerCapture(event.pointerId);

        drag.current = { from: event.clientX, value: valueOf(id) };
        setDragging(true);
        setHeld(id, true);

        gesture(id, 'begin');
        event.preventDefault();
    };

    const onPointerMove = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (!dragging) return;

        const scale = event.shiftKey ? 0.2 : 1;
        const rail = railRef.current;
        const width = rail && rail.clientWidth > 0 ? rail.clientWidth : 1;

        commitValue(id, drag.current.value + ((event.clientX - drag.current.from) / width) * scale);
    };

    const release = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (!dragging) return;

        setDragging(false);
        setHeld(id, false);

        try {
            railRef.current?.releasePointerCapture(event.pointerId);
        } catch {
            // Already released.
        }

        gesture(id, 'end');
    };

    const nudge = (next: number): void => {
        gesture(id, 'begin');
        commitValue(id, next);
        gesture(id, 'end');
    };

    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        const current = valueOf(id);
        let next: number;

        switch (event.key) {
            case 'ArrowRight': next = current + (event.shiftKey ? 0.002 : 0.01); break;
            case 'ArrowLeft': next = current - (event.shiftKey ? 0.002 : 0.01); break;
            case 'Home': next = 0; break;
            case 'End': next = 1; break;

            // Reaches the centre, which is the one value a depth control needs
            // most and the one Home and End cannot reach.
            case 'Delete':
            case 'Backspace': next = toNormalized(definition, definition.default); break;

            default: return;
        }

        event.preventDefault();
        nudge(next);
    };

    return (
        <div className="bipolar" data-midi-id={id}>
            <div
                ref={railRef}
                className="bipolar__rail"
                tabIndex={0}
                role="slider"
                aria-label={definition.name}
                aria-valuemin={definition.min}
                aria-valuemax={definition.max}
                aria-valuenow={Number(plain.toFixed(3))}
                aria-valuetext={text}
                data-negative={negative ? 'true' : 'false'}
                data-held={dragging ? 'true' : 'false'}
                onPointerDown={onPointerDown}
                onPointerMove={onPointerMove}
                onPointerUp={release}
                onPointerCancel={release}
                onDoubleClick={() => nudge(toNormalized(definition, definition.default))}
                onKeyDown={onKeyDown}
            >
                <div className="bipolar__centre" />
                <div
                    className="bipolar__fill"
                    style={{
                        left: `${negative ? fraction * 100 : 50}%`,
                        width: `${Math.abs(fraction - 0.5) * 100}%`,
                    }}
                />
            </div>

            <div className="bipolar__value">{text}</div>
        </div>
    );
}
