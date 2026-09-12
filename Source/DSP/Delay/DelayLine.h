#pragma once

/*
    A single-channel fractional delay line.

    Deliberately separate from the delay *effect*. The effect is a musical
    object — feedback, ping-pong, tempo sync, a mix control — while this is the
    buffer underneath it, and the reverb in 8c will want the same buffer with
    none of the musical part. Keeping them apart is what stops the reverb from
    having to borrow a delay.

    FRACTIONAL, AND WHY IT MATTERS. A delay of 23.4 ms at 48 kHz is 1123.2
    samples, and rounding that to 1123 is inaudible on its own. It stops being
    inaudible the moment the delay time moves: rounding makes the read pointer
    jump by a whole sample at a time, and a stepped read pointer is a click on
    every step. Interpolating between the two neighbouring samples lets the time
    glide continuously, which is what a delay is expected to do when its time is
    swept — the tape-machine behaviour everyone reaches for on purpose.

    Linear interpolation rather than anything higher order. It attenuates the
    top octave slightly as the fractional part approaches a half sample, and
    that is a fair description of a tape delay anyway; a Lagrange or allpass
    interpolator would buy a fraction of a decibel up there and cost either
    state that has to be reset on every time change or arithmetic on every
    sample of every repeat.

    REAL-TIME CONTRACT: `prepare` allocates and runs while audio is stopped.
    Everything else is audio-thread callable and allocates nothing.
*/

#include <cmath>
#include <vector>

namespace apollo::dsp
{

class DelayLine
{
public:
    DelayLine() = default;

    /** Sizes the buffer for @p maxDelaySamples of delay. Allocates.

        One sample of headroom is added so that the longest delay the caller
        asked for is still a *read behind the write*, rather than the write
        itself.
    */
    void prepare (int maxDelaySamples)
    {
        maximumDelay = maxDelaySamples > 1 ? maxDelaySamples : 1;

        buffer.assign (static_cast<std::size_t> (maximumDelay + 1), 0.0f);
        writeIndex = 0;
    }

    /** Clears the buffer without reallocating. Audio-thread safe.

        The whole buffer, not just the part currently in use: a delay time that
        is turned up after a reset would otherwise read samples the reset was
        supposed to have removed.
    */
    void reset() noexcept
    {
        for (auto& sample : buffer)
            sample = 0.0f;

        writeIndex = 0;
    }

    [[nodiscard]] int getMaximumDelay() const noexcept { return maximumDelay; }

    [[nodiscard]] bool isPrepared() const noexcept { return buffer.size() > 1; }

    /** Stores one sample and advances the write position. */
    void write (float sample) noexcept
    {
        if (buffer.empty())
            return;

        buffer[static_cast<std::size_t> (writeIndex)] = sample;

        ++writeIndex;

        if (writeIndex >= static_cast<int> (buffer.size()))
            writeIndex = 0;
    }

    /** @returns the sample @p delaySamples behind the write position,
        interpolated between its two neighbours.

        The delay is clamped to what the buffer can hold and to at least one
        sample: a delay of zero would read the sample about to be overwritten,
        which in a feedback loop is a division by nothing at all.
    */
    [[nodiscard]] float read (float delaySamples) const noexcept
    {
        if (buffer.empty())
            return 0.0f;

        const auto length = static_cast<int> (buffer.size());

        // Positive test first, so a NaN clamps to the minimum rather than
        // indexing the buffer with garbage (CLAUDE.md §34.2).
        auto delay = ! (delaySamples > 1.0f) ? 1.0f : delaySamples;

        if (delay > static_cast<float> (maximumDelay))
            delay = static_cast<float> (maximumDelay);

        const auto whole = static_cast<int> (delay);
        const auto fraction = delay - static_cast<float> (whole);

        auto first = writeIndex - whole;

        if (first < 0)
            first += length;

        auto second = first - 1;

        if (second < 0)
            second += length;

        const auto a = buffer[static_cast<std::size_t> (first)];
        const auto b = buffer[static_cast<std::size_t> (second)];

        return a + (b - a) * fraction;
    }

private:
    std::vector<float> buffer;

    int writeIndex = 0;
    int maximumDelay = 0;
};

} // namespace apollo::dsp
