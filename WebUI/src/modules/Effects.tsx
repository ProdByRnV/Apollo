/*
    The effects rack, and the effects in it.

    The rack panel is the chain itself: six positions, in order, each naming what
    occupies it. It is a row of dropdowns rather than a drag-and-drop list
    because the chain is six values, and six values with names are easier to read
    — and far easier to reach from a keyboard (CLAUDE.md §39) — than six things
    to pick up and drop. Automation sees the same six values, which is the other
    reason: a chain you can rearrange by hand but not automate would be a chain
    with a secret.

    Each effect then gets its own panel, marked OFF when nothing in the chain
    selects it (ADR-0052). The panel stays put either way: a control that moves
    when it becomes relevant is a control you have to find twice.
*/

import { useState } from 'react';

import { EqCurve } from '../components/EqCurve';
import { Knob } from '../components/Knob';
import { Module, Tabs } from '../components/Module';
import { Segmented } from '../components/Segmented';
import { Select } from '../components/Select';
import { gesture } from '../bridge/bridge';
import { EQ_BAND_COUNT, EQ_FALLBACK_SAMPLE_RATE, eqTypeHasGain } from '../params/eqCurve';
import type { BandSettings } from '../params/eqCurve';
import { LABELS } from '../params/labels';
import { toNormalized } from '../params/mapping';
import {
    commitValue,
    definitionOf,
    plainOf,
    useParameterValue,
} from '../state/parameters';
import { useSampleRate } from '../state/telemetry';

export const RACK_SLOT_COUNT = 6;

/** Matches dsp::EffectType. */
export const EFFECT_DISTORTION = 1;
export const EFFECT_DELAY = 2;
export const EFFECT_REVERB = 3;
export const EFFECT_GATE = 4;
export const EFFECT_COMPRESSOR = 5;
export const EFFECT_EQUALISER = 6;

/** The effects that exist in this build, matching dsp::EffectsRack::isImplemented.

    A slot naming anything else resolves to empty in the engine, so it must read
    as empty here too: lighting a slot that the rack is going to ignore would be
    the interface telling the user something the instrument does not agree with.
    With 8e landed this is every effect the rack knows about, and the list is
    kept rather than replaced by a range: the next effect to be added will need
    it again.
*/
const IMPLEMENTED_EFFECTS: readonly number[] = [
    EFFECT_DISTORTION,
    EFFECT_DELAY,
    EFFECT_REVERB,
    EFFECT_GATE,
    EFFECT_COMPRESSOR,
    EFFECT_EQUALISER,
];

export const RACK_PARAMETER_IDS: string[] = Array.from(
    { length: RACK_SLOT_COUNT },
    (_, index) => `fx_slot${index + 1}`,
);

/** What the engine will actually run in each slot, in order, with 0 for empty.

    The rack does not run the chain it is handed, it runs the chain it resolves:
    an effect this build does not have leaves the slot empty, and so does a
    *second* occurrence of an effect already placed earlier, because running one
    effect object twice would feed its own output back into its own state
    (ADR-0054, `EffectsRack::setChain`).

    The page has to resolve it the same way and by the same algorithm rather than
    an equivalent-looking one. A slot lit up that the engine is going to ignore is
    the interface telling the user something the instrument does not agree with —
    which is the reason `IMPLEMENTED_EFFECTS` exists, and applies just as much to
    a duplicate. Found by driving the running rack in Phase 8f, where two slots
    ended up naming the reverb and both claimed to be in the chain.

    Subscribed to as well as read, so the answer changes the moment any slot
    does. The loop calls a hook per slot, which is allowed here because the
    number of slots is a constant: every render subscribes to the same six ids
    in the same order.
*/
function useResolvedChain(): number[] {
    const requested: number[] = [];

    for (const id of RACK_PARAMETER_IDS) {
        useParameterValue(id);
        requested.push(Math.round(plainOf(id)));
    }

    const resolved: number[] = [];

    for (const effect of requested) {
        const runnable = IMPLEMENTED_EFFECTS.includes(effect) && !resolved.includes(effect);
        resolved.push(runnable ? effect : 0);
    }

    return resolved;
}

