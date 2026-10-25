/*
    The piano strip: Apollo played from the computer keyboard or the mouse, in
    the standalone (Phase 15, ADR-0111).

    For anyone without a MIDI keyboard, who before this could open the
    standalone and hear nothing. Two octaves and a top C, labelled with the
    computer keys that play them, lit while they sound; the octave and the
    velocity beside it, with the keys that move them. The strip is on screen as
    much to say that typing plays notes as to be clicked: a keyboard nobody
    knows is there is a keyboard nobody uses.

    The page decides nothing about sound. A press sends notePlay and a release
    noteRelease, and the engine handles them exactly as a MIDI keyboard's notes.
    What the page does own is never leaving a note held: every press is
    counted, a note is released when its last holder lets go, and leaving the
    window lets go of everything.
*/

import { useCallback, useEffect, useRef, useState } from 'react';

import { notePlay, noteRelease, noteReleaseAll } from '../bridge/bridge';
import {
    DEFAULT_VELOCITY,
    HARDER,
    OCTAVE_DOWN,
    OCTAVE_UP,
    SOFTER,
    belongsElsewhere,
    noteForKey,
    noteName,
    octaveText,
    shiftOctave,
    stepVelocity,
    stripKeys,
} from '../params/typingKeyboard';

const NO_NOTES: ReadonlySet<number> = new Set();

