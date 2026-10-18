/*
    Where the arrow keys go inside a group of controls.

    A radio group, a tab strip and a list are each ONE stop on the Tab key, and
    the arrows move within them. That is the WAI-ARIA pattern for all three, and
    it is not a nicety: before it, every option of every switch on the page was
    its own Tab stop — 206 radios, 253 stops in all — so reaching the filter from
    the presets took a hundred presses (CLAUDE.md §39).

    Pure arithmetic and nothing else, so it can be tested without a browser
    (WebUI/test/). The components decide what *happens* at the new index — a
    radio selects it, a tab shows its page, a list row only takes focus.
*/

export type Orientation = 'horizontal' | 'vertical' | 'both';

export interface RovingOptions {
    /** Which arrows move. A horizontal strip ignores up and down, so they stay
        free for whatever the page does with them. */
    orientation?: Orientation;
    /** Whether moving past an end comes round to the other. Radios and tabs
        wrap; a list stops, because a list's ends are places. */
    wrap?: boolean;
}

/** @returns the index focus should move to for `key`, or null when the key is
    not one a group handles — in which case the caller must leave the event
    alone, so Tab, Enter and Space go on doing what they do.

    `current` is where focus is now. Anything outside the group — nothing
    focused yet, or an index from a list that has since shrunk — is treated as
    "before the first", so the first press lands somewhere sensible rather than
    nowhere.
*/
export function rovingTarget(
    key: string,
    current: number,
    count: number,
    { orientation = 'both', wrap = true }: RovingOptions = {},
): number | null {
    if (!Number.isInteger(count) || count <= 0) return null;

    const inside = Number.isInteger(current) && current >= 0 && current < count;

    const horizontal = orientation !== 'vertical';
    const vertical = orientation !== 'horizontal';

    const forward = (horizontal && key === 'ArrowRight') || (vertical && key === 'ArrowDown');
    const backward = (horizontal && key === 'ArrowLeft') || (vertical && key === 'ArrowUp');

    if (key === 'Home') return 0;
    if (key === 'End') return count - 1;

    if (forward) {
        if (!inside) return 0;
        if (current < count - 1) return current + 1;
        return wrap ? 0 : current;
    }

    if (backward) {
        if (!inside) return count - 1;
        if (current > 0) return current - 1;
        return wrap ? count - 1 : current;
    }

    return null;
}

/** @returns which member of a group of `count` carries the group's one Tab
    stop: the selected one, or the first if nothing valid is selected — a group
    with no stop at all could not be reached from the keyboard. */
export function tabStopIndex(selected: number, count: number): number {
    return Number.isInteger(selected) && selected >= 0 && selected < count ? selected : 0;
}

/** True when a modifier other than Shift is held. A group leaves those keys to
    the platform, so Alt+Left still means what the system says it means. */
export function hasCommandModifier(event: { altKey: boolean; ctrlKey: boolean; metaKey: boolean }): boolean {
    return event.altKey || event.ctrlKey || event.metaKey;
}