/** True while @p effect is one the engine will actually run.

    A panel's OFF chip therefore disappears the moment the effect is put into the
    chain and comes back the moment it is taken out — and does *not* disappear
    because a second slot names an effect the rack is going to ignore.
*/
function useInChain(effect: number): boolean {
    return useResolvedChain().includes(effect);
}

function Slot({ index }: { index: number }): JSX.Element {
    const id = `fx_slot${index}`;

    const resolved = useResolvedChain();
    const requested = Math.round(plainOf(id));

    const filled = resolved[index - 1] !== 0;

    // Named something real, and ignored anyway, because an earlier slot already
    // has it. Said in a word rather than by being dim, because "this control is
    // doing nothing" is exactly the kind of thing a user should not have to
    // infer from brightness (CLAUDE.md §39).
    const duplicate = !filled && IMPLEMENTED_EFFECTS.includes(requested);

    return (
        <div className="rack__slot" data-filled={filled ? 'true' : 'false'}>
            <div className="rack__index">
                {index}
                {duplicate ? <span className="rack__note">duplicate</span> : null}
            </div>
            <Select id={id} table={LABELS.fxSlot} ariaLabel={`FX slot ${index}`} />
        </div>
    );
}

export function Rack(): JSX.Element {
    return (
        <Module title="FX Rack" bodyClassName="module__body--rack">
            <div className="rack">
                {Array.from({ length: RACK_SLOT_COUNT }, (_, index) => (
                    <Slot index={index + 1} key={index + 1} />
                ))}
            </div>
        </Module>
    );
}

export function Distortion(): JSX.Element {
    const placed = useInChain(EFFECT_DISTORTION);

    return (
        <Module title="Distortion" inactive={!placed}>
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id="fx_distortion_mode" label="Curve" table={LABELS.fx_distortion_mode} />
                    <Segmented
                        id="fx_distortion_bypass"
                        label="State"
                        table={LABELS.fx_distortion_bypass}
                    />
                </div>
            </div>

            {/*
                Drive first, because it is the control the effect is about, and
                Mix last, because it is the one that decides how much of any of
                it is heard. Tone sits between them for the same reason it sits
                there in the signal path.
            */}
            <Knob id="fx_distortion_drive" label="Drive" />
            <Knob id="fx_distortion_tone" label="Tone" />
            <Knob id="fx_distortion_output" label="Output" />
            <Knob id="fx_distortion_mix" label="Mix" />
        </Module>
    );
}

export function DelayEffect(): JSX.Element {
    const placed = useInChain(EFFECT_DELAY);

    return (
        <Module title="Delay" inactive={!placed}>
            {/*
                Two ways of asking for a time, and the switch says which one is
                being read — the same job the filter's type control does. Both
                stay visible and at full strength either way: a control that
                dims when it is not in use makes a panel look broken, and a
                control that disappears has to be rediscovered (ADR-0052).
            */}
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id="fx_delay_sync" label="Timing" table={LABELS.fx_delay_sync} />
                    <Segmented
                        id="fx_delay_pingpong"
                        label="Stereo"
                        table={LABELS.fx_delay_pingpong}
                    />
                    <Segmented id="fx_delay_bypass" label="State" table={LABELS.fx_delay_bypass} />
                </div>
            </div>

            <div className="cluster cluster--banner">
                <Segmented
                    id="fx_delay_division"
                    label="Division"
                    table={LABELS.fx_delay_division}
                />
            </div>

            <Knob id="fx_delay_time" label="Time" />
            <Knob id="fx_delay_feedback" label="Feedback" />
            <Knob id="fx_delay_damping" label="Damping" />
            <Knob id="fx_delay_lowcut" label="Low Cut" />
            <Knob id="fx_delay_mix" label="Mix" />
        </Module>
    );
}

