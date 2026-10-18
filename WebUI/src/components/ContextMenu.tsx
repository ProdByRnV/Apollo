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

    FOCUS GOES BACK WHERE IT CAME FROM. Shift+F10 and the Menu key open this
    from the keyboard — Chromium turns both into a `contextmenu` event on the
    focused control — and choosing Reset used to leave focus on the document
    body, so the next Tab started again from the top of the page. On close, the
    control that had focus when the menu opened gets it back, unless the user
    has since put it somewhere else themselves.
*/

import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';

import { rovingTarget } from './roving';

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

    /** True once something has been chosen, so nothing can be chosen twice.

        The item commits on pointer-up and on click, because only one of the two
        can be relied on to arrive (see the button below). A guard is cheaper
        than working out which one it will be.
    */
    const chosen = useRef(false);

    // Declared before the positioning effect so that it runs first, and so
    // records the opener before the menu takes focus away from it.
    useLayoutEffect(() => {
        const opener = document.activeElement;
        const menu = ref.current;

        return () => {
            const active = document.activeElement;

            // Only when focus has nowhere better to be. A click on another
            // control closes the menu too, and focus belongs on that control,
            // not dragged back to the one the menu was opened from.
            const stranded = active === null
                || active === document.body
                || (menu !== null && menu.contains(active));

            if (stranded && opener instanceof HTMLElement && opener.isConnected) {
                opener.focus({ preventScroll: true });
            }
        };
    }, []);

    const choose = (item: ContextMenuItem): void => {
        if (chosen.current || item.disabled === true) return;

        chosen.current = true;

        onClose();
        item.onChoose();
    };

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
        //
        // When every item is disabled — Reset on a knob already at its default —
        // the menu itself takes focus instead. Otherwise focus stayed behind on
        // the control, and the arrows went on turning a knob underneath a menu
        // that appeared to be the thing being driven.
        const first = element.querySelector<HTMLButtonElement>('button:not(:disabled)');

        (first ?? element).focus();
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

        // Capture-phase, so a click that lands on a control closes the menu
        // before that control acts on it — otherwise dismissing the menu would
        // also move whatever happened to be underneath.
        document.addEventListener('pointerdown', closeIfOutside, true);
        window.addEventListener('blur', close);
        window.addEventListener('resize', close);

        // A scroll moves the page under a menu that is positioned against the
        // viewport, so the menu would end up pointing at something else.
        document.addEventListener('scroll', close, true);

        return () => {
            document.removeEventListener('pointerdown', closeIfOutside, true);
            window.removeEventListener('blur', close);
            window.removeEventListener('resize', close);
            document.removeEventListener('scroll', close, true);
        };
    }, [onClose]);

    /** The menu's keys, handled on the menu and stopped there.

        On the menu rather than on the document, for the same reason the
        pointer events below are stopped: a portal moves the node and not the
        component, so a key pressed in the menu goes on to the control that
        opened it. On a knob that meant the arrows walked the menu *and* turned
        the knob underneath it — two presses took Osc 1 Level from 99 % to 97 %
        while the user was choosing Reset.
    */
    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        // Nothing typed in a menu belongs to the control behind it. Enter and
        // Space keep their default, which is what presses the focused item.
        event.stopPropagation();

        if (event.key === 'Escape') {
            event.preventDefault();
            onClose();
            return;
        }

        const buttons = Array.from(
            ref.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? [],
        );

        const current = buttons.findIndex((button) => button === document.activeElement);

        // Wraps, because a menu this short is faster to cycle than to reverse
        // out of.
        const target = rovingTarget(event.key, current, buttons.length, { orientation: 'vertical' });

        if (target === null) return;

        event.preventDefault();
        buttons[target]?.focus();
    };

    /** Tab leaves the menu, and a menu nobody is in should not stay open. */
    const onBlur = (event: React.FocusEvent<HTMLDivElement>): void => {
        const next = event.relatedTarget;

        if (next instanceof Node && ref.current?.contains(next)) return;

        onClose();
    };

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
            tabIndex={-1}
            style={{ left: `${position.left}px`, top: `${position.top}px` }}

            /* THE EVENTS STOP HERE, AND THIS IS THE FIX THAT MATTERS.

               A React portal moves the DOM node; it does not move the
               component. Events still propagate along the *React* tree, so a
               pointer-down on a menu item went on to the control that rendered
               the menu — and on a knob that means `onPointerDown`, which calls
               `setPointerCapture`. The knob then owned the pointer, the release
               was delivered to the knob instead of the menu, no click was ever
               generated, and the item could not be chosen with a mouse at all.

               It could still be chosen from the keyboard, which is how this
               survived being "fixed" once already: moving the node to the
               document body changed where it was drawn and nothing about where
               its events went. */
            onPointerDown={(event) => event.stopPropagation()}
            onPointerMove={(event) => event.stopPropagation()}
            onPointerUp={(event) => event.stopPropagation()}
            onClick={(event) => event.stopPropagation()}
            onDoubleClick={(event) => event.stopPropagation()}
            onContextMenu={(event) => event.preventDefault()}
            onKeyDown={onKeyDown}
            onBlur={onBlur}
        >
            {items.map((item) => (
                <button
                    key={item.label}
                    type="button"
                    role="menuitem"
                    className="context-menu__item"
                    disabled={item.disabled === true}

                    /* COMMITTED ON POINTER-UP, NOT ON CLICK, and this is the
                       difference between a menu that works with a mouse and one
                       that does not.

                       A `click` is the browser's own conclusion drawn from a
                       press and a release on the same element, and inside this
                       WebView it was not being drawn for this menu: the item
                       took the press, took the release, and no click arrived.
                       The menu could be driven from the keyboard and not from
                       the pointer, which is the one combination nobody tests
                       and everybody uses.

                       Pointer-up is also simply what a menu does. Press on a
                       menu, drag down the items, release on the one you want —
                       that is how every menu in every operating system behaves,
                       and committing on release is what makes it work.

                       `onClick` stays for the keyboard, where there is no
                       pointer at all: Enter and Space on a focused button
                       produce a click and nothing else. Whichever arrives
                       first closes the menu, and the guard makes sure the
                       second cannot act on a menu that is already gone. */
                    onPointerUp={(event) => { event.preventDefault(); choose(item); }}
                    onClick={() => choose(item)}
                >
                    {item.label}
                </button>
            ))}
        </div>,
        menuContainer(),
    );
}

/** Where a menu is rendered.

    NOT `document.body`, and the difference is the whole reason clicking a menu
    item did nothing. React delegates events by listening on its own root
    container; a portal into a node outside that container leaves the item
    receiving the pointer but never delivering a `click` to React, so the menu
    highlighted under the cursor, refused to be chosen, and could only be driven
    from the keyboard — which is exactly what it did.

    The React root is a node the menu can still escape into: it is the
    application frame, so rendering here is still outside the control that
    opened the menu — no `setPointerCapture` to swallow the press — and outside
    every panel's `overflow`, which is what made them clip a child menu once the
    panels became scroll containers.

    Falls back to the body if the frame is somehow missing, which is a page that
    has bigger problems than a menu.
*/
function menuContainer(): HTMLElement {
    return document.getElementById('root') ?? document.body;
}
