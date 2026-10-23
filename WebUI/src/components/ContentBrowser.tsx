/*
    Choosing your own wavetable for an oscillator, or your own noise sample
    (Phase 13g, PRD §33).

    A LINE AND A LIST. The line is always there, under the oscillator's
    wavetable switch or the noise type: what this slot holds — name, size,
    where it came from — or that it holds nothing yet, and the four things
    that can be done about it. Browse opens the list of the library underneath
    it; the list closes again on its own button.

    WHAT IT DOES NOT DO is pick the table that plays. That is the switch above
    it, which is a parameter a host can automate: USER plays this slot, SAMPLE
    plays the noise sample. Loading something selects USER or SAMPLE for you,
    as one movement a host records; choosing a built-in afterwards leaves your
    table loaded, ready to come back to, and says so.

    NOTHING HERE DECIDES ANYTHING, as in the preset browser: a row sends a
    number and waits, and the highlight and the line move when the engine says
    the slot changed. A file that turns out not to be usable changes nothing,
    and the message saying why appears on the line, where the user is looking.

    THE LIST IS ONE TAB STOP, AND THE ARROWS ONLY MOVE, for the preset browser's
    reason: loading replaces what the slot held, so it happens on Enter or a
    click, never on an arrow.

    Content is carried inside the patch (ADR-0104), so nothing here asks the
    user to keep a file where it was.
*/

import { useState } from 'react';

import {
    chooseContentFile,
    clearContent,
    loadContent,
    rescanContent,
    revealContentFolder,
    saveContentToLibrary,
} from '../bridge/bridge';
import type { ContentEntry, ContentSlot } from '../bridge/protocol';
import { describeEntry, describeSlot, filterContent, useContent } from '../state/content';
import { hasCommandModifier, rovingTarget } from './roving';

export interface ContentBrowserProps {
    slot: ContentSlot;

    /** True when the slot's own choice — USER, or SAMPLE — is selected, so the
        line can say when what is loaded is not what is playing. */
    selected: boolean;
}

const FAILURES = new Set(['LOAD_FAILED', 'RESTORE_FAILED', 'SAVE_FAILED']);

