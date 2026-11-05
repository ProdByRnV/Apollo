/*
    How large the interface is drawn (18b).

    The size somebody chooses is a per cent of the designed size; the page draws
    at it as far as the window leaves room. Below 900 by 560 CSS pixels the
    panels overlap rather than reflow (UI/EditorSizing.h), so a size that would
    leave the layout less than that is drawn only as large as fits, and the
    masthead — where the size is chosen — can never be pushed off the window.
    A smaller size is always drawn as chosen: it only ever gives the layout more
    room.
*/

/** The smallest the layout works at, in CSS pixels at 100%. The editor's own
    minimum, from UI/EditorSizing.h. */
export const MINIMUM_LAYOUT = { width: 900, height: 560 } as const;

/** What the page offers before the engine has said: just the default, so
    that nothing can be chosen that the engine has not offered. */
export const DEFAULT_SCALE = 100;

/** @returns the zoom to draw at for a chosen size, in a viewport of the given
    size in CSS pixels at 100%. */
export function zoomFor(scalePercent: number, viewportWidth: number, viewportHeight: number): number {
    const chosen = Number.isFinite(scalePercent) && scalePercent > 0 ? scalePercent / 100 : 1;

    if (chosen <= 1) return chosen;
    if (!(viewportWidth > 0) || !(viewportHeight > 0)) return 1;

    const fits = Math.min(viewportWidth / MINIMUM_LAYOUT.width, viewportHeight / MINIMUM_LAYOUT.height);

    return Math.max(1, Math.min(chosen, fits));
}

/** @returns how a size is written in the menu: "125%". */
export function scaleLabel(scalePercent: number): string {
    return `${Math.round(scalePercent)}%`;
}

/** @returns what the size control's tooltip says: the size, and — when the
    window is too small to draw it — how large it is being drawn instead and
    why. */
export function scaleTitle(scalePercent: number, zoom: number): string {
    const drawn = Math.round(zoom * 100);

    if (drawn >= Math.round(scalePercent)) return `Interface size: ${scaleLabel(scalePercent)}`;

    return `Interface size: ${scaleLabel(scalePercent)}, drawn at ${drawn}% until the window is larger`;
}
