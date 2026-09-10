/*
    Apollo — interface behaviour.

    Two principles run through this file.

    First, the native side owns the truth. Every range, default, step and skew
    comes from the parameter metadata the engine sends; this file never states
    what a filter cutoff's range is, because the day someone changes it in
    ParameterDefinitions.h the UI must follow without being edited
    (UI_BINDINGS.md §16). What this file does own is presentation: which module
    a control belongs to, what a discrete value is called, and how a number is
    written out.

    Second, nothing re-renders. Controls are built once when metadata arrives
    and afterwards only their own attributes change, so a value arriving at
    30 Hz from the engine costs one text node and one path attribute rather
    than a rebuild of the page.
*/

'use strict';

const PROTOCOL_VERSION = 1;

/* ==========================================================================
   BRIDGE
   ========================================================================== */

const bridge = (() => {
    const available = typeof window.__JUCE__ !== 'undefined'
        && window.__JUCE__.backend !== undefined;

    return {
        available,

        send (message) {
            if (!available) return;

            // Sent as a JSON string rather than an object so the native side
            // validates exactly the bytes this page produced, with no
            // intermediate var conversion.
            window.__JUCE__.backend.emitEvent('apolloCommand', JSON.stringify(message));
        },

        listen (handler) {
            if (!available) return;

            window.__JUCE__.backend.addEventListener('apolloMessage', (payload) => {
                try {
                    handler(typeof payload === 'string' ? JSON.parse(payload) : payload);
                } catch (error) {
                    // A malformed frame is dropped. The UI staying up with a
                    // stale value beats it going blank (CLAUDE.md §33).
                }
            });
        }
    };
})();

function setParameter (id, normalized) {
    bridge.send({ type: 'setParameter', version: PROTOCOL_VERSION, id, normalizedValue: normalized });
}

function gesture (id, state) {
    bridge.send({ type: 'gesture', version: PROTOCOL_VERSION, id, state });
}

/* ==========================================================================
   VALUE MAPPING

   Mirrors juce::NormalisableRange, which is what the native side applies. The
   two must agree or a knob will show one number while the engine holds
   another.
   ========================================================================== */

function skewOf (definition) {
    // A zero or missing skew would make the mapping undefined rather than
    // linear, so it is treated as linear.
    return definition.skew > 0 ? definition.skew : 1;
}

function toPlain (definition, normalized) {
    const skew = skewOf(definition);
    const t = skew === 1 ? normalized : Math.pow(normalized, 1 / skew);

    let plain = definition.min + (definition.max - definition.min) * t;

    if (definition.step > 0)
        plain = definition.min + Math.round((plain - definition.min) / definition.step) * definition.step;

    return plain;
}

function toNormalized (definition, plain) {
    const clamped = Math.min(definition.max, Math.max(definition.min, plain));
    const span = definition.max - definition.min;

    if (span <= 0) return 0;

    const t = (clamped - definition.min) / span;
    const skew = skewOf(definition);

    return skew === 1 ? t : Math.pow(t, skew);
}

/** How many discrete positions a stepped parameter has, or 0 if continuous. */
function stepCount (definition) {
    if (!(definition.step > 0)) return 0;

    return Math.round((definition.max - definition.min) / definition.step) + 1;
}

/* ==========================================================================
   FORMATTING

   The unit arrives as an enum precisely so this layer can decide how to write
   it. 20000 Hz reads as "20.00 kHz" and 1500 ms as "1.50 s", because a synth
   that makes you count zeroes is a synth you misread at 2am.
   ========================================================================== */

function formatPlain (definition, plain) {
    switch (definition.unit) {
        case 'Hz':
            return plain >= 1000
                ? (plain / 1000).toFixed(2) + ' kHz'
                : plain.toFixed(plain < 10 ? 2 : 1) + ' Hz';

        case 'ms':
            return plain >= 1000
                ? (plain / 1000).toFixed(2) + ' s'
                : plain.toFixed(plain < 10 ? 1 : 0) + ' ms';

        case 'dB':
            return (plain > 0 ? '+' : '') + plain.toFixed(1) + ' dB';

        case '%':
            return Math.round(plain * 100) + ' %';

        case 'voices':
            return String(Math.round(plain));

        case 'st':
            return (plain > 0 ? '+' : '') + Math.round(plain) + ' st';

        case 'cents':
            return (plain > 0 ? '+' : '') + plain.toFixed(0) + ' c';

        case 'oct':
            return (plain > 0 ? '+' : '') + Math.round(plain);

        default:
            if (definition.step >= 1) return String(Math.round(plain));
            return plain.toFixed(2);
    }
}

/* ==========================================================================
   ENUMERATION LABELS

   The metadata carries a discrete parameter's range but not the names of its
   positions, because those are presentation. They live here — and are used
   only when the table length matches the range the engine reported, so a list
   that falls out of step with the enum shows honest numbers instead of
   confidently wrong words.
   ========================================================================== */

