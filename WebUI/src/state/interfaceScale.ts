/*
    The interface size, as the engine last said, and the zoom it is drawn at
    (18b).

    The size belongs to the computer and is kept by the editor; the page draws
    what it is told, so a size chosen here changes nothing until the engine
    answers with it.

    Drawn with CSS zoom on the root, which every WebView Apollo uses supports
    and which reflows the layout rather than stretching a picture of it. It has
    one consequence the rest of the page must respect: a pointer's position and
    getBoundingClientRect are in zoomed pixels, while CSS lengths, scrollTop and
    clientWidth are not. viewportZoom converts between the two, and the canvases
    draw at the zoom as well as the display's pixel ratio so they stay sharp.
*/

import { useSyncExternalStore } from 'react';

import { DEFAULT_SCALE, zoomFor } from '../params/interfaceScale';

export interface InterfaceScale {
    /** The size chosen, in per cent. */
    scale: number;
    /** The sizes the engine offers. */
    scales: readonly number[];
    /** The zoom the page is drawn at: the size, as far as the window allows. */
    zoom: number;
}

let current: InterfaceScale = { scale: DEFAULT_SCALE, scales: [DEFAULT_SCALE], zoom: 1 };

const listeners = new Set<() => void>();

function announce(next: InterfaceScale): void {
    current = next;
    for (const listener of listeners) listener();
}

/** Draws at the zoom the chosen size and the window allow. */
function draw(): void {
    // The window's size in pixels at 100%: a zoom on the root does not change
    // innerWidth, which is the viewport and not the layout.
    const zoom = zoomFor(current.scale, window.innerWidth, window.innerHeight);

    if (zoom === current.zoom) return;

    document.documentElement.style.zoom = zoom === 1 ? '' : String(zoom);
    announce({ ...current, zoom });
}

export function applyInterfaceScale(scale: unknown, scales: unknown): void {
    const offered = Array.isArray(scales)
        ? scales.filter((value): value is number => typeof value === 'number' && value > 0)
        : [];

    const chosen = typeof scale === 'number' && offered.includes(scale) ? scale : DEFAULT_SCALE;

    announce({ ...current, scale: chosen, scales: offered.length > 0 ? offered : [DEFAULT_SCALE] });
    draw();
}

// A window made larger can draw a size it could not before, and one made
// smaller may have to draw it smaller.
if (typeof window !== 'undefined') window.addEventListener('resize', draw);

/** The zoom the page is drawn at, for code that is not rendering. */
export function currentZoom(): number {
    return current.zoom;
}

/** How many pointer pixels make one CSS pixel: the zoom as this engine reports
    it, measured rather than assumed, so that converting a pointer position
    into a CSS length is right whatever the WebView does with zoom. */
export function viewportZoom(): number {
    const body = document.body;
    if (!body || body.offsetWidth <= 0) return 1;

    const measured = body.getBoundingClientRect().width / body.offsetWidth;

    return Number.isFinite(measured) && measured > 0 ? measured : 1;
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

export function useInterfaceScale(): InterfaceScale {
    return useSyncExternalStore(subscribe, () => current);
}
