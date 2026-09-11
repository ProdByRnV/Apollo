/*
    Anything the layout did not place.

    A safety net rather than a section: if a parameter is added to the registry
    and nobody gives it a home, it appears at the bottom instead of becoming
    invisible and un-editable.

    THE SET IS SCRAPED FROM THE DOM RATHER THAN DECLARED, which looks like the
    wrong way round until you consider what the alternative would guarantee. A
    hand-written list of "ids the layout places" is a second copy of the layout,
    and the failure it allows is exactly the one this component exists to catch:
    someone adds a control, forgets the list, and the parameter is reported
    missing when it is right there on screen — or worse, removes one and it is
    silently never reported at all. Reading back what was actually rendered
    cannot drift, because it *is* the layout.

    The scan deliberately ignores this module's own subtree. Without that the
    knobs rendered here would count as placed on the next pass, the set would
    empty, and the module would flicker in and out for ever.
*/

import { useEffect, useState } from 'react';
import type { RefObject } from 'react';

import { Knob } from '../components/Knob';
import { Module } from '../components/Module';
import { definitionOf, useMetadataVersion } from '../state/parameters';
import { allParameterIds } from '../state/parameters';

export interface UnplacedProps {
    workspace: RefObject<HTMLElement>;
}

export function Unplaced({ workspace }: UnplacedProps): JSX.Element | null {
    const version = useMetadataVersion();
    const [missing, setMissing] = useState<string[]>([]);

    useEffect(() => {
        const root = workspace.current;
        if (root === null) return;

        const placed = new Set<string>();

        for (const element of root.querySelectorAll<HTMLElement>('[data-midi-id]')) {
            if (element.closest('[data-unplaced="true"]') !== null) continue;

            const id = element.dataset.midiId;
            if (id) placed.add(id);
        }

        const next = allParameterIds().filter((id) => !placed.has(id));

        setMissing((current) =>
            current.length === next.length && current.every((id, index) => id === next[index])
                ? current
                : next);
    });

    // `version` is read so this runs again when the registry is replaced; the
    // effect above has no dependency list because it must also run after the
    // layout it measures has rendered.
    void version;

    if (missing.length === 0) return null;

    return (
        <div className="rank rank--wide" data-unplaced="true">
            <Module title="Unassigned" index={String(missing.length)}>
                {missing.map((id) => (
                    <Knob key={id} id={id} label={definitionOf(id)?.name ?? id} />
                ))}
            </Module>
        </div>
    );
}
