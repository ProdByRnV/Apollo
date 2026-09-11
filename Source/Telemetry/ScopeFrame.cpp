#include "Telemetry/ScopeFrame.h"

#include <cmath>

namespace apollo::telemetry
{

namespace
{

/** @returns the index of the latest upward zero crossing at or before
    @p searchEnd, or -1 if there is none.

    Searched backwards from the newest end, so a frame follows the most recent
    cycle rather than one from the far side of the window — which is what keeps
    the trace still when the frequency changes.
*/
[[nodiscard]] int findLatestRisingCrossing (const float* window, int searchEnd) noexcept
{
    for (int i = searchEnd; i > 0; --i)
        if (window[i - 1] <= 0.0f && window[i] > 0.0f)
            return i;

    return -1;
}

} // namespace

bool buildScopeFrame (const ScopeBuffer& buffer, ScopeFrame& frame)
{
    // MESSAGE THREAD.
    frame = ScopeFrame {};

    // On the stack rather than a member: this runs at the interface's refresh
    // rate on the message thread, where a few kilobytes of stack is free and a
    // buffer owned per source would be memory held for the whole session to
    // save nothing.
    float window[static_cast<std::size_t> (scopeWindowSamples)];

    if (! buffer.readWindow (window, scopeWindowSamples))
        return false;

    frame.valid = true;

    for (int i = 0; i < scopeWindowSamples; ++i)
    {
        const auto magnitude = std::abs (window[i]);

        if (magnitude > frame.peak)
            frame.peak = magnitude;
    }

    frame.silent = frame.peak < scopeSilenceThreshold;

    // The frame may start anywhere that still leaves a whole sweep after it,
    // and that range is the entire trigger budget.
    constexpr int latestStart = scopeWindowSamples - scopeFrameSpan;

    // A silent source is not searched for a trigger: every sample is zero, the
    // comparison would find nothing, and reporting `triggered` for a flat line
    // would be a lie the interface might act on.
    const auto crossing = frame.silent ? -1 : findLatestRisingCrossing (window, latestStart);

    frame.triggered = crossing >= 0;

    // Free-running frames are taken from the newest end of the window, which is
    // what a scope with nothing to lock onto should show.
    const auto start = crossing >= 0 ? crossing : latestStart;

    for (int i = 0; i < scopeFramePoints; ++i)
        frame.points[static_cast<std::size_t> (i)] = window[start + i * scopeFrameStride];

    return true;
}

} // namespace apollo::telemetry
