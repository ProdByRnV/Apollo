/*
    The frame every section of the instrument sits in, and the tab strip two of
    them use.

    `inactive` marks a module whose source is not in the signal path — a level at
    zero, a filter set to Off. It puts an OFF chip in the heading and changes
    nothing else. It used to dim the whole panel instead, which made the two
    oscillators look like different products whenever one of them was silent, and
    said so only in brightness (see the stylesheet).

    Marked rather than hidden either way: a user needs to see that the sub
    oscillator is at zero, not to find that its controls have vanished.

    In the hand-built page that was `dimWhen`, which pushed a callback onto a
    global watcher list and matched it against the id of whatever had just
    changed. Here the module reads the value it cares about and React does the
    rest — the single clearest saving of the migration, and the reason there is
    no watcher list in this codebase at all.

    A SECTION WITH A HEADING, rather than a box with some bold text in it. The
    page is nineteen panels long, and a screen reader moves through a page by
    its headings and regions: without them the only way from the presets to the
    filter was through every control in between (CLAUDE.md §39). The heading
    says the index too — "Oscillator 2", not "Oscillator" twice — in text a
    sighted reader does not need, because the chip beside it already says so.
*/

import { useId } from 'react';
import type { ReactNode } from 'react';

import { hasCommandModifier, rovingTarget, tabStopIndex } from './roving';

export interface ModuleProps {
    title: string;
    index?: string | undefined;
    /** Extra class on the body, for the matrix — which lays itself out as a
        block rather than as a row of controls. */
    bodyClassName?: string | undefined;
    /** Rendered at the right of the heading row — a tab strip, normally. */
    head?: ReactNode;
    inactive?: boolean | undefined;
    children: ReactNode;
}

export function Module(
    { title, index, bodyClassName, head, inactive, children }: ModuleProps,
): JSX.Element {
    const headingId = useId();

    return (
        <section
            className="module"
            aria-labelledby={headingId}
            data-inactive={inactive ? 'true' : 'false'}
        >
            <div className="module__head">
                <h2 className="module__title" id={headingId}>
                    {title}
                    {index !== undefined ? <span className="visually-hidden"> {index}</span> : null}
                    {inactive ? <span className="visually-hidden">, off</span> : null}
                </h2>
                {index !== undefined ? <span className="module__index" aria-hidden="true">{index}</span> : null}
                {inactive ? <span className="module__off" aria-hidden="true">off</span> : null}
                <div className="module__spacer" />
                {head}
            </div>

            <div className={bodyClassName ? `module__body ${bodyClassName}` : 'module__body'}>
                {children}
            </div>
        </section>
    );
}

/*
    Four envelopes and four LFOs are sixty controls. Shown at once they are a
    wall; behind a tab strip they are one instrument section that happens to have
    four of everything, which is how they are used.

    A real tab strip, which is three things the first version was not: one Tab
    stop with the arrows moving between tabs, each tab naming the page it shows,
    and each page saying which tab it belongs to. Activation follows the arrow,
    because showing a page costs nothing — every page stays mounted.
*/

export interface TabsProps {
    count: number;
    prefix: string;
    selected: number;
    onSelect: (index: number) => void;
    /** What the strip chooses between, for the reader that cannot see that it
        sits in the Envelope heading. */
    label: string;
    /** Shared with the pages, from `useId` in the owning module, so a tab and
        its page can name each other. */
    idBase: string;
}

/** The attributes that make an element the page for tab `index`. */
export function tabPanelProps(idBase: string, index: number): {
    id: string;
    role: 'tabpanel';
    'aria-labelledby': string;
} {
    return {
        id: `${idBase}-panel-${index}`,
        role: 'tabpanel',
        'aria-labelledby': `${idBase}-tab-${index}`,
    };
}

export function Tabs({ count, prefix, selected, onSelect, label, idBase }: TabsProps): JSX.Element {
    const stop = tabStopIndex(selected, count);

    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(event)) return;

        const tabs = Array.from(event.currentTarget.querySelectorAll<HTMLButtonElement>('[role="tab"]'));
        const focused = tabs.findIndex((tab) => tab === document.activeElement);

        // Horizontal only: the strip sits in a heading row, and up and down are
        // left for scrolling the page.
        const target = rovingTarget(event.key, focused >= 0 ? focused : selected, count, {
            orientation: 'horizontal',
        });

        if (target === null) return;

        event.preventDefault();
        onSelect(target);
        tabs[target]?.focus();
    };

    return (
        <div
            className="segmented__options"
            role="tablist"
            aria-label={label}
            style={{ marginLeft: 'auto' }}
            onKeyDown={onKeyDown}
        >
            {Array.from({ length: count }, (_, index) => (
                <button
                    key={index}
                    id={`${idBase}-tab-${index}`}
                    type="button"
                    className="segmented__option"
                    role="tab"
                    aria-selected={index === selected}
                    aria-controls={`${idBase}-panel-${index}`}
                    tabIndex={index === stop ? 0 : -1}
                    onClick={() => onSelect(index)}
                >
                    {prefix}{index + 1}
                </button>
            ))}
        </div>
    );
}
