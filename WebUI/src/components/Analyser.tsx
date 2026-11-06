/*
    The output analyser (18d): the spectrum of what leaves Apollo, on its own.

    The equaliser has drawn the spectrum behind its curve since ADR-0101, but
    only while it is in the rack, and only of the signal around it. This is the
    whole instrument's output, always: the question it answers is "where is the
    energy in this sound", which is asked of a bass with no equaliser anywhere
    near it.

    The engine does the transform, from the output scope's ring, so the audio
    thread pays nothing for it (Engine/SpectrumFrame.h). The levels arrive with
    the equaliser's, thirty times a second, on the same axis and with the same
    tilt and ballistics, so the two pictures read alike.

    WHAT IS DRAWN. The live level as a gold line over a faint fill; behind it a
    dimmer peak line that falls slowly, so a note that has stopped can still be
    read; a grid in hertz and decibels. Pointing at the picture reads the
    frequency there, the nearest note, and the level, which is how a
    fundamental, a resonance or a harsh band is found by ear and then named.

    THE LEVELS ARE TILTED, by 4.5 dB per octave about 1 kHz, as every analyser
    a musician has used is, and the caption says so: untilted, almost any sound
    is a slope that leaves the top of the picture empty.
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import { cssColour, fitCanvas } from '../canvas/draw';
import { useRedrawOnResize } from '../canvas/useRedrawOnResize';
import {
    ANALYSER_TILT_DB_PER_OCTAVE,
    formatHz,
    frequencyAt,
    holdPeaks,
    levelAt,
    noteOf,
    pointAt,
    positionOfHz,
} from '../params/analyser';
import { subscribeToSpectrum } from '../state/telemetry';
import { Module } from './Module';

/** Height on a narrow window from the aspect, capped on a wide one, as the
    equaliser's curve is: more height buys nothing on a decibel axis. */
const ASPECT = 0.3;
const MIN_HEIGHT = 140;
const MAX_HEIGHT = 240;

/** The vertical scale, in dB, the same as the spectrum behind the equaliser's
    curve, so a level reads the same on both. */
const TOP_DB = 6;
const BOTTOM_DB = -90;

const GRID_DB = [0, -24, -48, -72];
const GRID_HZ = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const GRID_LABELS: Record<number, string> = {
    50: '50', 100: '100', 200: '200', 500: '500', 1000: '1k', 2000: '2k', 5000: '5k', 10000: '10k',
};

function yForDb(db: number, height: number): number {
    const t = (TOP_DB - db) / (TOP_DB - BOTTOM_DB);
    return Math.min(1, Math.max(0, t)) * height;
}

function toLevels(tenths: readonly number[]): Float32Array {
    const levels = new Float32Array(tenths.length);
    for (let i = 0; i < tenths.length; ++i) levels[i] = (tenths[i] ?? 0) / 10;
    return levels;
}

/** Traces levels evenly across the width, through the midpoints between
    points, as the equaliser's spectrum is drawn. */
function trace(context: CanvasRenderingContext2D, levels: Float32Array, width: number, height: number): void {
    const last = levels.length - 1;
    const x = (i: number): number => (i / last) * width;
    const y = (i: number): number => yForDb(levels[i] ?? BOTTOM_DB, height);

    context.moveTo(x(0), y(0));

    for (let i = 1; i < last; ++i) {
        context.quadraticCurveTo(x(i), y(i), (x(i) + x(i + 1)) / 2, (y(i) + y(i + 1)) / 2);
    }

    context.lineTo(x(last), y(last));
}

const PROMPT = 'Point at the picture to read it';

