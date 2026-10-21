#pragma once

/*
    A vowel filter, for the voice's filter slots (PRD §13.1, Phase 13c).

    WHAT IT IS. Three band-passes in parallel, one per formant — the resonances
    of a vocal tract that make a vowel recognisable. Each is the state-variable
    filter's band-pass, normalised to unity at its centre and weighted by the
    formant's level. Three formants because the first two decide the vowel and
    the third its brightness; the fourth and fifth in the reference table shape
    a particular voice, which a synthesiser does not have.

    THE CUTOFF CHOOSES THE VOWEL. A filter slot has one frequency control, and
    sweeping it should do what sweeping a formant filter is for: talk. So the
    cutoff moves through U, O, A, E and I — ordered by their second formant,
    which rises steadily along that list, so turning the control up makes the
    sound brighter as it would for any other filter type. The vowels sit
    an octave and a half apart, from U at 160 Hz to I at 10.24 kHz, which puts
    A at 1.28 kHz, close to the middle of the control's travel. Between two
    vowels every formant glides: frequency on a logarithmic scale, level in
    decibels, bandwidth linearly.

    RESONANCE NARROWS THE FORMANTS. Each formant's Q is its frequency over its
    bandwidth in the table, times Q / 1.414. At the default, 0.707, the formants
    are twice as wide as the table's, which keeps the vowel audible on a note
    whose harmonics would otherwise fall between narrow peaks; at the top of the
    range they are seven times narrower than the table, and the filter sings.

    THE TABLE. Frequencies, levels and bandwidths of a tenor voice, from the
    formant table distributed with Csound (Appendix: Formant Values), which is
    the conventional source for synthesis rather than for phonetics.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O. Resolving coefficients costs three `tan`s.
*/

#include "DSP/Filters/StateVariableFilter.h"

#include <array>

namespace apollo::dsp
{

/** What a cutoff and a resonance resolve to, for the formant filter. */
struct FormantCoefficients
{
    static constexpr int numFormants = 3;

    /** @param cutoffHz    chooses the vowel (see the header comment).
        @param q           narrows the formants.
        @param sampleRate  hertz.
    */
    void set (float cutoffHz, float q, double sampleRate) noexcept;

    /** Where the vowel sits for a cutoff: 0 is U, 1 O, 2 A, 3 E, 4 I, and the
        values between are the glides. Exposed for tests and for the interface's
        description of the control. */
    [[nodiscard]] static float vowelPosition (float cutoffHz) noexcept;

    [[nodiscard]] bool operator== (const FormantCoefficients&) const = default;

    std::array<SvfCoefficients, numFormants> bands {};

    /** Each band's level, including the band-pass's own normalisation and the
        filter's makeup gain. */
    std::array<float, numFormants> gains {};
};

class FormantFilter
{
public:
    void reset() noexcept;

    void setCoefficients (const FormantCoefficients& newCoefficients) noexcept;

    [[nodiscard]] float processSample (float input) noexcept;

private:
    std::array<StateVariableFilter, FormantCoefficients::numFormants> bands;
    std::array<float, FormantCoefficients::numFormants> gains {};
};

} // namespace apollo::dsp
