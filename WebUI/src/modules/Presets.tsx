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
*/

import { useEffect, useState } from 'react';

import { loadPreset, rescanPresets, savePreset } from '../bridge/bridge';
import { Module } from '../components/Module';
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

    return (
        <Module
            title="Presets"
            bodyClassName="presets"
            head={
                <>
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

                    <button
                        className="masthead__button masthead__button--quiet"
                        type="button"
                        title="Look at the preset folders again"
                        onClick={rescanPresets}
                    >
                        {presets.scanning ? 'Scanning…' : 'Rescan'}
                    </button>
                </>
            }
        >
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

                <button className="presets__action" type="button" onClick={openSave}>
                    Save…
                </button>
            </div>

            <div className="presets__list" role="listbox" aria-label="Presets" tabIndex={-1}>
                {visible.map((entry) => (
                    <button
                        key={entry.id}
                        type="button"
                        role="option"
                        className="presets__row"
                        aria-selected={entry.id === presets.loaded}
                        data-loaded={entry.id === presets.loaded}
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

            {saveOpen ? (
                <div className="presets__save">
                    <div className="presets__fields">
                        <label className="presets__field">
                            <span>Name</span>
                            <input
                                type="text"
                                value={draft.name}
                                autoFocus
                                onChange={(event) => update('name', event.target.value)}
                            />
                        </label>

                        <label className="presets__field">
                            <span>Author</span>
                            <input
                                type="text"
                                value={draft.author}
                                onChange={(event) => update('author', event.target.value)}
                            />
                        </label>

                        <label className="presets__field">
                            <span>Category</span>
                            <input
                                type="text"
                                list="apollo-preset-categories"
                                value={draft.category}
                                onChange={(event) => update('category', event.target.value)}
                            />
                        </label>

                        <label className="presets__field">
                            <span>Bank</span>
                            <input
                                type="text"
                                list="apollo-preset-banks"
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
                            value={draft.comment}
                            onChange={(event) => update('comment', event.target.value)}
                        />
                    </label>

                    <div className="presets__save-actions">
                        {replacing ? (
                            <>
                                <span className="presets__warning">{presets.statusMessage}</span>
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
                            onClick={() => { clearPresetStatus(); setSaveOpen(false); }}
                        >
                            Cancel
                        </button>
                    </div>
                </div>
            ) : null}
        </Module>
    );
}

/** @returns the bank of the loaded preset, or "" if it did not come from one. */
function currentBank(entries: readonly PresetEntry[], loaded: number): string {
    return entries.find((entry) => entry.id === loaded)?.bank ?? '';
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
