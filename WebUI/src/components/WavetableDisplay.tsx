/*
    One cycle of the wave an oscillator is actually reading, at the position it
    is actually reading it (PRD §30.2).

    Not the table's first frame, and not the parameter's value: a modulated
    position sweeps, and this is the one picture whose job is to show that sweep
    happening.
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import type { WavetableFrame } from '../bridge/protocol';
import { cssColour, decodePoints, fitCanvas, strokeSeries, strokeZeroLine } from '../canvas/draw';
import { subscribeToWavetable } from '../state/telemetry';

const ASPECT = 0.55;

export interface WavetableDisplayProps {
    oscillator: number;
}

export function WavetableDisplay({ oscillator }: WavetableDisplayProps): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const pointsRef = useRef<number[] | null>(null);

    const [reading, setReading] = useState('—');

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');

        if (!root || !canvas || !context) return;

        const size = fitCanvas(root, canvas, ASPECT);

        context.clearRect(0, 0, size.width, size.height);
        strokeZeroLine(context, size, cssColour(root, '--scope-grid', '#2b3138'));

        strokeSeries(context, pointsRef.current, size, {
            colour: cssColour(root, '--wave-line', '#f7ef8a'),
            zero: 0.5,
            weight: 1.6,
        });
    }, []);

    useEffect(() => {
        const onFrame = (frame: WavetableFrame): void => {
            pointsRef.current = decodePoints(frame.points);

            // The effective position, which is the parameter plus whatever the
            // matrix is adding. Shown as a number so a sweep can be read exactly
            // and not only watched.
            setReading(`${Math.round(frame.position * 100)} %`);

            draw();
        };

        return subscribeToWavetable(oscillator, onFrame);
    }, [oscillator, draw]);

    useEffect(() => {
        draw();

        window.addEventListener('resize', draw);
        return () => { window.removeEventListener('resize', draw); };
    }, [draw]);

    return (
        <div ref={rootRef} className="wavetable" data-oscillator={oscillator}>
            <canvas ref={canvasRef} className="wavetable__canvas" />

            <div className="wavetable__caption">
                <span className="wavetable__name">wave</span>
                <span className="wavetable__reading">{reading}</span>
            </div>
        </div>
    );
}