const LABELS = {
    // 0..3, in the order WavetableLibrary builds them.
    osc1_wavetable: ['SIN→SAW', 'SIN→SQR', 'TRI→SAW', 'SAW→SQR'],
    osc2_wavetable: ['SIN→SAW', 'SIN→SQR', 'TRI→SAW', 'SAW→SQR'],

    sub_octave: ['-2', '-1'],

    // 0 off, 1 lowpass, 2 highpass, 3 bandpass, 4 notch.
    filter1_type: ['OFF', 'LP', 'HP', 'BP', 'NOTCH'],
    filter2_type: ['OFF', 'LP', 'HP', 'BP', 'NOTCH'],
    filter_routing: ['SERIES', 'PARALLEL'],

    // Matches dsp::LfoShape.
    lfoShape: ['SINE', 'TRI', 'SAW', 'RSAW', 'SQR', 'S&H', 'STEP'],
    lfoRetrigger: ['FREE', 'RETRIG'],
    lfoPolarity: ['UNI', 'BI'],

    // Matches dsp::ModSource.
    modSource: ['—', 'ENV 1', 'ENV 2', 'ENV 3', 'ENV 4',
                'LFO 1', 'LFO 2', 'LFO 3', 'LFO 4',
                'Velocity', 'Key Track', 'Mod Wheel', 'Pitch Bend', 'Aftertouch', 'Random'],

    // Matches dsp::ModDestination.
    modDestination: ['—', 'All Pitch', 'Osc 1 Pitch', 'Osc 2 Pitch',
                     'Osc 1 Position', 'Osc 2 Position',
                     'Osc 1 Level', 'Osc 2 Level', 'Osc 1 Pan', 'Osc 2 Pan',
                     'Sub Level', 'Noise Level',
                     'Filter 1 Cutoff', 'Filter 1 Resonance',
                     'Filter 2 Cutoff', 'Filter 2 Resonance',
                     'Amplitude']
};

/** Which knob a modulation destination points at, so an assigned routing can
    light up the control it actually moves. Destinations with no single
    parameter of their own — All Pitch, Osc 1 Pitch, voice Amplitude — are
    absent rather than guessed at.
*/
const DESTINATION_PARAMETER = {
    3: 'osc2_semitones',
    4: 'osc1_position',
    5: 'osc2_position',
    6: 'osc1_level',
    7: 'osc2_level',
    8: 'osc1_pan',
    9: 'osc2_pan',
    10: 'sub_level',
    11: 'noise_level',
    12: 'filter1_cutoff',
    13: 'filter1_resonance',
    14: 'filter2_cutoff',
    15: 'filter2_resonance'
};

function labelsFor (definition, table) {
    if (!table) return null;

    // Only trusted when it lines up exactly with the range the engine sent.
    return table.length === stepCount(definition) ? table : null;
}

/* ==========================================================================
   STATE
   ========================================================================== */

const definitions = new Map();   // id -> definition
const values = new Map();        // id -> normalised value
const controls = new Map();      // id -> [control, ...]
const held = new Set();          // ids currently under the pointer
const watchers = [];             // run after any value change

function valueOf (id) {
    return values.has(id) ? values.get(id) : 0;
}

function plainOf (id) {
    const definition = definitions.get(id);
    return definition ? toPlain(definition, valueOf(id)) : 0;
}

/** Applies a value to every control bound to it. */
function apply (id, normalized) {
    values.set(id, normalized);

    const bound = controls.get(id);
    if (bound) for (const control of bound) control.update(normalized);

    for (const watcher of watchers) watcher(id);
}

/** Applies a value locally and pushes it to the engine. */
function commit (id, normalized) {
    const clamped = Math.min(1, Math.max(0, normalized));

    // Applied here as well as sent, so the control tracks the hand at pointer
    // rate rather than at the engine's coalesced echo rate. The echo still
    // arrives and still wins; this only removes the lag before it does.
    apply(id, clamped);
    setParameter(id, clamped);
}

function bind (id, control) {
    if (!controls.has(id)) controls.set(id, []);
    controls.get(id).push(control);

    // Marked here rather than in each control constructor, so a control type
    // added later is assignable without anyone having to remember this.
    if (control.element) control.element.dataset.midiId = id;
}

/* ==========================================================================
   DOM HELPERS
   ========================================================================== */

function make (tag, className, parent) {
    const element = document.createElement(tag);
    if (className) element.className = className;
    if (parent) parent.append(element);
    return element;
}

function makeSvg (tag, parent) {
    const element = document.createElementNS('http://www.w3.org/2000/svg', tag);
    if (parent) parent.append(element);
    return element;
}

/* ==========================================================================
   KNOB

   270 degrees of travel, drag-vertical. Fine adjustment on shift, because the
   alternative — a control you cannot place accurately — is the single most
   common complaint about software synthesisers.
   ========================================================================== */

const KNOB_START_ANGLE = -135;
const KNOB_END_ANGLE = 135;
const KNOB_RADIUS = 18;
const KNOB_CENTRE = 23;

/** Full travel in pixels of vertical drag. */
const KNOB_DRAG_RANGE = 190;

function polar (radius, degrees) {
    const radians = (degrees - 90) * Math.PI / 180;
    return [KNOB_CENTRE + radius * Math.cos(radians), KNOB_CENTRE + radius * Math.sin(radians)];
}

function arcPath (from, to) {
    if (Math.abs(to - from) < 0.15) return '';

    const [x0, y0] = polar(KNOB_RADIUS, from);
    const [x1, y1] = polar(KNOB_RADIUS, to);
    const large = Math.abs(to - from) > 180 ? 1 : 0;
    const sweep = to > from ? 1 : 0;

    return 'M ' + x0.toFixed(2) + ' ' + y0.toFixed(2)
         + ' A ' + KNOB_RADIUS + ' ' + KNOB_RADIUS + ' 0 ' + large + ' ' + sweep
         + ' ' + x1.toFixed(2) + ' ' + y1.toFixed(2);
}

