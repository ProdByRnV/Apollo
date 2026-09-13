#include "DSP/Dynamics/NoiseGate.h"

namespace apollo::dsp
{

namespace
{

[[nodiscard]] float decibelsToGain (float decibels) noexcept
{
    return std::pow (10.0f, decibels * 0.05f);
}

} // namespace

void NoiseGate::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;

    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    detector.setMode (LevelDetector::Mode::peak);
    detector.prepare (preparedSampleRate);

    ramp.prepare (preparedSampleRate);

    // Through the same path `setSettings` uses, rather than repeating the
    // derivations here. An earlier version did repeat them and got one of them
    // wrong — it set the ramp's times without the attack/release swap below, so
    // a prepared gate opened slowly and shut quickly, which is precisely
    // backwards and made it deaf to anything short.
    refreshDerived();

    holdCountdown = 0;
}

void NoiseGate::reset() noexcept
{
    detector.reset();
    ramp.reset();

    holdCountdown = 0;
}

void NoiseGate::setSettings (const Settings& newSettings) noexcept
{
    if (newSettings == settings && preparedSampleRate > 0.0)
        return;

    settings = newSettings;

    refreshDerived();
}

void NoiseGate::refreshDerived() noexcept
{
    thresholdLinear = decibelsToGain (settings.thresholdDb);

    // A range at the bottom of its travel is silence rather than -79 dB, so a
    // user who wants the channel gone gets it gone.
    closedGain = settings.rangeDb <= silentRangeDb ? 0.0f : decibelsToGain (settings.rangeDb);

    // Attack opens the gate and release shuts it, which is the opposite of the
    // ramp's own convention — the ramp calls the direction that *reduces* gain
    // the attack. Swapping them here is what lets the ramp be shared with the
    // compressor while the controls keep the names a gate is expected to have.
    // It is also the one line in this class that must not be written twice.
    ramp.setTimes (settings.releaseMs, settings.attackMs);

    holdSamples = static_cast<int> (static_cast<double> (settings.holdMs) * 0.001 * preparedSampleRate);
}

void NoiseGate::process (float* const* channels, int numChannels, int numSamples) noexcept
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
        const auto sampleLeft = left[i];
        const auto sampleRight = stereo ? right[i] : sampleLeft;

        // One detector for both channels, fed the louder of the two: separate
        // detectors would let a loud left channel gate only the left, which
        // pulls the image sideways every time anything transient happens.
        const auto loudest = std::abs (sampleLeft) > std::abs (sampleRight)
                               ? sampleLeft
                               : sampleRight;

        const auto level = detector.process (loudest);

        if (level > thresholdLinear)
            holdCountdown = holdSamples;
        else if (holdCountdown > 0)
            --holdCountdown;

        // Open while the signal is above the threshold *or* while the hold
        // timer is still running. The second half is what stops a signal
        // sitting at the threshold from strobing the gate.
        const auto target = level > thresholdLinear || holdCountdown > 0 ? 1.0f : closedGain;

        const auto gain = ramp.process (target);

        left[i] = sampleLeft * gain;

        if (stereo)
            right[i] = sampleRight * gain;
    }
}

} // namespace apollo::dsp
