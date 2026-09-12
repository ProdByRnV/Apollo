/*
    A modulator's trace.

    An envelope or an LFO drawn as what it is doing rather than as the shape it
    was configured with (CLAUDE.md §26.1). The distinction is the whole point: an
    outline with a playhead shows the settings, and settings and behaviour part
    company the moment anything is modulated, retriggered or clamped — and it is
    the outline that is wrong when they do.

    The native side sends a second of history at 128 Hz and the page draws it
    whole, so a dropped frame costs one repaint rather than a gap in the history
    (UI_BINDINGS.md §10.5).
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import type { ModulatorFrame } from '../bridge/protocol';
import { ENVELOPE_STAGES } from '../params/labels';
import { cssColour, decodePoints, fitCanvas, strokeSeries, strokeZeroLine } from '../canvas/draw';
import { subscribeToModulator } from '../state/telemetry';

const ASPECT = 0.34;

export interface TraceProps {
    source: string;
    label: string;
}

export function Trace({ source, label }: TraceProps): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const pointsRef = useRef<number[] | null>(null);

    const [routed, setRouted] = useState(false);
    const [reading, setReading] = useState('—');

    const bipolar = source.startsWith('lfo');

    // An envelope never goes below zero, so its floor is the bottom of the
    // canvas; an LFO swings both ways about the middle.
    const zero = bipolar ? 0.5 : 0.97;

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');

        if (!root || !canvas || !context) return;

        const size = fitCanvas(root, canvas, ASPECT);

        context.clearRect(0, 0, size.width, size.height);
        strokeZeroLine(context, size, cssColour(root, '--scope-grid', '#d9c99f'), zero);

        strokeSeries(context, pointsRef.current, size, {
            colour: cssColour(root, '--trace-line', '#2e8b46'),
            zero,
        });
    }, [zero]);

    useEffect(() => {
        const onFrame = (frame: ModulatorFrame): void => {
            // Absent for an unrouted modulator, and that is not an omission: the
            // engine does not advance one, so there is no trace to send and the
            // page draws the zero line and says why.
            pointsRef.current = decodePoints(frame.points);

            setRouted(frame.routed);

            if (!frame.routed) {
                setReading('unrouted');
            } else if (bipolar) {
                setReading(frame.current.toFixed(2));
            } else {
                const stage = ENVELOPE_STAGES[frame.stage] ?? 'idle';
                setReading(`${stage} · ${frame.current.toFixed(2)}`);
            }

            draw();
        };

        return subscribeToModulator(source, onFrame);
    }, [source, bipolar, draw]);

    useEffect(() => {
        draw();

        window.addEventListener('resize', draw);
        return () => { window.removeEventListener('resize', draw); };
    }, [draw]);

    return (
        <div ref={rootRef} className="trace" data-trace-source={source} data-routed={routed}>
            <canvas ref={canvasRef} className="trace__canvas" />

            <div className="trace__caption">
                <span className="trace__name">{label}</span>
                <span className="trace__reading">{reading}</span>
            </div>
        </div>
    );
}
