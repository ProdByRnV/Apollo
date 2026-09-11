#pragma once

/*
    A modulator's recent history, as a picture of what it is doing now.

    CLAUDE.md §26.1 asks for envelopes and LFOs to be shown "a live trace of the
    value each is currently producing, not a static picture of its shape". Every
    synthesiser draws the second thing — an editable DAHDSR outline with a
    playhead running along it — and the second thing is easier, so it is worth
    being clear about why this is the first: an outline shows what the envelope
    was *configured* to do, and a trace shows what it *did*. When those disagree,
    and they disagree whenever a control is modulated, retriggered or clamped, it
    is the outline that is wrong.

    NOT A SCOPE, AND SIZED NOTHING LIKE ONE. A modulator moves at a few hertz;
    sampling it at 48 kHz and throwing away 99.97 % of that on the way out would
    be a ring three hundred times larger than the picture drawn from it. So this
    ring is exactly the picture: `modulationTracePoints` entries, one per
    interval, holding exactly the window the interface draws, and the frame is
    the ring copied out. There is no decimation because there is nothing to
    decimate — which is also why there is no decimation *rule* to defend, unlike
    ScopeFrame's.

    NO READ MARGIN, WHICH ScopeBuffer NEEDS AND THIS DOES NOT. A scope reads 2048
    samples out of a ring the writer is racing through at the sample rate, so it
    stands well back. Here the writer advances one entry every few milliseconds
    while the reader copies 128 of them in microseconds: the worst case is one
    entry of 128 read mid-write, which is one pixel of one frame, and standing
    back would cost a slice of the only window there is.

    JUCE-free, like the rest of Telemetry, so it can be tested with no host and
    no message loop.
*/

#include <array>
#include <atomic>
#include <cstddef>

namespace apollo::telemetry
{

/** Points in one modulator's trace, and therefore entries in its ring. */
inline constexpr int modulationTracePoints = 128;

/** Seconds the trace covers.

    A second is about the longest span over which a modulator's *recent* motion
    is still what you are asking about: a 4-second release will run off the left
    edge, which is correct — the question a live trace answers is "what is it
    doing", not "what did it do a bar ago".
*/
inline constexpr double modulationTraceSeconds = 1.0;

/** Entries written per second, and so the resolution of the picture.

    128 Hz is one entry every 7.8 ms. Anything faster than that — a 5 ms attack,
    say — is below the trace's resolution and will show as a single step rather
    than a ramp. That is a fact about the picture and not a defect in it, and it
    is stated in UI_BINDINGS.md §10.5 rather than hidden by interpolation that
    would invent the intermediate values.
*/
inline constexpr double modulationTraceHz
    = static_cast<double> (modulationTracePoints) / modulationTraceSeconds;

//==============================================================================

/** One modulator's ring of recent values. */
class ModulationTrace
{
public:
    ModulationTrace();

    /** AUDIO THREAD. Appends one value. */
    void write (float value) noexcept;

    /** MESSAGE THREAD. Copies the whole window, oldest first.

        @param destination  at least `modulationTracePoints` floats.
        @returns            false if nothing has ever been written, in which case
                            @p destination is left alone.
    */
    [[nodiscard]] bool read (float* destination) const noexcept;

    /** True once anything has been written. Separates "this modulator is sitting
        still" from "nothing is tracing this modulator", which the interface must
        not draw alike — the same distinction ScopeBuffer draws.
    */
    [[nodiscard]] bool isActive() const noexcept
    {
        return active.load (std::memory_order_relaxed);
    }

    void reset() noexcept;

private:
    std::array<std::atomic<float>, static_cast<std::size_t> (modulationTracePoints)> values;

    /** Free-running count of entries written; the ring position is this modulo
        the size, which for a power of two is a mask.
    */
    std::atomic<unsigned int> writePosition { 0 };

    std::atomic<bool> active { false };
};

static_assert ((modulationTracePoints & (modulationTracePoints - 1)) == 0,
               "modulationTracePoints must be a power of two for the wrap to be a mask");

} // namespace apollo::telemetry
