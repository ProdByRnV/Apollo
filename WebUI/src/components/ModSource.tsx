/*
    Modulation sources you can pick up and put on a control (Phase 13h-4,
    ADR-0108).

    A handle is a source. Drag it onto a knob outlined in gold and the engine
    routes it there, in the first free matrix slot, at a quarter of the
    destination's range. Or click it — or give it Enter — and it is held until a
    control is clicked or given Enter, which is the way that works from the
    keyboard. Escape puts it down.

    The page only says what was dropped where. Which slot it takes, whether the
    pairing already exists and whether there is room are the engine's answers,
    because only the engine knows the matrix as it is at the moment the drop
    lands.

    Pointer events with capture rather than the browser's drag and drop: those
    show a ghost image of the operating system's choosing, cannot be driven
    from the keyboard, and inside a plugin's web view are at the mercy of
    whatever the host does with a drag that leaves its window.
*/

import { useEffect, useRef } from 'react';

import { modulationAssign } from '../bridge/bridge';
import { LABELS } from '../params/labels';
import { SOURCE_COUNT } from '../params/modulationRouting';
import { definitionOf } from '../state/parameters';
import { beginCarry, currentCarry, endCarry, moveCarry, useCarry } from '../state/modDrag';
import { announce } from '../state/status';
import { hasCommandModifier, rovingTarget, tabStopIndex } from './roving';

/** Pixels the pointer must travel before a press becomes a drag. Below it, a
    press is a click, and picks the source up. */
const DRAG_THRESHOLD = 4;

/** How near the top or bottom of the page a dragged source starts scrolling
    it, and how fast at the very edge, in pixels per frame. */
const SCROLL_EDGE = 56;
const SCROLL_SPEED = 18;

export function sourceName(source: number): string {
    return LABELS.modSource?.[source] ?? `Source ${source}`;
}

/** The control under a point that can take a source, or null. */
function targetAt(x: number, y: number): string | null {
    const element = document.elementFromPoint(x, y);
    const target = element?.closest<HTMLElement>('[data-mod-target]');
    return target?.dataset.modTarget ?? null;
}

function controlName(id: string): string {
    return definitionOf(id)?.name ?? id;
}

/** Routes the carried source to @p id. Called by a knob when it is clicked or
    given Enter with a source picked up. */
export function dropOn(id: string): boolean {
    const carried = currentCarry();
    if (carried === null) return false;

    endCarry();
    modulationAssign(carried.source, id);
    return true;
}

function pickUp(source: number): void {
    const carried = currentCarry();

    if (carried !== null && carried.source === source) {
        endCarry();
        announce(`${sourceName(source)} put down.`);
        return;
    }

    beginCarry(source, 'pick', 0, 0);
    announce(`${sourceName(source)} picked up. Click a control outlined in gold, or press Enter on one, `
        + 'to modulate it. Escape puts it down.');
}

interface HandleProps {
    source: number;
    /** -1 when it is not in a roving group. */
    tabIndex?: number;
    compact?: boolean;
}

export function ModSourceHandle({ source, tabIndex = 0, compact = false }: HandleProps): JSX.Element {
    const press = useRef<{ x: number; y: number; dragging: boolean } | null>(null);
    const scroll = useRef<{ frame: number; x: number; y: number } | null>(null);
    const carry = useCarry();

    const held = carry !== null && carry.source === source;

    const stopScrolling = (): void => {
        if (scroll.current !== null) cancelAnimationFrame(scroll.current.frame);
        scroll.current = null;
    };

    useEffect(() => stopScrolling, []);

    /** While dragging, a pointer held near the top or bottom of the page
        scrolls it, so a source in the matrix can reach an oscillator. */
    const scrollStep = (scroller: Element): void => {
        const state = scroll.current;
        if (state === null) return;

        const bounds = scroller.getBoundingClientRect();
        let step = 0;

        if (state.y < bounds.top + SCROLL_EDGE) {
            step = -SCROLL_SPEED * Math.min(1, (bounds.top + SCROLL_EDGE - state.y) / SCROLL_EDGE);
        } else if (state.y > bounds.bottom - SCROLL_EDGE) {
            step = SCROLL_SPEED * Math.min(1, (state.y - (bounds.bottom - SCROLL_EDGE)) / SCROLL_EDGE);
        }

        if (step !== 0) {
            scroller.scrollTop += step;
            moveCarry(state.x, state.y, targetAt(state.x, state.y));
        }

        state.frame = requestAnimationFrame(() => scrollStep(scroller));
    };

    const onPointerDown = (event: React.PointerEvent<HTMLButtonElement>): void => {
        if (event.button !== 0) return;

        event.currentTarget.setPointerCapture(event.pointerId);
        press.current = { x: event.clientX, y: event.clientY, dragging: false };

        // Not preventDefault: the button still takes focus, so a source picked
        // up with the mouse can be put down with Escape.
    };

    const onPointerMove = (event: React.PointerEvent<HTMLButtonElement>): void => {
        const state = press.current;
        if (state === null) return;

        if (!state.dragging) {
            if (Math.hypot(event.clientX - state.x, event.clientY - state.y) < DRAG_THRESHOLD) return;

            state.dragging = true;
            beginCarry(source, 'drag', event.clientX, event.clientY);

            const scroller = event.currentTarget.closest('.workspace') ?? document.scrollingElement;

            if (scroller !== null) {
                scroll.current = { frame: 0, x: event.clientX, y: event.clientY };
                scroll.current.frame = requestAnimationFrame(() => scrollStep(scroller));
            }
        }

        if (scroll.current !== null) {
            scroll.current.x = event.clientX;
            scroll.current.y = event.clientY;
        }

        moveCarry(event.clientX, event.clientY, targetAt(event.clientX, event.clientY));
    };

    const onPointerUp = (event: React.PointerEvent<HTMLButtonElement>): void => {
        const state = press.current;
        press.current = null;
        stopScrolling();

        // Taken before the capture is released, because losing the capture
        // puts a dragged source down.
        const carried = state?.dragging ? endCarry() : null;

        try {
            event.currentTarget.releasePointerCapture(event.pointerId);
        } catch {
            // Already released.
        }

        if (state === null) return;

        if (!state.dragging) {
            pickUp(source);
            return;
        }

        const over = carried?.over ?? null;

        if (over !== null) modulationAssign(source, over);
        else announce(`${sourceName(source)} was not dropped on a control it can move. `
            + 'The ones it can are outlined in gold while it is carried.');
    };

    const onPointerCancel = (): void => {
        press.current = null;
        stopScrolling();
        if (currentCarry()?.mode === 'drag') endCarry();
    };

    const onKeyDown = (event: React.KeyboardEvent<HTMLButtonElement>): void => {
        if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            pickUp(source);
        }
    };

    const name = sourceName(source);

    return (
        <button
            type="button"
            className={compact ? 'mod-source mod-source--compact' : 'mod-source'}
            data-mod-source={source}
            data-held={held ? 'true' : 'false'}
            aria-pressed={held}
            aria-label={`${name}: drag onto a control to modulate it, or press Enter and choose one`}
            title={`Drag ${name} onto a control outlined in gold to modulate it, or click it and then click the control.`}
            tabIndex={tabIndex}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={onPointerUp}
            onPointerCancel={onPointerCancel}
            onLostPointerCapture={onPointerCancel}

            // A click is handled on pointer-up above; the keyboard's own
            // click is handled by onKeyDown. Left alone, Enter would do both.
            onClick={(event) => event.preventDefault()}
            onKeyDown={onKeyDown}
        >
            <span className="mod-source__grip" aria-hidden="true">⠿</span>
            {name}
        </button>
    );
}

