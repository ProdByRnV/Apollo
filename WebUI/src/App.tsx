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

import { useCallback, useEffect, useId, useRef, useState } from 'react';

import {
    bridgeAvailable,
    midiMappingClearAll,
    midiMappingRemove,
    requestControllerProfiles,
    requestLfoCurves,
    requestMacroNames,
    requestMetadata,
    requestMidiMappings,
    requestContent,
    requestPresets,
    redo,
    requestHistory,
    requestInterfaceScale,
    send,
    setInterfaceScale,
    subscribe,
    toggleFullscreen,
    undo,
} from './bridge/bridge';
import { PROTOCOL_VERSION } from './bridge/protocol';
import { shortcutLabel, undoActionOf } from './params/undoKeys';
import type { UndoAction } from './params/undoKeys';
import { applyContentIndex, applyContentSlots } from './state/content';
import { applyHistory, currentHistory, useHistory } from './state/history';
import { applyInterfaceScale, useInterfaceScale, viewportZoom } from './state/interfaceScale';
import { scaleLabel, scaleTitle } from './params/interfaceScale';
import { applyLfoCurves } from './state/lfoCurves';
import { applyMacroNames } from './state/macroNames';
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
import { SectionBar, SectionPage } from './components/Sections';
import { PlayStrip } from './components/PlayStrip';
import { Analyser } from './components/Analyser';
import { setNotePlaying, useNotePlaying } from './state/notePlaying';

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

/** Whether to name the shortcuts as a Mac does. */
const IS_MAC = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform);

/** Asks for an undo or a redo, and says on the status line what it was: the
    edit may have been on a tab that is not showing. Sent even when the page
    thinks there is nothing, so that a page out of step is put right by the
    answer. */
function perform(action: UndoAction): void {
    const { undo: undoName, redo: redoName } = currentHistory();
    const name = action === 'undo' ? undoName : redoName;

    if (action === 'undo') undo();
    else redo();

    if (name) announce(action === 'undo' ? `Undid ${name}.` : `Redid ${name}.`);
    else announce(action === 'undo' ? 'Nothing to undo.' : 'Nothing to redo.');
}

