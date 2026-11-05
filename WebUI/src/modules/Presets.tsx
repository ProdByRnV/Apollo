/*
    The preset browser.

    WHERE IT SITS, AND WHY IT SITS THERE. Every other panel is ordered by signal
    flow, and this one is not part of it — a preset is what you choose before
    the signal exists. So it goes above the oscillators rather than into the
    chain, which also puts it where the eye starts.

    NOTHING HERE DECIDES ANYTHING. Choosing a row sends an id and waits; the
    highlight moves when the engine says the sound changed, not when the click
    happens. Saving sends a name and waits; if a preset of that name is already
    there the engine refuses, says so, and the refusal is what raises the
    replace prompt. That is the same rule the rest of the interface follows for
    parameter values, and it exists because a browser that moved its own
    highlight optimistically would eventually be showing a sound that is not
    playing (UI_BINDINGS.md §1, §6).

    THE SEARCH BOX AND THE TWO MENUS NEVER SEND ANYTHING. They narrow a list the
    page already holds. A round trip per keystroke would be a round trip to
    compute something a microsecond of filtering answers.

    THE LIST IS ONE TAB STOP, AND THE ARROWS ONLY MOVE. Up and down walk the
    rows; Enter or a click loads one. Loading on the arrow — auditioning — was
    rejected, because a load replaces the whole sound, and a list a keyboard
    user cannot cross without discarding what they had built is a list they
    cannot use. The stop is the row last focused, then the loaded one, then the
    first, so Tab returns to where the user was.

    THE ‹ AND › BUTTONS DO AUDITION (Phase 17). They are buttons, so each press
    is a choice, and they step through the list as it is shown — search and
    filters applied — so a category can be played through one sound at a time.

    THE LIST CAN BE HIDDEN (Phase 17, ADR-0117). It opens showing, and Hide list
    folds the panel to its heading: the sound's name, the arrows, and Init,
    Save, Export and Import, which work the same either way. Folded for the
    rest of the session, not remembered, as nothing about the layout is.
*/

import { useEffect, useState } from 'react';

import { exportPreset, importPresets, loadPreset, rescanPresets, revealPresetFolder, savePreset } from '../bridge/bridge';
import { Module } from '../components/Module';
import { hasCommandModifier, rovingTarget } from '../components/roving';
import { stepPreset } from '../params/presetStep';
import { announce } from '../state/status';

/** The engine's bounds on a preset's text, from State/PresetDocument.h. A field
    stops at them rather than letting a save be refused for its length. */
const PRESET_NAME_MAX = 128;
const PRESET_COMMENT_MAX = 1024;
import type { PresetEntry } from '../bridge/protocol';
import type { PresetState } from '../state/presets';
import {
    clearPresetStatus,
    distinctValues,
    filteredPresets,
    setPresetBankFilter,
    setPresetCategoryFilter,
    setPresetSearch,
    usePresets,
} from '../state/presets';

/** What the save fields hold, and whether the user has touched them.

    Kept as component state rather than in the store because it is a half-filled
    form: it belongs to this panel while it is open and means nothing once it is
    not. The store holds what the *engine* said, which is a different thing —
    and conflating the two is how a form ends up fighting a message that arrives
    while somebody is typing in it.
*/
interface Draft {
    name: string;
    author: string;
    category: string;
    bank: string;
    comment: string;
}

const emptyDraft: Draft = { name: '', author: '', category: '', bank: '', comment: '' };

