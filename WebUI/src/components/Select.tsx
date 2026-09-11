/*
    A dropdown, for the matrix's fifteen sources and seventeen destinations,
    where a segmented control would be a wall.

    A native `<select>` rather than a styled listbox: it is keyboard- and
    screen-reader-correct without any work, and on a touch screen the platform
    gives it a picker no hand-built menu would match.

    It carries no MIDI badge — an element cannot contain one — and does not need
    it: assigning a matrix source to a hardware knob is not a thing anyone does,
    and the outline and tooltip still say so if they have.
*/

import { labelsFor } from '../params/labels';
import { stepCount } from '../params/mapping';
import { commitValue, useParameterDefinition, useParameterValue } from '../state/parameters';
import { gesture } from '../bridge/bridge';

export interface SelectProps {
    id: string;
    table?: readonly string[] | undefined;
    ariaLabel?: string | undefined;
}

export function Select({ id, table, ariaLabel }: SelectProps): JSX.Element | null {
    const definition = useParameterDefinition(id);
    const normalized = useParameterValue(id);

    if (!definition) return null;

    const steps = stepCount(definition);
    if (steps < 2) return null;

    const names = labelsFor(definition, table);
    const selected = Math.round(normalized * (steps - 1));

    return (
        <select
            className="select"
            aria-label={ariaLabel ?? definition.name}
            data-midi-id={id}
            value={String(selected)}
            onChange={(event) => {
                gesture(id, 'begin');
                commitValue(id, Number(event.target.value) / (steps - 1));
                gesture(id, 'end');
            }}
        >
            {Array.from({ length: steps }, (_, index) => (
                <option key={index} value={String(index)}>
                    {names?.[index] ?? String(index)}
                </option>
            ))}
        </select>
    );
}
