/*
    The modulation matrix: sixteen slots of source, destination and depth.

    Two banks of eight rather than one column of sixteen: a routing should read
    as a single line from source to depth, and across a wide window one table
    puts half a screen between the two ends of the same statement.

    Above them, every source as a handle (Phase 13h-4): drag one onto a control
    and the engine fills the first free slot here.
*/

import { useEffect } from 'react';

import { BipolarRail } from '../components/BipolarRail';
import { ModSourceStrip } from '../components/ModSource';
import { Module } from '../components/Module';
import { Select } from '../components/Select';
import { LABELS } from '../params/labels';
import { liveSlotCount, routingsByControl, SILENT_DEPTH } from '../params/modulationRouting';
import type { SlotState } from '../params/modulationRouting';
import { controlForDestination, plainOf, useMetadataVersion, useParameterValue } from '../state/parameters';
import { publishRouting } from '../state/modulation';

export const SLOT_COUNT = 16;

function slotId(slot: number, part: 'source' | 'destination' | 'depth' | 'via'): string {
    return `mod${String(slot).padStart(2, '0')}_${part}`;
}

/** Every parameter the matrix places, so the safety net below knows about them
    without having to scrape the DOM for ids the layout may not have rendered
    yet.
*/
export const MATRIX_PARAMETER_IDS: string[] = Array.from(
    { length: SLOT_COUNT },
    (_, index) => index + 1,
).flatMap((slot) => [
    slotId(slot, 'source'), slotId(slot, 'destination'), slotId(slot, 'depth'), slotId(slot, 'via'),
]);

interface RowProps {
    slot: number;
}

function Row({ slot }: RowProps): JSX.Element {
    const sourceId = slotId(slot, 'source');
    const destinationId = slotId(slot, 'destination');
    const depthId = slotId(slot, 'depth');
    const viaId = slotId(slot, 'via');

    // Subscribed to individually, so a row re-renders when its own slot moves.
    useParameterValue(sourceId);
    useParameterValue(destinationId);
    useParameterValue(depthId);
    useParameterValue(viaId);

    // A routing counts as assigned when it has a source, a destination and a
    // depth that is not zero — all three, because any one of them missing means
    // the slot moves nothing, and showing it as live would be a lie.
    const live = Math.round(plainOf(sourceId)) > 0
        && Math.round(plainOf(destinationId)) > 0
        && Math.abs(plainOf(depthId)) > SILENT_DEPTH;

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

            {/*
                A second source that scales the routing: its depth, modulated
                (13e-4). "—" leaves the slot as it was.
            */}
            <td className="matrix__via">
                <Select id={viaId} table={LABELS.modSource} ariaLabel={`Slot ${slot} via`} />
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
                    <th className="matrix__via">Via</th>
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

    // And the metadata, which says which control each destination is.
    useMetadataVersion();

    useEffect(() => {
        const slots: SlotState[] = Array.from({ length: SLOT_COUNT }, (_, index) => ({
            source: plainOf(slotId(index + 1, 'source')),
            destination: plainOf(slotId(index + 1, 'destination')),
            depth: plainOf(slotId(index + 1, 'depth')),
        }));

        publishRouting(routingsByControl(slots, controlForDestination), liveSlotCount(slots));
    });

    return (
        <Module title="Modulation Matrix" bodyClassName="module__body--block">
            <ModSourceStrip />

            <div className="matrix-banks">
                <Bank bank={0} />
                <Bank bank={1} />
            </div>
        </Module>
    );
}
