/*
    What moves the sound: four DAHDSR envelopes and four LFOs, behind tab strips.
    Each envelope is drawn as a shape that can be dragged (Phase 13f-2), beside
    the knobs it moves another way.

    The tab is the one piece of genuinely local state on the page — nothing else
    needs to know which envelope is on screen — so it is `useState` and nothing
    more. In the hand-built page the same thing needed a closure over an array of
    buttons and a hidden flag on each page element.

    Every page stays mounted. That matters: a trace is a running picture of its
    own modulator, and unmounting the three that are not on screen would mean
    each one starting from an empty ring every time you looked at it.
*/

import { useId, useRef, useState } from 'react';

import { setMacroName } from '../bridge/bridge';
import { EnvelopeEditor } from '../components/EnvelopeEditor';
import { Knob } from '../components/Knob';
import { LfoCurveEditor } from '../components/LfoCurveEditor';
import { ModSourceHandle } from '../components/ModSource';
import { Module, Tabs, tabPanelProps } from '../components/Module';
import { Segmented } from '../components/Segmented';
import { Trace } from '../components/Trace';
import { LABELS } from '../params/labels';
import { defaultMacroName, limitMacroName, macroLabel, macroNameToSend } from '../params/macroNames';
import { depthText, routingsFromSource } from '../params/modulationRouting';
import type { SlotState } from '../params/modulationRouting';
import { setLocalMacroName, useDestinationLabels, useMacroNames } from '../state/macroNames';
import { plainOf, useParameterValue } from '../state/parameters';

const INDICES = [1, 2, 3, 4];

/** dsp::ModSource::follower. */
const FOLLOWER_SOURCE = 20;

export function Envelopes(): JSX.Element {
    const [selected, setSelected] = useState(0);
    const idBase = useId();

    return (
        <Module
            title="Envelope"
            index={String(selected + 1)}
            head={(
                <>
                {/* The envelope on show, as a source to drag (13h-4). */}
                <ModSourceHandle source={selected + 1} />
                <Tabs
                    count={4}
                    prefix="ENV "
                    selected={selected}
                    onSelect={setSelected}
                    label="Envelope"
                    idBase={idBase}
                />
                </>
            )}
        >
            {INDICES.map((index) => {
                const p = `env${index}_`;

                return (
                    <div
                        key={index}
                        className="cluster"
                        hidden={index !== selected + 1}
                        {...tabPanelProps(idBase, index - 1)}
                    >
                        <div className="cluster cluster--banner">
                            <Segmented
                                id={`${p}retrigger`}
                                label="Retrigger"
                                table={LABELS.envRetrigger}
                            />
                        </div>

                        <EnvelopeEditor envelope={index} />

                        <Trace source={`env${index}`} label={`env ${index}`} />

                        <Knob id={`${p}delay`} label="Delay" />
                        <Knob id={`${p}attack`} label="Attack" />
                        <Knob id={`${p}hold`} label="Hold" />
                        <Knob id={`${p}decay`} label="Decay" />
                        <Knob id={`${p}sustain`} label="Sustain" />
                        <Knob id={`${p}release`} label="Release" />
                        <Knob id={`${p}curve`} label="Curve" />
                        {/* Each stage's own bend, added to the curve (18f). */}
                        <Knob id={`${p}attack_bend`} label="A Bend" />
                        <Knob id={`${p}decay_bend`} label="D Bend" />
                        <Knob id={`${p}release_bend`} label="R Bend" />
                    </div>
                );
            })}
        </Module>
    );
}

/** `lfoN_sync`'s value when the rate is taken from the host's tempo. */
const SYNCED = 1;

/** `lfoN_shape`'s value for a drawn curve (Phase 13f-1). */
const DRAWN = 7;

