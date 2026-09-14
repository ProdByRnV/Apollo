/*
    The small menu that appears where you right-clicked.

    A WebView has no access to the operating system's own context menu, and the
    browser's default one is a page menu — "Reload", "Save image as" — which is
    worse than nothing inside a plugin. So this is Apollo's, drawn in the page
    and styled like the rest of it.

    ONE MENU AT A TIME, OWNED BY WHOEVER OPENED IT. There is no global menu
    manager: the component that wants a menu renders one and is told when it
    closes. Closing happens on a choice, on Escape, on a click anywhere else, on
    a scroll, and on the window losing focus — all five, because a menu that
    outstays its welcome floating over a synthesiser is worse than one that
    closes too eagerly.

    IT IS KEPT ON SCREEN. A right-click near the right or bottom edge would
    otherwise put the menu half outside the window, where a plugin cannot scroll
    to reach it. The position is measured after layout and pulled back inside.
*/

import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';

export interface ContextMenuItem {
    label: string;
    onChoose: () => void;
    /** Shown greyed and not choosable. */
    disabled?: boolean | undefined;
}

export interface ContextMenuProps {
    x: number;
    y: number;
    items: readonly ContextMenuItem[];
    onClose: () => void;
}

/** Distance kept between the menu and the edge of the window. */
const MARGIN = 6;

export function ContextMenu({ x, y, items, onClose }: ContextMenuProps): JSX.Element {
    const ref = useRef<HTMLDivElement>(null);
    const [position, setPosition] = useState({ left: x, top: y });

    // Measured after the browser has laid the menu out, because its size depends
    // on the longest label and is not known before then.
    useLayoutEffect(() => {
        const element = ref.current;
        if (!element) return;

        const box = element.getBoundingClientRect();

        const left = Math.max(MARGIN, Math.min(x, window.innerWidth - box.width - MARGIN));
        const top = Math.max(MARGIN, Math.min(y, window.innerHeight - box.height - MARGIN));

        setPosition({ left, top });

        // The first choosable item takes focus, so the menu can be driven
        // entirely from the keyboard: Enter or Space picks, the arrows move,
        // Escape leaves (CLAUDE.md §39). It also means the menu behaves like a
        // menu rather than like a small box that happens to contain buttons.
        const first = element.querySelector<HTMLButtonElement>('button:not(:disabled)');

        first?.focus();
    }, [x, y]);

    useEffect(() => {
        const close = (): void => onClose();

        /** Closes on a pointer-down anywhere *except* inside the menu itself.

            The containment check rather than `stopPropagation` on the menu, and
            the difference is not stylistic: this listener is in the capture
            phase, so it runs before the item's own handler and a
            `stopPropagation` there comes too late. Without the check the menu
            closed on pointer-down and the click never became a choice — which
            is a menu whose items cannot be picked.
        */
        const closeIfOutside = (event: Event): void => {
            const target = event.target;

            if (target instanceof Node && ref.current?.contains(target)) return;

            onClose();
        };

        const onKeyDown = (event: KeyboardEvent): void => {
            if (event.key === 'Escape')
            {
                event.preventDefault();
                onClose();
                return;
            }

            if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;

            const buttons = Array.from(
                ref.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? [],
            );

            if (buttons.length === 0) return;

            event.preventDefault();

            const current = buttons.findIndex((button) => button === document.activeElement);
            const step = event.key === 'ArrowDown' ? 1 : -1;

            // Wraps, because a menu this short is faster to cycle than to
            // reverse out of.
            const next = (current + step + buttons.length) % buttons.length;

            buttons[next]?.focus();
        };

        // Capture-phase, so a click that lands on a control closes the menu
        // before that control acts on it — otherwise dismissing the menu would
        // also move whatever happened to be underneath.
        document.addEventListener('pointerdown', closeIfOutside, true);
        document.addEventListener('keydown', onKeyDown);
        window.addEventListener('blur', close);
        window.addEventListener('resize', close);

        // A scroll moves the page under a menu that is positioned against the
        // viewport, so the menu would end up pointing at something else.
        document.addEventListener('scroll', close, true);

        return () => {
            document.removeEventListener('pointerdown', closeIfOutside, true);
            document.removeEventListener('keydown', onKeyDown);
            window.removeEventListener('blur', close);
            window.removeEventListener('resize', close);
            document.removeEventListener('scroll', close, true);
        };
    }, [onClose]);

    // A PORTAL, AND THIS IS THE PART THAT MATTERS. Rendered where the component
    // sits in the tree, the menu is a *child of the control that opened it* —
    // so pressing a menu item first runs that control's own pointer handler. On
    // a knob that means starting a drag and calling `setPointerCapture`, which
    // redirects the pointer-up away from the menu and means the click never
    // happens. The menu opened, highlighted under the cursor, and could not be
    // chosen.
    //
    // Moving it to the document body takes it out of every control's event path
    // and out of every `overflow` and stacking context on the way — including
    // the panels, which became scroll containers when they became resizeable.
    return createPortal(
        <div
            ref={ref}
            className="context-menu"
            role="menu"
            style={{ left: `${position.left}px`, top: `${position.top}px` }}
        >
            {items.map((item) => (
                <button
                    key={item.label}
                    type="button"
                    role="menuitem"
                    className="context-menu__item"
                    disabled={item.disabled === true}
                    onClick={() => {
                        onClose();
                        item.onChoose();
                    }}
                >
                    {item.label}
                </button>
            ))}
        </div>,
        document.body,
    );
}
