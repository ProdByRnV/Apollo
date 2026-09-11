/*
    What a discrete parameter's positions are called.

    The metadata carries a discrete parameter's range but not the names of its
    positions, because those are presentation. They live here — and are applied
    only when the table's length matches the range the engine reported, so a list
    that has fallen out of step with its enum shows honest numbers instead of
    confidently wrong words. That guard has already earned itself once: appending
    a modulation source left this table one longer than the parameter's range,
    and the matrix showed "0" rather than the wrong name (ADR-0044).
*/

import type { ParameterDefinition } from '../bridge/protocol';
import { stepCount } from './mapping';

export const LABELS: Record<string, readonly string[]> = {
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
                'Velocity', 'Key Track', 'Mod Wheel', 'Pitch Bend', 'Aftertouch', 'Random',
                'Timbre'],

    // Matches midi::MpeZoneType.
    mpe_zone: ['OFF', 'LOWER', 'UPPER'],

    // Matches dsp::ModDestination.
    modDestination: ['—', 'All Pitch', 'Osc 1 Pitch', 'Osc 2 Pitch',
                     'Osc 1 Position', 'Osc 2 Position',
                     'Osc 1 Level', 'Osc 2 Level', 'Osc 1 Pan', 'Osc 2 Pan',
                     'Sub Level', 'Noise Level',
                     'Filter 1 Cutoff', 'Filter 1 Resonance',
                     'Filter 2 Cutoff', 'Filter 2 Resonance',
                     'Amplitude'],
};

/** Which knob a modulation destination points at, so an assigned routing can
    light up the control it actually moves. Destinations with no single parameter
    of their own — All Pitch, Osc 1 Pitch, voice Amplitude — are absent rather
    than guessed at.
*/
export const DESTINATION_PARAMETER: Record<number, string> = {
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
    15: 'filter2_resonance',
};

/** `dsp::EnvelopeStage`, in its own numeric order. Shown as a word because a
    colour cannot say "decay" (CLAUDE.md §39).
*/
export const ENVELOPE_STAGES = [
    'idle', 'delay', 'attack', 'hold', 'decay', 'sustain', 'release',
] as const;

export function labelsFor(
    definition: ParameterDefinition,
    table: readonly string[] | undefined,
): readonly string[] | null {
    if (!table) return null;

    // Only trusted when it lines up exactly with the range the engine sent.
    return table.length === stepCount(definition) ? table : null;
}