function LfoPage({ index, hidden, idBase }: { index: number; hidden: boolean; idBase: string }): JSX.Element {
    const p = `lfo${index}_`;

    // Synced, the division replaces the rate rather than sitting beside a
    // control that does nothing. Both stay in the page and the one not in use
    // is hidden, because a control that is not rendered at all is reported as
    // having no home (Unplaced.tsx).
    useParameterValue(`${p}sync`);
    const synced = Math.round(plainOf(`${p}sync`)) === SYNCED;

    // The editor appears with the drawn shape. It is not a parameter, so
    // nothing reports it as unplaced when it is not rendered.
    useParameterValue(`${p}shape`);
    const drawn = Math.round(plainOf(`${p}shape`)) === DRAWN;

    return (
        <div className="cluster" hidden={hidden} {...tabPanelProps(idBase, index - 1)}>
            <div className="cluster cluster--banner">
                <div className="cluster">
                    <Segmented id={`${p}shape`} label="Shape" table={LABELS.lfoShape} />
                    <Segmented
                        id={`${p}retrigger`}
                        label="Trigger"
                        table={LABELS.lfoRetrigger}
                    />
                    <Segmented
                        id={`${p}polarity`}
                        label="Polarity"
                        table={LABELS.lfoPolarity}
                    />
                    <Segmented id={`${p}sync`} label="Rate" table={LABELS.lfoSync} />
                </div>

                <div className="cluster" hidden={!synced}>
                    <Segmented id={`${p}division`} label="Division" table={LABELS.lfoDivision} />
                </div>
            </div>

            {drawn && <LfoCurveEditor lfo={index} />}

            <Trace source={`lfo${index}`} label={`lfo ${index}`} />

            <div hidden={synced}>
                <Knob id={`${p}rate`} label="Rate" />
            </div>
            <Knob id={`${p}phase`} label="Phase" />
            <Knob id={`${p}fade`} label="Fade In" />
            <Knob id={`${p}smoothing`} label="Smoothing" />
            <Knob id={`${p}steps`} label="Steps" />
        </div>
    );
}

export function Lfos(): JSX.Element {
    const [selected, setSelected] = useState(0);
    const idBase = useId();

    return (
        <Module
            title="LFO"
            index={String(selected + 1)}
            head={(
                <>
                <ModSourceHandle source={selected + 5} />
                <Tabs
                    count={4}
                    prefix="LFO "
                    selected={selected}
                    onSelect={setSelected}
                    label="LFO"
                    idBase={idBase}
                />
                </>
            )}
        >
            {INDICES.map((index) => (
                <LfoPage key={index} index={index} hidden={index !== selected + 1} idBase={idBase} />
            ))}
        </Module>
    );
}

/**
    A macro's name, as a field that reads as a heading until it is clicked
    (Phase 16c). Enter or leaving the field keeps what was typed; Escape puts
    the name back as it was; an empty field gives the macro its number back.
    What is kept is the engine's answer, which arrives as the macro's names.
*/
function MacroName({ macro }: { macro: number }): JSX.Element {
    const { names, maxLength } = useMacroNames();
    const stored = names[macro - 1] ?? '';
    const shown = macroLabel(macro, stored);

    const [draft, setDraft] = useState<string | null>(null);
    const cancelled = useRef(false);

    const finish = (typed: string): void => {
        setDraft(null);

        if (cancelled.current) {
            cancelled.current = false;
            return;
        }

        const name = macroNameToSend(macro, typed);
        if (name === stored) return;

        setLocalMacroName(macro, name);
        setMacroName(macro, name);
    };

    return (
        <label className="macro__name" data-named={stored !== '' ? 'true' : 'false'}>
            <input
                type="text"
                spellCheck={false}
                autoComplete="off"
                aria-label={`${defaultMacroName(macro)} name`}
                title={`Rename ${defaultMacroName(macro)}: up to ${maxLength} characters. Empty gives it its number back.`}
                value={draft ?? shown}
                onFocus={(event) => {
                    setDraft(shown);

                    // After the click that focused it has placed its caret,
                    // so the whole name is selected and typing replaces it.
                    const input = event.currentTarget;
                    requestAnimationFrame(() => input.select());
                }}
                onChange={(event) => setDraft(limitMacroName(event.currentTarget.value, maxLength))}
                onKeyDown={(event) => {
                    if (event.key === 'Enter') {
                        event.preventDefault();
                        event.currentTarget.blur();
                    } else if (event.key === 'Escape') {
                        event.preventDefault();
                        cancelled.current = true;
                        event.currentTarget.blur();
                    }
                }}
                onBlur={(event) => finish(event.currentTarget.value)}
            />
            <span className="macro__pencil" aria-hidden="true">✎</span>
        </label>
    );
}