function createKnob (id, labelText) {
    const definition = definitions.get(id);
    if (!definition) return null;

    // A parameter whose range crosses zero fills outward from the centre, so a
    // detune of zero looks like a centred control rather than a quarter turn.
    const bipolar = definition.min < 0 && definition.max > 0;
    const originAngle = bipolar
        ? KNOB_START_ANGLE + (KNOB_END_ANGLE - KNOB_START_ANGLE) * toNormalized(definition, 0)
        : KNOB_START_ANGLE;

    const root = make('div', 'knob');
    root.tabIndex = 0;
    root.setAttribute('role', 'slider');
    root.setAttribute('aria-label', definition.name);
    root.setAttribute('aria-valuemin', String(definition.min));
    root.setAttribute('aria-valuemax', String(definition.max));
    root.title = definition.name;

    const svg = makeSvg('svg', root);
    svg.setAttribute('class', 'knob__dial');
    svg.setAttribute('viewBox', '0 0 46 46');
    svg.setAttribute('aria-hidden', 'true');

    const track = makeSvg('path', svg);
    track.setAttribute('class', 'knob__track');
    track.setAttribute('d', arcPath(KNOB_START_ANGLE, KNOB_END_ANGLE));

    const arc = makeSvg('path', svg);
    arc.setAttribute('class', 'knob__arc');

    const cap = makeSvg('circle', svg);
    cap.setAttribute('class', 'knob__cap');
    cap.setAttribute('cx', String(KNOB_CENTRE));
    cap.setAttribute('cy', String(KNOB_CENTRE));
    cap.setAttribute('r', '11.5');

    const pointer = makeSvg('line', svg);
    pointer.setAttribute('class', 'knob__pointer');

    const value = make('div', 'knob__value', root);
    const label = make('div', 'knob__label', root);
    label.textContent = labelText;

    const control = {
        element: root,

        update (normalized) {
            const plain = toPlain(definition, normalized);
            const angle = KNOB_START_ANGLE + (KNOB_END_ANGLE - KNOB_START_ANGLE) * normalized;

            arc.setAttribute('d', arcPath(Math.min(originAngle, angle), Math.max(originAngle, angle)));

            const inner = polar(6, angle);
            const outer = polar(11.5, angle);
            pointer.setAttribute('x1', inner[0].toFixed(2));
            pointer.setAttribute('y1', inner[1].toFixed(2));
            pointer.setAttribute('x2', outer[0].toFixed(2));
            pointer.setAttribute('y2', outer[1].toFixed(2));

            const text = formatPlain(definition, plain);
            value.textContent = text;

            root.setAttribute('aria-valuenow', plain.toFixed(4));
            root.setAttribute('aria-valuetext', text);
        }
    };

    // --- Pointer ---------------------------------------------------------
    let dragFrom = 0;
    let dragValue = 0;

    root.addEventListener('pointerdown', (event) => {
        if (event.button !== 0) return;

        root.setPointerCapture(event.pointerId);
        root.dataset.held = 'true';
        held.add(id);

        dragFrom = event.clientY;
        dragValue = valueOf(id);

        gesture(id, 'begin');
        event.preventDefault();
    });

    root.addEventListener('pointermove', (event) => {
        if (root.dataset.held !== 'true') return;

        const scale = event.shiftKey ? 0.2 : 1;
        const delta = (dragFrom - event.clientY) / KNOB_DRAG_RANGE * scale;

        commit(id, dragValue + delta);
    });

    const release = (event) => {
        if (root.dataset.held !== 'true') return;

        root.dataset.held = 'false';
        held.delete(id);

        try { root.releasePointerCapture(event.pointerId); } catch (error) { /* already released */ }

        gesture(id, 'end');
    };

    root.addEventListener('pointerup', release);
    root.addEventListener('pointercancel', release);

    // --- Reset -----------------------------------------------------------
    root.addEventListener('dblclick', () => {
        gesture(id, 'begin');
        commit(id, toNormalized(definition, definition.default));
        gesture(id, 'end');
    });

    // --- Keyboard --------------------------------------------------------
    // A stepped parameter moves one position per press; a continuous one moves
    // in hundredths, which is fine enough to be useful and coarse enough to
    // cross the range without holding the key down for a minute.
    root.addEventListener('keydown', (event) => {
        const steps = stepCount(definition);
        const unit = steps > 1 ? 1 / (steps - 1) : 0.01;

        let next = null;

        switch (event.key) {
            case 'ArrowUp':
            case 'ArrowRight': next = valueOf(id) + unit * (event.shiftKey ? 0.2 : 1); break;
            case 'ArrowDown':
            case 'ArrowLeft':  next = valueOf(id) - unit * (event.shiftKey ? 0.2 : 1); break;
            case 'PageUp':     next = valueOf(id) + unit * 10; break;
            case 'PageDown':   next = valueOf(id) - unit * 10; break;
            case 'Home':       next = 0; break;
            case 'End':        next = 1; break;

            // The keyboard equivalent of double-clicking. Without it the
            // extremes are reachable from the keyboard but the default is not,
            // which leaves anyone not using a mouse unable to undo a nudge
            // (CLAUDE.md §39).
            case 'Delete':
            case 'Backspace':  next = toNormalized(definition, definition.default); break;

            default: return;
        }

        event.preventDefault();
        gesture(id, 'begin');
        commit(id, next);
        gesture(id, 'end');
    });

    bind(id, control);
    return root;
}

