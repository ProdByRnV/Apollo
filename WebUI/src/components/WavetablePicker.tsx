/*
    An oscillator's wavetable, chosen from one list (Phase 16d, ADR-0116).

    Every table Apollo has and every table in the user's library, together:
    the button says what is playing, the arrows beside it step to the table
    before or after it — which is how to audition a library, with a note held —
    and the list opens beneath it. Add wavetable… opens a file from anywhere.

    Underneath, the first four set the Wavetable parameter, and everything else
    loads into the oscillator's own slot, which USER plays (wavetableChoices.ts).
    A host automating the parameter still moves among the four and USER, so the
    parameter keeps its MIDI Learn and its badge here, as the switch had them.

    NOTHING HERE DECIDES ANYTHING, as in the preset browser. A row sends a
    parameter value or a number and the button changes when the engine says
    what is playing; a file that turns out not to be a table changes nothing,
    and the sentence saying why appears under the button.

    THE LIST IS ONE TAB STOP, AND THE ARROW KEYS ONLY MOVE inside it: choosing
    replaces what the oscillator plays, so it happens on Enter or a click. The
    ‹ and › buttons are the exception, by being buttons: each press is a choice.
*/

import { useEffect, useRef, useState } from 'react';

import {
    chooseContentFile,
    clearContent,
    gesture,
    loadBuiltInTable,
    loadContent,
    rescanContent,
    revealContentFolder,
    saveContentToLibrary,
} from '../bridge/bridge';
import type { ContentSlot } from '../bridge/protocol';
import { LABELS, labelsFor } from '../params/labels';
import { toNormalized } from '../params/mapping';
import {
    filterChoices,
    playingKey,
    stepChoice,
    tableChoices,
    USER_TABLE_CHOICE,
} from '../params/wavetableChoices';
import type { TableChoice } from '../params/wavetableChoices';
import { describeSlot, useContent } from '../state/content';
import { commitValue, plainOf, useParameterDefinition, useParameterValue } from '../state/parameters';
import { useMidiChrome } from './midiChrome';
import { hasCommandModifier, rovingTarget } from './roving';

const FAILURES = new Set(['LOAD_FAILED', 'RESTORE_FAILED', 'SAVE_FAILED']);

