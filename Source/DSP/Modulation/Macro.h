#pragma once

/*
    A macro control: one knob a performer moves, shaped into the value the
    modulation matrix reads (Phase 13e-3, PRD §17).

    The knob goes through a curve and then into a range. The curve bends the
    travel: positive moves fast at the start and slowly at the top, negative
    the reverse, both as powers of the knob so that the ends stay where they
    are. The range is where in 0 to 1 that travel lands, and a minimum above
    the maximum turns the macro round. The matrix then sends the result to as
    many destinations as route it, each with its own bipolar depth, which is
    where "several destinations" and "bipolar depth" live: a macro is a source,
    and the matrix already does both.

    At the defaults — a range of 0 to 1 and a straight curve — the result is
    the knob itself, exactly.

    REAL-TIME CONTRACT: callable from the audio thread. No allocation.
*/

#include <cmath>

namespace apollo::dsp
{

inline constexpr int numMacros = 4;

struct MacroSettings
{
    /** Where the knob's bottom and top land, each 0 to 1. */
    float minimum = 0.0f;
    float maximum = 1.0f;

    /** -1 to 1. Zero is straight; positive is fast then slow. */
    float curve = 0.0f;

    [[nodiscard]] bool operator== (const MacroSettings&) const = default;
};

/** @returns @p knob, 0 to 1, through @p settings' curve and range. Anything
    out of range or not a number is clamped, NaN to the bottom. */
[[nodiscard]] inline float shapeMacro (float knob, const MacroSettings& settings) noexcept
{
    const auto unit = [] (float value, float low, float high) noexcept
    {
        return value >= low ? (value <= high ? value : high) : low;
    };

    const auto value = unit (knob, 0.0f, 1.0f);
    const auto curve = unit (settings.curve, -1.0f, 1.0f);

    // knob ^ 4^-curve: a curve of 1 is a fourth root, -1 a fourth power.
    const auto shaped = curve == 0.0f ? value : std::pow (value, std::exp2 (-2.0f * curve));

    const auto low = unit (settings.minimum, 0.0f, 1.0f);
    const auto high = unit (settings.maximum, 0.0f, 1.0f);

    return low + (high - low) * shaped;
}

} // namespace apollo::dsp