/* ==========================================================================
   SEGMENTED CHOICE
   ========================================================================== */

function createSegmented (id, labelText, table) {
    const definition = definitions.get(id);
    if (!definition) return null;

    const steps = stepCount(definition);
    if (steps < 2) return null;

    const names = labelsFor(definition, table);

    const root = make('div', 'segmented');

    const label = make('div', 'segmented__label', root);
    label.textContent = labelText;

    const options = make('div', 'segmented__options', root);
    options.setAttribute('role', 'radiogroup');
    options.setAttribute('aria-label', definition.name);

    const buttons = [];

    for (let index = 0; index < steps; ++index) {
        const button = make('button', 'segmented__option', options);
        button.type = 'button';
        button.setAttribute('role', 'radio');
        button.setAttribute('aria-checked', 'false');
        button.textContent = names ? names[index] : String(definition.min + index * definition.step);

        button.addEventListener('click', () => {
            gesture(id, 'begin');
            commit(id, index / (steps - 1));
            gesture(id, 'end');
        });

        buttons.push(button);
    }

    bind(id, {
        element: root,

        update (normalized) {
            const selected = Math.round(normalized * (steps - 1));

            buttons.forEach((button, index) =>
                button.setAttribute('aria-checked', index === selected ? 'true' : 'false'));
        }
    });

    return root;
}

/* ==========================================================================
   SELECT
   For the matrix's fifteen sources and seventeen destinations, where a
   segmented control would be a wall.
   ========================================================================== */

function createSelect (id, table, ariaLabel) {
    const definition = definitions.get(id);
    if (!definition) return null;

    const steps = stepCount(definition);
    if (steps < 2) return null;

    const names = labelsFor(definition, table);

    const select = make('select', 'select');
    select.setAttribute('aria-label', ariaLabel || definition.name);

    for (let index = 0; index < steps; ++index) {
        const option = make('option', null, select);
        option.value = String(index);
        option.textContent = names ? names[index] : String(index);
    }

    select.addEventListener('change', () => {
        gesture(id, 'begin');
        commit(id, Number(select.value) / (steps - 1));
        gesture(id, 'end');
    });

    bind(id, {
        element: select,

        update (normalized) {
            select.value = String(Math.round(normalized * (steps - 1)));
        }
    });

    return select;
}

/* ==========================================================================
   BIPOLAR RAIL
   The matrix depth control. Fills outward from centre so the sign of a routing
   is a direction rather than a minus sign to be noticed.
   ========================================================================== */

function createBipolar (id) {
    const definition = definitions.get(id);
    if (!definition) return null;

    const root = make('div', 'bipolar');

    const rail = make('div', 'bipolar__rail', root);
    rail.tabIndex = 0;
    rail.setAttribute('role', 'slider');
    rail.setAttribute('aria-label', definition.name);
    rail.setAttribute('aria-valuemin', String(definition.min));
    rail.setAttribute('aria-valuemax', String(definition.max));

    make('div', 'bipolar__centre', rail);
    const fill = make('div', 'bipolar__fill', rail);

    const value = make('div', 'bipolar__value', root);

    bind(id, {
        element: root,

        update (normalized) {
            const plain = toPlain(definition, normalized);
            const fraction = Math.min(1, Math.max(0, normalized));
            const negative = fraction < 0.5;

            rail.dataset.negative = negative ? 'true' : 'false';
            fill.style.left = (negative ? fraction * 100 : 50) + '%';
            fill.style.width = Math.abs(fraction - 0.5) * 100 + '%';

            const text = (plain > 0 ? '+' : '') + (plain * 100).toFixed(0) + ' %';
            value.textContent = text;

            rail.setAttribute('aria-valuenow', plain.toFixed(3));
            rail.setAttribute('aria-valuetext', text);
        }
    });

    let dragFrom = 0;
    let dragValue = 0;

    rail.addEventListener('pointerdown', (event) => {
        if (event.button !== 0) return;

        rail.setPointerCapture(event.pointerId);
        rail.dataset.held = 'true';
        held.add(id);

        dragFrom = event.clientX;
        dragValue = valueOf(id);

        gesture(id, 'begin');
        event.preventDefault();
    });

    rail.addEventListener('pointermove', (event) => {
        if (rail.dataset.held !== 'true') return;

        const scale = event.shiftKey ? 0.2 : 1;
        const width = rail.clientWidth > 0 ? rail.clientWidth : 1;

        commit(id, dragValue + (event.clientX - dragFrom) / width * scale);
    });

    const release = (event) => {
        if (rail.dataset.held !== 'true') return;

        rail.dataset.held = 'false';
        held.delete(id);

        try { rail.releasePointerCapture(event.pointerId); } catch (error) { /* already released */ }

        gesture(id, 'end');
    };

    rail.addEventListener('pointerup', release);
    rail.addEventListener('pointercancel', release);

    rail.addEventListener('dblclick', () => {
        gesture(id, 'begin');
        commit(id, toNormalized(definition, definition.default));
        gesture(id, 'end');
    });

    rail.addEventListener('keydown', (event) => {
        let next = null;

        switch (event.key) {
            case 'ArrowRight': next = valueOf(id) + (event.shiftKey ? 0.002 : 0.01); break;
            case 'ArrowLeft':  next = valueOf(id) - (event.shiftKey ? 0.002 : 0.01); break;
            case 'Home':       next = 0; break;
            case 'End':        next = 1; break;

            // Reaches the centre, which is the one value a depth control needs
            // most and the one Home and End cannot reach.
            case 'Delete':
            case 'Backspace':  next = toNormalized(definition, definition.default); break;

            default: return;
        }

        event.preventDefault();
        gesture(id, 'begin');
        commit(id, next);
        gesture(id, 'end');
    });

    return root;
}

