/*
    The arithmetic of controller profiles on the page (Phase 13h-3), kept apart
    from the store so it can be tested without a bridge.
*/

import type { ControllerProfileEntry, ControllerProfilesMessage } from '../bridge/protocol';

export interface ProfileFields {
    profiles: readonly ControllerProfileEntry[];
    profileStatus: string;
    profileStatusMessage: string;
    selectedProfile: string;
    selectedSerial: number;
    unreadableProfiles: number;
}

/** What a profiles message changes. A selection bumps the serial, so the
    same profile selected twice still moves the picker; a message without one
    leaves the picker where it was. An older engine sends no status at all. */
export function profileFieldsFrom(message: ControllerProfilesMessage, serial: number): Partial<ProfileFields> {
    return {
        profiles: message.profiles.map((profile) => ({ ...profile, user: profile.user === true })),
        profileStatus: message.status ?? '',
        profileStatusMessage: message.statusMessage ?? '',
        unreadableProfiles: message.unreadable ?? 0,
        ...(message.selected ? { selectedProfile: message.selected, selectedSerial: serial + 1 } : {}),
    };
}

/** How loudly a profile outcome is shown. */
export function profileTone(status: string): 'info' | 'caution' | 'error' {
    if (status.endsWith('_FAILED')) return 'error';
    if (status === 'ALREADY_EXISTS') return 'caution';
    return 'info';
}

export interface MappingLike {
    controller: number;
    channel: number;
}

/** Assignments in the order a controller is laid out: by controller number,
    then channel. The engine's own order is the order they were made in, which
    an edit would shuffle. */
export function mappingRows<T extends MappingLike>(mappings: ReadonlyMap<string, T>): Array<[string, T]> {
    return [...mappings.entries()].sort(([, a], [, b]) =>
        a.controller !== b.controller ? a.controller - b.controller : a.channel - b.channel);
}
