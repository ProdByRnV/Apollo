/*
    The frame every section of the instrument sits in, and the tab strip two of
    them use.

    `inactive` dims a module when the thing it controls is not in the signal
    path. Dimming rather than hiding: a user needs to see that the sub oscillator
    is at zero, not to find that its controls have vanished.

    In the hand-built page that was `dimWhen`, which pushed a callback onto a
    global watcher list and matched it against the id of whatever had just
    changed. Here the module reads the value it cares about and React does the
    rest — the single clearest saving of the migration, and the reason there is
    no watcher list in this codebase at all.
*/

import type { ReactNode } from 'react';

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
    return (
        <div className="module" data-inactive={inactive ? 'true' : 'false'}>
            <div className="module__head">
                <div className="module__title">{title}</div>
                {index !== undefined ? <span className="module__index">{index}</span> : null}
                <div className="module__spacer" />
                {head}
            </div>

            <div className={bodyClassName ? `module__body ${bodyClassName}` : 'module__body'}>
                {children}
            </div>
        </div>
    );
}

/*
    Four envelopes and four LFOs are sixty controls. Shown at once they are a
    wall; behind a tab strip they are one instrument section that happens to have
    four of everything, which is how they are used.
*/

export interface TabsProps {
    count: number;
    prefix: string;
    selected: number;
    onSelect: (index: number) => void;
}

export function Tabs({ count, prefix, selected, onSelect }: TabsProps): JSX.Element {
    return (
        <div className="segmented__options" role="tablist" style={{ marginLeft: 'auto' }}>
            {Array.from({ length: count }, (_, index) => (
                <button
                    key={index}
                    type="button"
                    className="segmented__option"
                    role="tab"
                    aria-selected={index === selected}
                    aria-checked={index === selected}
                    onClick={() => onSelect(index)}
                >
                    {prefix}{index + 1}
                </button>
            ))}
        </div>
    );
}
