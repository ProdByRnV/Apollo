#pragma once

/*
    A voice's filter section, as resolved data.

    The same split as the source section (ADR-0026): the engine turns parameters
    into concrete resources once, and the voice consumes only the resolved form.
    Here that means the engine has already done the `tan` — a voice receives
    coefficients, never a cutoff in hertz, and therefore never computes a
    transcendental of its own. At full polyphony that is the difference between
    two tangents per block and a hundred and twenty-eight.

    When per-voice modulation arrives with the matrix, a voice will compute its
    own coefficients from its own modulated cutoff. The shape of this struct
    does not change; only who fills it in does.
*/

#include "DSP/Filters/StateVariableFilter.h"

namespace apollo::engine
{

/** How the two filters are connected. */
enum class FilterRouting
{
    /** Filter 1 feeds filter 2. Two lowpasses in series make a steeper
        lowpass, which is the usual reason for having two.
    */
    series = 0,

    /** Both filters see the same input and their outputs are summed, then
        halved so that two identical filters in parallel are the same loudness
        as one rather than twice it.
    */
    parallel
};

/** One filter slot. */
struct FilterSlotSettings
{
    dsp::StateVariableFilter::Mode mode = dsp::StateVariableFilter::Mode::off;

    /** Already resolved from cutoff and resonance by the engine. */
    dsp::SvfCoefficients coefficients;

    /** Saturation in front of the filter, 0 to 1. Exactly zero skips it. */
    float drive = 0.0f;

    [[nodiscard]] bool operator== (const FilterSlotSettings&) const = default;
};

/** Everything in a voice's filter section. */
struct VoiceFilterSettings
{
    FilterSlotSettings filter1;
    FilterSlotSettings filter2;

    FilterRouting routing = FilterRouting::series;

    /** True when neither slot would change the signal, so the whole section can
        be skipped rather than run as two transparent filters.
    */
    [[nodiscard]] bool isBypassed() const noexcept
    {
        return filter1.mode == dsp::StateVariableFilter::Mode::off
            && filter2.mode == dsp::StateVariableFilter::Mode::off;
    }

    [[nodiscard]] bool operator== (const VoiceFilterSettings&) const = default;
};

} // namespace apollo::engine
