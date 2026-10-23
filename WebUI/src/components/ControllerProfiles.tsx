/*
    Controller profiles and the assignments they make (Phase 13h-3).

    A profile is edited the way a sound is: apply it, change the assignments in
    the table below, and save it again under its name. Saving over a profile
    has to be confirmed, as saving over a preset does. Export and import open
    the system's own dialogs, which only the editor can do; the page asks, and
    never names a path (ADR-0063).

    Nothing here decides anything. The table shows what the engine says the
    assignments are, the picker moves when the engine says a profile was saved
    or imported, and every edit is a request that can be refused.
*/

import { useEffect, useState } from 'react';

import {
    applyControllerProfile,
    deleteControllerProfile,
    exportControllerProfile,
    importControllerProfile,
    midiMappingEdit,
    midiMappingRemove,
    saveControllerProfile,
} from '../bridge/bridge';
import { clearProfileStatus, mappingRows, useMidi, type MidiMapping } from '../state/midi';
import { definitionOf, useMetadataVersion } from '../state/parameters';

export function ProfilePicker(): JSX.Element {
    const midi = useMidi();
    const { profiles } = midi;

    const [chosen, setChosen] = useState('');
    const [name, setName] = useState('');
    const [description, setDescription] = useState('');

    // The engine names the profile a save or an import produced; the picker
    // moves to it then, and not when a button is pressed.
    useEffect(() => {
        if (midi.selectedProfile) setChosen(midi.selectedProfile);
    }, [midi.selectedSerial, midi.selectedProfile]);

    const selected = profiles.find((profile) => profile.id === chosen) ?? profiles[0];

    // Choosing one of the user's fills the save form with it, so the edit is
    // apply, change, save: the same name, and a question before replacing it.
    useEffect(() => {
        if (selected?.user) {
            setName(selected.name);
            setDescription(selected.description);
        }
    }, [selected?.id, selected?.user]);

    const builtIn = profiles.filter((profile) => !profile.user);
    const yours = profiles.filter((profile) => profile.user);

    const replacing = midi.profileStatus === 'ALREADY_EXISTS';
    const refusal = replacing || midi.profileStatus === 'SAVE_FAILED' ? midi.profileStatusMessage : '';

    const editName = (value: string): void => {
        setName(value);

        // A different name is a different file, so the question about the old
        // one no longer applies (as in the preset browser, CLAUDE.md §29.2).
        clearProfileStatus();
    };

    const save = (overwrite: boolean): void => {
        saveControllerProfile(name.trim(), description.trim(), overwrite);
    };

    return (
        <div className="cluster cluster--banner profiles">
            <div className="segmented__label">Controller Profile</div>

            <select
                className="select"
                aria-label="Controller profile"
                value={selected?.id ?? ''}
                onChange={(event) => setChosen(event.target.value)}
            >
                <optgroup label="Apollo's">
                    {builtIn.map((profile) => (
                        <option key={profile.id} value={profile.id}>{profile.name}</option>
                    ))}
                </optgroup>
                {yours.length > 0 && (
                    <optgroup label="Yours">
                        {yours.map((profile) => (
                            <option key={profile.id} value={profile.id}>{profile.name}</option>
                        ))}
                    </optgroup>
                )}
            </select>

            <div className="profiles__actions">
                <button
                    type="button"
                    className="masthead__button"
                    title="Release every existing assignment, then apply this profile"
                    onClick={() => { if (selected) applyControllerProfile(selected.id, 'replace'); }}
                >
                    REPLACE
                </button>
                <button
                    type="button"
                    className="masthead__button"
                    title="Add this profile to the assignments already made"
                    onClick={() => { if (selected) applyControllerProfile(selected.id, 'merge'); }}
                >
                    MERGE
                </button>
                <button
                    type="button"
                    className="masthead__button masthead__button--quiet"
                    title="Write this profile to a file, to keep or to send to someone"
                    disabled={!selected}
                    onClick={() => { if (selected) exportControllerProfile(selected.id); }}
                >
                    EXPORT…
                </button>
                <button
                    type="button"
                    className="masthead__button masthead__button--quiet"
                    title="Add a profile file to your profiles"
                    onClick={importControllerProfile}
                >
                    IMPORT…
                </button>
                {selected?.user && (
                    <button
                        type="button"
                        className="masthead__button masthead__button--quiet"
                        title="Remove this profile. The assignments it made stay"
                        onClick={() => deleteControllerProfile(selected.id)}
                    >
                        DELETE
                    </button>
                )}
            </div>

            <div className="profiles__note">
                {selected
                    ? `${selected.description || (selected.user ? 'One of yours.' : '')} · ${selected.assignments} assignments`
                    : 'No profiles available.'}
                {midi.unreadableProfiles > 0
                    && ` · ${midi.unreadableProfiles} file${midi.unreadableProfiles === 1 ? '' : 's'} in your profiles folder could not be read`}
            </div>

            <form
                className="profiles__save"
                onSubmit={(event) => {
                    event.preventDefault();
                    save(false);
                }}
            >
                <label className="presets__field">
                    <span>Save the assignments as</span>
                    <input
                        type="text"
                        value={name}
                        maxLength={64}
                        placeholder="Profile name"
                        onChange={(event) => editName(event.target.value)}
                    />
                </label>
                <label className="presets__field">
                    <span>Description</span>
                    <input
                        type="text"
                        value={description}
                        maxLength={200}
                        placeholder="Optional"
                        onChange={(event) => setDescription(event.target.value)}
                    />
                </label>
                <div className="presets__save-actions">
                    {replacing ? (
                        <>
                            <button type="button" className="presets__action" onClick={() => save(true)}>
                                Replace it
                            </button>
                            <button type="button" className="presets__action" onClick={clearProfileStatus}>
                                Keep it
                            </button>
                        </>
                    ) : (
                        <button type="submit" className="presets__action" disabled={name.trim() === ''}>
                            Save profile
                        </button>
                    )}
                    {refusal && <span className="presets__warning" role="alert">{refusal}</span>}
                </div>
            </form>
        </div>
    );
}

