#pragma once

/*
    A minimal linear parameter ramp.

    Apollo already uses juce::SmoothedValue in the processor, and this is
    deliberately not a replacement for it. It exists because the synthesis engine
    lives in apollo_core, which is JUCE-free by design (ARCHITECTURE.md §2): the
    voice must be testable without a host, and it is the only library held to
    Apollo's strict warning set (ADR-0005). Pulling juce_core into it to get one
    ramp would trade both of those away for forty lines.

    Linear rather than multiplicative. The values ramped here — a per-source
    level and a pair of pan gains — legitimately reach zero, which a
    multiplicative ramp cannot represent. Master gain, which never reaches zero
    because its floor is -60 dB, keeps its multiplicative smoother in the
    processor where dB-linear travel is what a user expects from a fader.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O.
*/

namespace apollo::dsp
{

class LinearSmoothedValue
{
public:
    LinearSmoothedValue() = default;

    /** Sets the ramp length. Safe on the audio thread; allocates nothing.

        A non-positive sample rate or ramp length degrades to "instant" rather
        than producing a division by zero or an infinite step count.
    */
    void reset (double sampleRate, double rampSeconds) noexcept
    {
        const auto samples = sampleRate > 0.0 && rampSeconds > 0.0 ? sampleRate * rampSeconds : 0.0;

        rampLengthSamples = samples > 1.0 ? static_cast<int> (samples) : 1;

        setCurrentAndTargetValue (target);
    }

    /** Jumps to a value with no ramp. Used when a voice starts, so a fresh note
        begins at the current parameter value instead of gliding up to it from
        wherever the last note left this voice.
    */
    void setCurrentAndTargetValue (float newValue) noexcept
    {
        current = newValue;
        target = newValue;
        step = 0.0f;
        countdown = 0;
    }

    void setTargetValue (float newValue) noexcept
    {
        if (newValue == target)
            return;

        target = newValue;

        if (rampLengthSamples <= 1)
        {
            setCurrentAndTargetValue (newValue);
            return;
        }

        countdown = rampLengthSamples;
        step = (target - current) / static_cast<float> (rampLengthSamples);
    }

    /** @returns the next value in the ramp, advancing it by one sample. */
    [[nodiscard]] float getNextValue() noexcept
    {
        if (countdown <= 0)
            return current;

        --countdown;

        if (countdown == 0)
        {
            // Assigned rather than accumulated, so repeated ramps cannot drift
            // away from the target through accumulated rounding.
            current = target;
            step = 0.0f;
        }
        else
        {
            current += step;
        }

        return current;
    }

    [[nodiscard]] float getCurrentValue() const noexcept { return current; }
    [[nodiscard]] float getTargetValue() const noexcept { return target; }
    [[nodiscard]] bool isSmoothing() const noexcept { return countdown > 0; }

private:
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;

    int countdown = 0;
    int rampLengthSamples = 1;
};

} // namespace apollo::dsp