/* ==========================================================================
   MODULE FRAME
   ========================================================================== */

function createModule (title, index) {
    const root = make('div', 'module');

    const head = make('div', 'module__head', root);

    const heading = make('div', 'module__title', head);
    heading.textContent = title;

    if (index !== null && index !== undefined) {
        const badge = make('span', 'module__index', head);
        badge.textContent = index;
    }

    make('div', 'module__spacer', head);

    const body = make('div', 'module__body', root);

    return { root, head, body };
}

/** Dims a module when the thing it controls is not in the signal path.

    Dimming rather than hiding: a user needs to see that the sub oscillator is
    at zero, not to find that its controls have vanished.
*/
function dimWhen (module, id, predicate) {
    watchers.push((changed) => {
        if (changed !== id) return;

        module.root.dataset.inactive = predicate(plainOf(id)) ? 'true' : 'false';
    });
}

/* ==========================================================================
   TAB STRIP

   Four envelopes and four LFOs are sixty controls. Shown at once they are a
   wall; behind a tab strip they are one instrument section that happens to
   have four of everything, which is how they are used.
   ========================================================================== */

function createTabs (head, count, prefix, onSelect) {
    const strip = make('div', 'segmented__options', head);
    strip.setAttribute('role', 'tablist');
    strip.style.marginLeft = 'auto';

    const buttons = [];

    for (let index = 0; index < count; ++index) {
        const button = make('button', 'segmented__option', strip);
        button.type = 'button';
        button.setAttribute('role', 'tab');
        button.setAttribute('aria-selected', index === 0 ? 'true' : 'false');
        button.setAttribute('aria-checked', index === 0 ? 'true' : 'false');
        button.textContent = prefix + (index + 1);

        button.addEventListener('click', () => {
            buttons.forEach((other, otherIndex) => {
                const selected = otherIndex === index;
                other.setAttribute('aria-selected', selected ? 'true' : 'false');
                other.setAttribute('aria-checked', selected ? 'true' : 'false');
            });

            onSelect(index);
        });

        buttons.push(button);
    }

    return buttons;
}

/* ==========================================================================
   LAYOUT

   Ordered by signal flow: what makes the sound, then what shapes it, then what
   moves it, then what leaves. A parameter list sorted alphabetically is a
   database; this is an instrument.
   ========================================================================== */

const placed = new Set();

function knob (body, id, labelText) {
    const element = createKnob(id, labelText);

    if (element) {
        body.append(element);
        placed.add(id);
    }
}

function segmented (body, id, labelText, table) {
    const element = createSegmented(id, labelText, table);

    if (element) {
        body.append(element);
        placed.add(id);
    }
}

function buildOscillator (rank, index) {
    const p = 'osc' + index + '_';
    const module = createModule('Oscillator', String(index));
    rank.append(module.root);

    const banner = make('div', 'cluster cluster--banner', module.body);
    segmented(banner, p + 'wavetable', 'Wavetable', LABELS[p + 'wavetable']);

    knob(module.body, p + 'position', 'Position');
    knob(module.body, p + 'level', 'Level');
    knob(module.body, p + 'pan', 'Pan');
    knob(module.body, p + 'unison', 'Unison');
    knob(module.body, p + 'detune', 'Detune');
    knob(module.body, p + 'spread', 'Spread');

    if (index === 2) {
        knob(module.body, 'osc2_semitones', 'Semitones');
        knob(module.body, 'osc2_fine', 'Fine');
    }

    dimWhen(module, p + 'level', (level) => level <= 0.0001);
}

function buildSubAndNoise (rank) {
    const sub = createModule('Sub Oscillator', null);
    rank.append(sub.root);
    knob(sub.body, 'sub_level', 'Level');
    segmented(sub.body, 'sub_octave', 'Octave', LABELS.sub_octave);
    dimWhen(sub, 'sub_level', (level) => level <= 0.0001);

    const noise = createModule('Noise', null);
    rank.append(noise.root);
    knob(noise.body, 'noise_level', 'Level');
    dimWhen(noise, 'noise_level', (level) => level <= 0.0001);
}

function buildFilter (rank, index) {
    const p = 'filter' + index + '_';
    const module = createModule('Filter', String(index));
    rank.append(module.root);

    const banner = make('div', 'cluster cluster--banner', module.body);

    const modes = make('div', 'cluster', banner);
    segmented(modes, p + 'type', 'Type', LABELS[p + 'type']);

    // Routing lives on filter 2 because it describes how the pair is
    // connected, and filter 2 is where the pair becomes visible.
    if (index === 2)
        segmented(modes, 'filter_routing', 'Routing', LABELS.filter_routing);

    knob(module.body, p + 'cutoff', 'Cutoff');
    knob(module.body, p + 'resonance', 'Resonance');
    knob(module.body, p + 'drive', 'Drive');

    // Type 0 is Off, which is the filter's own bypass rather than a separate
    // enable, so the module follows it exactly.
    dimWhen(module, p + 'type', (type) => Math.round(type) === 0);
}