/*
    The assignments, editable.

    One row per assignment, in controller order. The controller number, the
    channel and the two ends of the range can each be changed; a change that
    takes a control from another parameter is allowed and reported, as learning
    one is. A row is never invented here: learn and the profiles make them.
*/
export function AssignmentList(): JSX.Element {
    const midi = useMidi();
    useMetadataVersion();

    const rows = mappingRows(midi.mappings);

    return (
        <div className="assignments">
            <div className="segmented__label">
                Assignments · {rows.length} of {midi.capacity || 64}
            </div>

            {rows.length === 0 ? (
                <div className="profiles__note">
                    None yet. Turn on MIDI LEARN and move a control on the controller, or apply a profile.
                </div>
            ) : (
                <table className="assignments__table">
                    <thead>
                        <tr>
                            <th scope="col">Control</th>
                            <th scope="col">CC</th>
                            <th scope="col">Channel</th>
                            <th scope="col">From</th>
                            <th scope="col">To</th>
                            <th scope="col"><span className="visually-hidden">Remove</span></th>
                        </tr>
                    </thead>
                    <tbody>
                        {rows.map(([id, mapping]) => (
                            <AssignmentRow key={id} id={id} mapping={mapping} />
                        ))}
                    </tbody>
                </table>
            )}
        </div>
    );
}

function AssignmentRow({ id, mapping }: { id: string; mapping: MidiMapping }): JSX.Element {
    const label = definitionOf(id)?.name ?? id;

    const send = (next: Partial<MidiMapping>): void => {
        const merged = { ...mapping, ...next };
        midiMappingEdit(id, merged.controller, merged.channel, merged.min, merged.max);
    };

    return (
        <tr>
            <th scope="row">{label}</th>
            <td>
                <NumberCell
                    label={`Controller number for ${label}`}
                    value={mapping.controller}
                    min={0}
                    max={127}
                    onCommit={(controller) => send({ controller })}
                />
            </td>
            <td>
                <select
                    className="select assignments__channel"
                    aria-label={`MIDI channel for ${label}`}
                    value={mapping.channel}
                    onChange={(event) => send({ channel: Number(event.target.value) })}
                >
                    <option value={0}>Any</option>
                    {Array.from({ length: 16 }, (_, i) => (
                        <option key={i + 1} value={i + 1}>{i + 1}</option>
                    ))}
                </select>
            </td>
            <td>
                <NumberCell
                    label={`Start of the range for ${label}, in percent`}
                    value={Math.round(mapping.min * 100)}
                    min={0}
                    max={100}
                    suffix="%"
                    onCommit={(percent) => send({ min: percent / 100 })}
                />
            </td>
            <td>
                <NumberCell
                    label={`End of the range for ${label}, in percent`}
                    value={Math.round(mapping.max * 100)}
                    min={0}
                    max={100}
                    suffix="%"
                    onCommit={(percent) => send({ max: percent / 100 })}
                />
            </td>
            <td>
                <button
                    type="button"
                    className="assignments__remove"
                    aria-label={`Release the MIDI control on ${label}`}
                    title="Release this assignment"
                    onClick={() => midiMappingRemove(id)}
                >
                    ×
                </button>
            </td>
        </tr>
    );
}

/** A whole number typed, committed on Enter or on leaving the field, put back
    on Escape. Out of range or not a number is put back rather than sent. */
function NumberCell({
    label,
    value,
    min,
    max,
    suffix,
    onCommit,
}: {
    label: string;
    value: number;
    min: number;
    max: number;
    suffix?: string;
    onCommit: (value: number) => void;
}): JSX.Element {
    const [draft, setDraft] = useState(String(value));

    // The engine's answer wins: an edit it refused, or one made elsewhere,
    // shows as what the assignment now is.
    useEffect(() => setDraft(String(value)), [value]);

    const commit = (): void => {
        const number = Number(draft.trim());

        if (draft.trim() === '' || !Number.isInteger(number) || number < min || number > max) {
            setDraft(String(value));
            return;
        }

        if (number !== value) onCommit(number);
    };

    return (
        <span className="assignments__number">
            <input
                type="text"
                inputMode="numeric"
                aria-label={label}
                value={draft}
                onChange={(event) => setDraft(event.target.value)}
                onBlur={commit}
                onKeyDown={(event) => {
                    if (event.key === 'Enter') commit();
                    if (event.key === 'Escape') setDraft(String(value));
                }}
            />
            {suffix && <span aria-hidden="true">{suffix}</span>}
        </span>
    );
}
