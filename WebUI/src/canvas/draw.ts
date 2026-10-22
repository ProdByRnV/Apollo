/*
    The drawing primitives every picture on the page shares.

    Kept out of the components because getting either of these wrong is invisible
    until someone opens Apollo on a different monitor, and having one copy is the
    difference between fixing that once and fixing it four times.
*/

export interface CanvasSize {
    width: number;
    height: number;
    ratio: number;
}

/** Sizes a canvas's backing store to its element's box and the display's pixel
    ratio, so a line is one physical pixel wide rather than a blurred two on a
    scaled display.
*/
export function fitCanvas(root: HTMLElement, canvas: HTMLCanvasElement, aspect: number): CanvasSize {
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.round(root.clientWidth));
    const height = Math.max(1, Math.round(width * aspect));

    canvas.style.height = `${height}px`;

    const backingWidth = Math.round(width * ratio);
    const backingHeight = Math.round(height * ratio);

    if (canvas.width !== backingWidth || canvas.height !== backingHeight) {
        canvas.width = backingWidth;
        canvas.height = backingHeight;
    }

    return { width: backingWidth, height: backingHeight, ratio };
}

/** Where a pointer is, in the coordinates a picture sized by fitCanvas draws in.

    Those are the root's width and that times the aspect, but the canvas shows
    them inside its own border, so its outer box is a couple of pixels off both
    ways. An editor that hit-tested against the outer box drew each handle up to
    a pixel from where it could be grabbed, and a drag crept away from the
    pointer. Found in Phase 13f-2.
*/
export function pointerOnCanvas(
    root: HTMLElement,
    canvas: HTMLCanvasElement,
    aspect: number,
    event: { clientX: number; clientY: number },
): { x: number; y: number; width: number; height: number } {
    const width = Math.max(1, Math.round(root.clientWidth));
    const height = Math.max(1, Math.round(width * aspect));
    const box = canvas.getBoundingClientRect();

    return {
        x: (event.clientX - box.left - canvas.clientLeft) * (width / Math.max(1, canvas.clientWidth)),
        y: (event.clientY - box.top - canvas.clientTop) * (height / Math.max(1, canvas.clientHeight)),
        width,
        height,
    };
}

/** Points arrive as integer thousandths of full scale.

    Not an optimisation after the fact: a JSON number is a double, and JUCE
    serialises a double between 0.1 and 1 to sixteen decimal places, so sending
    0.123 costs eighteen characters while sending 123 costs three. At six scope
    traces thirty times a second that was the difference between 550 KB and
    180 KB a second (UI_BINDINGS.md §12).
*/
export function decodePoints(raw: readonly number[] | undefined): number[] | null {
    if (!raw || raw.length === 0) return null;

    const out = new Array<number>(raw.length);

    for (let i = 0; i < raw.length; ++i) out[i] = (raw[i] ?? 0) / 1000;

    return out;
}

export interface SeriesOptions {
    colour: string;
    /** Where the value 0 sits vertically: 0 is the top, 1 the bottom. */
    zero?: number;
    scale?: number;
    span?: number;
    weight?: number;
}

/** Draws a polyline across the full width of a canvas from an array of values.

    A bipolar signal is drawn about the middle and a unipolar one off the floor,
    because an envelope drawn centred would throw away half the canvas and imply
    it could go negative.
*/
export function strokeSeries(
    context: CanvasRenderingContext2D,
    points: readonly number[] | null,
    size: CanvasSize,
    options: SeriesOptions,
): void {
    if (!points || points.length === 0) return;

    const zero = options.zero ?? 0.5;
    const scale = options.scale ?? 1;
    const span = options.span ?? 0.94;

    const baseline = size.height * zero;
    const reach = (zero >= 0.5 ? size.height * zero : size.height * (1 - zero)) * span;

    context.strokeStyle = options.colour;
    context.lineWidth = Math.max(1, (options.weight ?? 1.4) * size.ratio);
    context.lineJoin = 'round';
    context.beginPath();

    for (let i = 0; i < points.length; ++i) {
        const x = points.length > 1 ? (i / (points.length - 1)) * size.width : 0;

        // Clamped rather than fitted: a trace stretched to whatever height it
        // happened to need would make everything look the same size, and a
        // signal past full scale must be visibly past it.
        const value = Math.max(-1, Math.min(1, (points[i] ?? 0) * scale));
        const y = baseline - value * reach;

        if (i === 0) context.moveTo(x, y);
        else context.lineTo(x, y);
    }

    context.stroke();
}

/** The horizontal line every picture draws behind its trace.

    What makes a silent scope read as a scope showing silence rather than as a
    panel that failed to draw.
*/
export function strokeZeroLine(
    context: CanvasRenderingContext2D,
    size: CanvasSize,
    colour: string,
    zero = 0.5,
): void {
    context.strokeStyle = colour;
    context.lineWidth = Math.max(1, size.ratio);
    context.beginPath();
    context.moveTo(0, size.height * zero);
    context.lineTo(size.width, size.height * zero);
    context.stroke();
}

/** Reads a CSS custom property off an element, with a literal fallback for the
    case where the stylesheet has not applied yet.
*/
export function cssColour(element: HTMLElement, name: string, fallback: string): string {
    return getComputedStyle(element).getPropertyValue(name).trim() || fallback;
}