export function ReverbEffect(): JSX.Element {
    const placed = useInChain(EFFECT_REVERB);

    return (
        <Module title="Reverb" inactive={!placed}>
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id="fx_reverb_mode" label="Space" table={LABELS.fx_reverb_mode} />
                    <Segmented id="fx_reverb_bypass" label="State" table={LABELS.fx_reverb_bypass} />
                </div>
            </div>

            {/*
                Size and Decay sit next to each other because the pair is the
                control: they are independent, and which combination you pick is
                the difference between a tiled bathroom and a treated studio.
            */}
            <Knob id="fx_reverb_size" label="Size" />
            <Knob id="fx_reverb_decay" label="Decay" />
            <Knob id="fx_reverb_predelay" label="Pre-Delay" />
            <Knob id="fx_reverb_damping" label="Damping" />
            <Knob id="fx_reverb_width" label="Width" />
            <Knob id="fx_reverb_mix" label="Mix" />
        </Module>
    );
}

export function GateEffect(): JSX.Element {
    const placed = useInChain(EFFECT_GATE);

    return (
        <Module title="Gate" inactive={!placed}>
            <div className="cluster cluster--banner">
                <Segmented id="fx_gate_bypass" label="State" table={LABELS.fx_gate_bypass} />
            </div>

            {/*
                Hold sits between attack and release because that is where it
                acts: it is the time the gate stays open after the signal has
                already gone, and it is what stops a signal sitting at the
                threshold from strobing the gate open and shut.
            */}
            <Knob id="fx_gate_threshold" label="Threshold" />
            <Knob id="fx_gate_attack" label="Attack" />
            <Knob id="fx_gate_hold" label="Hold" />
            <Knob id="fx_gate_release" label="Release" />
            <Knob id="fx_gate_range" label="Range" />
        </Module>
    );
}

export function CompressorEffect(): JSX.Element {
    const placed = useInChain(EFFECT_COMPRESSOR);

    return (
        <Module title="Compressor" inactive={!placed}>
            <div className="cluster cluster--banner">
                <Segmented
                    id="fx_compressor_bypass"
                    label="State"
                    table={LABELS.fx_compressor_bypass}
                />
            </div>

            <Knob id="fx_compressor_threshold" label="Threshold" />
            <Knob id="fx_compressor_ratio" label="Ratio" />
            <Knob id="fx_compressor_attack" label="Attack" />
            <Knob id="fx_compressor_release" label="Release" />
            <Knob id="fx_compressor_makeup" label="Makeup" />
            <Knob id="fx_compressor_mix" label="Mix" />
        </Module>
    );
}

/*
    The equaliser.

    Seven bands is forty-two controls, which is more than the rest of the rack
    put together and far more than a panel can show at once. So the panel shows
    the *curve* — all seven bands at once, as the shape they make — and one
    band's controls at a time behind a tab strip, which is the arrangement the
    reference uses and the same one the envelopes and LFOs already use here.

    The band on screen and the band selected on the curve are the same band:
    clicking a handle moves the tab strip, and moving the tab strip changes which
    handle is filled. Two ways of selecting that disagreed would be worse than
    either alone.
*/

const BAND_INDICES = Array.from({ length: EQ_BAND_COUNT }, (_, index) => index + 1);

/** Reads one band's six parameters and subscribes to each.

    Hooks in a loop, which is allowed here for the same reason it is in
    `useInChain`: the number of bands is a compile-time constant, so every render
    calls the same hooks in the same order.
*/
function useBand(index: number): BandSettings {
    const prefix = `fx_eq_band${index}_`;

    useParameterValue(`${prefix}type`);
    useParameterValue(`${prefix}freq`);
    useParameterValue(`${prefix}gain`);
    useParameterValue(`${prefix}bandwidth`);
    useParameterValue(`${prefix}order`);
    useParameterValue(`${prefix}mute`);

    return {
        type: Math.round(plainOf(`${prefix}type`)),
        frequencyHz: plainOf(`${prefix}freq`),
        gainDb: plainOf(`${prefix}gain`),
        bandwidthOctaves: plainOf(`${prefix}bandwidth`),
        order: Math.round(plainOf(`${prefix}order`)),
        muted: plainOf(`${prefix}mute`) >= 0.5,
    };
}

