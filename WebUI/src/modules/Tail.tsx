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

            {/*
                The effects' controls live in their own panels, beside the rest
                of the effect they belong to: the distortion's moved there in 8a
                and `fx_delay_time` followed in 8b, when the delay that owns it
                was built. Nothing from the rack is left here.
            */}
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
    // NO OFF CHIP ON THIS PANEL, though there was one. It lit whenever MPE was
    // off — which is the default — and a panel called MIDI marked OFF reads as
    // "MIDI input is disabled", while the bend range and the profiles in it
    // work regardless (12c-3). The MPE Zone switch already shows OFF where it
    // applies, which is the only place it is true.
    return (
        <Module title="MIDI">
            <ProfilePicker />

            <Knob id="midi_bend_range" label="Bend Range" />

            <div className="cluster cluster--banner">
                <Segmented id="mpe_zone" label="MPE Zone" table={LABELS.mpe_zone} />
                {/*
                    Beside the zone because both decide what a message from the
                    controller is taken to mean. A program change loads a preset
                    by number: bank 0 is the built-in sounds, banks from 1 the
                    rest of the library as the browser lists it (ADR-0106).
                */}
                <Segmented
                    id="midi_program_change"
                    label="Program Change"
                    table={LABELS.midi_program_change}
                />
            </div>

            <Knob id="mpe_members" label="Members" />
            <Knob id="mpe_bend_range" label="Note Bend" />
        </Module>
    );
}
