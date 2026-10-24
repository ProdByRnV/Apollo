/*
    A tag on a control for every routing that moves it (Phase 13h-4, ADR-0108).

    "L1" for LFO 1, green while it moves something, with a bar under it that
    leans right for a positive depth and left for a negative one. Dragged up or
    down it changes that routing's depth — the matrix slot's own depth
    parameter, with a gesture around it like any other edit — and the
    control's readout shows the depth while it does. Right-click inverts or
    removes it.

    Depth, not the range the control will sweep. A depth of a quarter is two
    octaves of cutoff and six semitones of pitch, and a ring drawn on the dial
    would have to know every destination's units to be true; one that was not
    would be a picture of something the engine is not doing. The depth is the
    number the matrix shows, and the tag says it.

    The tags are not Tab stops: the control is. Its own menu removes a routing,
    and the matrix row sets a depth from the keyboard.
*/

import { useRef, useState } from 'react';

import { modulationClear } from '../bridge/bridge';
import { toNormalized } from '../params/mapping';
import {
    depthText,
    draggedDepth,
    SOURCE_TAGS,
    tagTitle,
    visibleTags,
} from '../params/modulationRouting';
import type { ControlRouting } from '../params/modulationRouting';
import { commitValue, definitionOf, setHeld } from '../state/parameters';
import { useRoutingsOf } from '../state/modulation';
import { gesture } from '../bridge/bridge';
import { ContextMenu } from './ContextMenu';
import { sourceName } from './ModSource';

export interface Readout {
    label: string;
    value: string;
}

function depthIdOf(slot: number): string {
    return `mod${String(slot).padStart(2, '0')}_depth`;
}

function setDepth(slot: number, depth: number): void {
    const id = depthIdOf(slot);
    const definition = definitionOf(id);
    if (definition) commitValue(id, toNormalized(definition, depth));
}

interface TagsProps {
    id: string;
    controlName: string;
    onReadout: (readout: Readout | null) => void;
}

export function RoutingTags({ id, controlName, onReadout }: TagsProps): JSX.Element | null {
    const routings = useRoutingsOf(id);
    const drag = useRef<{ slot: number; source: number; start: number; from: number } | null>(null);
    const [menu, setMenu] = useState<{ x: number; y: number; routing: ControlRouting } | null>(null);

    if (routings.length === 0) return null;

    const { shown, hidden } = visibleTags(routings);

    const onPointerDown = (event: React.PointerEvent<HTMLSpanElement>, routing: ControlRouting): void => {
        // The tag sits on the knob, and a press here is not a press on it.
        event.stopPropagation();
        if (event.button !== 0) return;

        event.preventDefault();
        event.currentTarget.setPointerCapture(event.pointerId);

        drag.current = { slot: routing.slot, source: routing.source, start: routing.depth, from: event.clientY };
        setHeld(depthIdOf(routing.slot), true);
        gesture(depthIdOf(routing.slot), 'begin');
        onReadout({ label: `${sourceName(routing.source)} depth`, value: depthText(routing.depth) });
    };

    const onPointerMove = (event: React.PointerEvent<HTMLSpanElement>): void => {
        const state = drag.current;
        if (state === null) return;

        const depth = draggedDepth(state.start, state.from - event.clientY, event.shiftKey);
        setDepth(state.slot, depth);
        onReadout({ label: `${sourceName(state.source)} depth`, value: depthText(depth) });
    };

    const onPointerUp = (event: React.PointerEvent<HTMLSpanElement>): void => {
        const state = drag.current;
        if (state === null) return;

        drag.current = null;
        setHeld(depthIdOf(state.slot), false);
        gesture(depthIdOf(state.slot), 'end');
        onReadout(null);

        try {
            event.currentTarget.releasePointerCapture(event.pointerId);
        } catch {
            // Already released.
        }
    };

    const nudge = (slot: number, depth: number): void => {
        gesture(depthIdOf(slot), 'begin');
        setDepth(slot, depth);
        gesture(depthIdOf(slot), 'end');
    };

    return (
        <>
            <div className="mod-tags" aria-hidden="true">
                {shown.map((routing) => (
                    <span
                        key={routing.slot}
                        className="mod-tag"
                        data-live={routing.live ? 'true' : 'false'}
                        title={tagTitle(sourceName(routing.source), controlName, routing)}
                        style={{
                            '--depth-left': `${50 + Math.min(0, routing.depth) * 50}%`,
                            '--depth-width': `${Math.abs(routing.depth) * 50}%`,
                        } as React.CSSProperties}
                        onPointerDown={(event) => onPointerDown(event, routing)}
                        onPointerMove={onPointerMove}
                        onPointerUp={onPointerUp}
                        onPointerCancel={onPointerUp}
                        onDoubleClick={(event) => event.stopPropagation()}
                        onContextMenu={(event) => {
                            event.preventDefault();
                            event.stopPropagation();
                            setMenu({ x: event.clientX, y: event.clientY, routing });
                        }}
                    >
                        {SOURCE_TAGS[routing.source] ?? '?'}
                    </span>
                ))}

                {hidden > 0 ? (
                    <span
                        className="mod-tag mod-tag--more"
                        title={`${hidden} more routing${hidden === 1 ? '' : 's'} in the matrix: `
                            + routings.slice(shown.length)
                                .map((routing) => `${sourceName(routing.source)} ${depthText(routing.depth)}`)
                                .join(', ')}
                        onPointerDown={(event) => event.stopPropagation()}
                        onDoubleClick={(event) => event.stopPropagation()}
                    >
                        +{hidden}
                    </span>
                ) : null}
            </div>

            {menu !== null ? (
                <ContextMenu
                    x={menu.x}
                    y={menu.y}
                    items={[
                        {
                            label: `Invert to ${depthText(-menu.routing.depth)}`,
                            onChoose: () => nudge(menu.routing.slot, -menu.routing.depth),
                            disabled: !menu.routing.live,
                        },
                        {
                            label: `Remove ${sourceName(menu.routing.source)} (slot ${menu.routing.slot})`,
                            onChoose: () => modulationClear(menu.routing.slot),
                        },
                    ]}
                    onClose={() => setMenu(null)}
                />
            ) : null}
        </>
    );
}
