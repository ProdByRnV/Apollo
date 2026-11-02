/*
    The sub oscillator's octave: −2, −1 or 0, one radio group over two
    parameters (Phase 16b, ADR-0114).

    `sub_octave` is −2 or −1, as released, and `sub_octave_up` raises it by one.
    See params/subOctave.ts for why there are two. Everything else is the
    Segmented control's: real radios, one tab stop, the arrows choose, MIDI
    Learn on the parameter 1.0 shipped.
*/

import { gesture } from '../bridge/bridge';
import { LABELS } from '../params/labels';
import { SUB_OCTAVES, shownSubOctave, subOctaveTargets } from '../params/subOctave';
import type { SubOctave } from '../params/subOctave';
import { commitValue, useParameterDefinition, useParameterValue } from '../state/parameters';
import { useMidiChrome } from './midiChrome';
import { hasCommandModifier, rovingTarget, tabStopIndex } from './roving';
import { Segmented } from './Segmented';

export function SubOctaveSelector(): JSX.Element | null {
    const octaveDefinition = useParameterDefinition('sub_octave');
    const upDefinition = useParameterDefinition('sub_octave_up');
    const octaveValue = useParameterValue('sub_octave');
    const upValue = useParameterValue('sub_octave_up');
    const chrome = useMidiChrome('sub_octave', octaveDefinition?.name ?? 'sub_octave');

    if (!octaveDefinition) return null;

    // An engine that does not report the switch offers −2 and −1, as it did.
    if (!upDefinition) return <Segmented id="sub_octave" label="Octave" table={LABELS.sub_octave} />;

    const shown = shownSubOctave(octaveValue, upValue);
    const selected = SUB_OCTAVES.indexOf(shown);

    // Each parameter that has to move is its own gesture, so a host records
    // what changed; one that is already right is left alone.
    const set = (id: string, current: number, target: number): void => {
        if (Math.round(current) === target) return;

        gesture(id, 'begin');
        commitValue(id, target);
        gesture(id, 'end');
    };

    const choose = (octave: SubOctave): void => {
        const targets = subOctaveTargets(octave);

        set('sub_octave', octaveValue, targets.octave);
        set('sub_octave_up', upValue, targets.up);
    };

    const stop = tabStopIndex(selected, SUB_OCTAVES.length);

    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(event)) return;

        const options = Array.from(
            event.currentTarget.querySelectorAll<HTMLButtonElement>('[role="radio"]'),
        );

        const focused = options.findIndex((option) => option === document.activeElement);
        const target = rovingTarget(event.key, focused >= 0 ? focused : selected, SUB_OCTAVES.length);

        if (target === null) return;

        event.preventDefault();

        const octave = SUB_OCTAVES[target];
        if (target !== selected && octave !== undefined) choose(octave);

        options[target]?.focus();
    };

    return (
        <div
            className="segmented"
            title={chrome.title}
            data-midi-id="sub_octave"
            data-also-places="sub_octave_up"
            data-midi={chrome.state}
        >
            <div className="segmented__label">Octave</div>

            <div className="segmented__options" role="radiogroup" aria-label="Sub Octave" onKeyDown={onKeyDown}>
                {SUB_OCTAVES.map((octave, index) => (
                    <button
                        key={octave}
                        type="button"
                        className="segmented__option"
                        role="radio"
                        aria-checked={index === selected}
                        tabIndex={index === stop ? 0 : -1}
                        onClick={() => choose(octave)}
                    >
                        {String(octave)}
                    </button>
                ))}
            </div>

            <span
                className="midi-badge"
                aria-hidden="true"
                hidden={chrome.state === undefined}
                data-state={chrome.state}
            >
                {chrome.badge}
            </span>
        </div>
    );
}
