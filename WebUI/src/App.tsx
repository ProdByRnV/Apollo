/*
    The page.

    Ordered by signal flow: what makes the sound, then what shapes it, then what
    moves it, then what leaves. A parameter list sorted alphabetically is a
    database; this is an instrument.

    App owns exactly three things — the status line, the inbound message routing,
    and the MIDI assignment mode — and nothing else. Every value on the page
    reaches its control through a store rather than through props, which is why
    moving a knob does not re-render this component and a project load does not
    re-render the page.
*/

import { useEffect, useRef, useState } from 'react';

import {
    bridgeAvailable,
    midiMappingClearAll,
    midiMappingRemove,
    requestControllerProfiles,
    requestMetadata,
    requestMidiMappings,
    send,
    subscribe,
} from './bridge/bridge';
import { PROTOCOL_VERSION } from './bridge/protocol';
import type { InboundMessage } from './bridge/protocol';

import { DelayEffect, Distortion, Rack, ReverbEffect } from './modules/Effects';
import { Envelopes, Lfos } from './modules/Modulators';
import { Filter } from './modules/Filters';
import { Matrix } from './modules/Matrix';
import { Noise, Oscillator, SubOscillator } from './modules/Sources';
import { Expression, Output } from './modules/Tail';
import { Unplaced } from './modules/Unplaced';

import { applyProfiles, applyMappings, midiState, setMidiMode, toggleLearn, useMidi } from './state/midi';
import { useAssignedRoutings } from './state/modulation';
import {
    applySnapshot,
    applyValue,
    definitionCount,
    definitionOf,
    isHeld,
    setDefinitions,
    useMetadataVersion,
} from './state/parameters';
import { deliverInstrumentFrame, deliverScopeFrames } from './state/telemetry';