function buildEnvelopes (rank) {
    const module = createModule('Envelope', '1');
    rank.append(module.root);

    const pages = [];

    for (let index = 1; index <= 4; ++index) {
        const page = make('div', 'cluster', module.body);
        const p = 'env' + index + '_';

        knob(page, p + 'delay', 'Delay');
        knob(page, p + 'attack', 'Attack');
        knob(page, p + 'hold', 'Hold');
        knob(page, p + 'decay', 'Decay');
        knob(page, p + 'sustain', 'Sustain');
        knob(page, p + 'release', 'Release');
        knob(page, p + 'curve', 'Curve');

        page.hidden = index !== 1;
        pages.push(page);
    }

    const badge = module.head.querySelector('.module__index');

    createTabs(module.head, 4, 'ENV ', (index) => {
        pages.forEach((page, pageIndex) => { page.hidden = pageIndex !== index; });
        badge.textContent = String(index + 1);
    });
}

function buildLfos (rank) {
    const module = createModule('LFO', '1');
    rank.append(module.root);

    const pages = [];

    for (let index = 1; index <= 4; ++index) {
        const page = make('div', 'cluster', module.body);
        const p = 'lfo' + index + '_';

        const banner = make('div', 'cluster cluster--banner', page);

        const switches = make('div', 'cluster', banner);
        segmented(switches, p + 'shape', 'Shape', LABELS.lfoShape);
        segmented(switches, p + 'retrigger', 'Trigger', LABELS.lfoRetrigger);
        segmented(switches, p + 'polarity', 'Polarity', LABELS.lfoPolarity);

        knob(page, p + 'rate', 'Rate');
        knob(page, p + 'phase', 'Phase');
        knob(page, p + 'fade', 'Fade In');
        knob(page, p + 'smoothing', 'Smoothing');
        knob(page, p + 'steps', 'Steps');

        page.hidden = index !== 1;
        pages.push(page);
    }

    const badge = module.head.querySelector('.module__index');

    createTabs(module.head, 4, 'LFO ', (index) => {
        pages.forEach((page, pageIndex) => { page.hidden = pageIndex !== index; });
        badge.textContent = String(index + 1);
    });
}

function buildMatrix (rank) {
    const module = createModule('Modulation Matrix', null);
    rank.append(module.root);
    module.body.style.display = 'block';

    const banks = make('div', 'matrix-banks', module.body);
    const rows = [];

    // Two banks of eight rather than one column of sixteen: a routing should
    // read as a single line from source to depth, and across a wide window one
    // table puts half a screen between the two ends of the same statement.
    for (let bank = 0; bank < 2; ++bank) {
        const table = make('table', 'matrix', banks);

        const headRow = make('tr', null, make('thead', null, table));

        const columns = [
            { title: '', className: 'matrix__slot' },
            { title: 'Source', className: 'matrix__source' },
            { title: '', className: 'matrix__arrow' },
            { title: 'Destination', className: 'matrix__destination' },
            { title: 'Depth', className: 'matrix__depth' }
        ];

        for (const column of columns) {
            const cell = make('th', column.className, headRow);
            cell.textContent = column.title;
        }

        const body = make('tbody', null, table);

        for (let offset = 1; offset <= 8; ++offset) {
            const slot = bank * 8 + offset;
            const index = String(slot).padStart(2, '0');
            const sourceId = 'mod' + index + '_source';
            const destinationId = 'mod' + index + '_destination';
            const depthId = 'mod' + index + '_depth';

            const row = make('tr', null, body);

            const number = make('td', 'matrix__slot', row);
            number.textContent = index;

            const sourceCell = make('td', 'matrix__source', row);
            const sourceSelect = createSelect(sourceId, LABELS.modSource, 'Slot ' + slot + ' source');
            if (sourceSelect) { sourceCell.append(sourceSelect); placed.add(sourceId); }

            const arrow = make('td', 'matrix__arrow', row);
            arrow.textContent = '→';

            const destinationCell = make('td', 'matrix__destination', row);
            const destinationSelect = createSelect(destinationId, LABELS.modDestination,
                                                   'Slot ' + slot + ' destination');
            if (destinationSelect) { destinationCell.append(destinationSelect); placed.add(destinationId); }

            const depthCell = make('td', 'matrix__depth', row);
            const depthControl = createBipolar(depthId);
            if (depthControl) { depthCell.append(depthControl); placed.add(depthId); }

            rows.push({ row, sourceId, destinationId, depthId });
        }
    }

    // A routing counts as assigned when it has a source, a destination and a
    // depth that is not zero — all three, because any one of them missing means
    // the slot moves nothing, and showing it as live would be a lie.
    function refresh () {
        let assigned = 0;
        const litParameters = new Set();

        for (const entry of rows) {
            const source = Math.round(plainOf(entry.sourceId));
            const destination = Math.round(plainOf(entry.destinationId));
            const depth = plainOf(entry.depthId);

            const live = source > 0 && destination > 0 && Math.abs(depth) > 0.0005;

            entry.row.dataset.assigned = live ? 'true' : 'false';

            if (live) {
                ++assigned;

                const target = DESTINATION_PARAMETER[destination];
                if (target) litParameters.add(target);
            }
        }

        document.getElementById('mod-count').textContent = String(assigned);
        document.getElementById('mod-lamp').dataset.on = assigned > 0 ? 'true' : 'false';

        // Mark every knob a live routing actually moves. Green means motion
        // here exactly as it does everywhere else in the interface.
        for (const entry of controls) {
            const id = entry[0];

            for (const control of entry[1])
                if (control.element.classList.contains('knob'))
                    control.element.dataset.modulated = litParameters.has(id) ? 'true' : 'false';
        }
    }

    watchers.push((changed) => {
        if (changed.lastIndexOf('mod', 0) === 0) refresh();
    });

    return refresh;
}

