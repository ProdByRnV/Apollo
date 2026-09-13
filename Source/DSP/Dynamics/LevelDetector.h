#pragma once

/*
    The level detector the dynamics processors share.

    A dynamics processor is two halves: something that decides how loud the
    signal currently is, and something that decides what to do about it. This is
    the first half, and it is separate because the two processors that use it
    disagree about it — the compressor wants RMS, the gate wants peak — while
    agreeing about everything else in how it is driven.

    PEAK OR RMS, AND WHY IT IS NOT A PREFERENCE. RMS is the average of the
    squared signal over a window: it follows how loud something *sounds*, which
    is what a compressor is for, and it deliberately ignores single-sample
    transients that no listener perceives as level. A gate has the opposite job.
    It is deciding whether there is a signal at all, and a transient arriving
    while the gate is shut is exactly the thing it must not miss — so it watches
    the peak, which reacts instantly and never underestimates.

    THE CONVENTION FOR ATTACK AND RELEASE. Both are one-pole ramps, and "10 ms
    attack" means the gain travels 1 - 1/e of the way to its target in 10 ms —
    about 63 %. That is the standard convention for a one-pole smoother and it is
    stated here because it is *a* convention rather than the only one: some
    designs quote 10 % to 90 %, which for the same filter is a different number.
    `Tests/DSP/DynamicsTests.cpp` measures against this definition.

    STEREO IS LINKED BY THE CALLER, NOT HERE. One detector per channel would let
    a loud left channel duck only the left, which pulls the stereo image toward
    the right every time something transient happens. The processors feed both
    channels into one detector by taking the larger, which is why this class has
    no opinion about channels at all.

    REAL-TIME CONTRACT: every function is audio-thread callable. None allocate,
    lock or perform I/O.
*/

#include <cmath>

namespace apollo::dsp
{

class LevelDetector
{
public:
    enum class Mode
    {
        /** Instantaneous magnitude. Never underestimates a transient. */
        peak = 0,

        /** Mean square over a window, square-rooted. Follows perceived loudness
            and ignores transients too short to hear as level.
        */
        rms
    };

    LevelDetector() = default;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        setAveragingTime (averagingMs);
        reset();
    }

    void reset() noexcept { state = 0.0f; }

    void setMode (Mode newMode) noexcept { mode = newMode; }

    [[nodiscard]] Mode getMode() const noexcept { return mode; }

    /** Sets the RMS window, in milliseconds. Ignored in peak mode.

        Short enough to follow a musical phrase, long enough not to follow the
        waveform itself: an RMS window shorter than a cycle of the lowest
        frequency present tracks the wave rather than the level, and the gain
        reduction then modulates at the signal's own frequency — which is
        distortion, not compression.
    */
    void setAveragingTime (float milliseconds) noexcept
    {
        averagingMs = milliseconds > 0.1f ? milliseconds : 0.1f;

        const auto samples = static_cast<double> (averagingMs) * 0.001 * sampleRate;

        averagingCoefficient = samples > 1.0
                                 ? static_cast<float> (1.0 - std::exp (-1.0 / samples))
                                 : 1.0f;
    }

    /** @returns the current level of @p input, in linear amplitude. */
    [[nodiscard]] float process (float input) noexcept
    {
        if (mode == Mode::peak)
            return std::abs (input);

        const auto squared = input * input;

        state += averagingCoefficient * (squared - state);

        // Guarded rather than trusted: a negative state can only come from
        // arithmetic that has already gone wrong, and `sqrt` of it would put a
        // NaN into the gain computer where it would stay (CLAUDE.md §34.2).
        return state > 0.0f ? std::sqrt (state) : 0.0f;
    }

private:
    Mode mode = Mode::rms;

    double sampleRate = 44100.0;

    float averagingMs = 10.0f;
    float averagingCoefficient = 1.0f;
    float state = 0.0f;
};

/** A one-pole gain ramp with separate attack and release times.

    Shared by both processors because "move towards the target quickly when it
    falls and slowly when it rises" — or the other way round — is the whole of
    what attack and release mean, and neither processor has a different idea
    about it.
*/
class GainRamp
{
public:
    GainRamp() = default;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        setTimes (attackMs, releaseMs);
        reset();
    }

    /** Jumps to unity gain: no reduction, nothing held over. */
    void reset() noexcept { current = 1.0f; }

    void setTimes (float newAttackMs, float newReleaseMs) noexcept
    {
        attackMs = newAttackMs;
        releaseMs = newReleaseMs;

        attackCoefficient = coefficientFor (attackMs);
        releaseCoefficient = coefficientFor (releaseMs);
    }

    [[nodiscard]] float getCurrentGain() const noexcept { return current; }

    /** Moves one sample towards @p target and @returns where it got to.

        Attack is the direction that *reduces* gain and release the direction
        that restores it, for both processors — a compressor attacks when it
        starts working and releases when it stops, and so does a gate closing
        and opening. Keeping the two words attached to the direction rather than
        to the processor is what lets one class serve both.
    */
    [[nodiscard]] float process (float target) noexcept
    {
        const auto coefficient = target < current ? attackCoefficient : releaseCoefficient;

        current += coefficient * (target - current);

        return current;
    }

private:
    [[nodiscard]] float coefficientFor (float milliseconds) const noexcept
    {
        const auto samples = static_cast<double> (milliseconds > 0.0f ? milliseconds : 0.0f)
                           * 0.001 * sampleRate;

        // A time of zero is instant rather than a division by zero: some gates
        // legitimately want to open in no time at all.
        return samples > 1.0 ? static_cast<float> (1.0 - std::exp (-1.0 / samples)) : 1.0f;
    }

    double sampleRate = 44100.0;

    float attackMs = 10.0f;
    float releaseMs = 100.0f;

    float attackCoefficient = 1.0f;
    float releaseCoefficient = 1.0f;

    float current = 1.0f;
};

} // namespace apollo::dsp
