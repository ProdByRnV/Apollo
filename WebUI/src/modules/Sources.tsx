/*
    What makes the sound: two wavetable oscillators, a sub and a noise generator.
*/

import { Knob } from '../components/Knob';
import { Module } from '../components/Module';
import { Scope } from '../components/Scope';
import { Segmented } from '../components/Segmented';
import { WavetableDisplay } from '../components/WavetableDisplay';
import { LABELS } from '../params/labels';
import { plainOf, useParameterValue } from '../state/parameters';

/** True while a source is at a level that puts it outside the signal path. */
function useSilent(id: string): boolean {
    // Subscribed to as well as read, so the module's OFF chip appears and
    // disappears the moment the level crosses zero. The value itself comes from
    // the store's plain form, which applies the engine's own range.
    useParameterValue(id);
    return plainOf(id) <= 0.0001;
}

export function Oscillator({ index }: { index: number }): JSX.Element {
    const p = `osc${index}_`;

    return (
        <Module title="Oscillator" index={String(index)} inactive={useSilent(`${p}level`)}>
            <div className="cluster cluster--banner">
                <Segmented id={`${p}wavetable`} label="Wavetable" table={LABELS[`${p}wavetable`]} />
            </div>

            {/*
                First among the controls rather than last, because these are the
                two you watch while you move Position: the wave being crafted,
                beside the control that crafts it (CLAUDE.md §26.1).

                The pair is deliberate. The wavetable display is the *shape* at
                the current position — one cycle, unshaded by anything — and the
                scope is what that shape is producing once unison, detune and
                level have had their say. Sweeping Position moves both, and the
                difference between them is precisely what unison is doing.
            */}
            <WavetableDisplay oscillator={index} />
            <Scope source={`osc${index}`} />

            <Knob id={`${p}position`} label="Position" />
            <Knob id={`${p}level`} label="Level" />
            <Knob id={`${p}pan`} label="Pan" />

            {/*
                Both oscillators have the same controls since Phase 13a. Only
                oscillator 2 could be tuned before; oscillator 1 played the note.
            */}
            <Knob id={`${p}semitones`} label="Semitones" />
            <Knob id={`${p}fine`} label="Fine" />
            <Knob id={`${p}phase`} label="Phase" />

            {/*
                The warp: how the cycle is reshaped as it is read (PRD §12), and
                how far. Together, because an amount means nothing without the
                mode beside it.
            */}
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id={`${p}warp_mode`} label="Warp" table={LABELS[`${p}warp_mode`]} />
                    <Knob id={`${p}warp_amount`} label="Amount" />
                </div>
            </div>

            {/*
                The unison stack, together: how many, how far apart and how they
                are spread across that distance, how wide, how loud the outer
                voices are against the centre, and how scattered their starts.
            */}
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Knob id={`${p}unison`} label="Unison" />
                    <Knob id={`${p}detune`} label="Detune" />
                    <Segmented id={`${p}detune_curve`} label="Curve" table={LABELS[`${p}detune_curve`]} />
                    <Knob id={`${p}spread`} label="Spread" />
                    <Knob id={`${p}unison_blend`} label="Blend" />
                    <Knob id={`${p}phase_random`} label="Random" />
                </div>
            </div>
        </Module>
    );
}

export function SubOscillator(): JSX.Element {
    return (
        <Module title="Sub Oscillator" inactive={useSilent('sub_level')}>
            <Scope source="sub" />
            <Segmented id="sub_waveform" label="Wave" table={LABELS.sub_waveform} />
            <Segmented id="sub_octave" label="Octave" table={LABELS.sub_octave} />
            <Segmented id="sub_tracking" label="Tracks" table={LABELS.sub_tracking} />
            <Knob id="sub_level" label="Level" />
            <Knob id="sub_phase" label="Phase" />
        </Module>
    );
}

export function Noise(): JSX.Element {
    return (
        <Module title="Noise" inactive={useSilent('noise_level')}>
            <Scope source="noise" />
            <Segmented id="noise_type" label="Type" table={LABELS.noise_type} />
            <Knob id="noise_level" label="Level" />
            <Knob id="noise_keytrack" label="Key Track" />
        </Module>
    );
}
