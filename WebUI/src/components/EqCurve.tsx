/*
    The equaliser's response, and the seven handles that shape it.

    THE PICTURE IS THE CONTROL. Every other display on this page is read-only —
    a scope shows what is happening and a knob beside it changes it. This one is
    both, because an equaliser is the one effect whose settings *are* a shape:
    dragging a bell to where it should sit is the gesture, and reading the
    frequency off a knob to type it in is the long way round. The knobs are still
    there, still built from metadata, and still the way to set an exact value or
    to reach the control from a keyboard (CLAUDE.md §39); this is the fast path,
    not the only one.

    WHAT IS DRAWN. The summed curve of all seven bands plus the output trim, a
    logarithmic frequency axis from 20 Hz to 20 kHz, a decibel grid at ±18, and
    one handle per band at its own frequency and gain. The selected band's own
    contribution is drawn behind the sum, so it can be seen separately from what
    the other six are doing to it — which is the question being asked whenever
    two bands overlap.

    BEHIND THE CURVE, THE SOUND. The spectrum of what goes into the equaliser,
    as a grey line, and of what comes out of it, as a gold line over a faint
    fill, both live (CLAUDE.md §22). The curve is what the equaliser is set to
    do; the gap between the two spectra is what it is doing, measured from the
    audio rather than predicted from the settings. Every knob, button and handle
    on this panel moves the gold one. The spectra have their own vertical scale,
    from SPECTRUM_TOP_DB at the top to SPECTRUM_BOTTOM_DB at the bottom, because
    a signal's level and a filter's gain are different quantities; only the
    frequency axis is shared. Nothing is drawn while the equaliser is out of the
    rack, which the module already says.

    A MUTED OR DISABLED BAND STILL SHOWS ITS HANDLE, greyed. A handle that
    vanished would have to be found again, and the band is still where it was.

    THE ARITHMETIC IS IN `params/eqCurve.ts`, which explains why the page holds
    any at all.

    KEYBOARD. The handles are not focusable and this is deliberate rather than an
    omission: everything a handle does, the band's own knobs do, and they are
    already in the tab order with their values spoken. Seven more tab stops that
    duplicate twenty-one controls would make the panel slower to get through with
    a keyboard, not faster.
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import type { SpectrumFrameMessage } from '../bridge/protocol';
import { cssColour, fitCanvas } from '../canvas/draw';
import { useRedrawOnResize } from '../canvas/useRedrawOnResize';
import { subscribeToSpectrum } from '../state/telemetry';
import {
    EQ_MAX_GAIN_DB,
    EQ_MAX_HZ,
    EQ_MIN_HZ,
    EQ_TYPE_OFF,
    bandMagnitudeDbAt,
    curveDbAt,
    eqTypeHasGain,
} from '../params/eqCurve';
import type { BandSettings } from '../params/eqCurve';

/* How tall the curve is drawn, in CSS pixels.

   An aspect alone is not enough here, unlike every other picture on the page.
   Those are fixed-width controls that sit beside knobs; this one stretches to
   its module, so on a wide window a constant aspect would make it half the
   height of the screen — and the extra height buys nothing, because a decibel
   axis only spans ±18 either way and is perfectly readable in a third of that.
   So the aspect sets the height on a narrow window and the ceiling takes over on
   a wide one. */
const ASPECT = 0.42;
const MIN_HEIGHT = 150;
const MAX_HEIGHT = 260;

/** Decibels the vertical axis covers each way. The same ±18 the gain and the
    output trim have, so the curve fills the box rather than sitting in the
    middle of it, and a band at full boost touches the top.
*/
const RANGE_DB = EQ_MAX_GAIN_DB;

const GRID_DB = [-12, -6, 0, 6, 12];
const GRID_HZ = [50, 100, 200, 500, 1000, 2000, 5000, 10000];

const LOG_MIN = Math.log(EQ_MIN_HZ);
const LOG_MAX = Math.log(EQ_MAX_HZ);

