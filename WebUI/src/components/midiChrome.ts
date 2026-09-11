/*
    What a control shows about its MIDI assignment.

    In the hand-built page this was `refreshMidiIndicators`, a function that
    walked every registered control and wrote data attributes, a title and a
    badge into each one. It was the clearest example of the thing the migration
    is for: the state and the drawing of it were a hundred and fifty lines apart,
    and adding a control type meant remembering to register it or its badge
    silently never appeared.

    Here each control asks what its own assignment is and renders it. There is
    nothing to register and nothing to keep in step.

    Every control subscribes to the whole MIDI store rather than to its own id,
    so a mapping change re-renders all of them. That is deliberate: mappings
    change a few times a minute at most, the components are small, and one
    listener set is a great deal less machinery than one per parameter for a
    frequency that does not need it. Parameter *values* are the opposite case,
    and are handled the opposite way.
*/

import { useMidi } from '../state/midi';

export interface MidiChrome {
    /** Value for `data-midi`, or undefined when the control is unassigned. */
    state: 'learning' | 'mapped' | undefined;
    /** Text for the badge, or "" when there is nothing to show. */
    badge: string;
    /** The full tooltip, name included. */
    title: string;
    /** True while clicking assigns rather than moves. */
    assignMode: boolean;
}

export function useMidiChrome(id: string, name: string): MidiChrome {
    const midi = useMidi();

    const mapping = midi.mappings.get(id);
    const learning = midi.learning === id;

    const state = learning ? 'learning' : (mapping ? 'mapped' : undefined);

    const badge = learning ? 'LEARN' : (mapping ? `CC ${mapping.controller}` : '');

    // The tooltip carries what the badge cannot: which channel, and what to do
    // about it (CLAUDE.md §39).
    const detail = learning
        ? 'Waiting for a MIDI control — move one, or press Escape'
        : (mapping
            ? `MIDI CC ${mapping.controller}`
                + (mapping.channel > 0 ? ` on channel ${mapping.channel}` : ' (any channel)')
                + ' — press Delete in MIDI Learn mode to release it'
            : '');

    return {
        state,
        badge,
        title: detail ? `${name} · ${detail}` : name,
        assignMode: midi.mode,
    };
}
