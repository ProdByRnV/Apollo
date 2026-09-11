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
    // Subscribed to rather than read, so the module dims the moment the level
    // does. The value itself comes from the store's plain form, which applies
    // the engine's own range.
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
            <Knob id={`${p}unison`} label="Unison" />
            <Knob id={`${p}detune`} label="Detune" />
            <Knob id={`${p}spread`} label="Spread" />

            {index === 2 ? <Knob id="osc2_semitones" label="Semitones" /> : null}
            {index === 2 ? <Knob id="osc2_fine" label="Fine" /> : null}
        </Module>
    );
}

export function SubOscillator(): JSX.Element {
    return (
        <Module title="Sub Oscillator" inactive={useSilent('sub_level')}>
            <Scope source="sub" />
            <Knob id="sub_level" label="Level" />
            <Segmented id="sub_octave" label="Octave" table={LABELS.sub_octave} />
        </Module>
    );
}

export function Noise(): JSX.Element {
    return (
        <Module title="Noise" inactive={useSilent('noise_level')}>
            <Scope source="noise" />
            <Knob id="noise_level" label="Level" />
        </Module>
    );
}