function buildOutput (rank) {
    const module = createModule('Output', null);
    rank.append(module.root);

    knob(module.body, 'fx_distortion_mix', 'Dist Mix');
    knob(module.body, 'fx_delay_time', 'Delay Time');
    knob(module.body, 'master_gain', 'Master');
}

/** Anything the layout above did not place.

    A safety net rather than a section: if a parameter is added to the registry
    and nobody gives it a home here, it appears at the bottom instead of
    becoming invisible and un-editable.
*/
function buildUnplaced (workspace) {
    const missing = [];

    for (const id of definitions.keys())
        if (!placed.has(id)) missing.push(id);

    if (missing.length === 0) return;

    const rank = make('div', 'rank rank--wide', workspace);
    const module = createModule('Unassigned', String(missing.length));
    rank.append(module.root);

    for (const id of missing)
        knob(module.body, id, definitions.get(id).name);
}

function rankIn (workspace, modifier) {
    return make('div', 'rank' + (modifier ? ' ' + modifier : ''), workspace);
}

let refreshMatrix = () => {};

function build () {
    const workspace = document.getElementById('workspace');
    workspace.innerHTML = '';

    controls.clear();
    watchers.length = 0;
    placed.clear();

    const sources = rankIn(workspace, 'rank--pair');
    buildOscillator(sources, 1);
    buildOscillator(sources, 2);

    buildSubAndNoise(rankIn(workspace, 'rank--compact'));

    const filters = rankIn(workspace, 'rank--pair');
    buildFilter(filters, 1);
    buildFilter(filters, 2);

    buildEnvelopes(rankIn(workspace, 'rank--wide'));
    buildLfos(rankIn(workspace, 'rank--wide'));
    refreshMatrix = buildMatrix(rankIn(workspace, 'rank--wide'));
    buildOutput(rankIn(workspace, 'rank--compact'));

    buildUnplaced(workspace);

    // The controls are new objects; anything already known about their MIDI
    // assignments has to be drawn onto them again.
    refreshMidiIndicators();
}

/* ==========================================================================
   MIDI LEARN

   The native side owns the mappings, exactly as it owns parameter values: this
   page never invents one, and never assumes a request succeeded. Every command
   is answered with the whole mapping state, and that answer is what the page
   draws (UI_BINDINGS.md §1, §6).

   Assignment is a *mode*. In it, clicking a control arms learn on it rather
   than moving it, which is why the pointer, click and key handlers below run in
   the capture phase — they have to reach the event before the control's own
   handlers do, and cancel it there.
   ========================================================================== */

const midi = {
    mode: false,
    learning: '',
    capacity: 0,
    mappings: new Map()   // id -> { controller, channel, min, max }
};

function midiCommand (type, id) {
    const message = { type, version: PROTOCOL_VERSION };
    if (id) message.id = id;
    bridge.send(message);
}

/** The badge element for a control, created on demand.

    Returns null for a <select>, which cannot contain one. Those still show
    their assignment through the outline and the tooltip; a matrix source is an
    unusual thing to put on a hardware knob anyway.
*/
function midiBadgeFor (element) {
    if (element.tagName === 'SELECT') return null;

    let badge = element.querySelector(':scope > .midi-badge');

    if (!badge) {
        badge = make('span', 'midi-badge', element);
        badge.setAttribute('aria-hidden', 'true');
    }

    return badge;
}

/** Redraws every control's assignment state from `midi`. */
function refreshMidiIndicators () {
    for (const [id, bound] of controls) {
        const mapping = midi.mappings.get(id);
        const learning = midi.learning === id;

        const state = learning ? 'learning' : (mapping ? 'mapped' : '');
        const text = learning
            ? 'LEARN'
            : (mapping ? 'CC ' + mapping.controller : '');

        // The tooltip carries what the badge cannot: which channel, and what to
        // do about it (CLAUDE.md §39).
        const title = learning
            ? 'Waiting for a MIDI control — move one, or press Escape'
            : (mapping
                ? 'MIDI CC ' + mapping.controller
                    + (mapping.channel > 0 ? ' on channel ' + mapping.channel : ' (any channel)')
                    + ' — press Delete in MIDI Learn mode to release it'
                : '');

        for (const control of bound) {
            const element = control.element;
            if (!element) continue;

            if (state) element.dataset.midi = state;
            else delete element.dataset.midi;

            const definition = definitions.get(id);
            const name = definition ? definition.name : id;
            element.title = title ? name + ' · ' + title : name;

            const badge = midiBadgeFor(element);
            if (!badge) continue;

            badge.textContent = text;
            badge.hidden = !state;

            if (state) badge.dataset.state = state;
            else delete badge.dataset.state;
        }
    }

    const count = midi.mappings.size;
    document.getElementById('midi-count').textContent = String(count);
    document.getElementById('midi-lamp').dataset.on = count > 0 ? 'true' : 'false';
    document.getElementById('midi-clear').hidden = !(midi.mode && count > 0);

    renderStatus();
}