/** Every source, in matrix order, as one Tab stop with the arrows moving
    along it. */
export function ModSourceStrip(): JSX.Element {
    const sources = Array.from({ length: SOURCE_COUNT - 1 }, (_, index) => index + 1);
    const stop = tabStopIndex(0, sources.length);

    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(event)) return;

        const buttons = Array.from(event.currentTarget.querySelectorAll<HTMLButtonElement>('.mod-source'));
        const focused = buttons.findIndex((button) => button === document.activeElement);
        const target = rovingTarget(event.key, Math.max(0, focused), buttons.length, { orientation: 'horizontal' });

        if (target === null) return;

        event.preventDefault();
        buttons.forEach((button, index) => { button.tabIndex = index === target ? 0 : -1; });
        buttons[target]?.focus();
    };

    return (
        <div className="mod-sources" role="toolbar" aria-label="Modulation sources" onKeyDown={onKeyDown}>
            <span className="mod-sources__hint">Drag a source onto a control</span>
            {sources.map((source, index) => (
                <ModSourceHandle key={source} source={source} tabIndex={index === stop ? 0 : -1} compact />
            ))}
        </div>
    );
}

/** What is being carried, drawn under the pointer, and the page-wide rules
    that hold while it is: the controls that can take it are outlined, Escape
    puts it down, and a click on anything else does too. Mounted once. */
export function ModCarryLayer(): JSX.Element | null {
    const carry = useCarry();
    const carrying = carry !== null;
    const picked = carry?.mode === 'pick';

    useEffect(() => {
        if (carrying) document.body.dataset.carrying = 'true';
        else delete document.body.dataset.carrying;
    }, [carrying]);

    useEffect(() => {
        if (!carrying) return undefined;

        const onKeyDown = (event: KeyboardEvent): void => {
            if (event.key !== 'Escape') return;

            const was = endCarry();
            if (was !== null) announce(`${sourceName(was.source)} put down.`);
            event.preventDefault();
            event.stopPropagation();
        };

        document.addEventListener('keydown', onKeyDown, true);
        return () => document.removeEventListener('keydown', onKeyDown, true);
    }, [carrying]);

    useEffect(() => {
        if (!picked) return undefined;

        // A source that is picked up lights the control under the pointer as a
        // dragged one does.
        const onPointerMove = (event: PointerEvent): void => {
            moveCarry(event.clientX, event.clientY, targetAt(event.clientX, event.clientY));
        };

        // Anything but a control that can take it, or a source, puts it down.
        // The knob's own handler makes the drop.
        const onPointerDown = (event: PointerEvent): void => {
            const element = event.target instanceof Element ? event.target : null;
            if (element?.closest('[data-mod-target], [data-mod-source]')) return;

            const was = endCarry();
            if (was !== null) announce(`${sourceName(was.source)} put down.`);
        };

        document.addEventListener('pointermove', onPointerMove, true);
        document.addEventListener('pointerdown', onPointerDown, true);

        return () => {
            document.removeEventListener('pointermove', onPointerMove, true);
            document.removeEventListener('pointerdown', onPointerDown, true);
        };
    }, [picked]);

    if (carry === null || carry.mode !== 'drag') return null;

    return (
        <div
            className="mod-ghost"
            aria-hidden="true"
            data-over={carry.over !== null ? 'true' : 'false'}
            style={{ left: carry.x + 14, top: carry.y + 14 }}
        >
            {sourceName(carry.source)}
            {carry.over !== null ? <span className="mod-ghost__target"> → {controlName(carry.over)}</span> : null}
        </div>
    );
}
