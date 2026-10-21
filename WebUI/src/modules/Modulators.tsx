/*
    What moves the sound: four DAHDSR envelopes and four LFOs, behind tab strips.

    The tab is the one piece of genuinely local state on the page — nothing else
    needs to know which envelope is on screen — so it is `useState` and nothing
    more. In the hand-built page the same thing needed a closure over an array of
    buttons and a hidden flag on each page element.

    Every page stays mounted. That matters: a trace is a running picture of its
    own modulator, and unmounting the three that are not on screen would mean
    each one starting from an empty ring every time you looked at it.
*/

import { useId, useState } from 'react';

import { Knob } from '../components/Knob';
import { Module, Tabs, tabPanelProps } from '../components/Module';
import { Segmented } from '../components/Segmented';
import { Trace } from '../components/Trace';
import { LABELS } from '../params/labels';
import { plainOf, useParameterValue } from '../state/parameters';

const INDICES = [1, 2, 3, 4];

export function Envelopes(): JSX.Element {
    const [selected, setSelected] = useState(0);
    const idBase = useId();

    return (
        <Module
            title="Envelope"
            index={String(selected + 1)}
            head={(
                <Tabs
                    count={4}
                    prefix="ENV "
                    selected={selected}
                    onSelect={setSelected}
                    label="Envelope"
                    idBase={idBase}
                />
            )}
        >
            {INDICES.map((index) => {
                const p = `env${index}_`;

                return (
                    <div
                        key={index}
                        className="cluster"
                        hidden={index !== selected + 1}
                        {...tabPanelProps(idBase, index - 1)}
                    >
                        <div className="cluster cluster--banner">
                            <Segmented
                                id={`${p}retrigger`}
                                label="Retrigger"
                                table={LABELS.envRetrigger}
                            />
                        </div>

                        <Trace source={`env${index}`} label={`env ${index}`} />

                        <Knob id={`${p}delay`} label="Delay" />
                        <Knob id={`${p}attack`} label="Attack" />
                        <Knob id={`${p}hold`} label="Hold" />
                        <Knob id={`${p}decay`} label="Decay" />
                        <Knob id={`${p}sustain`} label="Sustain" />
                        <Knob id={`${p}release`} label="Release" />
                        <Knob id={`${p}curve`} label="Curve" />
                    </div>
                );
            })}
        </Module>
    );
}

/** `lfoN_sync`'s value when the rate is taken from the host's tempo. */
const SYNCED = 1;

function LfoPage({ index, hidden, idBase }: { index: number; hidden: boolean; idBase: string }): JSX.Element {
    const p = `lfo${index}_`;

    // Synced, the division replaces the rate rather than sitting beside a
    // control that does nothing. Both stay in the page and the one not in use
    // is hidden, because a control that is not rendered at all is reported as
    // having no home (Unplaced.tsx).
    useParameterValue(`${p}sync`);
    const synced = Math.round(plainOf(`${p}sync`)) === SYNCED;

    return (
        <div className="cluster" hidden={hidden} {...tabPanelProps(idBase, index - 1)}>
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id={`${p}shape`} label="Shape" table={LABELS.lfoShape} />
                    <Segmented
                        id={`${p}retrigger`}
                        label="Trigger"
                        table={LABELS.lfoRetrigger}
                    />
                    <Segmented
                        id={`${p}polarity`}
                        label="Polarity"
                        table={LABELS.lfoPolarity}
                    />
                    <Segmented id={`${p}sync`} label="Rate" table={LABELS.lfoSync} />
                </div>

                <div className="cluster" hidden={!synced}>
                    <Segmented id={`${p}division`} label="Division" table={LABELS.lfoDivision} />
                </div>
            </div>

            <Trace source={`lfo${index}`} label={`lfo ${index}`} />

            <div hidden={synced}>
                <Knob id={`${p}rate`} label="Rate" />
            </div>
            <Knob id={`${p}phase`} label="Phase" />
            <Knob id={`${p}fade`} label="Fade In" />
            <Knob id={`${p}smoothing`} label="Smoothing" />
            <Knob id={`${p}steps`} label="Steps" />
        </div>
    );
}

export function Lfos(): JSX.Element {
    const [selected, setSelected] = useState(0);
    const idBase = useId();

    return (
        <Module
            title="LFO"
            index={String(selected + 1)}
            head={(
                <Tabs
                    count={4}
                    prefix="LFO "
                    selected={selected}
                    onSelect={setSelected}
                    label="LFO"
                    idBase={idBase}
                />
            )}
        >
            {INDICES.map((index) => (
                <LfoPage key={index} index={index} hidden={index !== selected + 1} idBase={idBase} />
            ))}
        </Module>
    );
}

/**
    The four macros. Each is a knob a performer moves — or a controller does,
    once MIDI Learn has assigned one; none is assigned by default — and the
    range and curve between that knob and what the matrix reads. Where a macro
    goes, and how far, is the matrix's: route "Macro 1" to as many destinations
    as it should reach, each with its own depth.
*/
export function Macros(): JSX.Element {
    return (
        <Module title="Macros">
            {INDICES.map((index) => {
                const p = `macro${index}`;

                return (
                    <div key={index} className="macro" role="group" aria-label={`Macro ${index}`}>
                        <Knob id={`${p}_value`} label={`Macro ${index}`} />
                        <Knob id={`${p}_min`} label="Min" />
                        <Knob id={`${p}_max`} label="Max" />
                        <Knob id={`${p}_curve`} label="Curve" />
                    </div>
                );
            })}
        </Module>
    );
}
