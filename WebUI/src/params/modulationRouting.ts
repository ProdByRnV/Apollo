/*
    The arithmetic of drag-and-drop modulation (Phase 13h-4, ADR-0108), kept
    free of React and of the bridge so the interface tests can import it.

    Which routings land on which control, what a routing's tag says, and how
    far a tag dragged by so many pixels moves its depth. Which control a
    destination is comes from the engine, in the parameter metadata; nothing
    here knows it.
*/

/** Two characters for each source, in dsp::ModSource order: what a routing's
    tag on a control says. Short because the tag sits beside a 46-pixel dial;
    the full name is in its tooltip and in the status line. */
export const SOURCE_TAGS: readonly string[] = [
    '', 'E1', 'E2', 'E3', 'E4', 'L1', 'L2', 'L3', 'L4',
    'VL', 'KT', 'MW', 'PB', 'AT', 'RN', 'TB', 'M1', 'M2', 'M3', 'M4',
];

/** The number of sources, none included, as dsp::ModSource::count. */
export const SOURCE_COUNT = SOURCE_TAGS.length;

/** A depth this close to zero moves nothing, and is drawn as moving nothing.
    The matrix uses the same bound for "assigned". */
export const SILENT_DEPTH = 0.0005;

/** Full travel of a tag, in pixels: from -100% to +100%, as far as a knob
    goes from one end to the other. */
export const TAG_DRAG_RANGE = 190;

/** One matrix slot, in plain values. */
export interface SlotState {
    source: number;
    destination: number;
    depth: number;
}

/** A routing as the control it moves draws it. */
export interface ControlRouting {
    /** From 1, as the matrix numbers its rows. */
    slot: number;
    source: number;
    depth: number;
    /** True when it moves something: a depth that is not zero. */
    live: boolean;
}

/** Groups the matrix by the control each slot moves. A slot with no source,
    no destination, or a destination with no control of its own — All Pitch,
    Amplitude — lands on nothing. A slot with a depth of zero is kept, so the
    tag is there to be dragged up from nothing. */
export function routingsByControl(
    slots: readonly SlotState[],
    controlFor: (destination: number) => string | undefined,
): Map<string, ControlRouting[]> {
    const result = new Map<string, ControlRouting[]>();

    slots.forEach((slot, index) => {
        const source = Math.round(slot.source);
        const destination = Math.round(slot.destination);

        if (source <= 0 || destination <= 0) return;

        const control = controlFor(destination);
        if (control === undefined) return;

        const routing: ControlRouting = {
            slot: index + 1,
            source,
            depth: slot.depth,
            live: Math.abs(slot.depth) > SILENT_DEPTH,
        };

        const list = result.get(control);
        if (list) list.push(routing);
        else result.set(control, [routing]);
    });

    return result;
}

/** How many slots move something, wherever they go. */
export function liveSlotCount(slots: readonly SlotState[]): number {
    return slots.filter((slot) => Math.round(slot.source) > 0
        && Math.round(slot.destination) > 0
        && Math.abs(slot.depth) > SILENT_DEPTH).length;
}

export function sameRoutings(a: readonly ControlRouting[], b: readonly ControlRouting[]): boolean {
    if (a.length !== b.length) return false;

    return a.every((routing, index) => {
        const other = b[index];
        return other !== undefined
            && routing.slot === other.slot
            && routing.source === other.source
            && routing.depth === other.depth;
    });
}

/** A depth as the matrix reads it: "+25%", "-60%", "0%". */
export function depthText(depth: number): string {
    const percent = Math.round(depth * 100);
    if (percent === 0) return '0%';
    return `${percent > 0 ? '+' : ''}${percent}%`;
}

/** Where a tag dragged @p rise pixels upwards from @p start puts the depth.
    A fifth as far with @p fine, as a knob is with Shift. */
export function draggedDepth(start: number, rise: number, fine: boolean): number {
    const moved = start + (rise / TAG_DRAG_RANGE) * 2 * (fine ? 0.2 : 1);
    return Math.min(1, Math.max(-1, moved));
}

/** What a routing's tag says when the pointer rests on it. */
export function tagTitle(sourceName: string, controlName: string, routing: ControlRouting): string {
    const how = routing.live ? depthText(routing.depth) : 'no depth yet';
    return `${sourceName} → ${controlName}, ${how}, slot ${routing.slot}. `
        + 'Drag up or down to change how far; right-click to invert or remove.';
}

/** The tags a control shows: all of them up to four, or three and a count
    of the rest. */
export function visibleTags(routings: readonly ControlRouting[]): {
    shown: readonly ControlRouting[];
    hidden: number;
} {
    if (routings.length <= 4) return { shown: routings, hidden: 0 };
    return { shown: routings.slice(0, 3), hidden: routings.length - 3 };
}