export function App(): JSX.Element {
    const workspaceRef = useRef<HTMLElement>(null);

    /** True once the first state snapshot has arrived. */
    const boundRef = useRef(false);

    const midi = useMidi();
    const assigned = useAssignedRoutings();
    const metadataVersion = useMetadataVersion();
    const presets = usePresets();
    const sampleRate = useSampleRate();
    const notePlaying = useNotePlaying();
    const history = useHistory();
    const interfaceScale = useInterfaceScale();

    /** What the connection established, for the readout's tooltip. */
    const [binding, setBinding] = useState('');

    // Which of the five tabs is showing (Phase 17). Not remembered: every
    // session opens on the oscillators, as every session opens on the designed
    // layout.
    const [section, setSection] = useState(0);
    const sectionsId = useId();
    const sectionsAnchor = useRef<HTMLDivElement>(null);

    // A new page starts at its top. If the presets have scrolled away, the
    // workspace goes back to where the tabs stick rather than to the top of
    // the presets; if they are still in view, nothing moves.
    const selectSection = useCallback((index: number): void => {
        setSection(index);

        const workspace = workspaceRef.current;
        const anchor = sectionsAnchor.current;
        if (workspace === null || anchor === null) return;

        // The boxes are in zoomed pixels and scrollTop is not (18b).
        const natural = (anchor.getBoundingClientRect().top - workspace.getBoundingClientRect().top)
            / viewportZoom() + workspace.scrollTop;

        if (workspace.scrollTop > natural) workspace.scrollTop = natural;
    }, []);

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
                setNotePlaying(message.notePlaying === true);

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

                // And for the macros' names, which travel the same way.
                requestMacroNames();

                // And for what can be undone, which the processor kept while
                // no window was open.
                requestHistory();
                requestInterfaceScale();

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

            case 'macroNames':
                applyMacroNames(message.names, message.maxLength);
                break;

            case 'history':
                applyHistory(message.undo, message.redo);
                break;

            case 'interfaceScale':
                applyInterfaceScale(message.scale, message.scales);
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

                // The field showed the name before the engine answered, so a
                // refusal has to put back what the engine actually holds.
                if (message.code === 'INVALID_MACRO_NAME') requestMacroNames();

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

    // Undo and redo from the keyboard, anywhere on the page but a text field,
    // which keeps them for its own letters (18a).
    useEffect(() => {
        const onKeyDown = (event: KeyboardEvent): void => {
            const target = event.target instanceof HTMLElement ? event.target : null;
            const action = undoActionOf(event, target && {
                tagName: target.tagName,
                type: target instanceof HTMLInputElement ? target.type : undefined,
                isContentEditable: target.isContentEditable,
            });

            if (!action) return;

            // The WebView's own undo would otherwise act on whatever it
            // thinks was last typed, somewhere.
            event.preventDefault();
            perform(action);
        };

        document.addEventListener('keydown', onKeyDown);
        return () => { document.removeEventListener('keydown', onKeyDown); };
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
                    Undo and redo, named in their tooltips after what they
                    would take back, since the edit may have been on another
                    tab. Not disabled when there is nothing, only marked so:
                    a disabled button shows no tooltip and says nothing when
                    pressed, and "Nothing to undo" is an answer.
                */}
                <button
                    className="masthead__button masthead__button--quiet"
                    type="button"
                    aria-disabled={!history.undo}
                    title={history.undo ? `Undo ${history.undo} (${shortcutLabel('undo', IS_MAC)})` : 'Nothing to undo'}
                    onClick={() => perform('undo')}
                >
                    Undo
                </button>
                <button
                    className="masthead__button masthead__button--quiet"
                    type="button"
                    aria-disabled={!history.redo}
                    title={history.redo ? `Redo ${history.redo} (${shortcutLabel('redo', IS_MAC)})` : 'Nothing to redo'}
                    onClick={() => perform('redo')}
                >
                    Redo
                </button>

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

                {/*
                    How large everything is drawn (18b). A native select, as the
                    matrix's are: keyboard- and screen-reader-correct as it
                    stands. Shows the size the engine last confirmed, so a choice
                    the engine has not taken is never shown as made.
                */}
                <label className="masthead__size" title={scaleTitle(interfaceScale.scale, interfaceScale.zoom)}>
                    <span>Size</span>
                    <select
                        className="select"
                        aria-label="Interface size"
                        value={String(interfaceScale.scale)}
                        onChange={(event) => setInterfaceScale(Number(event.target.value))}
                    >
                        {interfaceScale.scales.map((scale) => (
                            <option key={scale} value={String(scale)}>{scaleLabel(scale)}</option>
                        ))}
                    </select>
                </label>

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

                        {/*
                            The five tabs, and the strip of modulation sources
                            under them, which stays in view (Phase 17, ADR-0117).
                            The anchor marks where the bar sits before it sticks,
                            so a change of tab can return there.
                        */}
                        <div ref={sectionsAnchor} className="sections__anchor" aria-hidden="true" />
                        <SectionBar selected={section} onSelect={selectSection} idBase={sectionsId} />

                        <SectionPage index={0} selected={section} idBase={sectionsId}>
                            <div className="rank rank--pair">
                                <Oscillator index={1} />
                                <Oscillator index={2} />
                            </div>

                            <div className="rank rank--compact">
                                <SubOscillator />
                                <Noise />
                            </div>
                        </SectionPage>

                        <SectionPage index={1} selected={section} idBase={sectionsId}>
                            <div className="rank rank--pair">
                                <Filter index={1} />
                                <Filter index={2} />
                            </div>
                        </SectionPage>

                        <SectionPage index={2} selected={section} idBase={sectionsId}>
                            {/*
                                The envelopes and the LFOs side by side: both
                                are four of the same thing behind a tab strip,
                                and one above the other they were a page and a
                                half of scrolling.
                            */}
                            <div className="rank rank--pair">
                                <Envelopes />
                                <Lfos />
                            </div>
                            <div className="rank rank--wide"><Macros /></div>
                            <div className="rank rank--wide"><Matrix /></div>
                        </SectionPage>

                        <SectionPage index={3} selected={section} idBase={sectionsId}>
                            {/*
                                The rack first, because it says what order the
                                effects below it run in.
                            */}
                            <div className="rank rank--wide"><Rack /></div>

                            {/*
                                The effects in two columns that each fill from
                                the top, rather than in rows: an effect that is
                                not in the rack folds to its heading, and in a
                                row it would leave a hole the height of the
                                delay beside it.
                            */}
                            <div className="rank rank--flow">
                                <Distortion />
                                <DelayEffect />
                                <ReverbEffect />
                                <GateEffect />
                                <CompressorEffect />
                            </div>

                            {/*
                                The equaliser on its own row: it is a picture as
                                well as a set of controls, and a response curve
                                squeezed into half a row would be a curve nobody
                                could read a notch off.
                            */}
                            <div className="rank rank--wide"><EqualiserEffect /></div>
                        </SectionPage>

                        <SectionPage index={4} selected={section} idBase={sectionsId}>
                            <div className="rank rank--compact">
                                <Output />
                                <Expression />
                            </div>

                            {/*
                                The analyser on its own row, as the equaliser's
                                curve is: a frequency axis squeezed into half a
                                row is one nobody can read a note off (18d).
                            */}
                            <div className="rank rank--wide"><Analyser /></div>
                        </SectionPage>

                        <Unplaced workspace={workspaceRef} />
                    </>
                ) : null}
            </main>

            {/* A modulation source being carried to a control (13h-4). */}
            <ModCarryLayer />

            {/*
                The piano strip, in the standalone only (Phase 15): played with
                the computer keyboard or the mouse, for anyone without a MIDI
                keyboard. Between the instrument and the status line, so it
                never covers a control.
            */}
            {notePlaying && ready ? <PlayStrip /> : null}

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
    if (status === 'LOAD_FAILED' || status === 'SAVE_FAILED'
        || status === 'EXPORT_FAILED' || status === 'IMPORT_FAILED'
        || status === 'REVEAL_FAILED') return 'error';
    if (status === 'ALREADY_EXISTS' || status === 'PROGRAM_EMPTY') return 'caution';
    return 'info';
}
