/*
    The page.

    Ordered by signal flow: what makes the sound, then what shapes it, then what
    moves it, then what leaves. A parameter list sorted alphabetically is a
    database; this is an instrument.

    App owns exactly two things — the inbound message routing and the MIDI
    assignment mode — and draws the status line, which anything may write to
    (state/status.ts). Every value on the page
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
    requestLfoCurves,
    requestMetadata,
    requestMidiMappings,
    requestContent,
    requestPresets,
    send,
    subscribe,
    toggleFullscreen,
} from './bridge/bridge';
import { PROTOCOL_VERSION } from './bridge/protocol';
import { applyContentIndex, applyContentSlots } from './state/content';
import { applyLfoCurves } from './state/lfoCurves';
import type { InboundMessage } from './bridge/protocol';

import {
    CompressorEffect,
    DelayEffect,
    Distortion,
    EqualiserEffect,
    GateEffect,
    Rack,
    ReverbEffect,
} from './modules/Effects';
import { Envelopes, Lfos, Macros } from './modules/Modulators';
import { Filter } from './modules/Filters';
import { Matrix } from './modules/Matrix';
import { Presets } from './modules/Presets';
import { Noise, Oscillator, SubOscillator } from './modules/Sources';
import { Expression, Output } from './modules/Tail';
import { Unplaced } from './modules/Unplaced';
import { ModCarryLayer } from './components/ModSource';

import { applyProfiles, applyMappings, midiState, profileTone, setMidiMode, toggleLearn, useMidi } from './state/midi';
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
import { applyPresetIndex, applyPresetRefusal, applyPresetStatus, usePresets } from './state/presets';
import { announce, useAnnouncement } from './state/status';
import type { StatusTone } from './state/status';
import {
    deliverInstrumentFrame,
    deliverScopeFrames,
    deliverSpectrumFrame,
    useSampleRate,
} from './state/telemetry';

export function App(): JSX.Element {
    const workspaceRef = useRef<HTMLElement>(null);

    /** True once the first state snapshot has arrived. */
    const boundRef = useRef(false);

    const midi = useMidi();
    const assigned = useAssignedRoutings();
    const metadataVersion = useMetadataVersion();
    const presets = usePresets();
    const sampleRate = useSampleRate();

    /** What the connection established, for the readout's tooltip. */
    const [binding, setBinding] = useState('');

    // The status line lives in a store (state/status.ts), so that the parts of
    // the page that finish something can say so. What it says first depends on
    // whether there is an engine to talk to at all: opened outside the plugin,
    // it says so plainly rather than sitting on "Connecting" for ever
    // (CLAUDE.md §33, §39).
    useEffect(() => {
        if (bridgeAvailable) announce('Connecting to engine…');
        else announce('No engine connection — this page is running outside Apollo.', 'caution');
    }, []);

    const announcement = useAnnouncement();

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

                // And for the drawn LFO curves, which travel in the same
                // document as the parameters but are not parameters.
                requestLfoCurves();

                // And for the library, which was scanned when the editor
                // attached and so may already be waiting.
                requestPresets();

                // And for the user's own wavetables and noise sample, which
                // travel in the patch, and the libraries they come from.
                requestContent();
                break;

            case 'stateSnapshot':
                applySnapshot(message.parameters);

                // ONLY THE FIRST ONE SAYS SO. This line is a connection
                // message, and a snapshot also arrives whenever state is
                // replaced wholesale — which is exactly when the user has just
                // been told something more useful. Announcing the parameter
                // count again there wiped "Loaded Glass Bell." off the status
                // line a fraction of a second after it appeared.
                //
                // And it says something a user can use. It used to read "227
                // parameters bound · protocol v1", which is true and is a
                // sentence about the bridge rather than about the instrument.
                // The count stays available in the readout's tooltip, where
                // somebody diagnosing a connection will look for it.
                if (!boundRef.current) {
                    boundRef.current = true;
                    announce('Ready.');
                    setBinding(`${definitionCount()} parameters · protocol v${message.version}`);
                }
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

            case 'spectrumFrame':
                deliverSpectrumFrame(message);
                break;

            case 'lfoCurves':
                applyLfoCurves(message.curves);
                break;

            case 'midiMappings':
                applyMappings(message.mappings, message.learning, message.capacity);

                // A replacement is reported rather than left to be discovered:
                // assigning a control that was already in use silently takes it
                // away from whatever had it (CLAUDE.md §33).
                if (message.status === 'PROFILE_APPLIED'
                    || message.status === 'RELEASED'
                    || message.status === 'CLEARED'
                    || message.status === 'EDITED'
                    || message.status === 'EDIT_FAILED'
                    || message.status.startsWith('REPLACED')
                    || message.status.startsWith('REJECTED')) {
                    announce(message.statusMessage,
                             message.status.startsWith('REJECTED') || message.status === 'EDIT_FAILED'
                                 ? 'error' : 'info');
                }
                break;

            case 'controllerProfiles':
                applyProfiles(message);

                // The list alone is not news; a save, a removal, an export or
                // an import is something the user just asked for.
                if (message.status && message.statusMessage) {
                    announce(message.statusMessage, profileTone(message.status));
                }
                break;

            case 'presetIndex':
                applyPresetIndex(message);
                break;

            // A drop or a cleared slot: the slots arrive as parameters, and
            // this is what to say about them (13h-4).
            case 'modulationRouting':
                announce(message.statusMessage, message.status === 'MATRIX_FULL' ? 'caution' : 'info');
                break;

            case 'presetStatus':
                applyPresetStatus(message);

                // Only the events worth interrupting the status line for. A
                // load, a save and a refusal are all things the user just asked
                // for and is owed an answer about; the current name arriving at
                // startup, or after a project load, is not news.
                if (message.statusMessage) announce(message.statusMessage, presetTone(message.status));
                break;

            case 'contentIndex':
                applyContentIndex(message);
                break;

            case 'contentSlots':
                applyContentSlots(message);

                // Said in the status bar as well as on the slot's own line:
                // a dialog may have covered the line when it happened.
                if (message.statusMessage && !['CURRENT', 'RESTORED'].includes(message.status)) {
                    announce(message.statusMessage,
                             message.status.endsWith('_FAILED') ? 'error' : 'info');
                }
                break;

            case 'error':
                // A save refused for its name is shown in the save form as
                // well, which is where the user is looking when it happens.
                if (message.code === 'INVALID_PRESET_NAME') applyPresetRefusal(message.message);

                announce(message.message, 'error');
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
        : announcement.text;

    // Learning is an instruction, not a warning, whatever came before it.
    const statusTone = learningName ? 'info' : announcement.tone;

    const hint = midi.mode
        ? 'Click a control to assign it · Delete to release · Escape to cancel'
        : 'Drag a knob · Shift for fine · Double-click to reset · Right-click or Enter to type a value';

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
                {/*
                    The developer's handle rather than a description of the
                    product, at their request: the masthead is the one place a
                    plugin is looked at in every screenshot anyone takes of it,
                    and "wavetable synthesiser" was telling people something the
                    rest of the interface already makes obvious.
                */}
                <span className="masthead__tag">@ProdByRnV</span>

                {/*
                    What is loaded, where a plugin normally says it. Absent
                    rather than "Init" when the sound has no name: a patch that
                    was never a preset has not been given one, and inventing one
                    would make the masthead claim something the library does not
                    contain.
                */}
                {presets.name ? (
                    <span className="masthead__preset" title="The preset currently loaded">
                        {presets.name}
                    </span>
                ) : null}

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

                {/*
                    Fills the display the plugin is on, and pressing it again
                    goes back to the size it was. The editor does the resizing —
                    a WebView cannot resize the window hosting it — so this only
                    asks.

                    Not a toggle with two labels: the page is never told whether
                    the editor is currently full size, and a button that claimed
                    to know would be wrong the moment somebody dragged the
                    window's own corner.
                */}
                <button
                    className="masthead__button masthead__button--quiet"
                    type="button"
                    title="Fill the screen, or return to the previous size"
                    onClick={toggleFullscreen}
                >
                    Fullscreen
                </button>

                <div className="readout">
                    <span className="lamp" data-on={midi.mappings.size > 0} aria-hidden="true" />
                    <span>MIDI <strong>{midi.mappings.size}</strong></span>
                </div>
                <div className="readout">
                    <span className="lamp" data-on={assigned > 0} aria-hidden="true" />
                    <span>MOD <strong>{assigned}</strong>/16</span>
                </div>
                {/*
                    The sample rate the engine is actually running at, which is
                    what "SR" means on every other piece of audio software. It
                    used to show the bridge protocol version, "v1", under that
                    label — a reading nobody could act on, labelled as one they
                    could. The protocol is still here, in the tooltip.
                */}
                <div
                    className="readout"
                    title={`Sample rate the engine is running at${binding ? ` · ${binding}` : ` · protocol v${PROTOCOL_VERSION}`}`}
                >
                    <span>SR <strong>{formatSampleRate(sampleRate)}</strong></span>
                </div>
            </header>

            <main className="workspace" ref={workspaceRef}>
                {ready ? (
                    <>
                        {/*
                            Above the signal path rather than in it, because a
                            preset is what you choose before there is a signal.
                        */}
                        <div className="rank rank--wide"><Presets /></div>

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
                        <div className="rank rank--wide"><Macros /></div>
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
                            two at a time. With five of them that is two full
                            rows and one panel on a third, which is also what
                            keeps their controls on one line at the narrowest
                            window the instrument is usable in.
                        */}
                        <div className="rank rank--pair">
                            <Distortion />
                            <DelayEffect />
                            <ReverbEffect />
                            <GateEffect />
                            <CompressorEffect />
                        </div>

                        {/*
                            The equaliser is on its own row because it is the one
                            effect that is a picture as well as a set of
                            controls, and a response curve squeezed into half a
                            row would be a curve nobody could read a notch off.
                        */}
                        <div className="rank rank--wide"><EqualiserEffect /></div>

                        <div className="rank rank--compact">
                            <Output />
                            <Expression />
                        </div>

                        <Unplaced workspace={workspaceRef} />
                    </>
                ) : null}
            </main>

            {/* A modulation source being carried to a control (13h-4). */}
            <ModCarryLayer />

            <footer className="footer">
                <span role="status" className="footer__status" data-tone={statusTone}>
                    {statusTone !== 'info' ? (
                        <>
                            <span aria-hidden="true">⚠ </span>
                            <span className="visually-hidden">
                                {statusTone === 'error' ? 'Error: ' : 'Warning: '}
                            </span>
                        </>
                    ) : null}
                    {statusText}
                </span>
                <span className="footer__spacer" />
                <span>{hint}</span>
            </footer>
        </>
    );
}

/** "48 kHz", "44.1 kHz", or a dash until the engine has said. */
function formatSampleRate(rate: number): string {
    if (!(rate > 0)) return '—';

    return `${Number((rate / 1000).toFixed(1))} kHz`;
}

/** How loudly a preset outcome is shown. A refusal that destroyed nothing is a
    caution; a load or save that failed is an error; the rest is news. */
function presetTone(status: string): StatusTone {
    if (status === 'LOAD_FAILED' || status === 'SAVE_FAILED') return 'error';
    if (status === 'ALREADY_EXISTS' || status === 'PROGRAM_EMPTY') return 'caution';
    return 'info';
}
