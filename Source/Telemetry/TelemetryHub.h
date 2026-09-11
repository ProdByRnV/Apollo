#pragma once

/*
    Every place Apollo can be watched from.

    PRD §30.1 asks for a scope on the final output *and one for every individual
    source*, so that the user sees the wave each part is producing rather than
    only the sum. This enumerates those taps and owns a capture ring for each.

    ORDER IS PART OF THE CONTRACT, in the same way ModSource's is: the interface
    keys its scopes on these tokens, so entries are appended and never reordered.

    Only some of them are written yet. A source nothing has tapped reports
    inactive rather than silent, and the interface draws nothing for it — which
    is the difference between "this part is quiet" and "this build does not
    capture that part", and the two must not look alike.
*/

#include "Telemetry/ScopeBuffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <string_view>

namespace apollo::telemetry
{

/** A point in the signal path that can be watched. */
enum class ScopeSource
{
    /** The finished stereo mix, after master gain. What leaves the plugin. */
    output = 0,

    oscillator1,
    oscillator2,
    sub,
    noise,

    /** The voice mix after the filter section and before master gain. */
    postFilter,

    count
};

inline constexpr std::size_t scopeSourceCount = static_cast<std::size_t> (ScopeSource::count);

/** @returns the stable wire token, e.g. "output".

    Part of the bridge contract: the frontend keys its scopes on these.
*/
[[nodiscard]] constexpr std::string_view toToken (ScopeSource source) noexcept
{
    switch (source)
    {
        case ScopeSource::output:      return "output";
        case ScopeSource::oscillator1: return "osc1";
        case ScopeSource::oscillator2: return "osc2";
        case ScopeSource::sub:         return "sub";
        case ScopeSource::noise:       return "noise";
        case ScopeSource::postFilter:  return "filter";
        case ScopeSource::count:
        default:                       return "";
    }
}

/** @returns a display name. Presentation, so the frontend may override it. */
[[nodiscard]] constexpr std::string_view describe (ScopeSource source) noexcept
{
    switch (source)
    {
        case ScopeSource::output:      return "Output";
        case ScopeSource::oscillator1: return "Oscillator 1";
        case ScopeSource::oscillator2: return "Oscillator 2";
        case ScopeSource::sub:         return "Sub";
        case ScopeSource::noise:       return "Noise";
        case ScopeSource::postFilter:  return "Filter";
        case ScopeSource::count:
        default:                       return "";
    }
}

//==============================================================================

/** One capture ring per source.

    Held by the processor and handed out by reference. Not copyable or movable,
    because the audio thread writes through a reference to it and a move would
    leave that reference aimed at the original — the same reasoning that makes
    VoiceEngine immovable.
*/
class TelemetryHub
{
public:
    TelemetryHub() = default;

    TelemetryHub (const TelemetryHub&) = delete;
    TelemetryHub& operator= (const TelemetryHub&) = delete;
    TelemetryHub (TelemetryHub&&) = delete;
    TelemetryHub& operator= (TelemetryHub&&) = delete;

    [[nodiscard]] ScopeBuffer& scope (ScopeSource source) noexcept
    {
        return buffers[indexOf (source)];
    }

    [[nodiscard]] const ScopeBuffer& scope (ScopeSource source) const noexcept
    {
        return buffers[indexOf (source)];
    }

    /** Clears every capture, so nothing survives a device or sample-rate change
        as a picture of audio that is no longer being produced.
    */
    void reset() noexcept
    {
        for (auto& buffer : buffers)
            buffer.reset();
    }

    /** AUDIO THREAD. True while anything is watching.

        Capture is not free, and the two taps cost differently. The output tap is
        one linear pass over a buffer that already exists, so it is flat in voice
        count. The per-source taps live inside the voice loop, so their cost
        scales with polyphony — which is exactly the cost PRD §30.1 forbids
        growing without measuring. A plugin whose editor is closed has nobody
        watching, so it captures nothing and costs what it did before scopes
        existed. In a session holding twenty instances that is nineteen of them.

        Relaxed, because the audio thread reads it once per block only to decide
        whether to do optional work. Acting on a value one block out of date
        costs a single frame at the moment an editor opens, so there is nothing
        here worth ordering against.
    */
    [[nodiscard]] bool isCapturing() const noexcept
    {
        return capturing.load (std::memory_order_relaxed);
    }

    /** MESSAGE THREAD. Starts or stops capture.

        Starting clears every ring first. Between one viewer closing and the next
        opening, the rings still hold whatever was sounding when the first one
        closed, and a scope whose opening frame is audio from a previous session
        is precisely the stale trace CLAUDE.md §26.1 forbids. Clearing is safe
        here *because* capture is off: with the flag false no audio thread is
        writing to these rings, so there is no writer to race with.
    */
    void setCapturing (bool shouldCapture) noexcept
    {
        if (shouldCapture == capturing.load (std::memory_order_relaxed))
            return;

        if (shouldCapture)
            reset();

        capturing.store (shouldCapture, std::memory_order_relaxed);
    }

private:
    /** Clamped rather than asserted: an out-of-range source is a caller bug, and
        returning a reference to arbitrary memory would turn it into undefined
        behaviour on the audio thread.
    */
    [[nodiscard]] static std::size_t indexOf (ScopeSource source) noexcept
    {
        const auto index = static_cast<std::size_t> (source);
        return index < scopeSourceCount ? index : 0;
    }

    std::array<ScopeBuffer, scopeSourceCount> buffers;

    std::atomic<bool> capturing { false };
};

} // namespace apollo::telemetry