/** Radius of a handle in CSS pixels, and how close a pointer must come to grab
    one. The grab radius is larger than the dot: a four-pixel target is not a
    target.
*/
const HANDLE_RADIUS = 5;
const GRAB_RADIUS = 14;

/** The spectra's own vertical scale, in dB. The engine tilts the spectrum
    upwards towards the treble, so a full mix sits in the upper half of this and
    a single quiet oscillator in the lower; ninety-six decibels holds both. */
const SPECTRUM_TOP_DB = 6;
const SPECTRUM_BOTTOM_DB = -90;

/** The newest spectrum frame, as levels in dB, or null while there is none to
    draw. */
interface Spectrum {
    input: Float32Array;
    output: Float32Array;
}

function toLevels(tenths: readonly number[]): Float32Array {
    const levels = new Float32Array(tenths.length);
    for (let i = 0; i < tenths.length; ++i) levels[i] = (tenths[i] ?? 0) / 10;
    return levels;
}

function spectrumFrom(message: SpectrumFrameMessage): Spectrum | null {
    if (!message.active || !message.input || !message.output) return null;
    if (message.input.length < 2 || message.input.length !== message.output.length) return null;

    return { input: toLevels(message.input), output: toLevels(message.output) };
}

function yForSpectrumDb(db: number, height: number): number {
    const t = (SPECTRUM_TOP_DB - db) / (SPECTRUM_TOP_DB - SPECTRUM_BOTTOM_DB);
    return Math.min(1, Math.max(0, t)) * height;
}

/** Traces one spectrum as a path. The points are evenly spaced along the
    logarithmic axis the curve is drawn on, 20 Hz at the left edge and 20 kHz at
    the right, so a point's x is its index; the path runs through the midpoints
    between them with the points as control points, which draws a curve through
    the levels rather than a polyline with a corner every few pixels. */
function traceSpectrum(context: CanvasRenderingContext2D, levels: Float32Array,
                       width: number, height: number): void {
    const last = levels.length - 1;
    const x = (i: number): number => (i / last) * width;
    const y = (i: number): number => yForSpectrumDb(levels[i] ?? SPECTRUM_BOTTOM_DB, height);

    context.moveTo(x(0), y(0));

    for (let i = 1; i < last; ++i) {
        context.quadraticCurveTo(x(i), y(i), (x(i) + x(i + 1)) / 2, (y(i) + y(i + 1)) / 2);
    }

    context.lineTo(x(last), y(last));
}

/** The curve's heights, one per physical pixel, kept between frames.

    The curve moves when a control does; the spectrum behind it moves thirty
    times a second. Recomputing seven filters' response at every pixel for every
    spectrum frame would spend most of the panel's drawing on a picture that had
    not changed, so it is computed when the props or the size change and reused
    otherwise. */
interface CurveCache {
    props: EqCurveProps;
    width: number;
    height: number;
    ratio: number;
    sum: Float32Array;
    selected: Float32Array | null;
}

export interface EqCurveProps {
    bands: readonly BandSettings[];
    levelDb: number;
    sampleRate: number;
    selected: number;
    /** A handle has been taken hold of. Selects that band *and* opens the
        automation gesture the drag will write inside — which is why it is not
        called `onSelect`: picking a band from the tab strip selects without
        grabbing anything. */
    onGrab: (index: number) => void;
    /** Called while a handle is dragged, with the band's new frequency and gain.
        Gain is undefined for the shapes that have no gain control, so a drag up
        the screen on a low pass moves nothing rather than writing a value the
        engine will ignore. */
    onDrag: (index: number, frequencyHz: number, gainDb: number | undefined) => void;
    onDragEnd: (index: number) => void;
}

function xForHz(hz: number, width: number): number {
    return ((Math.log(Math.max(hz, EQ_MIN_HZ)) - LOG_MIN) / (LOG_MAX - LOG_MIN)) * width;
}

function hzForX(x: number, width: number): number {
    const t = width > 0 ? Math.min(1, Math.max(0, x / width)) : 0;
    return Math.exp(LOG_MIN + t * (LOG_MAX - LOG_MIN));
}

