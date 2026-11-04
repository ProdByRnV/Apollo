/*
    The keys that undo and redo (Phase 18a).

    Ctrl+Z and Ctrl+Shift+Z, or Ctrl+Y, on Windows and Linux; Cmd+Z and
    Cmd+Shift+Z on a Mac. Not while somebody is typing: a text field has an undo
    of its own, for the letters in it, and taking the keys from it would undo
    a knob from inside a preset's name.
*/

export type UndoAction = 'undo' | 'redo';

export interface UndoKeyEvent {
    key: string;
    ctrlKey: boolean;
    metaKey: boolean;
    shiftKey: boolean;
    altKey: boolean;
}

/** What the target of a key is, as much as matters here. */
export interface UndoKeyTarget {
    tagName: string;
    type?: string | undefined;
    isContentEditable?: boolean | undefined;
}

/** Inputs that hold text a person types, which keep the keys. A slider, a
    checkbox or a button holds none. */
const TEXT_INPUTS = new Set(['text', 'search', 'email', 'url', 'tel', 'password', 'number', '']);

export function isTextEntry(target: UndoKeyTarget | null): boolean {
    if (!target) return false;
    if (target.isContentEditable) return true;

    const tag = target.tagName.toUpperCase();
    if (tag === 'TEXTAREA') return true;
    if (tag === 'INPUT') return TEXT_INPUTS.has((target.type ?? '').toLowerCase());

    return false;
}

/** @returns what the key asks for, or null if it is not one of them. */
export function undoActionOf(event: UndoKeyEvent, target: UndoKeyTarget | null): UndoAction | null {
    if (event.altKey || isTextEntry(target)) return null;

    // Either modifier, so a Mac keyboard on Windows and Ctrl on a Mac both
    // work; nothing else on the page uses them with these letters.
    if (!event.ctrlKey && !event.metaKey) return null;

    const key = event.key.toLowerCase();

    if (key === 'z') return event.shiftKey ? 'redo' : 'undo';
    if (key === 'y' && !event.shiftKey) return 'redo';

    return null;
}

/** "Ctrl+Z" or "⌘Z", for a tooltip. */
export function shortcutLabel(action: UndoAction, mac: boolean): string {
    if (mac) return action === 'undo' ? '⌘Z' : '⇧⌘Z';
    return action === 'undo' ? 'Ctrl+Z' : 'Ctrl+Shift+Z';
}