function setMidiMode (on) {
    midi.mode = on;

    document.body.dataset.midiMode = on ? 'true' : 'false';

    const toggle = document.getElementById('midi-toggle');
    toggle.setAttribute('aria-pressed', on ? 'true' : 'false');

    // Leaving the mode with learn still armed would leave the engine waiting
    // for a control the user can no longer see they are choosing.
    if (!on && midi.learning) midiCommand('midiLearnCancel');

    document.getElementById('hint').textContent = on
        ? 'Click a control to assign it · Delete to release · Escape to cancel'
        : 'Drag a knob · Shift for fine · Double-click or Delete to reset';

    refreshMidiIndicators();
}

/** Arms, or disarms, learn for the control under an assignment-mode event. */
function midiAssignFrom (target) {
    const id = target.dataset.midiId;
    if (!id) return;

    if (midi.learning === id) midiCommand('midiLearnCancel');
    else midiCommand('midiLearnBegin', id);
}

function installMidiMode () {
    const workspace = document.getElementById('workspace');

    const assignable = (event) => {
        if (!midi.mode) return null;

        const target = event.target.closest ? event.target.closest('[data-midi-id]') : null;
        return target;
    };

    // Capture phase, and the event is stopped: in assignment mode a knob must
    // not also start a drag, and a segmented option must not also change value.
    workspace.addEventListener('pointerdown', (event) => {
        const target = assignable(event);
        if (!target) return;

        event.preventDefault();
        event.stopPropagation();
        midiAssignFrom(target);
    }, true);

    for (const type of ['click', 'dblclick', 'change', 'pointerup']) {
        workspace.addEventListener(type, (event) => {
            if (assignable(event)) {
                event.preventDefault();
                event.stopPropagation();
            }
        }, true);
    }

    workspace.addEventListener('keydown', (event) => {
        const target = assignable(event);
        if (!target) return;

        const id = target.dataset.midiId;

        if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            event.stopPropagation();
            midiAssignFrom(target);
        } else if (event.key === 'Delete' || event.key === 'Backspace') {
            // In assignment mode Delete releases the control rather than
            // resetting the parameter, which is the only thing it could
            // sensibly mean here.
            event.preventDefault();
            event.stopPropagation();

            if (midi.mappings.has(id)) midiCommand('midiMappingRemove', id);
        }
    }, true);

    document.addEventListener('keydown', (event) => {
        if (event.key !== 'Escape') return;

        if (midi.learning) midiCommand('midiLearnCancel');
        else if (midi.mode) setMidiMode(false);
    });

    document.getElementById('midi-toggle')
        .addEventListener('click', () => setMidiMode(!midi.mode));

    document.getElementById('midi-clear')
        .addEventListener('click', () => midiCommand('midiMappingClearAll'));
}

/* ==========================================================================
   MESSAGES
   ========================================================================== */

/** The last thing the engine said, kept so a transient MIDI prompt can be
    shown over it and then cleared without losing it.
*/
let baseStatus = 'Connecting to engine…';

function renderStatus () {
    const element = document.getElementById('status');

    if (midi.learning) {
        const definition = definitions.get(midi.learning);
        element.textContent = 'Move a MIDI control to assign it to '
            + (definition ? definition.name : midi.learning)
            + ' — Escape to cancel';
        return;
    }

    element.textContent = baseStatus;
}

function setStatus (text) {
    baseStatus = text;
    renderStatus();
}

function handle (message) {
    switch (message.type) {
        case 'parameterMetadata':
            definitions.clear();

            for (const definition of message.parameters)
                definitions.set(definition.id, definition);

            build();

            // Everything is built from metadata but nothing yet holds a value,
            // so ask for the state that fills it in.
            bridge.send({ type: 'requestState', version: PROTOCOL_VERSION });

            // And for the mappings, which are stored per project and so may
            // already exist before this page has ever been opened.
            midiCommand('requestMidiMappings');
            break;

        case 'stateSnapshot':
            for (const id of Object.keys(message.parameters))
                apply(id, message.parameters[id]);

            refreshMatrix();
            setStatus(definitions.size + ' parameters bound · protocol v' + message.version);
            break;

        case 'parameterChanged':
            // A control under the pointer is not overwritten by the echo of
            // what it just sent: the value would arrive a frame late and pull
            // the knob backwards under the hand.
            if (!held.has(message.id))
                apply(message.id, message.normalizedValue);
            break;

        case 'midiMappings':
            midi.mappings.clear();

            for (const entry of message.mappings)
                midi.mappings.set(entry.id, {
                    controller: entry.controller,
                    channel: entry.channel,
                    min: entry.min,
                    max: entry.max
                });

            midi.learning = message.learning || '';
            midi.capacity = message.capacity;

            // A replacement is reported rather than left to be discovered:
            // assigning a control that was already in use silently takes it
            // away from whatever had it (CLAUDE.md §33).
            if (message.status && message.status.indexOf('REPLACED') === 0)
                setStatus(message.statusMessage);
            else if (message.status && message.status.indexOf('REJECTED') === 0)
                setStatus(message.statusMessage);

            refreshMidiIndicators();
            break;

        case 'error':
            setStatus('Engine reported: ' + message.message);
            break;

        default:
            break;
    }
}

/* ==========================================================================
   START
   ========================================================================== */

bridge.listen(handle);
installMidiMode();
setMidiMode(false);

if (bridge.available) {
    bridge.send({ type: 'requestMetadata', version: PROTOCOL_VERSION });
} else {
    // Opened outside the plugin. Say so plainly rather than sitting on
    // "Connecting" forever (CLAUDE.md §33, §39).
    setStatus('No engine connection — this page is running outside Apollo.');
}
