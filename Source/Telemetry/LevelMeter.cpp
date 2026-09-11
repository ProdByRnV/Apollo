#include "Telemetry/LevelMeter.h"

#include <cmath>

namespace apollo::telemetry
{

LevelMeter::LevelMeter()
{
    reset();
}

void LevelMeter::prepare (double sampleRate) noexcept
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void LevelMeter::reset() noexcept
{
    heldPeak.fill (0.0f);
    meanSquare.fill (0.0f);

    clipCountdown = 0;

    for (auto& value : publishedPeak)
        value.store (0.0f, std::memory_order_relaxed);

    for (auto& value : publishedRms)
        value.store (0.0f, std::memory_order_relaxed);

    clipped.store (false, std::memory_order_release);
    active.store (false, std::memory_order_release);
}

void LevelMeter::process (const float* const* channels, int numChannels, int startSample,
                          int numSamples) noexcept
{
    // AUDIO THREAD.
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto blockSeconds = static_cast<double> (numSamples) / preparedSampleRate;

    // Decay applied once per block rather than per sample. The reading is only
    // ever looked at once per frame, and a per-sample exponential would be an
    // exp() per sample to produce a number nobody reads in between.
    const auto decayGain = static_cast<float> (
        std::pow (10.0, -meterPeakDecayDbPerSecond * blockSeconds / 20.0));

    // One-pole coefficient for the mean square, from the same block duration, so
    // the averaging window is the same length whatever the host's block size is.
    const auto rmsCoefficient = static_cast<float> (
        1.0 - std::exp (-blockSeconds / meterRmsSeconds));

    bool clippedThisBlock = false;

    for (int channel = 0; channel < meterChannels; ++channel)
    {
        // A mono layout reports the same signal on both sides rather than
        // leaving the second meter dead, which would read as a broken channel.
        const auto sourceChannel = channel < numChannels ? channel : numChannels - 1;
        const auto* samples = channels[sourceChannel];

        if (samples == nullptr)
            continue;

        float blockPeak = 0.0f;
        double sumOfSquares = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto value = samples[startSample + i];
            const auto magnitude = std::abs (value);

            if (magnitude > blockPeak)
                blockPeak = magnitude;

            sumOfSquares += static_cast<double> (value) * static_cast<double> (value);
        }

        if (blockPeak >= meterClipThreshold)
            clippedThisBlock = true;

        const auto index = static_cast<std::size_t> (channel);

        // Instant attack, slow release: a meter that smoothed its rise would
        // under-report exactly the transients worth reporting.
        heldPeak[index] = blockPeak > heldPeak[index] ? blockPeak
                                                      : heldPeak[index] * decayGain;

        const auto blockMeanSquare = static_cast<float> (
            sumOfSquares / static_cast<double> (numSamples));

        meanSquare[index] += (blockMeanSquare - meanSquare[index]) * rmsCoefficient;

        publishedPeak[index].store (heldPeak[index], std::memory_order_relaxed);
        publishedRms[index].store (std::sqrt (meanSquare[index]), std::memory_order_relaxed);
    }

    if (clippedThisBlock)
        clipCountdown = static_cast<int> (meterClipHoldSeconds * preparedSampleRate);
    else if (clipCountdown > 0)
        clipCountdown -= numSamples;

    clipped.store (clipCountdown > 0, std::memory_order_relaxed);

    // Published last, so a reader that sees the meter as active sees the
    // readings that made it so.
    active.store (true, std::memory_order_release);
}

LevelReading LevelMeter::read() const noexcept
{
    // MESSAGE THREAD.
    LevelReading reading;

    reading.active = active.load (std::memory_order_acquire);

    if (! reading.active)
        return reading;

    for (std::size_t i = 0; i < static_cast<std::size_t> (meterChannels); ++i)
    {
        reading.peak[i] = publishedPeak[i].load (std::memory_order_relaxed);
        reading.rms[i] = publishedRms[i].load (std::memory_order_relaxed);
    }

    reading.clipped = clipped.load (std::memory_order_relaxed);

    return reading;
}

} // namespace apollo::telemetry
