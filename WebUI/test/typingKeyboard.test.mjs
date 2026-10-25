/*
    Playing from the computer keyboard (Phase 15): which key plays which note,
    the octave and velocity steps, what the strip draws, and which key presses
    are left to whatever has focus. That the notes reach the engine and sound is
    tested there (Tests/UI/NotePlayingTests.cpp).
*/

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    DEFAULT_VELOCITY,
    MAX_OCTAVE,
    MIN_OCTAVE,
    NOTE_KEYS,
    VELOCITIES,
    belongsElsewhere,
    isBlack,
    noteForKey,
    noteName,
    octaveText,
    shiftOctave,
    stepVelocity,
    stripKeys,
} from '../src/params/typingKeyboard.ts';

test('the middle row is the white keys and the row above the black ones, from middle C', () => {
    assert.equal(noteForKey('KeyA', 0), 60, 'A is middle C');
    assert.equal(noteForKey('KeyW', 0), 61);
    assert.equal(noteForKey('KeyK', 0), 72, 'K is the C above');
    assert.equal(noteForKey('Semicolon', 0), 76);

    const whites = ['KeyA', 'KeyS', 'KeyD', 'KeyF', 'KeyG', 'KeyH', 'KeyJ', 'KeyK', 'KeyL', 'Semicolon'];
    const blacks = ['KeyW', 'KeyE', 'KeyT', 'KeyY', 'KeyU', 'KeyO', 'KeyP'];

    for (const code of whites) assert.ok(!isBlack(noteForKey(code, 0)), code);
    for (const code of blacks) assert.ok(isBlack(noteForKey(code, 0)), code);

    assert.equal(new Set(Object.values(NOTE_KEYS)).size, Object.keys(NOTE_KEYS).length, 'no two keys play one note');
    assert.equal(noteForKey('KeyR', 0), null, 'R has no black key under it, as E and F have none between them');
    assert.equal(noteForKey('KeyZ', 0), null, 'Z is the octave, not a note');
    assert.equal(noteForKey('Digit1', 0), null);
});

test('the octave moves the whole keyboard, and stops where MIDI would run out', () => {
    assert.equal(noteForKey('KeyA', 1), 72);
    assert.equal(noteForKey('KeyA', -1), 48);

    assert.equal(shiftOctave(0, 1), 1);
    assert.equal(shiftOctave(MAX_OCTAVE, 1), MAX_OCTAVE);
    assert.equal(shiftOctave(MIN_OCTAVE, -1), MIN_OCTAVE);

    for (let octave = MIN_OCTAVE; octave <= MAX_OCTAVE; ++octave) {
        for (const code of Object.keys(NOTE_KEYS)) {
            const note = noteForKey(code, octave);
            assert.ok(note !== null && note >= 0 && note <= 127, `${code} at ${octave}`);
        }
    }
});

test('velocity steps through its values and holds at the ends', () => {
    assert.equal(DEFAULT_VELOCITY, 100);
    assert.equal(stepVelocity(100, 1), 127);
    assert.equal(stepVelocity(127, 1), 127);
    assert.equal(stepVelocity(100, -1), 80);
    assert.equal(stepVelocity(20, -1), 20);
    assert.equal(stepVelocity(90, 1), 100, 'between steps, to the next one up');
    assert.equal(stepVelocity(90, -1), 80, 'between steps, to the next one down');
    for (const velocity of VELOCITIES) assert.ok(velocity >= 1 && velocity <= 127);
});

test('the strip draws two octaves and a top C, labelled with the keys that play them', () => {
    const keys = stripKeys(0);

    assert.equal(keys.length, 25);
    assert.equal(keys[0].note, 60);
    assert.equal(keys[24].note, 84);
    assert.equal(keys.filter((key) => !key.black).length, 15, 'fifteen white keys');

    const labelled = keys.filter((key) => key.label !== '');
    assert.equal(labelled.length, Object.keys(NOTE_KEYS).length, 'every playing key is on the strip');
    assert.equal(keys.find((key) => key.label === 'A').note, 60);
    assert.equal(keys.find((key) => key.label === ';').note, 76);
    assert.ok(keys.find((key) => key.note === 60).middleC);
    assert.equal(keys.filter((key) => key.middleC).length, 1);

    assert.equal(stripKeys(-1)[0].note, 48);
    assert.equal(stripKeys(-1).find((key) => key.middleC).label, 'K', 'an octave down, middle C is the K key');
    assert.ok(stripKeys(MAX_OCTAVE).every((key) => key.note <= 127), 'nothing past MIDI at the top');
});

test('notes and octaves read in words a musician knows', () => {
    assert.equal(noteName(60), 'C');
    assert.equal(noteName(61), 'C♯');
    assert.equal(octaveText(0), 'Octave 0');
    assert.equal(octaveText(2), 'Octave +2');
    assert.equal(octaveText(-1), 'Octave −1');
});

test('typing in a field, and shortcuts, are left alone', () => {
    const plain = { ctrlKey: false, altKey: false, metaKey: false };

    assert.equal(belongsElsewhere(plain, null), false);
    assert.equal(belongsElsewhere(plain, { tagName: 'DIV', isContentEditable: false }), false, 'a focused knob still plays');
    assert.equal(belongsElsewhere(plain, { tagName: 'BUTTON', isContentEditable: false }), false);
    assert.equal(belongsElsewhere(plain, { tagName: 'input', isContentEditable: false }), true);
    assert.equal(belongsElsewhere(plain, { tagName: 'TEXTAREA', isContentEditable: false }), true);
    assert.equal(belongsElsewhere(plain, { tagName: 'SELECT', isContentEditable: false }), true);
    assert.equal(belongsElsewhere(plain, { tagName: 'DIV', isContentEditable: true }), true);
    assert.equal(belongsElsewhere({ ...plain, ctrlKey: true }, null), true, 'Ctrl+S is not an E');
    assert.equal(belongsElsewhere({ ...plain, metaKey: true }, null), true);
    assert.equal(belongsElsewhere({ ...plain, altKey: true }, null), true);
});