/**
    The four macros. Each is a knob a performer moves — or a controller does,
    once MIDI Learn has assigned one; none is assigned by default — and the
    range and curve between that knob and what the matrix reads. Where a macro
    goes, and how far, is the matrix's: route "Macro 1" to as many destinations
    as it should reach, each with its own depth.
*/
/*
    The envelope follower (Phase 18e, ADR-0123): how loud the instrument is, as
    a source. Its trace is the instrument's rather than a note's, so it moves
    with chords and falls away after the last one; the handle in the heading is
    the one in the strip under the tabs, for dragging onto a control from here.
*/
export function Follower(): JSX.Element {
    return (
        <Module title="Follower" head={<ModSourceHandle source={FOLLOWER_SOURCE} />}>
            <Trace source="follow" label="follow" />
            <Knob id="follower_attack" label="Attack" />
            <Knob id="follower_release" label="Release" />
            <Knob id="follower_gain" label="Gain" />
        </Module>
    );
}

/** The matrix's sixteen slots, as the parameters that hold them. */
const SLOT_PARTS = Array.from({ length: 16 }, (_, index) => {
    const prefix = `mod${String(index + 1).padStart(2, '0')}`;
    return { source: `${prefix}_source`, destination: `${prefix}_destination`, depth: `${prefix}_depth` };
});

/**
    What one macro drives (Phase 19c): every routing from it, by the name of the
    control it moves and how far, in the order of the matrix's rows.

    A macro reaching several controls was always possible — drag its handle onto
    each — but nothing on the macro said so, and the first feedback on 1.4.0
    read it as a macro being limited to one. So the macro lists them, and says
    how to add another when it drives nothing.
*/
function MacroTargets({ macro }: { macro: number }): JSX.Element {
    // Every slot, so the list follows a routing added, moved or removed
    // anywhere: from a drop, a tag's menu or the matrix itself.
    for (const part of SLOT_PARTS) {
        useParameterValue(part.source);
        useParameterValue(part.destination);
        useParameterValue(part.depth);
    }

    const destinations = useDestinationLabels();

    const slots: SlotState[] = SLOT_PARTS.map((part) => ({
        source: plainOf(part.source),
        destination: plainOf(part.destination),
        depth: plainOf(part.depth),
    }));

    const routes = routingsFromSource(slots, 15 + macro);

    if (routes.length === 0) {
        return (
            <p className="macro__targets macro__targets--empty">
                Drives nothing yet. Drag its handle onto any control outlined in gold, as many as you like.
            </p>
        );
    }

    return (
        <ul className="macro__targets" aria-label={`What ${defaultMacroName(macro)} drives`}>
            {routes.map((route) => (
                <li
                    key={route.slot}
                    className="macro__target"
                    data-live={route.live ? 'true' : 'false'}
                    title={`Slot ${route.slot} of the matrix`}
                >
                    <span className="macro__target-name">
                        {destinations[route.destination] ?? `Destination ${route.destination}`}
                    </span>
                    <span className="macro__target-depth">{route.live ? depthText(route.depth) : 'no depth'}</span>
                </li>
            ))}
        </ul>
    );
}

export function Macros(): JSX.Element {
    return (
        <Module
            title="Macros"
            head={INDICES.map((index) => <ModSourceHandle key={index} source={15 + index} />)}
        >
            {INDICES.map((index) => {
                const p = `macro${index}`;

                return (
                    <div key={index} className="macro" role="group" aria-label={`Macro ${index}`}>
                        <MacroName macro={index} />
                        <Knob id={`${p}_value`} label="Amount" />
                        <Knob id={`${p}_min`} label="Min" />
                        <Knob id={`${p}_max`} label="Max" />
                        <Knob id={`${p}_curve`} label="Curve" />
                        <MacroTargets macro={index} />
                    </div>
                );
            })}
        </Module>
    );
}
