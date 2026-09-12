/*
    An oscilloscope.

    A canvas rather than an SVG path: this redraws thirty times a second, and
    rebuilding a path's `d` attribute at that rate would hand the browser a new
    string to parse on every frame for a picture it throws away immediately.

    Everything drawn here comes from the frame the engine sent. The page does no
    triggering, no smoothing and no scaling of its own — a scope that prettied up
    its input would be showing its own arithmetic rather than the audio, and the
    whole reason for having one is to see what is actually there.
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import type { ScopeFrame } from '../bridge/protocol';
import { cssColour, decodePoints, fitCanvas, strokeSeries, strokeZeroLine } from '../canvas/draw';
import { subscribeToScope } from '../state/telemetry';

const ASPECT = 0.42;

/** Zoom, not auto-gain. Apollo's gain staging is conservative, so an ordinary
    signal draws a small trace (ADR-0047) — which is honest, and after six of
    them appeared on one page became a standing annoyance as well. The answer is
    a control the user turns, not a scale the page chooses: the factor is stated
    on the button, so a magnified trace is never mistaken for a loud one, and the
    decibel reading beside it never changes.
*/
const ZOOM_FACTORS = [1, 2, 4, 8] as const;

export interface ScopeProps {
    source: string;
    /** Only where the module heading does not already name the source. */
    label?: string | undefined;
}

export function Scope({ source, label }: ScopeProps): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const pointsRef = useRef<number[] | null>(null);

    const [zoomIndex, setZoomIndex] = useState(0);
    const [silent, setSilent] = useState(true);
    const [reading, setReading] = useState('silent');
    const [free, setFree] = useState(false);

    const zoom = ZOOM_FACTORS[zoomIndex] ?? 1;

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');

        if (!root || !canvas || !context) return;

        const size = fitCanvas(root, canvas, ASPECT);

        context.clearRect(0, 0, size.width, size.height);
        strokeZeroLine(context, size, cssColour(root, '--scope-grid', '#2b3138'));

        strokeSeries(context, pointsRef.current, size, {
            colour: cssColour(root, '--scope-trace', '#f7ef8a'),
            zero: 0.5,
            scale: zoom,
        });
    }, [zoom]);

    useEffect(() => {
        const onFrame = (frame: ScopeFrame): void => {
            pointsRef.current = decodePoints(frame.points);

            setSilent(frame.silent);

            // A number as well as a picture, because a trace alone cannot tell
            // you whether a quiet signal is quiet or absent (CLAUDE.md §39).
            setReading(frame.silent
                ? 'silent'
                : (frame.peak >= 1
                    ? '0.0 dB'
                    : `${(20 * Math.log10(Math.max(frame.peak, 1e-6))).toFixed(1)} dB`));

            // Said in words rather than by a colour: a free-running trace is one
            // that will not stand still, and knowing why is the difference
            // between a puzzle and a fact.
            setFree(!frame.silent && !frame.triggered);

            draw();
        };

        return subscribeToScope(source, onFrame);
    }, [source, draw]);

    // Frames redraw the picture anyway, so this only matters for a scope that is
    // silent and therefore still.
    useEffect(() => {
        draw();

        window.addEventListener('resize', draw);
        return () => { window.removeEventListener('resize', draw); };
    }, [draw]);

    return (
        <div ref={rootRef} className="scope" data-scope-source={source} data-silent={silent}>
            <canvas ref={canvasRef} className="scope__canvas" />

            <div className="scope__caption">
                {label ? <span className="scope__name">{label}</span> : null}
                <span className="scope__reading">{reading}</span>
                <span className="scope__flag">{free ? 'FREE' : ''}</span>
                <button
                    type="button"
                    className="scope__zoom"
                    title="Display zoom — magnifies the trace only, not the reading"
                    data-active={zoomIndex > 0}
                    onClick={() => setZoomIndex((index) => (index + 1) % ZOOM_FACTORS.length)}
                >
                    ×{zoom}
                </button>
            </div>
        </div>
    );
}
