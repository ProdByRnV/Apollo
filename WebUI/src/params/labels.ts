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

/** Every effect's power switch, in the order `fx_*_bypass` stores it.

    Index 0 is *not bypassed*, so it reads ON; index 1 is bypassed, so it reads
    OFF. One table shared by all six, because six effects answering the same
    question with six copies of the same two words is six chances for one of
    them to drift.

    "OFF" rather than "BYPASS" at the developer's request. The engine still
    bypasses rather than removing — a switched-off effect keeps its slot and its
    latency, so toggling it does not make the host re-plan its graph (ADR-0054)
    — but that is a fact about the signal path, and the control is a power
    switch to the person using it.
*/
const fxPower = ['ON', 'OFF'] as const;

/** Where a source goes under split routing, one table for the four sources. */
const filterInput = ['F1', 'F2', 'DIRECT'] as const;

export const LABELS: Record<string, readonly string[]> = {
    // 0..3, in the order WavetableLibrary builds them.
    //
    // These name the four real tables that replaced the morphs between classic
    // shapes: a saw whose bandwidth opens, a square whose width narrows, a saw
    // under a climbing resonant peak, and a sine driven into a wavefolder.
    osc1_wavetable: ['SWEEP', 'PULSE', 'FORMANT', 'FOLD'],
    osc2_wavetable: ['SWEEP', 'PULSE', 'FORMANT', 'FOLD'],

    // dsp::UnisonLayout::DetuneCurve.
    osc1_detune_curve: ['LINEAR', 'EXP'],
    osc2_detune_curve: ['LINEAR', 'EXP'],

    sub_octave: ['-2', '-1'],

    // dsp::WavetableLibrary's sub waveforms, named as the LFO's shapes are.
    sub_waveform: ['SINE', 'TRI', 'SQR'],
    sub_tracking: ['NOTE', 'OSC 1'],

    // dsp::NoiseType.
    noise_type: ['WHITE', 'PINK', 'CLICK', 'PICK', 'THUMP', 'CHIFF'],

    // 0 off, 1 lowpass, 2 highpass, 3 bandpass, 4 notch.
    filter1_type: ['OFF', 'LP', 'HP', 'BP', 'NOTCH', 'COMB', 'FORMANT'],
    filter2_type: ['OFF', 'LP', 'HP', 'BP', 'NOTCH', 'COMB', 'FORMANT'],
    filter_routing: ['SERIES', 'PARALLEL', 'SPLIT'],

    // engine::FilterInput: where a source goes under split routing.
    osc1_filter: filterInput,
    osc2_filter: filterInput,
    sub_filter: filterInput,
    noise_filter: filterInput,

    // 0 empty, then dsp::EffectType in order. The effects whose phases have not
    // landed are selectable — the slot parameter's range is permanent, so it
    // covers all six from the start (ADR-0054) — and say so, rather than
    // appearing to work and doing nothing.
    fxSlot: ['—', 'DISTORTION', 'DELAY', 'REVERB',
             'GATE', 'COMPRESSOR', 'EQ'],

    // Matches dsp::Distortion::Mode.
    fx_distortion_mode: ['SOFT', 'HARD', 'DIODE'],
    fx_distortion_bypass: fxPower,

    fx_delay_bypass: fxPower,
    fx_delay_sync: ['FREE', 'SYNC'],
    fx_delay_pingpong: ['STEREO', 'PING-PONG'],

    // Matches dsp::Delay::Division. A dot is dotted and a T is a triplet, which
    // is how they are written on paper.
    fx_delay_division: ['1/1', '1/2.', '1/2', '1/2T', '1/4.', '1/4', '1/4T',
                        '1/8.', '1/8', '1/8T', '1/16.', '1/16', '1/16T', '1/32'],

    // Matches dsp::Reverb::Mode.
    fx_reverb_mode: ['ROOM', 'HALL'],
    fx_reverb_bypass: fxPower,
    fx_gate_bypass: fxPower,
    fx_compressor_bypass: fxPower,
    fx_eq_bypass: fxPower,

    // Matches dsp::EqualiserBand::Type. One table shared by all seven bands,
    // because the bands are identical — the module passes the same one to each.
    fx_eq_band_type: ['OFF', 'LP', 'BP', 'HP', 'NOTCH', 'LO SHELF', 'BELL', 'HI SHELF'],

    // The order is instances of the shape in series, which for a pass filter is
    // what its slope is. Labelled in decibels per octave because that is the
    // number an engineer reaches for, and it is the same four positions either
    // way — a bell at 24 is a bell applied twice.
    fx_eq_band_order: ['12', '24', '36', '48'],
    fx_eq_band_mute: ['IN', 'MUTE'],

    // Matches dsp::LfoShape.
    lfoShape: ['SINE', 'TRI', 'SAW', 'RSAW', 'SQR', 'S&H', 'STEP'],
    lfoRetrigger: ['FREE', 'RETRIG'],
    lfoPolarity: ['UNI', 'BI'],

    // `lfoN_sync`: the rate in hertz, or a division of the host's tempo.
    lfoSync: ['HZ', 'SYNC'],

    // dsp::lfoDivisionBeats. A dot is dotted and a T is a triplet.
    lfoDivision: ['8/1', '4/1', '2/1', '1/1', '1/2.', '1/2', '1/2T', '1/4.', '1/4', '1/4T',
                  '1/8.', '1/8', '1/8T', '1/16.', '1/16', '1/16T', '1/32'],

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
                     'Amplitude',
                     // Appended in 13b, so every destination before them keeps
                     // its number (ADR-0090).
                     'Osc 1 Warp', 'Osc 2 Warp'],

    // dsp::UnisonOscillator::WarpMode.
    osc1_warp_mode: ['OFF', 'SYNC', 'ASYM', 'PWM', 'BEND', 'MIRROR'],
    osc2_warp_mode: ['OFF', 'SYNC', 'ASYM', 'PWM', 'BEND', 'MIRROR'],
};

/** Which knob a modulation destination points at, so an assigned routing can
    light up the control it actually moves. Destinations with no single parameter
    of their own — All Pitch and voice Amplitude — are absent rather than guessed
    at. Osc 1 Pitch joined in 13a, when oscillator 1 gained a tuning control.
*/
export const DESTINATION_PARAMETER: Record<number, string> = {
    2: 'osc1_semitones',
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
    17: 'osc1_warp_amount',
    18: 'osc2_warp_amount',
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