export function WavetablePicker({ oscillator }: { oscillator: number }): JSX.Element | null {
    const id = `osc${oscillator}_wavetable`;
    const slot: ContentSlot = oscillator === 2 ? 'osc2' : 'osc1';

    const definition = useParameterDefinition(id);
    useParameterValue(id);
    const chrome = useMidiChrome(id, definition?.name ?? id);
    const content = useContent();

    const [open, setOpen] = useState(false);
    const [search, setSearch] = useState('');
    const [focusedKey, setFocusedKey] = useState<string | null>(null);
    const root = useRef<HTMLDivElement>(null);

    // A press anywhere else closes the list, as a menu would.
    useEffect(() => {
        if (!open) return undefined;

        const onPointerDown = (event: PointerEvent): void => {
            if (root.current !== null && event.target instanceof Node && !root.current.contains(event.target)) {
                setOpen(false);
            }
        };

        document.addEventListener('pointerdown', onPointerDown, true);
        return () => document.removeEventListener('pointerdown', onPointerDown, true);
    }, [open]);

    if (!definition) return null;

    const state = content.slots[slot];
    const event = content.events[slot];
    const choice = Math.round(plainOf(id));
    const classic = labelsFor(definition, LABELS[id]) ?? [];

    const choices = tableChoices(classic, content.builtInTables, content.wavetables);
    const playing = playingKey(choice, state);
    const visible = filterChoices(choices, search);

    const choose = (target: TableChoice): void => {
        switch (target.action.kind) {
            case 'parameter':
                gesture(id, 'begin');
                commitValue(id, toNormalized(definition, target.action.choice));
                gesture(id, 'end');
                break;

            case 'builtin':
                loadBuiltInTable(slot, target.action.table);
                break;

            case 'library':
                loadContent(slot, target.action.entry);
                break;
        }
    };

    const step = (direction: 1 | -1): void => {
        const target = stepChoice(choices, playing, direction);
        if (target !== null) choose(target);
    };

    // What the button says. A table loading says so; one from a file outside
    // the library is named though it has no row; USER with nothing loaded
    // plays the first table, and says that rather than an empty name.
    const current = choices.find((entry) => entry.key === playing);
    const userName = state.loading ? `Loading ${state.loadingName}…`
        : state.loaded ? state.name
            : `${classic[USER_TABLE_CHOICE] ?? 'USER'} · empty, plays ${classic[0] ?? 'the first table'}`;
    const shown = current?.name ?? (choice < USER_TABLE_CHOICE ? classic[choice] ?? '' : userName);

    const keptButIdle = choice < USER_TABLE_CHOICE && state.loaded;
    const line = choice >= USER_TABLE_CHOICE ? describeSlot(state)
        : keptButIdle ? `${state.name} is still in this patch, not playing.`
            : '';
    const saveable = choice >= USER_TABLE_CHOICE && state.loaded && (state.origin === 'file' || state.origin === 'factory');

    // The one Tab stop: the row last focused, then the one playing, then the
    // first that can be loaded.
    const stopKey = [focusedKey, playing].find((key) => visible.some((entry) => entry.key === key))
        ?? visible.find((entry) => entry.problem === '')?.key
        ?? null;

    const onListKeyDown = (keyEvent: React.KeyboardEvent<HTMLDivElement>): void => {
        if (keyEvent.key === 'Escape') {
            keyEvent.preventDefault();
            setOpen(false);
            root.current?.querySelector<HTMLButtonElement>('.wavetable-picker__current')?.focus();
            return;
        }

        if (hasCommandModifier(keyEvent)) return;

        const rows = Array.from(keyEvent.currentTarget.querySelectorAll<HTMLButtonElement>('[role="option"]'));
        const focused = rows.findIndex((row) => row === document.activeElement);
        const target = rovingTarget(keyEvent.key, focused, rows.length, { orientation: 'vertical', wrap: false });

        if (target === null) return;

        keyEvent.preventDefault();
        rows[target]?.focus();
    };

    const row = (entry: TableChoice): JSX.Element => (
        <button
            key={entry.key}
            type="button"
            role="option"
            className="content__row"
            aria-label={rowName(entry)}
            aria-selected={entry.key === playing}
            aria-disabled={entry.problem ? true : undefined}
            title={entry.problem || undefined}
            data-loaded={entry.key === playing}
            data-problem={entry.problem ? true : undefined}
            tabIndex={entry.key === stopKey ? 0 : -1}
            onFocus={() => setFocusedKey(entry.key)}
            onClick={() => {
                if (entry.problem) return;
                choose(entry);
                setOpen(false);
            }}
        >
            <span className="content__name">{entry.name}</span>
            <span className="content__meta">{entry.bank}</span>
            <span className="content__meta content__size">{entry.problem || framesText(entry.frames)}</span>
            <span className="content__origin">{entry.group === 'builtin' ? 'apollo' : entry.factory ? 'factory' : 'yours'}</span>
        </button>
    );

    const builtIns = visible.filter((entry) => entry.group === 'builtin');
    const library = visible.filter((entry) => entry.group === 'library');

    return (
        <div className="wavetable-picker content" ref={root} data-content-slot={slot}>
            <div className="segmented__label">Wavetable</div>

            <div className="wavetable-picker__bar">
                <div
                    className="wavetable-picker__choice"
                    title={chrome.title}
                    data-midi-id={id}
                    data-midi={chrome.state}
                >
                    <button
                        type="button"
                        className="wavetable-picker__step"
                        aria-label="Previous wavetable"
                        title="Previous wavetable"
                        onClick={() => step(-1)}
                    >
                        ‹
                    </button>

                    <button
                        type="button"
                        className="wavetable-picker__current"
                        aria-haspopup="listbox"
                        aria-expanded={open}
                        aria-label={`Oscillator ${oscillator} wavetable: ${shown}. Choose another`}
                        onClick={() => setOpen(!open)}
                    >
                        <span className="wavetable-picker__name">{shown}</span>
                        <span className="wavetable-picker__caret" aria-hidden="true">▾</span>
                    </button>

                    <button
                        type="button"
                        className="wavetable-picker__step"
                        aria-label="Next wavetable"
                        title="Next wavetable"
                        onClick={() => step(1)}
                    >
                        ›
                    </button>

                    <span
                        className="midi-badge"
                        aria-hidden="true"
                        hidden={chrome.state === undefined}
                        data-state={chrome.state}
                    >
                        {chrome.badge}
                    </span>
                </div>

                <span className="content__actions">
                    <button
                        type="button"
                        className="content__action"
                        title="Open a wavetable file from anywhere on this computer"
                        onClick={() => chooseContentFile(slot)}
                    >
                        Add wavetable…
                    </button>

                    {saveable ? (
                        <button
                            type="button"
                            className="content__action"
                            title="Write this table into your library as a file of its own, so it is listed here"
                            onClick={() => saveContentToLibrary(slot)}
                        >
                            Save to library
                        </button>
                    ) : null}

                    {keptButIdle ? (
                        <button
                            type="button"
                            className="content__action content__action--quiet"
                            title="Take this table out of the patch"
                            onClick={() => clearContent(slot)}
                        >
                            Remove
                        </button>
                    ) : null}
                </span>
            </div>

            {line ? (
                <div className="content__state" data-loaded={state.loaded} data-loading={state.loading}>{line}</div>
            ) : null}

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
                <div className="content__browser wavetable-picker__panel">
                    <div className="content__tools">
                        <input
                            type="search"
                            className="content__search"
                            placeholder="Search wavetables"
                            aria-label="Search wavetables"
                            value={search}
                            onChange={(changeEvent) => setSearch(changeEvent.target.value)}
                            onKeyDown={(keyEvent) => {
                                if (keyEvent.key === 'Escape') {
                                    keyEvent.preventDefault();
                                    setOpen(false);
                                }
                            }}
                        />

                        <button type="button" className="content__action" onClick={() => revealContentFolder('wavetable')}>
                            Show folder
                        </button>

                        <button type="button" className="content__action" onClick={() => rescanContent()}>
                            Rescan
                        </button>
                    </div>

                    <div
                        className="content__list wavetable-picker__list"
                        role="listbox"
                        aria-label={`Wavetables for oscillator ${oscillator}`}
                        tabIndex={-1}
                        onKeyDown={onListKeyDown}
                    >
                        {builtIns.length > 0 ? (
                            <div className="wavetable-picker__group" role="presentation">Apollo</div>
                        ) : null}
                        {builtIns.map(row)}

                        <div className="wavetable-picker__group" role="presentation">Your wavetables</div>
                        {library.map(row)}

                        {library.length === 0 ? (
                            <div className="content__empty">
                                {emptyMessage(content.wavetables.length, content.received, content.scanning)}
                            </div>
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

function framesText(frames: number): string {
    return frames > 0 ? `${frames} frame${frames === 1 ? '' : 's'}` : '';
}

/** A row as a sentence, for a screen reader: "Growl, bank Basses, 64 frames,
    yours". */
function rowName(entry: TableChoice): string {
    return [
        entry.name,
        entry.bank ? `bank ${entry.bank}` : '',
        entry.problem || framesText(entry.frames),
        entry.group === 'builtin' ? "Apollo's own" : entry.factory ? 'factory library' : 'your library',
    ].filter((part) => part !== '').join(', ');
}

function emptyMessage(total: number, received: boolean, scanning: boolean): string {
    if (!received || scanning) return 'Looking in your library…';
    if (total > 0) return 'None of yours match.';

    return 'None of your own yet. Add wavetable… opens one from anywhere; '
        + 'Show folder is where to keep them so they are listed here.';
}