export function Analyser(): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const readoutRef = useRef<HTMLSpanElement>(null);

    const levelsRef = useRef<Float32Array | null>(null);
    const peaksRef = useRef<Float32Array | null>(null);

    /** Where the pointer is, as a fraction across the picture, or null. */
    const pointerRef = useRef<number | null>(null);

    // Markup, so state; it changes when a sound starts or stops, not per frame.
    const [silent, setSilent] = useState(true);
    const [running, setRunning] = useState(false);

    /** The caption under the pointer, written straight into its element: it
        changes thirty times a second while the pointer is over the picture,
        and a React render per frame for one line of text is not worth it. */
    const updateReadout = useCallback(() => {
        const element = readoutRef.current;
        if (!element) return;

        const t = pointerRef.current;
        const levels = levelsRef.current;

        if (t === null || levels === null) {
            element.textContent = PROMPT;
            return;
        }

        const hz = frequencyAt(t);
        const db = levelAt(levels, pointAt(t, levels.length));
        const level = db <= BOTTOM_DB ? 'below −90 dB' : `${db.toFixed(1).replace('-', '−')} dB`;

        element.textContent = `${formatHz(hz)} · ${noteOf(hz)} · ${level}`;
    }, []);

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');

        if (!root || !canvas || !context) return;

        const boxWidth = Math.max(1, root.clientWidth);
        const boxHeight = Math.min(MAX_HEIGHT, Math.max(MIN_HEIGHT, boxWidth * ASPECT));
        const { width, height, ratio } = fitCanvas(root, canvas, boxHeight / boxWidth);

        const grid = cssColour(root, '--scope-grid', '#2b3138');
        const accent = cssColour(root, '--wave-line', '#f7ef8a');
        const muted = cssColour(root, '--text-dim', '#7c848c');

        context.clearRect(0, 0, width, height);

        //----------------------------------------------------------------------
        // Grid, dimly: a reference, not a subject.

        context.lineWidth = Math.max(1, ratio);
        context.strokeStyle = grid;
        context.fillStyle = muted;
        context.font = `${Math.round(9 * ratio)}px ui-monospace, monospace`;
        context.textBaseline = 'bottom';
        context.textAlign = 'left';
        context.beginPath();

        for (const hz of GRID_HZ) {
            const x = Math.round(positionOfHz(hz) * width) + 0.5;
            context.moveTo(x, 0);
            context.lineTo(x, height);
        }

        for (const db of GRID_DB) {
            const y = Math.round(yForDb(db, height)) + 0.5;
            context.moveTo(0, y);
            context.lineTo(width, y);
        }

        context.stroke();

        context.globalAlpha = 0.7;

        for (const hz of GRID_HZ) {
            context.fillText(GRID_LABELS[hz] ?? '', Math.round(positionOfHz(hz) * width) + 3 * ratio, height - 2 * ratio);
        }

        context.textBaseline = 'top';

        for (const db of GRID_DB) {
            context.fillText(`${db}`, 3 * ratio, Math.round(yForDb(db, height)) + 2 * ratio);
        }

        context.globalAlpha = 1;

        //----------------------------------------------------------------------
        // The peak line, then the live level over its fill.

        const levels = levelsRef.current;
        const peaks = peaksRef.current;

        if (peaks && peaks.length > 1) {
            context.beginPath();
            trace(context, peaks, width, height);
            context.strokeStyle = muted;
            context.globalAlpha = 0.6;
            context.lineWidth = Math.max(1, ratio);
            context.stroke();
            context.globalAlpha = 1;
        }

        if (levels && levels.length > 1) {
            context.beginPath();
            trace(context, levels, width, height);
            context.lineTo(width, height);
            context.lineTo(0, height);
            context.closePath();
            context.fillStyle = accent;
            context.globalAlpha = 0.1;
            context.fill();
            context.globalAlpha = 1;

            context.beginPath();
            trace(context, levels, width, height);
            context.strokeStyle = accent;
            context.lineWidth = 1.4 * ratio;
            context.lineJoin = 'round';
            context.stroke();
        }

        //----------------------------------------------------------------------
        // Where the pointer is reading.

        const t = pointerRef.current;

        if (t !== null) {
            const x = Math.round(t * width) + 0.5;
            context.beginPath();
            context.moveTo(x, 0);
            context.lineTo(x, height);
            context.strokeStyle = muted;
            context.lineWidth = Math.max(1, ratio);
            context.setLineDash([3 * ratio, 3 * ratio]);
            context.stroke();
            context.setLineDash([]);
        }

        updateReadout();
    }, [updateReadout]);

    // The levels, thirty times a second while the engine runs, drawn from the
    // handler: the canvas is the same element every frame.
    useEffect(() => subscribeToSpectrum((message) => {
        const mix = message.mix;

        if (!mix || mix.length < 2) {
            if (levelsRef.current !== null) {
                levelsRef.current = null;
                peaksRef.current = null;
                setRunning(false);
                draw();
            }
            return;
        }

        const levels = toLevels(mix);
        levelsRef.current = levels;
        peaksRef.current = holdPeaks(levels, peaksRef.current);

        setRunning(true);
        setSilent(message.mixSilent !== false);
        draw();
    }), [draw]);

    useRedrawOnResize(rootRef, draw);

    // Read in the canvas's own box as the pointer reports both, so it holds at
    // any interface size (18b).
    const onPointerMove = (event: React.PointerEvent<HTMLCanvasElement>): void => {
        const box = event.currentTarget.getBoundingClientRect();
        pointerRef.current = box.width > 0 ? Math.min(1, Math.max(0, (event.clientX - box.left) / box.width)) : null;
        draw();
    };

    const onPointerLeave = (): void => {
        pointerRef.current = null;
        draw();
    };

    return (
        <Module title="Analyser">
            <div ref={rootRef} className="analyser">
                <canvas
                    ref={canvasRef}
                    className="analyser__canvas"
                    role="img"
                    aria-label="Spectrum of Apollo's output, 20 Hz to 20 kHz"
                    onPointerMove={onPointerMove}
                    onPointerLeave={onPointerLeave}
                />

                <div className="analyser__caption">
                    <span ref={readoutRef} className="analyser__readout">{PROMPT}</span>
                    <span className="analyser__state">
                        {!running ? 'not running' : silent ? 'silent' : 'output'}
                    </span>
                    <span
                        className="analyser__tilt"
                        title="Levels rise 4.5 dB per octave above 1 kHz and fall below it, so that most sounds read roughly level"
                    >
                        tilted {ANALYSER_TILT_DB_PER_OCTAVE} dB/oct
                    </span>
                </div>
            </div>
        </Module>
    );
}
