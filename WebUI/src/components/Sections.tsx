/*
    The instrument in five tabs (Phase 17, ADR-0117): Oscillators, Filters,
    Modulation, Effects, and MIDI & Output — the signal path, in the order it
    runs. It replaced one page twenty panels long.

    THE BAR STAYS IN VIEW. It sits under the presets and sticks to the top of
    the workspace once the presets have scrolled away, with the modulation
    sources under the tabs, so that a source can be dragged onto a control in
    whichever tab is showing. The sources are hidden on the MIDI & Output tab,
    where nothing can be modulated.

    MODULATION CROSSES TABS. A source picked up with a click stays picked up
    while a tab is chosen, and one being dragged opens the tab it is held over
    for a moment, as a folder does on a desktop. So an LFO can go from here to a
    filter's cutoff either way.

    EVERY PAGE STAYS MOUNTED, as the envelope and LFO pages do (Module.tsx): a
    scope is a running picture of its source, and a page that was rebuilt each
    time it was shown would start every one of them from nothing. The pages
    that are not showing are `hidden`, which also keeps them out of the Tab
    order and away from a screen reader.

    A real tab strip, as the module tabs are: one Tab stop, the arrows move and
    choose, each tab names the page it shows and each page the tab it belongs to.
*/

import { useEffect, useRef } from 'react';
import type { ReactNode } from 'react';

import { useCarry } from '../state/modDrag';
import { ModSourceStrip } from './ModSource';
import { hasCommandModifier, rovingTarget, tabStopIndex } from './roving';

export const SECTIONS = ['Oscillators', 'Filters', 'Modulation', 'Effects', 'MIDI & Output'] as const;

/** How long a dragged source rests on a tab before the tab opens. Long enough
    that passing over the strip on the way to a control does not change page. */
const SPRING_MS = 450;

export interface SectionBarProps {
    selected: number;
    onSelect: (index: number) => void;
    idBase: string;
}

export function SectionBar({ selected, onSelect, idBase }: SectionBarProps): JSX.Element {
    const carry = useCarry();
    const stop = tabStopIndex(selected, SECTIONS.length);

    // A dragged source held over a tab opens it. Only a drag: a source picked
    // up with a click is carried by the pointer's position too, but the click
    // that chooses a tab is the way to change page with it.
    const dragging = carry !== null && carry.mode === 'drag';
    const overTab = dragging ? tabAt(carry.x, carry.y) : null;
    const pending = useRef<{ tab: number; timer: number } | null>(null);

    // The bar's height, for the pages' least height (the stylesheet's
    // .section): a short page has to be able to scroll to where the bar sticks.
    const bar = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const element = bar.current;
        const workspace = element?.closest<HTMLElement>('.workspace');
        if (!element || !workspace) return undefined;

        const observer = new ResizeObserver(() => {
            workspace.style.setProperty('--sections-height', `${element.offsetHeight}px`);
        });

        observer.observe(element);
        return () => observer.disconnect();
    }, []);

    useEffect(() => {
        // Every page has the strip since the master became a destination
        // (Phase 19b), so a source held over any tab opens it.
        if (overTab === null || overTab === selected) {
            if (pending.current !== null) {
                window.clearTimeout(pending.current.timer);
                pending.current = null;
            }
            return;
        }

        if (pending.current?.tab === overTab) return;
        if (pending.current !== null) window.clearTimeout(pending.current.timer);

        pending.current = {
            tab: overTab,
            timer: window.setTimeout(() => {
                pending.current = null;
                onSelect(overTab);
            }, SPRING_MS),
        };
    }, [overTab, selected, onSelect]);

    useEffect(() => () => {
        if (pending.current !== null) window.clearTimeout(pending.current.timer);
    }, []);

    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(event)) return;

        const tabs = Array.from(event.currentTarget.querySelectorAll<HTMLButtonElement>('[role="tab"]'));
        const focused = tabs.findIndex((tab) => tab === document.activeElement);
        const target = rovingTarget(event.key, focused >= 0 ? focused : selected, SECTIONS.length, {
            orientation: 'horizontal',
        });

        if (target === null) return;

        event.preventDefault();
        onSelect(target);
        tabs[target]?.focus();
    };

    return (
        <div className="sections" ref={bar}>
            <div className="sections__tabs" role="tablist" aria-label="Sections" onKeyDown={onKeyDown}>
                {SECTIONS.map((name, index) => (
                    <button
                        key={name}
                        id={`${idBase}-tab-${index}`}
                        type="button"
                        className="sections__tab"
                        role="tab"
                        aria-selected={index === selected}
                        aria-controls={`${idBase}-panel-${index}`}
                        tabIndex={index === stop ? 0 : -1}
                        data-section-tab={index}
                        data-spring={overTab === index && index !== selected ? 'true' : undefined}

                        // A source picked up with a click stays picked up while
                        // the page changes (ModCarryLayer).
                        data-keeps-carry="true"
                        onClick={() => onSelect(index)}
                    >
                        {name}
                    </button>
                ))}
            </div>

            {/* On every tab: since 19b the master, on MIDI & Output, takes a
                source too. */}
            <ModSourceStrip />
        </div>
    );
}

/** The tab under a point, or null. */
function tabAt(x: number, y: number): number | null {
    const element = document.elementFromPoint(x, y);
    const tab = element?.closest<HTMLElement>('[data-section-tab]');
    const index = tab ? Number(tab.dataset.sectionTab) : NaN;
    return Number.isInteger(index) ? index : null;
}

export interface SectionPageProps {
    index: number;
    selected: number;
    idBase: string;
    children: ReactNode;
}

/** One tab's page. Mounted always; shown when chosen. */
export function SectionPage({ index, selected, idBase, children }: SectionPageProps): JSX.Element {
    return (
        <div
            className="section"
            id={`${idBase}-panel-${index}`}
            role="tabpanel"
            aria-labelledby={`${idBase}-tab-${index}`}
            hidden={index !== selected}
        >
            {children}
        </div>
    );
}
