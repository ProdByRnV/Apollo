/*
    The keys that undo and redo (Phase 18a).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { isTextEntry, shortcutLabel, undoActionOf } from '../src/params/undoKeys.ts';

const key = (k, modifiers = {}) => ({
    key: k,
    ctrlKey: false,
    metaKey: false,
    shiftKey: false,
    altKey: false,
    ...modifiers,
});

const knob = { tagName: 'DIV' };

test('Ctrl+Z undoes, and Ctrl+Shift+Z and Ctrl+Y redo', () => {
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), knob), 'undo');
    assert.equal(undoActionOf(key('Z', { ctrlKey: true, shiftKey: true }), knob), 'redo');
    assert.equal(undoActionOf(key('y', { ctrlKey: true }), knob), 'redo');
});

test('Cmd does on a Mac what Ctrl does elsewhere', () => {
    assert.equal(undoActionOf(key('z', { metaKey: true }), knob), 'undo');
    assert.equal(undoActionOf(key('z', { metaKey: true, shiftKey: true }), knob), 'redo');
});

test('the letters alone, or with Alt, are not undo', () => {
    assert.equal(undoActionOf(key('z'), knob), null);
    assert.equal(undoActionOf(key('z', { shiftKey: true }), knob), null);
    assert.equal(undoActionOf(key('z', { ctrlKey: true, altKey: true }), knob), null);
    assert.equal(undoActionOf(key('y', { ctrlKey: true, shiftKey: true }), knob), null);
    assert.equal(undoActionOf(key('x', { ctrlKey: true }), knob), null);
});

test('a text field keeps the keys for its own letters', () => {
    const name = { tagName: 'INPUT', type: 'text' };
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), name), null);
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), { tagName: 'TEXTAREA' }), null);
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), { tagName: 'DIV', isContentEditable: true }), null);
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), { tagName: 'input', type: 'SEARCH' }), null);
});

test('a control that holds no text does not', () => {
    assert.equal(isTextEntry({ tagName: 'INPUT', type: 'checkbox' }), false);
    assert.equal(isTextEntry({ tagName: 'INPUT', type: 'range' }), false);
    assert.equal(isTextEntry({ tagName: 'SELECT' }), false);
    assert.equal(isTextEntry({ tagName: 'BUTTON' }), false);
    assert.equal(isTextEntry(null), false);
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), { tagName: 'SELECT' }), 'undo');
    assert.equal(undoActionOf(key('z', { ctrlKey: true }), null), 'undo');
});

test('the shortcut is named as the system names it', () => {
    assert.equal(shortcutLabel('undo', false), 'Ctrl+Z');
    assert.equal(shortcutLabel('redo', false), 'Ctrl+Shift+Z');
    assert.equal(shortcutLabel('undo', true), '⌘Z');
    assert.equal(shortcutLabel('redo', true), '⇧⌘Z');
});