export function PlayStrip(): JSX.Element {
    const [octave, setOctave] = useState(0);
    const [velocity, setVelocity] = useState(DEFAULT_VELOCITY);
    const [lit, setLit] = useState<ReadonlySet<number>>(NO_NOTES);

    // Read by listeners that are attached once, so they see the current
    // values without being reattached on every change.
    const octaveRef = useRef(octave);
    const velocityRef = useRef(velocity);
    octaveRef.current = octave;
    velocityRef.current = velocity;

    /** How many things hold each note: a typed key and the mouse can hold the
        same one, and it sounds until the last of them lets go. */
    const holders = useRef(new Map<number, number>());

    /** The note each typed key started, so a key let go after the octave moved
        still stops the note it played rather than one an octave away. */
    const typed = useRef(new Map<string, number>());

    /** The note under a pressed mouse button, or null. */
    const pointerNote = useRef<number | null>(null);

    const showHeld = useCallback(() => setLit(new Set(holders.current.keys())), []);

    const press = useCallback((note: number) => {
        const count = holders.current.get(note) ?? 0;
        holders.current.set(note, count + 1);

        if (count === 0) {
            notePlay(note, velocityRef.current);
            showHeld();
        }
    }, [showHeld]);

    const lift = useCallback((note: number) => {
        const count = holders.current.get(note) ?? 0;
        if (count === 0) return;

        if (count === 1) {
            holders.current.delete(note);
            noteRelease(note);
            showHeld();
        } else {
            holders.current.set(note, count - 1);
        }
    }, [showHeld]);

    const liftAll = useCallback(() => {
        const anything = holders.current.size > 0;

        holders.current.clear();
        typed.current.clear();
        pointerNote.current = null;

        // Sent whether or not anything is held here: the engine's own record
        // of what the page holds is the one that cannot be wrong.
        noteReleaseAll();
        if (anything) showHeld();
    }, [showHeld]);

    useEffect(() => {
        const onKeyDown = (event: KeyboardEvent): void => {
            const focused = document.activeElement instanceof HTMLElement ? document.activeElement : null;
            if (belongsElsewhere(event, focused)) return;

            const note = noteForKey(event.code, octaveRef.current);

            // Holding a key repeats it; the note is already sounding.
            if (event.repeat) {
                if (note !== null) event.preventDefault();
                return;
            }

            switch (event.code) {
                case OCTAVE_DOWN: setOctave((value) => shiftOctave(value, -1)); event.preventDefault(); return;
                case OCTAVE_UP: setOctave((value) => shiftOctave(value, 1)); event.preventDefault(); return;
                case SOFTER: setVelocity((value) => stepVelocity(value, -1)); event.preventDefault(); return;
                case HARDER: setVelocity((value) => stepVelocity(value, 1)); event.preventDefault(); return;
                default: break;
            }

            if (note === null || typed.current.has(event.code)) return;

            event.preventDefault();
            typed.current.set(event.code, note);
            press(note);
        };

        const onKeyUp = (event: KeyboardEvent): void => {
            const note = typed.current.get(event.code);
            if (note === undefined) return;

            typed.current.delete(event.code);
            lift(note);
        };

        // A key let go while the window is not looking never arrives as a
        // key-up, so leaving the window lets go of everything.
        const onBlur = (): void => liftAll();
        const onHidden = (): void => { if (document.hidden) liftAll(); };

        window.addEventListener('keydown', onKeyDown);
        window.addEventListener('keyup', onKeyUp);
        window.addEventListener('blur', onBlur);
        document.addEventListener('visibilitychange', onHidden);

        return () => {
            window.removeEventListener('keydown', onKeyDown);
            window.removeEventListener('keyup', onKeyUp);
            window.removeEventListener('blur', onBlur);
            document.removeEventListener('visibilitychange', onHidden);
            liftAll();
        };
    }, [press, lift, liftAll]);

    const noteAt = (x: number, y: number): number | null => {
        const key = document.elementFromPoint(x, y)?.closest<HTMLElement>('[data-note]');
        return key ? Number(key.dataset.note) : null;
    };

    const onPointerDown = (event: React.PointerEvent<HTMLDivElement>): void => {
        if (event.button !== 0) return;

        const note = noteAt(event.clientX, event.clientY);
        if (note === null) return;

        event.preventDefault();
        event.currentTarget.setPointerCapture(event.pointerId);
        pointerNote.current = note;
        press(note);
    };

    // Dragging along the keys plays each one it reaches, as a glissando does.
    const onPointerMove = (event: React.PointerEvent<HTMLDivElement>): void => {
        const current = pointerNote.current;
        if (current === null) return;

        const note = noteAt(event.clientX, event.clientY);
        if (note === current) return;

        lift(current);
        pointerNote.current = note;
        if (note !== null) press(note);
    };

    const onPointerEnd = (): void => {
        const current = pointerNote.current;
        pointerNote.current = null;
        if (current !== null) lift(current);
    };

    const keys = stripKeys(octave);
    const whites = keys.filter((key) => !key.black).length;

    // Where each key sits, in fifteenths of the strip: a white key one slot,
    // a black key straddling the line between two.
    let white = 0;
    const placed = keys.map((key) => {
        const left = key.black ? (white - 0.3) / whites : white / whites;
        if (!key.black) white += 1;
        return { ...key, left };
    });

    return (
        <section className="play-strip" aria-label="On-screen keyboard">
            <div className="play-strip__controls">
                <div className="play-strip__row">
                    <button
                        type="button"
                        className="play-strip__step"
                        aria-label="Octave down"
                        onClick={() => setOctave((value) => shiftOctave(value, -1))}
                    >
                        −
                    </button>
                    <span className="play-strip__value">{octaveText(octave)}</span>
                    <button
                        type="button"
                        className="play-strip__step"
                        aria-label="Octave up"
                        onClick={() => setOctave((value) => shiftOctave(value, 1))}
                    >
                        +
                    </button>
                    <span className="play-strip__keys-hint" aria-hidden="true"><kbd>Z</kbd><kbd>X</kbd></span>
                </div>

                <div className="play-strip__row">
                    <button
                        type="button"
                        className="play-strip__step"
                        aria-label="Softer"
                        onClick={() => setVelocity((value) => stepVelocity(value, -1))}
                    >
                        −
                    </button>
                    <span className="play-strip__value">Velocity {velocity}</span>
                    <button
                        type="button"
                        className="play-strip__step"
                        aria-label="Harder"
                        onClick={() => setVelocity((value) => stepVelocity(value, 1))}
                    >
                        +
                    </button>
                    <span className="play-strip__keys-hint" aria-hidden="true"><kbd>C</kbd><kbd>V</kbd></span>
                </div>

                <p className="play-strip__hint">
                    Play with your computer keyboard: <kbd>A</kbd>–<kbd>;</kbd> and <kbd>W</kbd>–<kbd>P</kbd>
                </p>
            </div>

            <div
                className="play-strip__piano"
                aria-hidden="true"
                onPointerDown={onPointerDown}
                onPointerMove={onPointerMove}
                onPointerUp={onPointerEnd}
                onPointerCancel={onPointerEnd}
                onLostPointerCapture={onPointerEnd}
            >
                {placed.map((key) => (
                    <div
                        key={key.note}
                        className={key.black ? 'play-key play-key--black' : 'play-key play-key--white'}
                        data-note={key.note}
                        data-lit={lit.has(key.note) ? 'true' : 'false'}
                        title={`${noteName(key.note)}${key.middleC ? ', middle C' : ''}${key.label ? ` — key ${key.label}` : ''}`}
                        style={key.black
                            ? { left: `${key.left * 100}%`, width: `${60 / whites}%` }
                            : { left: `${key.left * 100}%`, width: `${100 / whites}%` }}
                    >
                        {key.middleC ? <span className="play-key__middle" /> : null}
                        {key.label ? <span className="play-key__label">{key.label}</span> : null}
                    </div>
                ))}
            </div>
        </section>
    );
}