function yForDb(db: number, height: number): number {
    const t = (RANGE_DB - db) / (2 * RANGE_DB);
    return Math.min(1, Math.max(0, t)) * height;
}

function dbForY(y: number, height: number): number {
    const t = height > 0 ? Math.min(1, Math.max(0, y / height)) : 0.5;
    return RANGE_DB - t * 2 * RANGE_DB;
}

export function EqCurve(props: EqCurveProps): JSX.Element {
    const rootRef = useRef<HTMLDivElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);

    // The drawing reads the newest props without the effect that installs the
    // pointer listeners having to be torn down and rebuilt on every value
    // change — which, at drag rate, would be every frame.
    const propsRef = useRef(props);
    propsRef.current = props;

    const draggingRef = useRef<number | null>(null);

    const spectrumRef = useRef<Spectrum | null>(null);
    const curveRef = useRef<CurveCache | null>(null);

    // Whether the key to the two lines is shown. State, unlike the levels,
    // because it is markup — and it changes only when the equaliser enters or
    // leaves the rack, not at frame rate.
    const [spectrumShown, setSpectrumShown] = useState(false);

    const draw = useCallback(() => {
        const root = rootRef.current;
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');

        if (!root || !canvas || !context) return;

        // `fitCanvas` takes an aspect, so the clamp is expressed as the aspect
        // that produces the height wanted at this width.
        const boxWidth = Math.max(1, root.clientWidth);
        const boxHeight = Math.min(MAX_HEIGHT, Math.max(MIN_HEIGHT, boxWidth * ASPECT));

        const size = fitCanvas(root, canvas, boxHeight / boxWidth);
        const { width, height, ratio } = size;
        const { bands, levelDb, sampleRate, selected } = propsRef.current;

        const grid = cssColour(root, '--scope-grid', '#2b3138');
        const accent = cssColour(root, '--wave-line', '#f7ef8a');
        const muted = cssColour(root, '--text-dim', '#7c848c');

        context.clearRect(0, 0, width, height);

        //----------------------------------------------------------------------
        // Grid. Drawn first and dimly: it is a reference, not a subject.

        context.lineWidth = Math.max(1, ratio);
        context.strokeStyle = grid;

        for (const hz of GRID_HZ) {
            const x = Math.round(xForHz(hz, width)) + 0.5;

            context.beginPath();
            context.moveTo(x, 0);
            context.lineTo(x, height);
            context.stroke();
        }

        for (const db of GRID_DB) {
            const y = Math.round(yForDb(db, height)) + 0.5;

            context.beginPath();
            // The 0 dB line is the one a curve is read against, so it is drawn
            // solid across and the others are left to fade into the background.
            context.globalAlpha = db === 0 ? 1 : 0.55;
            context.moveTo(0, y);
            context.lineTo(width, y);
            context.stroke();
        }

        context.globalAlpha = 1;

        //----------------------------------------------------------------------
        // The spectra, behind everything that can be grabbed. The input first
        // and plainly, as the reference; the output over it in the accent, with
        // a faint fill beneath so that where the two part, the part the
        // equaliser took away is the part left unfilled.

        const spectrum = spectrumRef.current;

        if (spectrum) {
            context.lineJoin = 'round';

            context.beginPath();
            traceSpectrum(context, spectrum.output, width, height);
            context.lineTo(width, height);
            context.lineTo(0, height);
            context.closePath();
            context.fillStyle = accent;
            context.globalAlpha = 0.1;
            context.fill();

            context.beginPath();
            traceSpectrum(context, spectrum.input, width, height);
            context.strokeStyle = muted;
            context.globalAlpha = 0.85;
            context.lineWidth = Math.max(1, ratio);
            context.stroke();

            context.beginPath();
            traceSpectrum(context, spectrum.output, width, height);
            context.strokeStyle = accent;
            context.globalAlpha = 0.5;
            context.lineWidth = Math.max(1, 1.2 * ratio);
            context.stroke();

            context.globalAlpha = 1;
        }

        //----------------------------------------------------------------------
        // The curve's heights: from the cache while nothing it depends on has
        // moved, recomputed when something has. One sample per physical pixel:
        // a curve drawn at a coarser step and interpolated would round off a
        // narrow notch, which is the one feature a narrow notch has.

        let curve = curveRef.current;

        if (!curve || curve.props !== propsRef.current || curve.width !== width
            || curve.height !== height || curve.ratio !== ratio) {
            const count = Math.floor(width / ratio) + 1;
            const sum = new Float32Array(count);
            const selectedBand = bands[selected];
            const showSelected = selectedBand !== undefined
                && selectedBand.type !== EQ_TYPE_OFF && !selectedBand.muted;
            const alone = showSelected ? new Float32Array(count) : null;

            for (let i = 0; i < count; ++i) {
                const hz = hzForX(i * ratio, width);
                sum[i] = yForDb(curveDbAt(bands, levelDb, sampleRate, hz), height);

                if (alone && selectedBand) {
                    alone[i] = yForDb(bandMagnitudeDbAt(selectedBand, sampleRate, hz), height);
                }
            }

            curve = { props: propsRef.current, width, height, ratio, sum, selected: alone };
            curveRef.current = curve;
        }

        const strokeHeights = (heights: Float32Array): void => {
            context.beginPath();

            for (let i = 0; i < heights.length; ++i) {
                const y = heights[i] ?? 0;
                if (i === 0) context.moveTo(0, y);
                else context.lineTo(i * ratio, y);
            }

            context.stroke();
        };

        // The selected band on its own, behind the sum.
        if (curve.selected) {
            context.strokeStyle = accent;
            context.globalAlpha = 0.3;
            context.lineWidth = Math.max(1, 1.2 * ratio);
            strokeHeights(curve.selected);
            context.globalAlpha = 1;
        }

        // The sum.
        context.strokeStyle = accent;
        context.lineWidth = Math.max(1, 1.8 * ratio);
        context.lineJoin = 'round';
        strokeHeights(curve.sum);

        //----------------------------------------------------------------------
        // Handles.

        for (let i = 0; i < bands.length; ++i) {
            const band = bands[i];
            if (!band) continue;

            const inactive = band.type === EQ_TYPE_OFF || band.muted;
            const gain = eqTypeHasGain(band.type) ? band.gainDb : 0;

            const x = xForHz(band.frequencyHz, width);
            const y = yForDb(gain, height);
            const radius = (i === selected ? HANDLE_RADIUS + 1.5 : HANDLE_RADIUS) * ratio;

            context.beginPath();
            context.arc(x, y, radius, 0, 2 * Math.PI);

            // Selected is filled, unselected is outlined, and inactive is grey.
            // Three states told apart by shape as well as by colour, so the
            // difference survives a greyscale screen (CLAUDE.md §24.2).
            if (i === selected) {
                context.fillStyle = inactive ? muted : accent;
                context.fill();
            } else {
                context.strokeStyle = inactive ? muted : accent;
                context.lineWidth = Math.max(1, 1.4 * ratio);
                context.stroke();
            }

            // The band's number, inside the handle it belongs to, so a handle
            // dragged on top of another can still be told from it.
            context.fillStyle = i === selected
                ? cssColour(root, '--panel', '#181b1f')
                : (inactive ? muted : accent);
            context.font = `${Math.round(8 * ratio)}px ui-monospace, monospace`;
            context.textAlign = 'center';
            context.textBaseline = 'middle';
            context.fillText(String(i + 1), x, y + 0.5 * ratio);
        }
    }, []);

    //--------------------------------------------------------------------------
    // Dragging.

    useEffect(() => {
        const canvas = canvasRef.current;
        if (canvas === null) return;

        /** The band whose handle is nearest the pointer, if one is near enough. */
        const grab = (event: PointerEvent): number | null => {
            const box = canvas.getBoundingClientRect();
            const { bands } = propsRef.current;

            const px = event.clientX - box.left;
            const py = event.clientY - box.top;

            let best: number | null = null;
            let bestDistance = GRAB_RADIUS;

            for (let i = 0; i < bands.length; ++i) {
                const band = bands[i];
                if (!band) continue;

                const gain = eqTypeHasGain(band.type) ? band.gainDb : 0;

                const dx = px - xForHz(band.frequencyHz, box.width);
                const dy = py - yForDb(gain, box.height);
                const distance = Math.hypot(dx, dy);

                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }

            return best;
        };

        const onPointerDown = (event: PointerEvent): void => {
            // Left button only, and never while MIDI assignment mode is on —
            // there is no parameter behind a handle to assign.
            if (event.button !== 0 || document.body.dataset.midiMode === 'true') return;

            const index = grab(event);
            if (index === null) return;

            event.preventDefault();

            draggingRef.current = index;
            canvas.setPointerCapture(event.pointerId);

            propsRef.current.onGrab(index);
        };

        const onPointerMove = (event: PointerEvent): void => {
            const index = draggingRef.current;
            if (index === null) return;

            const box = canvas.getBoundingClientRect();
            const band = propsRef.current.bands[index];
            if (!band) return;

            const hz = hzForX(event.clientX - box.left, box.width);
            const db = eqTypeHasGain(band.type)
                ? dbForY(event.clientY - box.top, box.height)
                : undefined;

            propsRef.current.onDrag(index, hz, db);
        };

        const onPointerUp = (event: PointerEvent): void => {
            const index = draggingRef.current;
            if (index === null) return;

            draggingRef.current = null;

            if (canvas.hasPointerCapture(event.pointerId)) {
                canvas.releasePointerCapture(event.pointerId);
            }

            propsRef.current.onDragEnd(index);
        };

        canvas.addEventListener('pointerdown', onPointerDown);
        canvas.addEventListener('pointermove', onPointerMove);
        canvas.addEventListener('pointerup', onPointerUp);
        canvas.addEventListener('pointercancel', onPointerUp);

        return () => {
            canvas.removeEventListener('pointerdown', onPointerDown);
            canvas.removeEventListener('pointermove', onPointerMove);
            canvas.removeEventListener('pointerup', onPointerUp);
            canvas.removeEventListener('pointercancel', onPointerUp);
        };
    }, []);

    // The spectrum, thirty times a second while the equaliser is in the rack.
    // Drawn straight from the handler rather than through React: the canvas
    // is the same element every frame, and only its pixels change.
    useEffect(() => subscribeToSpectrum((message) => {
        const next = spectrumFrom(message);
        const hadOne = spectrumRef.current !== null;

        spectrumRef.current = next;
        setSpectrumShown(next !== null);

        // An inactive frame repeats every tick; only the first one, which
        // takes the picture away, needs a repaint.
        if (next !== null || hadOne) draw();
    }), [draw]);

    // Redrawn whenever anything it draws changes. The dependency is the props
    // object itself, which React gives a new identity on every render of the
    // parent — and the parent re-renders exactly when a band's value moves,
    // because that is what its parameter subscriptions are for.
    useEffect(draw);

    // Redrawn when the panel it sits in is resized, not only when the window
    // is. The curve stretches to its module, so this is the picture that most
    // needs it: a resized equaliser panel with a stale canvas would show the
    // response at the wrong width.
    useRedrawOnResize(rootRef, draw);

    return (
        <div ref={rootRef} className="eq-curve">
            <canvas ref={canvasRef} className="eq-curve__canvas" />

            <div className="eq-curve__caption">
                <span className="eq-curve__axis">20 Hz</span>
                <span className="eq-curve__range">±{RANGE_DB} dB</span>
                <span className="eq-curve__key" hidden={!spectrumShown}>
                    <span className="eq-curve__swatch eq-curve__swatch--in" aria-hidden="true" />
                    in
                    <span className="eq-curve__swatch eq-curve__swatch--out" aria-hidden="true" />
                    out
                </span>
                <span className="eq-curve__axis">20 kHz</span>
            </div>
        </div>
    );
}
