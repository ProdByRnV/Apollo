#include "DSP/Dynamics/Compressor.h"

namespace apollo::dsp
{

namespace
{

constexpr double controlRampSeconds = 0.02;

[[nodiscard]] float decibelsToGain (float decibels) noexcept
{
    return std::pow (10.0f, decibels * 0.05f);
}

[[nodiscard]] float gainToDecibels (float gain, float floor) noexcept
{
    return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : floor;
}

} // namespace

float Compressor::outputDbFor (const Settings& settings, float inputDb) noexcept
{
    const auto overshoot = inputDb - settings.thresholdDb;

    if (overshoot <= 0.0f)
        return inputDb;

    const auto ratio = settings.ratio > 1.0f ? settings.ratio : 1.0f;

    return settings.thresholdDb + overshoot / ratio;
}

void Compressor::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;

    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    detector.setMode (LevelDetector::Mode::rms);
    detector.setAveragingTime (averagingMs);
    detector.prepare (preparedSampleRate);

    ramp.prepare (preparedSampleRate);
    ramp.setTimes (settings.attackMs, settings.releaseMs);

    thresholdLinear = decibelsToGain (settings.thresholdDb);
    slope = settings.ratio > 1.0f ? 1.0f - 1.0f / settings.ratio : 0.0f;

    makeupGain.reset (preparedSampleRate, controlRampSeconds);
    mix.reset (preparedSampleRate, controlRampSeconds);

    makeupGain.setCurrentAndTargetValue (decibelsToGain (settings.makeupDb));
    mix.setCurrentAndTargetValue (settings.mix);
}

void Compressor::reset() noexcept
{
    detector.reset();
    ramp.reset();

    // Every smoothed control placed on its target, which is what reset means
    // across the whole rack (ADR-0060).
    makeupGain.settle();
    mix.settle();
}

float Compressor::getGainReductionDb() const noexcept
{
    return gainToDecibels (ramp.getCurrentGain(), floorDb);
}

void Compressor::setSettings (const Settings& newSettings) noexcept
{
    if (newSettings == settings && preparedSampleRate > 0.0)
        return;

    settings = newSettings;

    thresholdLinear = decibelsToGain (settings.thresholdDb);

    // The fraction of the overshoot that is removed. A ratio of 1 removes
    // nothing, 2 removes half, and 20 removes 95 per cent — close enough to
    // limiting that the remaining difference is academic.
    slope = settings.ratio > 1.0f ? 1.0f - 1.0f / settings.ratio : 0.0f;

    ramp.setTimes (settings.attackMs, settings.releaseMs);

    makeupGain.setTargetValue (decibelsToGain (settings.makeupDb));
    mix.setTargetValue (settings.mix);
}

void Compressor::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0 || preparedSampleRate <= 0.0)
        return;

    auto* left = channels[0];

    if (left == nullptr)
        return;

    const auto stereo = numChannels >= 2 && channels[1] != nullptr;
    auto* right = stereo ? channels[1] : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto dryLeft = left[i];
        const auto dryRight = stereo ? right[i] : 0.0f;

        // One detector, fed the louder channel: two would let a loud left
        // channel duck only the left, moving the stereo image every time the
        // compressor worked.
        const auto loudest = std::abs (dryLeft) > std::abs (dryRight) ? dryLeft : dryRight;

        const auto level = detector.process (loudest);

        auto target = 1.0f;

        if (level > thresholdLinear)
        {
            // Worked in decibels because that is the unit a ratio is defined
            // in: the overshoot above the threshold is reduced by the slope,
            // and what remains is the gain to apply.
            const auto overshootDb = gainToDecibels (level / thresholdLinear, 0.0f);

            target = decibelsToGain (-overshootDb * slope);
        }

        const auto gain = ramp.process (target);

        const auto makeup = makeupGain.getNextValue();
        const auto wetAmount = mix.getNextValue();

        const auto wetLeft = dryLeft * gain * makeup;
        left[i] = dryLeft + (wetLeft - dryLeft) * wetAmount;

        if (stereo)
        {
            const auto wetRight = dryRight * gain * makeup;
            right[i] = dryRight + (wetRight - dryRight) * wetAmount;
        }
    }
}

} // namespace apollo::dsp
