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

import { useState } from 'react';

import { Knob } from '../components/Knob';
import { Module, Tabs } from '../components/Module';
import { Segmented } from '../components/Segmented';
import { Trace } from '../components/Trace';
import { LABELS } from '../params/labels';

const INDICES = [1, 2, 3, 4];

export function Envelopes(): JSX.Element {
    const [selected, setSelected] = useState(0);

    return (
        <Module
            title="Envelope"
            index={String(selected + 1)}
            head={<Tabs count={4} prefix="ENV " selected={selected} onSelect={setSelected} />}
        >
            {INDICES.map((index) => {
                const p = `env${index}_`;

                return (
                    <div key={index} className="cluster" hidden={index !== selected + 1}>
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

export function Lfos(): JSX.Element {
    const [selected, setSelected] = useState(0);

    return (
        <Module
            title="LFO"
            index={String(selected + 1)}
            head={<Tabs count={4} prefix="LFO " selected={selected} onSelect={setSelected} />}
        >
            {INDICES.map((index) => {
                const p = `lfo${index}_`;

                return (
                    <div key={index} className="cluster" hidden={index !== selected + 1}>
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
                            </div>
                        </div>

                        <Trace source={`lfo${index}`} label={`lfo ${index}`} />

                        <Knob id={`${p}rate`} label="Rate" />
                        <Knob id={`${p}phase`} label="Phase" />
                        <Knob id={`${p}fade`} label="Fade In" />
                        <Knob id={`${p}smoothing`} label="Smoothing" />
                        <Knob id={`${p}steps`} label="Steps" />
                    </div>
                );
            })}
        </Module>
    );
}
