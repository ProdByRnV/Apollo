#pragma once

/*
    The complete configuration of a voice's source section, as plain data.

    WHY A STRUCT RATHER THAN SETTERS. Phase 4b takes a voice from one oscillator
    to four sources with fifteen parameters between them. Plumbing each of those
    through its own setter would put fifteen calls per voice in the audio
    callback, and every future source would add more — the kind of growth that
    ends with a parameter silently not reaching the engine because one call was
    forgotten. One struct, compared as a whole, cannot develop that gap: either
    the settings a voice holds match the ones the engine computed or they do
    not.

    It also draws the line between the two layers cleanly. The engine resolves
    parameters into concrete resources — a wavetable pointer, a unison layout, a
    tuning in semitones — and the voice consumes only the resolved form. Voices
    therefore never look up a table by index, never see a normalised parameter,
    and never need to know that a `WavetableLibrary` exists.

    LIFETIME. The pointers are non-owning and point at objects the engine owns
    for its whole life: the wavetables are immutable once built, and the unison
    layouts are updated by the engine on the audio thread before any voice reads
    them in the same callback.
*/

#include "DSP/Oscillators/Wavetable.h"
#include "DSP/Unison/UnisonLayout.h"

namespace apollo::engine
{

/** One primary wavetable oscillator's settings. */
struct OscillatorSourceSettings
{
    /** The table to read. Null silences the oscillator. */
    const dsp::Wavetable* table = nullptr;

    /** The unison stack's shape. Null falls back to a single centred voice. */
    const dsp::UnisonLayout* unison = nullptr;

    /** Scan position across the table, normalised to [0, 1]. */
    float position = 0.0f;

    /** Output level, 0 to 1. Exactly zero skips the oscillator entirely. */
    float level = 0.0f;

    /** Stereo balance: -1 hard left, 0 centre, +1 hard right.

        A balance rather than a pan, because by this point the oscillator is
        already a stereo signal built by the unison spread. Panning it again
        would narrow the image it just created.
    */
    float pan = 0.0f;

    /** Transposition from the played note, in semitones; fractional values are
        fine and are how a fine-tune control is expressed.
    */
    float tuneSemitones = 0.0f;

    [[nodiscard]] bool operator== (const OscillatorSourceSettings&) const = default;
};

/** Everything in a voice's source section. */
struct VoiceSourceSettings
{
    OscillatorSourceSettings osc1;
    OscillatorSourceSettings osc2;

    /** The sub oscillator's table — a single-frame sine. Null silences it. */
    const dsp::Wavetable* subTable = nullptr;

    /** Sub level, 0 to 1. Exactly zero skips it entirely. */
    float subLevel = 0.0f;

    /** Octaves below the played note. -1 or -2. */
    int subOctave = -1;

    /** Noise level, 0 to 1. Exactly zero skips it entirely. */
    float noiseLevel = 0.0f;

    [[nodiscard]] bool operator== (const VoiceSourceSettings&) const = default;
};

} // namespace apollo::engine
