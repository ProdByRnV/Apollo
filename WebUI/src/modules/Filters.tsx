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

/** `filter_routing`'s value for split routing (Source/Engine/FilterSettings.h). */
const SPLIT = 2;

/** The sources split routing sends somewhere, in the order the voice mixes them. */
const SPLIT_SOURCES = [
    { id: 'osc1_filter', label: 'Osc 1' },
    { id: 'osc2_filter', label: 'Osc 2' },
    { id: 'sub_filter', label: 'Sub' },
    { id: 'noise_filter', label: 'Noise' },
] as const;

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

    // Each source's input means something only under split routing, so the
    // selectors are shown with it rather than sitting there doing nothing.
    useParameterValue('filter_routing');
    const split = Math.round(plainOf('filter_routing')) === SPLIT;

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

                {/*
                    Hidden rather than left out, because the inputs still live
                    here when they are not in use: a control that is not
                    rendered at all is reported as having no home, and lands in
                    the Unassigned panel at the foot of the page (Unplaced.tsx).
                */}
                {index === 2 ? (
                    <div className="cluster" hidden={!split}>
                        {SPLIT_SOURCES.map(({ id, label }) => (
                            <Segmented key={id} id={id} label={label} table={LABELS[id]} />
                        ))}
                    </div>
                ) : null}
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
