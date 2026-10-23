/*
    The page's half of controller profiles (Phase 13h-3): what a profiles
    message leaves in the store, the order the assignment table is drawn in,
    and how loudly each outcome is shown.
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { mappingRows, profileFieldsFrom, profileTone } from '../src/params/profiles.ts';

const profiles = [
    { id: 'sound_controllers', name: 'Sound Controllers', description: '', assignments: 8, user: false },
    { id: 'user-1', name: 'Desk', description: 'Mine', assignments: 3, user: true },
];

test('a profiles message is the list, its outcome, and where the picker should go', () => {
    const fields = profileFieldsFrom({ type: 'controllerProfiles', version: 1, profiles, unreadable: 2,
                                       status: 'SAVED', statusMessage: 'Saved 3 assignments as Desk.',
                                       selected: 'user-1' }, 4);

    assert.equal(fields.profiles.length, 2);
    assert.equal(fields.profileStatus, 'SAVED');
    assert.equal(fields.selectedProfile, 'user-1');
    assert.equal(fields.selectedSerial, 5, 'the same profile selected again still moves the picker');
    assert.equal(fields.unreadableProfiles, 2);
});

test('a bare list is no outcome, and leaves the picker where it was', () => {
    const fields = profileFieldsFrom({ type: 'controllerProfiles', version: 1, profiles }, 4);

    assert.equal(fields.profileStatus, '');
    assert.equal(fields.profileStatusMessage, '');
    assert.equal('selectedProfile' in fields, false);
    assert.equal('selectedSerial' in fields, false);
});

test('an older engine, which sends no status and no user flag, still fills the list', () => {
    const fields = profileFieldsFrom({ type: 'controllerProfiles', version: 1,
                                       profiles: [{ id: 'general_purpose', name: 'General Purpose',
                                                    description: '', assignments: 8 }] }, 0);

    assert.equal(fields.profiles.length, 1);
    assert.equal(fields.profiles[0].user, false);
    assert.equal(fields.unreadableProfiles, 0);
});

test('the assignment table is in controller order, then channel, whatever order they were made in', () => {
    const mappings = new Map([
        ['filter1_cutoff', { controller: 74, channel: 0, min: 0, max: 1 }],
        ['osc1_level', { controller: 20, channel: 2, min: 0, max: 1 }],
        ['osc2_level', { controller: 20, channel: 1, min: 0, max: 1 }],
        ['env1_attack', { controller: 7, channel: 0, min: 0, max: 1 }],
    ]);

    assert.deepEqual(mappingRows(mappings).map(([id]) => id),
                     ['env1_attack', 'osc2_level', 'osc1_level', 'filter1_cutoff']);
});

test('failures are errors, a name already taken is a caution, the rest is news', () => {
    for (const status of ['SAVE_FAILED', 'DELETE_FAILED', 'EXPORT_FAILED', 'IMPORT_FAILED']) {
        assert.equal(profileTone(status), 'error', status);
    }

    assert.equal(profileTone('ALREADY_EXISTS'), 'caution');

    for (const status of ['SAVED', 'DELETED', 'EXPORTED', 'IMPORTED']) {
        assert.equal(profileTone(status), 'info', status);
    }
});
