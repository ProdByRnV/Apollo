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

import { Knob } from '../components/Knob';
import { Module } from '../components/Module';
import { Segmented } from '../components/Segmented';
import { Select } from '../components/Select';
import { LABELS } from '../params/labels';
import { plainOf, useParameterValue } from '../state/parameters';

export const RACK_SLOT_COUNT = 6;

/** Matches dsp::EffectType. */
export const EFFECT_DISTORTION = 1;

/** The effects that exist in this build, matching dsp::EffectsRack::isImplemented.

    A slot naming anything else resolves to empty in the engine, so it must read
    as empty here too: lighting a slot that the rack is going to ignore would be
    the interface telling the user something the instrument does not agree with.
    Each of 8b to 8e adds its effect to this list as it lands.
*/
const IMPLEMENTED_EFFECTS: readonly number[] = [EFFECT_DISTORTION];

export const RACK_PARAMETER_IDS: string[] = Array.from(
    { length: RACK_SLOT_COUNT },
    (_, index) => `fx_slot${index + 1}`,
);

/** True while @p effect occupies one of the six slots.

    Subscribed to as well as read, so a panel's OFF chip appears the moment the
    effect is put into the chain and disappears the moment it is taken out. The
    loop calls a hook per slot, which is allowed here and only here because the
    number of slots is a constant: every render subscribes to the same six ids
    in the same order.
*/
function useInChain(effect: number): boolean {
    let placed = false;

    for (const id of RACK_PARAMETER_IDS) {
        useParameterValue(id);
        placed = placed || Math.round(plainOf(id)) === effect;
    }

    return placed;
}

function Slot({ index }: { index: number }): JSX.Element {
    const id = `fx_slot${index}`;

    useParameterValue(id);
    const filled = IMPLEMENTED_EFFECTS.includes(Math.round(plainOf(id)));

    return (
        <div className="rack__slot" data-filled={filled ? 'true' : 'false'}>
            <div className="rack__index">{index}</div>
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
