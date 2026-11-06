#pragma once

/*
    How loud the instrument is, as a modulation source (Phase 18e).

    An envelope follower turns a signal's level into a slowly moving value, so
    that something can follow how loud the sound is: a reverb that opens up in
    the gaps between notes, a filter that brightens as the playing gets harder,
    distortion that bites only on the loud hits. The amplitude envelope cannot
    do this. It describes one note; this describes what is actually coming out,
    chords and unison and resonance and all.

    WHAT IT LISTENS TO is the summed voices, before the effects and before the
    master gain (VoiceEngine::render). Before the effects, so that an effect it
    modulates is not also what it is measuring, which would be a loop; before the
    master, so that turning the instrument down does not change what the
    modulation does.

    PEAK, NOT RMS. The larger of the channels' magnitudes, rising at the attack
    and falling at the release, each a one-pole ramp with the convention the
    dynamics processors use: "10 ms" is 1 - 1/e of the way, about 63 %
    (LevelDetector.h). Peak because the attack control already decides how
    quickly it follows; an RMS window in front of it would be a second, hidden
    attack.

    THE VALUE IS 0 TO 1 ON A DECIBEL SCALE: −60 dBFS and below is 0, 0 dBFS is
    1, plus the gain. A linear value would spend almost all of its range on the
    top 10 dB, so a sound at a sensible level would hardly move anything; on
    decibels, every 6 dB of change moves a tenth of the way, however loud the
    patch is. The gain shifts that window, for a quiet patch or a loud one.

    REAL-TIME CONTRACT: every function is audio-thread callable. None allocate,
    lock or perform I/O.
*/

#include <algorithm>
#include <cmath>

namespace apollo::dsp
{

class EnvelopeFollower
{
public:
    /** The level read as 0, and the span to 1, in dB before the gain. */
    static constexpr float floorDb = -60.0f;
    static constexpr float spanDb = 60.0f;

    EnvelopeFollower() = default;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        setTimes (attackMs, releaseMs);
        reset();
    }

    /** Back to silence. */
    void reset() noexcept { level = 0.0f; }

    void setTimes (float newAttackMs, float newReleaseMs) noexcept
    {
        if (newAttackMs == attackMs && newReleaseMs == releaseMs && preparedRate == sampleRate)
            return;

        attackMs = newAttackMs;
        releaseMs = newReleaseMs;
        preparedRate = sampleRate;

        attackCoefficient = coefficientFor (attackMs);
        releaseCoefficient = coefficientFor (releaseMs);
    }

    void setGainDb (float newGainDb) noexcept
    {
        gainDb = std::isfinite (newGainDb) ? newGainDb : 0.0f;
    }

    /** Follows @p numSamples of @p numChannels channels from @p startSample. */
    void process (const float* const* channels, int numChannels, int startSample, int numSamples) noexcept
    {
        if (channels == nullptr || numChannels <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            auto peak = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
                if (const auto* samples = channels[channel])
                    peak = std::max (peak, std::abs (samples[startSample + i]));

            // A NaN or infinity from upstream is not a level: it is ignored
            // rather than allowed into the state, where it would stay
            // (CLAUDE.md §34.2).
            if (! std::isfinite (peak))
                peak = 0.0f;

            const auto coefficient = peak > level ? attackCoefficient : releaseCoefficient;
            level += coefficient * (peak - level);
        }

        // Flushed at the bottom rather than left to decay into denormals,
        // which a release of seconds would otherwise reach (CLAUDE.md §37).
        if (level < 1.0e-6f)
            level = 0.0f;
    }

    /** The level followed, in linear amplitude. */
    [[nodiscard]] float getLevel() const noexcept { return level; }

    /** The modulation value, 0 to 1. */
    [[nodiscard]] float getValue() const noexcept
    {
        if (level <= 0.0f)
            return 0.0f;

        const auto db = 20.0f * std::log10 (level) + gainDb;
        return std::clamp ((db - floorDb) / spanDb, 0.0f, 1.0f);
    }

private:
    [[nodiscard]] float coefficientFor (float milliseconds) const noexcept
    {
        const auto samples = static_cast<double> (milliseconds > 0.0f ? milliseconds : 0.0f) * 0.001 * sampleRate;
        return samples > 1.0 ? static_cast<float> (1.0 - std::exp (-1.0 / samples)) : 1.0f;
    }

    double sampleRate = 44100.0;
    double preparedRate = 0.0;

    float attackMs = 10.0f;
    float releaseMs = 200.0f;
    float gainDb = 0.0f;

    float attackCoefficient = 1.0f;
    float releaseCoefficient = 1.0f;

    float level = 0.0f;
};

} // namespace apollo::dsp
