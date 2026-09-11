/*
    A radio group for short enumerations — filter type, LFO shape, sub octave.

    Rendered as real radios rather than as a styled listbox, because that is what
    it is: several mutually exclusive options, all of them visible, all of them
    reachable with a keyboard (CLAUDE.md §39).
*/

import { labelsFor } from '../params/labels';
import { stepCount } from '../params/mapping';
import { commitValue, useParameterDefinition, useParameterValue } from '../state/parameters';
import { gesture } from '../bridge/bridge';
import { useMidiChrome } from './midiChrome';

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

    return (
        <div
            className="segmented"
            title={chrome.title}
            data-midi-id={id}
            data-midi={chrome.state}
        >
            <div className="segmented__label">{label}</div>

            <div className="segmented__options" role="radiogroup" aria-label={definition.name}>
                {Array.from({ length: steps }, (_, index) => (
                    <button
                        key={index}
                        type="button"
                        className="segmented__option"
                        role="radio"
                        aria-checked={index === selected}
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
