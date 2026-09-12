/*
    What leaves, and how the controller on the desk is read.
*/

import { useState } from 'react';

import { Knob } from '../components/Knob';
import { Meter } from '../components/Meter';
import { Module } from '../components/Module';
import { Scope } from '../components/Scope';
import { Segmented } from '../components/Segmented';
import { LABELS } from '../params/labels';
import { applyControllerProfile } from '../bridge/bridge';
import { useMidi } from '../state/midi';
import { plainOf, useParameterValue } from '../state/parameters';

export function Output(): JSX.Element {
    return (
        <Module title="Output">
            <Scope source="output" />

            {/*
                Beside the scope rather than instead of it: the scope says what
                the wave looks like and the meter says how loud it is, and
                neither answers the other's question. The voice count lives in
                the meter's caption because it is the other thing you check when
                the output is not what you expected.
            */}
            <Meter />

            <Knob id="fx_distortion_mix" label="Dist Mix" />
            <Knob id="fx_delay_time" label="Delay Time" />
            <Knob id="master_gain" label="Master" />
        </Module>
    );
}

/*
    The controller-profile picker.

    Not built from parameter metadata, because a profile is not a parameter: it
    is a one-shot action with two outcomes the user has to choose between. Two
    buttons rather than a mode switch and one button, so what each does is
    readable without having to look anywhere else first.
*/
function ProfilePicker(): JSX.Element {
    const { profiles } = useMidi();
    const [chosen, setChosen] = useState('');

    const selected = profiles.find((profile) => profile.id === chosen) ?? profiles[0];

    return (
        <div className="cluster cluster--banner profiles">
            <div className="segmented__label">Controller Profile</div>

            <select
                className="select"
                aria-label="Controller profile"
                value={selected?.id ?? ''}
                onChange={(event) => setChosen(event.target.value)}
            >
                {profiles.map((profile) => (
                    <option key={profile.id} value={profile.id}>{profile.name}</option>
                ))}
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
            </div>

            <div className="profiles__note">
                {selected
                    ? `${selected.description} · ${selected.assignments} assignments`
                    : 'No profiles available.'}
            </div>
        </div>
    );
}

/*
    MIDI expression: how the controller on the desk is read.

    Deliberately its own module and deliberately last. These describe hardware
    rather than sound, and a bend range sitting among the oscillators would read
    as part of the patch.
*/
export function Expression(): JSX.Element {
    // Everything but the bend range is inert until a zone is chosen, and saying
    // so with the same OFF chip the silent sources carry is cheaper than
    // explaining it (ADR-0039).
    useParameterValue('mpe_zone');
    const zoneOff = plainOf('mpe_zone') < 0.5;

    return (
        <Module title="MIDI" inactive={zoneOff}>
            <ProfilePicker />

            <Knob id="midi_bend_range" label="Bend Range" />

            <div className="cluster cluster--banner">
                <Segmented id="mpe_zone" label="MPE Zone" table={LABELS.mpe_zone} />
            </div>

            <Knob id="mpe_members" label="Members" />
            <Knob id="mpe_bend_range" label="Note Bend" />
        </Module>
    );
}