export function App(): JSX.Element {
    const workspaceRef = useRef<HTMLElement>(null);

    const midi = useMidi();
    const assigned = useAssignedRoutings();
    const metadataVersion = useMetadataVersion();

    /** The last thing the engine said, kept so a transient MIDI prompt can be
        shown over it and then cleared without losing it. */
    const [status, setStatus] = useState(
        bridgeAvailable
            ? 'Connecting to engine…'
            // Opened outside the plugin. Say so plainly rather than sitting on
            // "Connecting" for ever (CLAUDE.md §33, §39).
            : 'No engine connection — this page is running outside Apollo.',
    );

    //--------------------------------------------------------------------------
    // Inbound messages.

    useEffect(() => subscribe((message: InboundMessage) => {
        switch (message.type) {
            case 'parameterMetadata':
                setDefinitions(message.parameters);

                // Everything is built from metadata but nothing yet holds a
                // value, so ask for the state that fills it in.
                send({ type: 'requestState', version: PROTOCOL_VERSION });

                // And for the mappings, which are stored per project and so may
                // already exist before this page has ever been opened.
                requestMidiMappings();
                requestControllerProfiles();
                break;

            case 'stateSnapshot':
                applySnapshot(message.parameters);
                setStatus(`${definitionCount()} parameters bound · protocol v${message.version}`);
                break;

            case 'parameterChanged':
                // A control under the pointer is not overwritten by the echo of
                // what it just sent: the value would arrive a frame late and
                // pull the knob backwards under the hand.
                if (!isHeld(message.id)) applyValue(message.id, message.normalizedValue);
                break;

            case 'scopeFrames':
                deliverScopeFrames(message.scopes);
                break;

            case 'instrumentFrame':
                deliverInstrumentFrame(message);
                break;

            case 'midiMappings':
                applyMappings(message.mappings, message.learning, message.capacity);

                // A replacement is reported rather than left to be discovered:
                // assigning a control that was already in use silently takes it
                // away from whatever had it (CLAUDE.md §33).
                if (message.status === 'PROFILE_APPLIED'
                    || message.status.startsWith('REPLACED')
                    || message.status.startsWith('REJECTED')) {
                    setStatus(message.statusMessage);
                }
                break;

            case 'controllerProfiles':
                applyProfiles(message.profiles);
                break;

            case 'error':
                setStatus(`Engine reported: ${message.message}`);
                break;

            default:
                break;
        }
    }), []);

    useEffect(() => {
        if (bridgeAvailable) requestMetadata();
    }, []);

    //--------------------------------------------------------------------------
    // MIDI assignment mode.
    //
    // Capture-phase DOM listeners rather than React handlers, and deliberately:
    // in assignment mode a knob must not also start a drag and a segmented
    // option must not also change value, which means reaching the event *before*
    // the control's own handler and cancelling it there. React's synthetic
    // events are delegated at the root and would run after the controls'.

    useEffect(() => {
        const workspace = workspaceRef.current;
        if (workspace === null) return;

        const assignable = (event: Event): HTMLElement | null => {
            if (!midiState().mode) return null;

            const target = event.target;
            if (!(target instanceof Element)) return null;

            return target.closest<HTMLElement>('[data-midi-id]');
        };

        const onPointerDown = (event: Event): void => {
            const target = assignable(event);
            if (!target) return;

            event.preventDefault();
            event.stopPropagation();

            if (target.dataset.midiId) toggleLearn(target.dataset.midiId);
        };

        const swallow = (event: Event): void => {
            if (assignable(event)) {
                event.preventDefault();
                event.stopPropagation();
            }
        };

        const onKeyDown = (event: Event): void => {
            const target = assignable(event);
            if (!target) return;

            const id = target.dataset.midiId;
            if (!id) return;

            const key = (event as KeyboardEvent).key;

            if (key === 'Enter' || key === ' ') {
                event.preventDefault();
                event.stopPropagation();
                toggleLearn(id);
            } else if (key === 'Delete' || key === 'Backspace') {
                // In assignment mode Delete releases the control rather than
                // resetting the parameter, which is the only thing it could
                // sensibly mean here.
                event.preventDefault();
                event.stopPropagation();

                if (midiState().mappings.has(id)) midiMappingRemove(id);
            }
        };

        const swallowed = ['click', 'dblclick', 'change', 'pointerup'];

        workspace.addEventListener('pointerdown', onPointerDown, true);
        workspace.addEventListener('keydown', onKeyDown, true);
        for (const type of swallowed) workspace.addEventListener(type, swallow, true);

        return () => {
            workspace.removeEventListener('pointerdown', onPointerDown, true);
            workspace.removeEventListener('keydown', onKeyDown, true);
            for (const type of swallowed) workspace.removeEventListener(type, swallow, true);
        };
    }, []);

    useEffect(() => {
        const onEscape = (event: KeyboardEvent): void => {
            if (event.key !== 'Escape') return;

            const state = midiState();

            if (state.learning) toggleLearn(state.learning);
            else if (state.mode) setMidiMode(false);
        };

        document.addEventListener('keydown', onEscape);
        return () => { document.removeEventListener('keydown', onEscape); };
    }, []);

    // The mode is on the body rather than on the workspace, because the cursor
    // and the outlined-control treatment apply to the whole window.
    useEffect(() => {
        document.body.dataset.midiMode = midi.mode ? 'true' : 'false';
    }, [midi.mode]);

    //--------------------------------------------------------------------------

    const learningName = midi.learning
        ? (definitionOf(midi.learning)?.name ?? midi.learning)
        : '';

    const statusText = learningName
        ? `Move a MIDI control to assign it to ${learningName} — Escape to cancel`
        : status;

    const hint = midi.mode
        ? 'Click a control to assign it · Delete to release · Escape to cancel'
        : 'Drag a knob · Shift for fine · Double-click or Delete to reset';

    // Nothing can be laid out until the engine has said what exists.
    const ready = metadataVersion > 0;

    // No wrapper element: the React root *is* the application frame, styled by
    // `#root`. Nesting one inside the other left the outer with no height and
    // the footer below the bottom of the window.
    return (
        <>
            <header className="masthead">
                <div>
                    <div className="wordmark">Apollo</div>
                </div>
                <span className="masthead__tag">wavetable synthesiser</span>

                <div className="masthead__spacer" />

                {/*
                    MIDI Learn is a mode rather than a hidden right-click, so that
                    it is reachable from the keyboard and visible to anyone who
                    has never been told it exists (CLAUDE.md §39). While it is on,
                    clicking a control assigns it instead of moving it, and the
                    whole workspace says so.
                */}
                <button
                    className="masthead__button"
                    type="button"
                    aria-pressed={midi.mode}
                    onClick={() => setMidiMode(!midi.mode)}
                >
                    MIDI Learn
                </button>

                <button
                    className="masthead__button masthead__button--quiet"
                    type="button"
                    hidden={!(midi.mode && midi.mappings.size > 0)}
                    onClick={midiMappingClearAll}
                >
                    Clear all
                </button>

                <div className="readout">
                    <span className="lamp" data-on={midi.mappings.size > 0} aria-hidden="true" />
                    <span>MIDI <strong>{midi.mappings.size}</strong></span>
                </div>
                <div className="readout">
                    <span className="lamp" data-on={assigned > 0} aria-hidden="true" />
                    <span>MOD <strong>{assigned}</strong>/16</span>
                </div>
                <div className="readout">
                    <span>SR <strong>v{PROTOCOL_VERSION}</strong></span>
                </div>
            </header>

            <main className="workspace" ref={workspaceRef}>
                {ready ? (
                    <>
                        <div className="rank rank--pair">
                            <Oscillator index={1} />
                            <Oscillator index={2} />
                        </div>

                        <div className="rank rank--compact">
                            <SubOscillator />
                            <Noise />
                        </div>

                        <div className="rank rank--pair">
                            <Filter index={1} />
                            <Filter index={2} />
                        </div>

                        <div className="rank rank--wide"><Envelopes /></div>
                        <div className="rank rank--wide"><Lfos /></div>
                        <div className="rank rank--wide"><Matrix /></div>

                        {/*
                            The rack sits after everything that makes and moves
                            the sound and before what leaves, because that is
                            where it sits in the signal path: effects process the
                            finished mix, and the master fader is still the last
                            thing in the chain.
                        */}
                        <div className="rank rank--wide"><Rack /></div>

                        {/*
                            The effects sit in a pair rank, so they fill the row
                            two at a time as 8b to 8e add them. With one built
                            that is a full-width panel, which is also what keeps
                            its controls on one line at the narrowest window the
                            instrument is usable in.
                        */}
                        <div className="rank rank--pair">
                            <Distortion />
                            <DelayEffect />
                            <ReverbEffect />
                        </div>

                        <div className="rank rank--compact">
                            <Output />
                            <Expression />
                        </div>

                        <Unplaced workspace={workspaceRef} />
                    </>
                ) : null}
            </main>

            <footer className="footer">
                <span role="status">{statusText}</span>
                <span className="footer__spacer" />
                <span>{hint}</span>
            </footer>
        </>
    );
}