export function Presets(): JSX.Element {
    const presets = usePresets();

    const [draft, setDraft] = useState<Draft>(emptyDraft);
    const [saveOpen, setSaveOpen] = useState(false);
    const [listOpen, setListOpen] = useState(true);

    /** The row the keyboard was last on, so leaving the list and coming back
        returns there rather than to the top. 0 until a row has had focus. */
    const [focusedId, setFocusedId] = useState(0);

    // The form closes when the engine confirms the save, not when the button is
    // pressed. A save can still be refused at that point, and a form that had
    // already gone would take the refusal with it.
    useEffect(() => {
        if (presets.status === 'SAVED') setSaveOpen(false);
    }, [presets.status]);

    const visible = filteredPresets(presets);
    const categories = distinctValues(presets.entries, 'category');
    const banks = distinctValues(presets.entries, 'bank');

    // Looked up in the unfiltered list rather than the visible one: the Init
    // button must go on working while a search is narrowing the rows away.
    const initId = presets.entries.find(
        (entry) => entry.factory && entry.name === 'Init',
    )?.id ?? 0;

    const update = (field: keyof Draft, value: string): void => {
        setDraft((current) => ({ ...current, [field]: value }));

        // A changed name or bank is a different file, so an answer about the
        // old one no longer applies. Above all the replace prompt: left up, it
        // named the preset it was asked about while "Replace it" sent the new
        // name with overwrite set — replacing a file nobody had been asked
        // about, which is the one thing a save must never do (CLAUDE.md §29.2).
        if ((field === 'name' || field === 'bank') && presets.status !== '') clearPresetStatus();
    };

    /** Opens the save form, filled in from the sound currently loaded.

        Filled rather than blank, because the overwhelmingly common save is a
        small change to something that already has a name, and retyping it is
        both work and a chance to misspell it into a second copy.
    */
    const openSave = (): void => {
        setDraft({
            name: presets.name,
            author: presets.author,
            category: presets.category,
            bank: currentBank(presets.entries, presets.loaded),
            comment: presets.comment,
        });

        clearPresetStatus();
        setSaveOpen(true);
    };

    const send = (overwrite: boolean): void => {
        savePreset({
            name: draft.name,
            author: draft.author,
            category: draft.category,
            comment: draft.comment,
            bank: draft.bank,
            overwrite,
        });
    };

    // The prompt is the rendering of a status rather than a flag of its own, so
    // there is no way for the two to disagree about whether it is showing.
    const replacing = presets.status === 'ALREADY_EXISTS';

    // Every answer to a save that is not "saved" is shown in the form, where
    // the user is looking, and not only on the footer behind it.
    const refusal = replacing || presets.status === 'SAVE_FAILED' || presets.status === 'INVALID_NAME'
        ? presets.statusMessage
        : '';

    // Whichever of the three candidates is actually on screen. A search can
    // filter the remembered row away, and a list with no stop in it could not
    // be reached from the keyboard at all.
    const isVisible = (id: number): boolean => visible.some((entry) => entry.id === id);
    const stopId = isVisible(focusedId)
        ? focusedId
        : (isVisible(presets.loaded) ? presets.loaded : (visible[0]?.id ?? 0));

    const step = (direction: 1 | -1): void => {
        const target = stepPreset(visible, presets.loaded, direction);
        if (target !== null) loadPreset(target.id);
    };

    // Where the loaded sound sits in the list being stepped through, for the
    // count beside its name; 0 for a sound the list does not show.
    const position = visible.findIndex((entry) => entry.id === presets.loaded) + 1;
    const filtered = visible.length !== presets.entries.length;

    const onListKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(event)) return;

        const rows = Array.from(event.currentTarget.querySelectorAll<HTMLButtonElement>('[role="option"]'));
        const focused = rows.findIndex((row) => row === document.activeElement);
        const target = rovingTarget(event.key, focused, rows.length, { orientation: 'vertical', wrap: false });

        if (target === null) return;

        event.preventDefault();
        rows[target]?.focus();
    };

    return (
        <Module
            title="Presets"
            bodyClassName="presets"
            beside={
                // The library is a folder in a hidden part of every system, so
                // this is the way to it: to back presets up, or drop in a pack
                // somebody made. Opened by the engine, which knows where it is.
                <button
                    className="masthead__button masthead__button--quiet"
                    type="button"
                    title="Open your presets folder. Preset files put there appear after Rescan"
                    onClick={revealPresetFolder}
                >
                    Open folder
                </button>
            }
            head={
                <>
                    <div className="presets__stepper">
                        <button
                            className="presets__step"
                            type="button"
                            aria-label="Previous preset"
                            title="Previous preset"
                            disabled={visible.length === 0}
                            onClick={() => step(-1)}
                        >
                            ‹
                        </button>

                        <span className="presets__current" title={presets.name || undefined}>
                            <span className="presets__current-name">{presets.name || 'Init'}</span>
                            {position > 0 ? (
                                <span className="presets__position">
                                    {position}/{visible.length}{filtered ? ' shown' : ''}
                                </span>
                            ) : null}
                        </span>

                        <button
                            className="presets__step"
                            type="button"
                            aria-label="Next preset"
                            title="Next preset"
                            disabled={visible.length === 0}
                            onClick={() => step(1)}
                        >
                            ›
                        </button>
                    </div>

                    {/*
                        Init is the first row of the list as well, but a list is
                        something you have to be looking at: once there are two
                        hundred presets and a search in the box, the one everybody
                        wants at the start of a sound should not have to be found.

                        It loads the built-in by its ordinary index id, so there
                        is no command here that the browser does not already
                        have — and if the factory content is somehow not in the
                        index, the button is absent rather than inert.
                    */}
                    <button
                        className="masthead__button masthead__button--quiet"
                        type="button"
                        hidden={initId === 0}
                        title="Return every control to its default"
                        onClick={() => loadPreset(initId)}
                    >
                        Init
                    </button>

                    <button className="presets__action" type="button" onClick={openSave}>
                        Save…
                    </button>

                    {/*
                        Sharing a sound (Phase 16a). The library is a hidden folder
                        on every system, so a preset is sent from a file put where
                        the user can find it, and one received is added from wherever
                        it landed. Both open the system's own dialog; the page never
                        names a file, and the answer arrives on the status line.
                    */}
                    <button
                        className="presets__action presets__action--quiet"
                        type="button"
                        title="Save the sound you are hearing as a file, to send to someone"
                        onClick={exportPreset}
                    >
                        Export…
                    </button>

                    <button
                        className="presets__action presets__action--quiet"
                        type="button"
                        title="Add preset files someone sent you to your presets"
                        onClick={importPresets}
                    >
                        Import…
                    </button>

                    <button
                        className="masthead__button masthead__button--quiet presets__fold"
                        type="button"
                        aria-expanded={listOpen}
                        title={listOpen ? 'Fold the presets away to this one line' : 'Show the list of presets'}
                        onClick={() => setListOpen(!listOpen)}
                    >
                        {listOpen ? 'Hide list ▴' : 'Show list ▾'}
                    </button>
                </>
            }
        >
            {/* The list, its filters and its count, while it is showing. */}
            {listOpen ? (
                <>
                    <div className="presets__filters">
                        <input
                            className="presets__search"
                            type="search"
                            placeholder="Search presets"
                            aria-label="Search presets"
                            value={presets.search}
                            onChange={(event) => setPresetSearch(event.target.value)}
                        />

                        <select
                            className="presets__filter"
                            aria-label="Category"
                            value={presets.categoryFilter}
                            onChange={(event) => setPresetCategoryFilter(event.target.value)}
                        >
                            <option value="">All categories</option>
                            {categories.map((category) => (
                                <option key={category} value={category}>{category}</option>
                            ))}
                        </select>

                        <select
                            className="presets__filter"
                            aria-label="Bank"
                            value={presets.bankFilter}
                            onChange={(event) => setPresetBankFilter(event.target.value)}
                        >
                            <option value="">All banks</option>
                            {banks.map((bank) => (
                                <option key={bank} value={bank}>{bank}</option>
                            ))}
                        </select>

                        <div className="module__spacer" />

                        <button
                            className="masthead__button masthead__button--quiet"
                            type="button"
                            title="Look at the preset folders again"
                            onClick={rescanPresets}
                        >
                            {presets.scanning ? 'Scanning…' : 'Rescan'}
                        </button>
                    </div>

                    <div
                        className="presets__list"
                        role="listbox"
                        aria-label="Presets"
                        tabIndex={-1}
                        onKeyDown={onListKeyDown}
                    >
                        {visible.length > 0 ? (
                            <div className="presets__row presets__columns" aria-hidden="true">
                                <span>Name</span>
                                <span>Category</span>
                                <span>Bank</span>
                                <span>Author</span>
                                <span>Library</span>
                            </div>
                        ) : null}

                        {visible.map((entry) => (
                            <button
                                key={entry.id}
                                type="button"
                                role="option"
                                className="presets__row"
                                // The headings are hidden from a screen reader, so the
                                // row says what each of its words is.
                                aria-label={presetRowName(entry)}
                                aria-selected={entry.id === presets.loaded}
                                data-loaded={entry.id === presets.loaded}
                                tabIndex={entry.id === stopId ? 0 : -1}
                                onFocus={() => setFocusedId(entry.id)}
                                onClick={() => loadPreset(entry.id)}
                            >
                                <span className="presets__name">{entry.name}</span>
                                <span className="presets__meta">{entry.category}</span>
                                <span className="presets__meta">{entry.bank}</span>
                                <span className="presets__meta presets__meta--author">{entry.author}</span>

                                {/*
                                    Said in a word rather than only in a colour, so the
                                    distinction survives a monochrome screenshot and a
                                    reader who cannot tell the two apart (CLAUDE.md
                                    §24.2, §39).
                                */}
                                <span className="presets__origin">{entry.factory ? 'factory' : 'user'}</span>
                            </button>
                        ))}

                        {visible.length === 0 ? (
                            <div className="presets__empty">{emptyMessage(presets)}</div>
                        ) : null}
                    </div>

                    {/*
                        The counts sit under the list rather than in a log nobody opens.
                        "Three files in this folder are not presets" is something a user
                        can act on, and a library quietly three shorter than the folder
                        is not (CLAUDE.md §33).
                    */}
                    <div className="presets__footnote">
                        <span>
                            {visible.length === presets.entries.length
                                ? `${presets.entries.length} preset${presets.entries.length === 1 ? '' : 's'}`
                                : `${visible.length} of ${presets.entries.length} presets`}
                        </span>

                        {presets.unreadable > 0 ? (
                            <span className="presets__warning">
                                {presets.unreadable} file{presets.unreadable === 1 ? '' : 's'} could not be
                                read as a preset
                            </span>
                        ) : null}

                        {presets.truncated ? (
                            <span className="presets__warning">
                                The library is larger than Apollo will index; this list is not all of it
                            </span>
                        ) : null}
                    </div>

                </>
            ) : null}

            {saveOpen ? (
                <div className="presets__save">
                    <div className="presets__fields">
                        <label className="presets__field">
                            <span>Name</span>
                            <input
                                type="text"
                                maxLength={PRESET_NAME_MAX}
                                value={draft.name}
                                autoFocus
                                onChange={(event) => update('name', event.target.value)}
                            />
                        </label>

                        <label className="presets__field">
                            <span>Author</span>
                            <input
                                type="text"
                                maxLength={PRESET_NAME_MAX}
                                value={draft.author}
                                onChange={(event) => update('author', event.target.value)}
                            />
                        </label>

                        <label className="presets__field">
                            <span>Category</span>
                            <input
                                type="text"
                                list="apollo-preset-categories"
                                maxLength={PRESET_NAME_MAX}
                                value={draft.category}
                                onChange={(event) => update('category', event.target.value)}
                            />
                        </label>

                        <label className="presets__field">
                            <span>Bank</span>
                            <input
                                type="text"
                                list="apollo-preset-banks"
                                maxLength={PRESET_NAME_MAX}
                                value={draft.bank}
                                onChange={(event) => update('bank', event.target.value)}
                            />
                        </label>

                        {/*
                            Offered rather than enforced. A list of what is
                            already in use saves typing and keeps categories from
                            splitting into three spellings, but a new category is
                            a normal thing to want and a menu would forbid it.
                        */}
                        <datalist id="apollo-preset-categories">
                            {categories.map((category) => <option key={category} value={category} />)}
                        </datalist>

                        <datalist id="apollo-preset-banks">
                            {banks.map((bank) => <option key={bank} value={bank} />)}
                        </datalist>
                    </div>

                    <label className="presets__field presets__field--wide">
                        <span>Comment</span>
                        <input
                            type="text"
                            maxLength={PRESET_COMMENT_MAX}
                            value={draft.comment}
                            onChange={(event) => update('comment', event.target.value)}
                        />
                    </label>

                    <div className="presets__save-actions">
                        {refusal ? (
                            <span className="presets__warning" role="alert">{refusal}</span>
                        ) : null}

                        {replacing ? (
                            <>
                                <button
                                    className="presets__action presets__action--danger"
                                    type="button"
                                    onClick={() => send(true)}
                                >
                                    Replace it
                                </button>
                            </>
                        ) : (
                            <button
                                className="presets__action"
                                type="button"
                                disabled={draft.name.trim() === ''}
                                onClick={() => send(false)}
                            >
                                Save
                            </button>
                        )}

                        <button
                            className="presets__action presets__action--quiet"
                            type="button"
                            onClick={() => {
                                clearPresetStatus();
                                setSaveOpen(false);

                                // Said, because the footer may still be showing
                                // the refusal that led here.
                                announce('Save cancelled. Nothing was written.');
                            }}
                        >
                            Cancel
                        </button>
                    </div>
                </div>
            ) : null}
        </Module>
    );
}

