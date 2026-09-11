/*
    The modulation matrix: sixteen slots of source, destination and depth.

    Two banks of eight rather than one column of sixteen: a routing should read
    as a single line from source to depth, and across a wide window one table
    puts half a screen between the two ends of the same statement.
*/

import { useEffect } from 'react';

import { BipolarRail } from '../components/BipolarRail';
import { Module } from '../components/Module';
import { Select } from '../components/Select';
import { DESTINATION_PARAMETER, LABELS } from '../params/labels';
import { plainOf, useParameterValue } from '../state/parameters';
import { publishRouting } from '../state/modulation';

export const SLOT_COUNT = 16;

function slotId(slot: number, part: 'source' | 'destination' | 'depth'): string {
    return `mod${String(slot).padStart(2, '0')}_${part}`;
}

/** Every parameter the matrix places, so the safety net below knows about them
    without having to scrape the DOM for ids the layout may not have rendered
    yet.
*/
export const MATRIX_PARAMETER_IDS: string[] = Array.from(
    { length: SLOT_COUNT },
    (_, index) => index + 1,
).flatMap((slot) => [slotId(slot, 'source'), slotId(slot, 'destination'), slotId(slot, 'depth')]);

interface RowProps {
    slot: number;
}

function Row({ slot }: RowProps): JSX.Element {
    const sourceId = slotId(slot, 'source');
    const destinationId = slotId(slot, 'destination');
    const depthId = slotId(slot, 'depth');

    // Subscribed to individually, so a row re-renders when its own slot moves.
    useParameterValue(sourceId);
    useParameterValue(destinationId);
    useParameterValue(depthId);

    // A routing counts as assigned when it has a source, a destination and a
    // depth that is not zero — all three, because any one of them missing means
    // the slot moves nothing, and showing it as live would be a lie.
    const live = Math.round(plainOf(sourceId)) > 0
        && Math.round(plainOf(destinationId)) > 0
        && Math.abs(plainOf(depthId)) > 0.0005;

    return (
        <tr data-assigned={live ? 'true' : 'false'}>
            <td className="matrix__slot">{String(slot).padStart(2, '0')}</td>

            <td className="matrix__source">
                <Select id={sourceId} table={LABELS.modSource} ariaLabel={`Slot ${slot} source`} />
            </td>

            <td className="matrix__arrow">→</td>

            <td className="matrix__destination">
                <Select
                    id={destinationId}
                    table={LABELS.modDestination}
                    ariaLabel={`Slot ${slot} destination`}
                />
            </td>

            <td className="matrix__depth">
                <BipolarRail id={depthId} />
            </td>
        </tr>
    );
}

function Bank({ bank }: { bank: number }): JSX.Element {
    return (
        <table className="matrix">
            <thead>
                <tr>
                    <th className="matrix__slot" />
                    <th className="matrix__source">Source</th>
                    <th className="matrix__arrow" />
                    <th className="matrix__destination">Destination</th>
                    <th className="matrix__depth">Depth</th>
                </tr>
            </thead>

            <tbody>
                {Array.from({ length: 8 }, (_, offset) => (
                    <Row key={offset} slot={bank * 8 + offset + 1} />
                ))}
            </tbody>
        </table>
    );
}

export function Matrix(): JSX.Element {
    // Read here as well as in each row, so the module re-renders whenever any
    // slot moves and can republish which knobs are lit. The rows are what
    // actually redraw; this is the one subscription that has to see all of them.
    for (const id of MATRIX_PARAMETER_IDS) useParameterValue(id);

    useEffect(() => {
        const lit = new Set<string>();
        let assigned = 0;

        for (let slot = 1; slot <= SLOT_COUNT; ++slot) {
            const source = Math.round(plainOf(slotId(slot, 'source')));
            const destination = Math.round(plainOf(slotId(slot, 'destination')));
            const depth = plainOf(slotId(slot, 'depth'));

            if (!(source > 0 && destination > 0 && Math.abs(depth) > 0.0005)) continue;

            ++assigned;

            const target = DESTINATION_PARAMETER[destination];
            if (target) lit.add(target);
        }

        publishRouting(lit, assigned);
    });

    return (
        <Module title="Modulation Matrix" bodyClassName="module__body--block">
            <div className="matrix-banks">
                <Bank bank={0} />
                <Bank bank={1} />
            </div>
        </Module>
    );
}
