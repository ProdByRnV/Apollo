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

#include "DSP/Filters/VoiceFilter.h"

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
    parallel,

    /** Each source chooses a filter, or neither, and the results are summed.
        Not halved: every source passes through exactly one path, so with both
        filters transparent the sum is the sources' own sum and the headroom is
        what it is in series (ADR-0093).
    */
    split
};

/** Where a source goes under split routing. Ignored by series and parallel,
    which put every source through the pair.
*/
enum class FilterInput
{
    filter1 = 0,
    filter2,

    /** Past both filters, straight to the amplifier. */
    direct
};

inline constexpr int numFilterInputs = 3;

/** Which input each source takes under split routing. The defaults divide the
    voice in two — each oscillator through its own filter, the sub beside
    oscillator 1 and the noise beside oscillator 2 — so choosing split does
    something before anything else is touched.
*/
struct FilterInputs
{
    FilterInput osc1 = FilterInput::filter1;
    FilterInput osc2 = FilterInput::filter2;
    FilterInput sub = FilterInput::filter1;
    FilterInput noise = FilterInput::filter2;

    [[nodiscard]] bool operator== (const FilterInputs&) const = default;
};

/** One filter slot. */
struct FilterSlotSettings
{
    dsp::FilterType mode = dsp::FilterType::off;

    /** Already resolved from cutoff and resonance by the engine, for the common
        case where nothing modulates this filter.
    */
    dsp::VoiceFilterCoefficients coefficients;

    /** The values those coefficients came from.

        Carried so that a voice whose cutoff or resonance is being modulated can
        re-resolve its own coefficients from its own offset. A voice with no
        modulation reaching this filter never looks at these and uses the shared
        coefficients above, which is what keeps the unmodulated path exactly as
        cheap as it was before the matrix existed.
    */
    float cutoffHz = 20000.0f;
    float q = 0.707f;

    /** Saturation in front of the filter, 0 to 1. Exactly zero skips it. */
    float drive = 0.0f;

    /** How much of the slot is heard against its input, 0 to 1. Exactly 1 —
        the default — runs the slot as it ran before the control existed. */
    float mix = 1.0f;

    /** Cutoff octaves per octave of played pitch, about middle C, -1 to 1.
        Zero leaves a voice on the shared coefficients above. */
    float keyTrack = 0.0f;

    [[nodiscard]] bool operator== (const FilterSlotSettings&) const = default;
};

/** Everything in a voice's filter section. */
struct VoiceFilterSettings
{
    FilterSlotSettings filter1;
    FilterSlotSettings filter2;

    FilterRouting routing = FilterRouting::series;
    FilterInputs inputs;

    /** True when neither slot would change the signal, so the whole section can
        be skipped rather than run as two transparent filters.
    */
    [[nodiscard]] bool isBypassed() const noexcept
    {
        return filter1.mode == dsp::FilterType::off
            && filter2.mode == dsp::FilterType::off;
    }

    [[nodiscard]] bool operator== (const VoiceFilterSettings&) const = default;
};

} // namespace apollo::engine