/** @returns a row as a sentence: "Glass Bell, Keys, bank Factory, by ProdByRnV,
    factory library". Empty fields are left out rather than read as blanks. */
function presetRowName(entry: PresetEntry): string {
    return [
        entry.name,
        entry.category,
        entry.bank ? `bank ${entry.bank}` : '',
        entry.author ? `by ${entry.author}` : '',
        entry.factory ? 'factory library' : 'user library',
    ].filter((part) => part !== '').join(', ');
}

/** @returns the bank of the loaded preset, or "" if it did not come from one.

    Also "" when it came from the factory library. A save always goes to the
    user library, so pre-filling "Factory" made a user folder of that name, and
    an edited Wire Pluck then sat in the Factory bank beside the real one,
    looking like a factory preset that was not one (12c-3).
*/
function currentBank(entries: readonly PresetEntry[], loaded: number): string {
    const entry = entries.find((candidate) => candidate.id === loaded);

    return entry && !entry.factory ? entry.bank : '';
}

/** @returns why the list is empty, in the terms that are actually true.

    Four different situations produce no rows, and they want four different
    sentences: nothing has been scanned yet, there are no presets at all, there
    are none in this user library, or the filters have excluded everything. A
    single "No presets" would be wrong three times out of four.
*/
function emptyMessage(presets: PresetState): string {
    if (!presets.received) return 'Looking for presets…';

    if (presets.entries.length > 0) return 'Nothing matches that search.';

    if (presets.userMissing && presets.factoryMissing) {
        return 'No preset folders yet. Saving a preset creates one.';
    }

    return 'No presets yet. Save the current sound to start a library.';
}
