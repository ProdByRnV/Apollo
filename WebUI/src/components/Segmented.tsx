/*
    A radio group for short enumerations — filter type, LFO shape, sub octave.

    Rendered as real radios rather than as a styled listbox, because that is what
    it is: several mutually exclusive options, all of them visible, all of them
    reachable with a keyboard (CLAUDE.md §39).

    ONE TAB STOP PER GROUP, and the arrows choose within it — the radio pattern
    every platform uses. The checked option is the stop; the arrows move to the
    next option and select it, which is what an arrow does on a radio group.
    Every option used to be its own stop, which made the keyboard path through
    the page 253 presses long (roving.ts).
*/

import { labelsFor } from '../params/labels';
import { stepCount } from '../params/mapping';
import { commitValue, useParameterDefinition, useParameterValue } from '../state/parameters';
import { gesture } from '../bridge/bridge';
import { useMidiChrome } from './midiChrome';
import { hasCommandModifier, rovingTarget, tabStopIndex } from './roving';

export interface SegmentedProps {
    id: string;
    label: string;
    table?: readonly string[] | undefined;
}

export function Segmented({ id, label, table }: SegmentedProps): JSX.Element | null {
    const definition = useParameterDefinition(id);
    const normalized = useParameterValue(id);
    const chrome = useMidiChrome(id, definition?.name ?? id);

    if (!definition) return null;

    const steps = stepCount(definition);
    if (steps < 2) return null;

    const names = labelsFor(definition, table);
    const selected = Math.round(normalized * (steps - 1));

    const choose = (index: number): void => {
        gesture(id, 'begin');
        commitValue(id, index / (steps - 1));
        gesture(id, 'end');
    };

    const stop = tabStopIndex(selected, steps);

    const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>): void => {
        if (hasCommandModifier(event)) return;

        const options = Array.from(
            event.currentTarget.querySelectorAll<HTMLButtonElement>('[role="radio"]'),
        );

        // Measured from the option that has focus rather than from the checked
        // one. They are normally the same; when they are not, the arrow should
        // move from where the user can see they are.
        const focused = options.findIndex((option) => option === document.activeElement);
        const target = rovingTarget(event.key, focused >= 0 ? focused : selected, steps);

        if (target === null) return;

        event.preventDefault();

        if (target !== selected) choose(target);

        options[target]?.focus();
    };

    return (
        <div
            className="segmented"
            title={chrome.title}
            data-midi-id={id}
            data-midi={chrome.state}
        >
            {/* An empty label renders nothing rather than an empty line: the
                rack's power switches sit under the slot's own number and do not
                need a second caption. */}
            {label ? <div className="segmented__label">{label}</div> : null}

            <div
                className="segmented__options"
                role="radiogroup"
                aria-label={definition.name}
                onKeyDown={onKeyDown}
            >
                {Array.from({ length: steps }, (_, index) => (
                    <button
                        key={index}
                        type="button"
                        className="segmented__option"
                        role="radio"
                        aria-checked={index === selected}
                        tabIndex={index === stop ? 0 : -1}
                        onClick={() => choose(index)}
                    >
                        {names?.[index] ?? String(definition.min + index * definition.step)}
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