export function ContentBrowser({ slot, selected }: ContentBrowserProps): JSX.Element {
    const content = useContent();
    const [open, setOpen] = useState(false);
    const [search, setSearch] = useState('');
    const [focusedId, setFocusedId] = useState(0);

    const noise = slot === 'noise';
    const kind = noise ? 'noise' : 'wavetable';
    const what = noise ? 'sample' : 'table';
    const state = content.slots[slot];
    const event = content.events[slot];

    const entries = noise ? content.noise : content.wavetables;
    const visible = filterContent(entries, search);

    // The one Tab stop: the row last focused, then the one loaded, then the
    // first that can be loaded.
    const stopId = [focusedId, state.entry].find((id) => visible.some((entry) => entry.id === id))
        ?? visible.find((entry) => !entry.problem)?.id
        ?? 0;

    const onListKeyDown = (keyEvent: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(keyEvent)) return;

        const rows = Array.from(keyEvent.currentTarget.querySelectorAll<HTMLButtonElement>('[role="option"]'));
        const focused = rows.findIndex((row) => row === document.activeElement);
        const target = rovingTarget(keyEvent.key, focused, rows.length, { orientation: 'vertical', wrap: false });

        if (target === null) return;

        keyEvent.preventDefault();
        rows[target]?.focus();
    };

    const line = describeSlot(state)
        || (noise ? 'No sample of your own yet. SAMPLE plays white noise until there is one.'
            : 'No table of your own yet. USER plays SWEEP until there is one.');

    return (
        <div className="content" data-content-slot={slot}>
            <div className="content__line">
                <span className="content__state" data-loaded={state.loaded} data-loading={state.loading}>
                    {line}
                </span>

                {state.loaded && !selected ? (
                    <span className="content__note">
                        {noise ? 'Plays when Type is SAMPLE' : 'Plays when Wavetable is USER'}
                    </span>
                ) : null}

                <span className="content__actions">
                    <button
                        type="button"
                        className="content__action"
                        aria-expanded={open}
                        onClick={() => setOpen(!open)}
                    >
                        {open ? 'Close' : 'Browse'}
                    </button>

                    <button type="button" className="content__action" onClick={() => chooseContentFile(slot)}>
                        Open file…
                    </button>

                    {state.loaded && state.origin !== 'library' ? (
                        <button
                            type="button"
                            className="content__action"
                            title={`Write this ${what} into your library as a file of its own`}
                            onClick={() => saveContentToLibrary(slot)}
                        >
                            Save to library
                        </button>
                    ) : null}

                    {state.loaded ? (
                        <button
                            type="button"
                            className="content__action content__action--quiet"
                            title={`Take this ${what} out of the patch`}
                            onClick={() => clearContent(slot)}
                        >
                            Remove
                        </button>
                    ) : null}
                </span>
            </div>

            {/*
                What last happened, in words. A failure is an alert, and said
                as one as well as coloured as one (CLAUDE.md §24.2).
            */}
            {event ? (
                <div
                    className="content__event"
                    data-tone={FAILURES.has(event.status) ? 'error' : 'info'}
                    role={FAILURES.has(event.status) ? 'alert' : 'status'}
                >
                    {event.message}
                </div>
            ) : null}

            {open ? (
                <div className="content__browser">
                    <div className="content__tools">
                        <input
                            type="search"
                            className="content__search"
                            placeholder={`Search ${noise ? 'samples' : 'wavetables'}`}
                            aria-label={`Search your ${noise ? 'noise samples' : 'wavetables'}`}
                            value={search}
                            onChange={(changeEvent) => setSearch(changeEvent.target.value)}
                        />

                        <button type="button" className="content__action" onClick={() => revealContentFolder(kind)}>
                            Show folder
                        </button>

                        <button type="button" className="content__action" onClick={() => rescanContent()}>
                            Rescan
                        </button>
                    </div>

                    <div
                        className="content__list"
                        role="listbox"
                        aria-label={noise ? 'Noise samples' : 'Wavetables'}
                        tabIndex={-1}
                        onKeyDown={onListKeyDown}
                    >
                        {visible.map((entry) => (
                            <button
                                key={entry.id}
                                type="button"
                                role="option"
                                className="content__row"
                                aria-label={rowName(entry)}
                                aria-selected={entry.id === state.entry}
                                aria-disabled={entry.problem ? true : undefined}
                                title={entry.problem || undefined}
                                data-loaded={entry.id === state.entry}
                                data-problem={entry.problem ? true : undefined}
                                tabIndex={entry.id === stopId ? 0 : -1}
                                onFocus={() => setFocusedId(entry.id)}
                                onClick={() => { if (!entry.problem) loadContent(slot, entry.id); }}
                            >
                                <span className="content__name">{entry.name}</span>
                                <span className="content__meta">{entry.bank}</span>
                                <span className="content__meta content__size">{describeEntry(entry)}</span>
                                <span className="content__origin">{entry.factory ? 'factory' : 'user'}</span>
                            </button>
                        ))}

                        {visible.length === 0 ? (
                            <div className="content__empty">{emptyMessage(entries.length, content.received, content.scanning, noise)}</div>
                        ) : null}
                    </div>

                    {content.truncated ? (
                        <div className="content__event" data-tone="error">
                            The library is larger than Apollo will index; this list is not all of it.
                        </div>
                    ) : null}
                </div>
            ) : null}
        </div>
    );
}

/** A row as a sentence, for a screen reader: "Growl, bank Basses, 64 frames,
    user library". */
function rowName(entry: ContentEntry): string {
    return [
        entry.name,
        entry.bank ? `bank ${entry.bank}` : '',
        describeEntry(entry),
        entry.factory ? 'factory library' : 'user library',
    ].filter((part) => part !== '').join(', ');
}

function emptyMessage(total: number, received: boolean, scanning: boolean, noise: boolean): string {
    if (!received || scanning) return 'Looking in your library…';
    if (total > 0) return 'Nothing matches.';

    return noise
        ? 'No noise samples in your library yet. Open a file, or use Show folder and put some there.'
        : 'No wavetables in your library yet. Open a file, or use Show folder and put some there.';
}