/** Writes a plain value to a parameter, converting through its own metadata.

    Nothing here knows a range: the definition the engine sent is what turns
    hertz or decibels into the normalised value the bridge carries (UI_BINDINGS
    §16). A parameter the engine has not described is left alone rather than
    guessed at.
*/
function commitPlain(id: string, plain: number): void {
    const definition = definitionOf(id);
    if (!definition) return;

    commitValue(id, toNormalized(definition, plain));
}

export function EqualiserEffect(): JSX.Element {
    const placed = useInChain(EFFECT_EQUALISER);
    const [selected, setSelected] = useState(0);

    // One entry per band, in order. The hook count is fixed by the constant, so
    // this loop is as stable as writing the seven calls out.
    const bands = BAND_INDICES.map((index) => useBand(index));

    useParameterValue('fx_eq_level');
    const levelDb = plainOf('fx_eq_level');

    // Before the first instrument frame there is no rate to draw at. Falling
    // back keeps the curve on screen from the moment the page loads; the real
    // rate arrives within a frame or two and redraws it.
    const reported = useSampleRate();
    const sampleRate = reported > 0 ? reported : EQ_FALLBACK_SAMPLE_RATE;

    const onDrag = (index: number, frequencyHz: number, gainDb: number | undefined): void => {
        const prefix = `fx_eq_band${index + 1}_`;

        commitPlain(`${prefix}freq`, frequencyHz);

        // Undefined for the four shapes with no gain, so dragging a low pass up
        // the screen moves its frequency and nothing else — rather than writing
        // a gain the engine is going to ignore and the curve is not going to
        // show, which would look like a broken control.
        if (gainDb !== undefined) commitPlain(`${prefix}gain`, gainDb);
    };

    // The drag is bracketed as one automation gesture per parameter, the same as
    // a knob's, so a host records one move rather than a few hundred writes.
    const onGrab = (index: number): void => {
        setSelected(index);

        const prefix = `fx_eq_band${index + 1}_`;
        gesture(`${prefix}freq`, 'begin');

        if (eqTypeHasGain(bands[index]?.type ?? 0)) gesture(`${prefix}gain`, 'begin');
    };

    const onDragEnd = (index: number): void => {
        const prefix = `fx_eq_band${index + 1}_`;
        gesture(`${prefix}freq`, 'end');

        if (eqTypeHasGain(bands[index]?.type ?? 0)) gesture(`${prefix}gain`, 'end');
    };

    return (
        <Module
            title="Equaliser"
            index={String(selected + 1)}
            inactive={!placed}
            head={(
                <Tabs
                    count={EQ_BAND_COUNT}
                    prefix=""
                    selected={selected}
                    onSelect={setSelected}
                />
            )}
        >
            <div className="cluster cluster--banner">
                <EqCurve
                    bands={bands}
                    levelDb={levelDb}
                    sampleRate={sampleRate}
                    selected={selected}
                    onGrab={onGrab}
                    onDrag={onDrag}
                    onDragEnd={onDragEnd}
                />
            </div>

            {BAND_INDICES.map((index) => {
                const prefix = `fx_eq_band${index}_`;

                return (
                    <div key={index} className="cluster" hidden={index !== selected + 1}>
                        <div className="cluster cluster--banner">
                            <div className="cluster">
                                <Segmented
                                    id={`${prefix}type`}
                                    label="Shape"
                                    table={LABELS.fx_eq_band_type}
                                />
                                <Segmented
                                    id={`${prefix}order`}
                                    label="Slope"
                                    table={LABELS.fx_eq_band_order}
                                />
                                <Segmented
                                    id={`${prefix}mute`}
                                    label="In Circuit"
                                    table={LABELS.fx_eq_band_mute}
                                />
                            </div>
                        </div>

                        <Knob id={`${prefix}freq`} label="Frequency" />
                        <Knob id={`${prefix}gain`} label="Gain" />
                        <Knob id={`${prefix}bandwidth`} label="Width" />
                    </div>
                );
            })}

            {/*
                The two controls that belong to the whole equaliser rather than
                to a band, kept outside the tab pages so they do not appear to
                be band 1's.
            */}
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id="fx_eq_bypass" label="State" table={LABELS.fx_eq_bypass} />
                    <Knob id="fx_eq_level" label="Level" />
                </div>
            </div>
        </Module>
    );
}
