/*
    Redraw a canvas whenever the box it lives in changes size.

    Every picture on this page sizes its backing store to its element's box, so
    every one of them has to be told when that box moves. Listening to the
    window was enough while the only thing that resized was the window. Panels
    are now individually resizeable by a handle in their corner, so a canvas can
    change size while the window does not move at all — and one that only
    watched the window would stay at its old size until something else happened
    to make it redraw.

    A `ResizeObserver` rather than a window listener for that reason, and the
    window listener is kept alongside it because a device-pixel-ratio change —
    dragging the plugin to a display with different scaling — changes what the
    backing store should be without changing the element's box at all.
*/

import { useEffect } from 'react';
import type { RefObject } from 'react';

/** Calls @p draw whenever @p ref's element changes size, and on window resize.

    @param ref  the element whose box decides the canvas size — the picture's
                root, not the canvas itself, because the canvas is what gets
                resized *to* it.
*/
export function useRedrawOnResize(ref: RefObject<HTMLElement | null>, draw: () => void): void {
    useEffect(() => {
        // Drawn once on mount as well: the first layout happens after the first
        // render, so a canvas that only drew on change would start blank.
        draw();

        const element = ref.current;

        // Guarded rather than assumed. `ResizeObserver` is in every WebView
        // backend JUCE uses, but a missing one must degrade to the old window
        // behaviour rather than throwing during the editor's first paint.
        const observer = typeof ResizeObserver === 'function' && element !== null
            ? new ResizeObserver(() => draw())
            : null;

        observer?.observe(element as Element);

        window.addEventListener('resize', draw);

        return () => {
            observer?.disconnect();
            window.removeEventListener('resize', draw);
        };
    }, [ref, draw]);
}
