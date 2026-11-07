/*
    Macro names on the page (Phase 16c): what an unnamed macro is called, what
    is kept of what was typed, and how the names reach the source lists. That
    the engine keeps, saves and refuses them is tested there
    (Tests/State/MacroNameTests.cpp).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    cleanMacroName,
    limitMacroName,
    macroLabel,
    macroNameToSend,
    macroOfDestination,
    macroOfSource,
    withMacroDestinationNames,
    withMacroNames,
} from '../src/params/macroNames.ts';

test('an unnamed macro reads as its number', () => {
    assert.equal(macroLabel(1, ''), 'Macro 1');
    assert.equal(macroLabel(4, ''), 'Macro 4');
    assert.equal(macroLabel(2, 'Wobble'), 'Wobble');
});

test('a name is one line, trimmed, as the engine keeps it', () => {
    assert.equal(cleanMacroName('  Filter\t\tSweep \n'), 'Filter Sweep');
    assert.equal(cleanMacroName('Line\r\nbreak'), 'Line break');
    assert.equal(cleanMacroName('a b'), 'a b');
    assert.equal(cleanMacroName('\t \n'), '');
});

test('what is sent takes a name away when it is empty or the default', () => {
    assert.equal(macroNameToSend(1, 'Wobble'), 'Wobble');
    assert.equal(macroNameToSend(1, '   '), '');
    assert.equal(macroNameToSend(3, ' Macro 3 '), '');

    // Another macro's default is a name like any other.
    assert.equal(macroNameToSend(1, 'Macro 4'), 'Macro 4');
});

test('the limit counts characters, not UTF-16 units', () => {
    assert.equal(limitMacroName('Sixteen chars ok!', 16), 'Sixteen chars ok');
    assert.equal(limitMacroName('Short', 16), 'Short');

    const sixteenEmoji = '\u{1F3B9}'.repeat(17);
    assert.equal(Array.from(limitMacroName(sixteenEmoji, 16)).length, 16);
});

test('only the four macro sources are macros', () => {
    assert.equal(macroOfSource(15), null);
    assert.equal(macroOfSource(16), 1);
    assert.equal(macroOfSource(19), 4);
    assert.equal(macroOfSource(20), null, 'the follower is not a macro');
    assert.equal(macroOfSource(20), null);
    assert.equal(macroOfSource(0), null);
});

test('the source list takes the names and keeps its length', () => {
    const table = Array.from({ length: 20 }, (_, source) => `S${source}`);
    const named = withMacroNames(table, ['', 'Wobble', '', 'Space']);

    assert.equal(named.length, table.length);
    assert.equal(named[15], 'S15');
    assert.equal(named[16], 'Macro 1');
    assert.equal(named[17], 'Wobble');
    assert.equal(named[18], 'Macro 3');
    assert.equal(named[19], 'Space');
});

test('since 18h the four macros end the destination list, named as they are named', () => {
    const table = Array.from({ length: 35 }, (_, destination) => `D${destination}`);
    assert.equal(macroOfDestination(30), null, 'the compressor mix is not a macro');
    assert.equal(macroOfDestination(31), 1);
    assert.equal(macroOfDestination(34), 4);
    assert.equal(macroOfDestination(35), null);

    const named = withMacroDestinationNames(table, ['Wobble', '', '', 'Space']);
    assert.equal(named.length, 35);
    assert.equal(named[30], 'D30');
    assert.equal(named[31], 'Wobble');
    assert.equal(named[32], 'Macro 2');
    assert.equal(named[34], 'Space');
});
