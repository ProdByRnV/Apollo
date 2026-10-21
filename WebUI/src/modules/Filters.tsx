/*
    What shapes the sound: two filters — state-variable, comb or formant — and
    how they are connected.
*/

import { Knob } from '../components/Knob';
import { Module } from '../components/Module';
import { Scope } from '../components/Scope';
import { Segmented } from '../components/Segmented';
import { LABELS } from '../params/labels';
import { vowelName } from '../params/vowel';
import { plainOf, useParameterValue } from '../state/parameters';

/** `filterN_type`'s value for the formant filter (Source/DSP/Filters/VoiceFilter.h). */
const FORMANT = 6;

export function Filter({ index }: { index: number }): JSX.Element {
    const p = `filter${index}_`;

    // Type 0 is Off, which is the filter's own bypass rather than a separate
    // enable, so the module follows it exactly.
    useParameterValue(`${p}type`);
    const type = Math.round(plainOf(`${p}type`));
    const bypassed = type === 0;

    // The formant filter's cutoff chooses a vowel rather than a corner, so the
    // control says which one; the readout still gives the frequency, which is
    // what the matrix and automation move.
    useParameterValue(`${p}cutoff`);
    const cutoffLabel = type === FORMANT ? `Vowel ${vowelName(plainOf(`${p}cutoff`))}` : 'Cutoff';

    return (
        <Module title="Filter" index={String(index)} inactive={bypassed}>
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id={`${p}type`} label="Type" table={LABELS[`${p}type`]} />

                    {/*
                        Routing lives on filter 2 because it describes how the
                        pair is connected, and filter 2 is where the pair becomes
                        visible.
                    */}
                    {index === 2 ? (
                        <Segmented
                            id="filter_routing"
                            label="Routing"
                            table={LABELS.filter_routing}
                        />
                    ) : null}
                </div>
            </div>

            {/*
                The scope belongs to the pair for the same reason the routing
                does, and says so: it is the signal leaving the whole section,
                not the signal leaving filter 2.
            */}
            {index === 2 ? <Scope source="filter" label="post-filter" /> : null}

            <Knob id={`${p}cutoff`} label={cutoffLabel} />
            <Knob id={`${p}resonance`} label="Resonance" />
            <Knob id={`${p}drive`} label="Drive" />
            <Knob id={`${p}keytrack`} label="Key Track" />
            <Knob id={`${p}mix`} label="Mix" />
        </Module>
    );
}
