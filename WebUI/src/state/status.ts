/*
    The status line: the last thing worth telling the user.

    A store rather than App's own state, so that a part of the page that ends
    something can say so. Cancelling the replace prompt used to leave "A preset
    called X is already there." on the footer, warning about a save that was no
    longer happening, because only App could write to it.

    THE TONE IS SAID IN A SHAPE AS WELL AS A COLOUR. An error or a caution is
    drawn with a warning sign beside it as well as in red or orange, so it does
    not depend on seeing the colour (CLAUDE.md §24.2). It replaced a prefix —
    "Engine reported: …" — which did the same job in words but read as a
    message from the plumbing rather than from the instrument.
*/

import { useSyncExternalStore } from 'react';

export type StatusTone = 'info' | 'caution' | 'error';

export interface Announcement {
    text: string;
    tone: StatusTone;
}

let current: Announcement = { text: '', tone: 'info' };

const listeners = new Set<() => void>();

export function announce(text: string, tone: StatusTone = 'info'): void {
    current = { text, tone };
    for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

function snapshot(): Announcement {
    return current;
}

export function useAnnouncement(): Announcement {
    return useSyncExternalStore(subscribe, snapshot);
}
