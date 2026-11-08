#pragma once

/*
    The modulators as destinations: an envelope's stage times and sustain, and
    an LFO's rate and phase, moved by the matrix (Phase 19d, ADR-0132).

    A TIME MOVES ALONG ITS KNOB. Envelope times and LFO rates are skewed
    controls — a quarter of an attack knob's travel is 6 ms and three quarters
    is 3.2 s — so an offset added in seconds or in hertz would mean nothing at
    one end and everything at the other. Instead the offset is added to where
    the knob sits along its own travel, on its own curve, and turned back into
    seconds or hertz from there: a depth of 1 is the knob's whole travel, as it
    is for every other knob a source is dropped on, and velocity taking a
    quarter off an attack takes a quarter of the knob off it whatever the
    attack is set to.

    The curves are the registry's (ParameterDefinitions.h), repeated here
    because the DSP does not include the registry; a test holds the two equal.

    REAL-TIME CONTRACT: plain arithmetic, no allocation, no state.
*/

#include "DSP/Envelopes/Envelope.h"
#include "DSP/Modulation/ModulationTypes.h"

#include <cmath>

namespace apollo::dsp
{

/** An envelope's delay and hold knobs run to 2 s, its attack, decay and release
    to 10 s, all with the same skew. */
inline constexpr float envelopeShortStageSeconds = 2.0f;
inline constexpr float envelopeLongStageSeconds = 10.0f;
inline constexpr float envelopeTimeSkew = 0.25f;

/** An LFO's rate knob. */
inline constexpr float lfoRateMinimumHz = 0.01f;
inline constexpr float lfoRateMaximumHz = 400.0f;
inline constexpr float lfoRateSkew = 0.13f;

/** Moves @p value along a knob's travel by @p offset, a fraction of the whole.

    The knob's travel is its range mapped the way JUCE maps a skewed one: the
    proportion of the range, raised to @p skew. So the result is what the knob
    would read moved by @p offset of its travel, clamped at its ends. No offset,
    or one that is not a number, returns @p value untouched.
*/
[[nodiscard]] inline float offsetAlongTravel (float value, float minimum, float maximum,
                                              float skew, float offset) noexcept
{
    if (offset == 0.0f || ! std::isfinite (offset) || ! (maximum > minimum))
        return value;

    auto proportion = (value - minimum) / (maximum - minimum);
    proportion = proportion >= 0.0f ? (proportion > 1.0f ? 1.0f : proportion) : 0.0f;

    auto travel = skew == 1.0f ? proportion : std::pow (proportion, skew);
    travel += offset;
    travel = travel >= 0.0f ? (travel > 1.0f ? 1.0f : travel) : 0.0f;

    proportion = skew == 1.0f ? travel : std::pow (travel, 1.0f / skew);
    return minimum + (maximum - minimum) * proportion;
}

/** @p base with the offsets the matrix put on envelope @p index applied.

    @p destinations is the voice's whole table of offsets. The times move along
    their knobs, the sustain is a level held to 0 to 1, and everything else —
    the curve, the bends, the retrigger rule — is the base's.
*/
[[nodiscard]] inline EnvelopeSettings modulatedEnvelope (const EnvelopeSettings& base, int index,
                                                         const ModDestinationValues& destinations) noexcept
{
    const auto offset = [&destinations, index] (EnvelopeTarget target)
    {
        const auto at = static_cast<std::size_t> (envelopeDestination (index, target));
        return at < destinations.size() ? destinations[at] : 0.0f;
    };

    const auto time = [] (float seconds, float maximum, float by)
    {
        return offsetAlongTravel (seconds, 0.0f, maximum, envelopeTimeSkew, by);
    };

    auto result = base;

    result.delaySeconds = time (base.delaySeconds, envelopeShortStageSeconds, offset (EnvelopeTarget::delay));
    result.attackSeconds = time (base.attackSeconds, envelopeLongStageSeconds, offset (EnvelopeTarget::attack));
    result.holdSeconds = time (base.holdSeconds, envelopeShortStageSeconds, offset (EnvelopeTarget::hold));
    result.decaySeconds = time (base.decaySeconds, envelopeLongStageSeconds, offset (EnvelopeTarget::decay));
    result.releaseSeconds = time (base.releaseSeconds, envelopeLongStageSeconds, offset (EnvelopeTarget::release));
    result.sustainLevel = offsetAlongTravel (base.sustainLevel, 0.0f, 1.0f, 1.0f, offset (EnvelopeTarget::sustain));

    return result;
}

/** True when an active slot of @p routing reaches envelope @p index. */
[[nodiscard]] constexpr bool routesToEnvelope (const ModulationRouting& routing, int index) noexcept
{
    const auto first = static_cast<int> (envelopeDestination (index, EnvelopeTarget::delay));

    for (const auto& slot : routing.slots)
    {
        const auto destination = static_cast<int> (slot.destination);

        if (slot.isActive() && destination >= first && destination < first + numEnvelopeTargets)
            return true;
    }

    return false;
}

/** True when an active slot of @p routing reaches LFO @p index's rate or phase. */
[[nodiscard]] constexpr bool routesToLfo (const ModulationRouting& routing, int index) noexcept
{
    for (const auto& slot : routing.slots)
        if (slot.isActive()
            && (slot.destination == lfoRateDestination (index) || slot.destination == lfoPhaseDestination (index)))
            return true;

    return false;
}

} // namespace apollo::dsp
