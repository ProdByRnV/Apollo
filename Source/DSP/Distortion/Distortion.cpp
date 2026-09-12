#include "DSP/Distortion/Distortion.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

/** Ramp length for the smoothed controls.

    20 ms is the figure the master gain already uses: long enough that an
    automated move has no step in it, short enough that a deliberate turn still
    feels immediate.
*/
constexpr double rampSeconds = 0.02;

/** Asymmetry of the diode curve.

    The negative half saturates at this fraction of the positive half's
    asymptote. Both halves have a derivative of exactly 1 at zero, so the curve
    is continuous and smooth through the origin and a quiet signal passes
    through whichever mode is selected unchanged.
*/
constexpr float diodeNegativeCeiling = 0.6f;

[[nodiscard]] float decibelsToGain (float decibels) noexcept
{
    return std::pow (10.0f, decibels * 0.05f);
}

} // namespace

float Distortion::shape (Mode mode, float input) noexcept
{
    switch (mode)
    {
        case Mode::hard:
            // Positive test first so a NaN clamps rather than propagating
            // (CLAUDE.md §34.2).
            if (! (input > -1.0f))
                return -1.0f;

            return input < 1.0f ? input : 1.0f;

        case Mode::diode:
            if (input >= 0.0f)
                return 1.0f - std::exp (-input);

            return -diodeNegativeCeiling * (1.0f - std::exp (input / diodeNegativeCeiling));

        case Mode::soft:
        default:
            return std::tanh (input);
    }
}

float Distortion::compensationFor (Mode mode, float driveGain) noexcept
{
    // What the shaper does to a reference level, divided out. The reference is
    // shaped *with* the drive applied, which is the whole point: at unity drive
    // the ratio is 1 and the control does nothing, and by the top of the range
    // it is holding back most of the gain that was added.
    const auto shaped = shape (mode, driveGain * referenceLevel);

    // A curve that flattens completely would divide by zero. None of the three
    // does at any reachable drive, but the floor costs one comparison and
    // removes the possibility entirely.
    constexpr float smallest = 1.0e-4f;

    return shaped > smallest ? referenceLevel / shaped : 1.0f;
}

void Distortion::prepare (double sampleRate, int maxBlockSize)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize = maxBlockSize > 0 ? maxBlockSize : 0;

    dcBlockCoefficients.set (dcBlockHz, 0.707f, sampleRate);
    toneCoefficients.set (settings.toneHz, 0.707f, sampleRate);
    toneActive = settings.toneHz < toneOpenHz;

    latencySamples = 0;

    for (auto& channel : channelState)
    {
        channel.oversampler.prepare (oversamplingFactor, preparedBlockSize);

        latencySamples = channel.oversampler.getLatencySamples();

        channel.preHighpass.setCoefficients (dcBlockCoefficients);
        channel.preHighpass.setMode (StateVariableFilter::Mode::highpass);

        channel.postHighpass.setCoefficients (dcBlockCoefficients);
        channel.postHighpass.setMode (StateVariableFilter::Mode::highpass);

        channel.toneLowpass.setCoefficients (toneCoefficients);
        channel.toneLowpass.setMode (toneActive ? StateVariableFilter::Mode::lowpass
                                                : StateVariableFilter::Mode::off);

        channel.work.assign (static_cast<std::size_t> (preparedBlockSize), 0.0f);
        channel.dry.assign (static_cast<std::size_t> (preparedBlockSize), 0.0f);
        channel.dryLine.assign (static_cast<std::size_t> (latencySamples > 0 ? latencySamples : 0), 0.0f);
        channel.dryIndex = 0;
    }

    driveGain.reset (sampleRate, rampSeconds);
    compensation.reset (sampleRate, rampSeconds);
    mix.reset (sampleRate, rampSeconds);
    outputGain.reset (sampleRate, rampSeconds);

    const auto drive = decibelsToGain (settings.driveDb);

    driveGain.setCurrentAndTargetValue (drive);
    compensation.setCurrentAndTargetValue (compensationFor (settings.mode, drive));
    mix.setCurrentAndTargetValue (settings.mix);
    outputGain.setCurrentAndTargetValue (decibelsToGain (settings.outputDb));
}

void Distortion::reset() noexcept
{
    for (auto& channel : channelState)
    {
        channel.oversampler.reset();
        channel.preHighpass.reset();
        channel.postHighpass.reset();
        channel.toneLowpass.reset();

        for (auto& sample : channel.dryLine)
            sample = 0.0f;

        channel.dryIndex = 0;
    }
}

void Distortion::setSettings (const Settings& newSettings) noexcept
{
    if (newSettings == settings)
        return;

    const auto toneChanged = newSettings.toneHz != settings.toneHz;

    settings = newSettings;

    const auto drive = decibelsToGain (settings.driveDb);

    driveGain.setTargetValue (drive);
    compensation.setTargetValue (compensationFor (settings.mode, drive));
    mix.setTargetValue (settings.mix);
    outputGain.setTargetValue (decibelsToGain (settings.outputDb));

    if (toneChanged)
    {
        toneCoefficients.set (settings.toneHz, 0.707f, preparedSampleRate);
        toneActive = settings.toneHz < toneOpenHz;

        for (auto& channel : channelState)
        {
            channel.toneLowpass.setCoefficients (toneCoefficients);
            channel.toneLowpass.setMode (toneActive ? StateVariableFilter::Mode::lowpass
                                                    : StateVariableFilter::Mode::off);
        }
    }

    // A mode change is deliberately not ramped. The curves share a derivative
    // of 1 at the origin and the compensation ramps with the drive, so what
    // changes at the switch is the harmonic content rather than the level —
    // and crossfading two transfer curves would produce, for 20 ms, a fourth
    // curve that is neither of them.
}

void Distortion::writeDry (const float* input, int channel, int numSamples) noexcept
{
    auto& state = channelState[static_cast<std::size_t> (channel)];

    if (state.dryLine.empty())
    {
        for (int i = 0; i < numSamples; ++i)
            state.dry[static_cast<std::size_t> (i)] = input[i];

        return;
    }

    const auto length = static_cast<int> (state.dryLine.size());

    // Read before write, which is what makes the delay exactly `length`
    // samples rather than one short or one long.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto index = static_cast<std::size_t> (state.dryIndex);

        state.dry[static_cast<std::size_t> (i)] = state.dryLine[index];
        state.dryLine[index] = input[i];

        state.dryIndex = state.dryIndex + 1 < length ? state.dryIndex + 1 : 0;
    }
}

void Distortion::processBypassed (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0)
        return;

    const auto channelsToProcess = numChannels < maxEffectChannels ? numChannels : maxEffectChannels;

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto* samples = channels[channel];

        if (samples == nullptr)
            continue;

        // The delay is applied and nothing else is. The unit reports the same
        // latency bypassed as active, so the host's compensation stays correct
        // across the switch (ADR-0054).
        writeDry (samples, channel, numSamples);

        const auto& dry = channelState[static_cast<std::size_t> (channel)].dry;

        for (int i = 0; i < numSamples; ++i)
            samples[i] = dry[static_cast<std::size_t> (i)];
    }
}

void Distortion::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0 || numSamples > preparedBlockSize)
        return;

    // Fully dry and not on its way anywhere: the shaper would be multiplied by
    // zero, so it is not run at all. The delay still is, because latency must
    // not depend on a mix control any more than it depends on bypass.
    if (settings.mix <= 0.0f && ! mix.isSmoothing())
    {
        processBypassed (channels, numChannels, numSamples);
        return;
    }

    const auto channelsToProcess = numChannels < maxEffectChannels ? numChannels : maxEffectChannels;
    const auto factor = channelState[0].oversampler.getFactorAsInt();

    // The smoothers are stepped once per channel, so the second channel must
    // travel the same ramp as the first rather than continuing from where the
    // first left off. Their state before the first channel is the state every
    // channel starts from.
    const auto driveStart = driveGain;
    const auto compensationStart = compensation;
    const auto mixStart = mix;
    const auto outputStart = outputGain;

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto* samples = channels[channel];

        if (samples == nullptr)
            continue;

        auto& state = channelState[static_cast<std::size_t> (channel)];

        driveGain = driveStart;
        compensation = compensationStart;
        mix = mixStart;
        outputGain = outputStart;

        writeDry (samples, channel, numSamples);

        for (int i = 0; i < numSamples; ++i)
            state.work[static_cast<std::size_t> (i)] = state.preHighpass.processSample (samples[i]);

        auto* wide = state.oversampler.upsample (state.work.data(), numSamples);

        if (wide == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // One drive value per base-rate sample, held across the four
            // oversampled samples it becomes. A ramp that stepped per
            // oversampled sample would be four times faster than the 20 ms it
            // is meant to be, and nothing can hear the difference between the
            // two at this ramp length.
            const auto drive = driveGain.getNextValue();
            const auto trim = compensation.getNextValue();
            const auto base = i * factor;

            for (int step = 0; step < factor; ++step)
            {
                auto& sample = wide[base + step];
                sample = shape (settings.mode, sample * drive) * trim;
            }
        }

        state.oversampler.downsample (state.work.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto wetWeight = mix.getNextValue();
            const auto level = outputGain.getNextValue();

            // The asymmetric curve leaves a DC offset behind, and the tone
            // control takes the top back off. Both run on the wet path only:
            // the dry path is the signal that arrived and must leave unaltered.
            auto wet = state.postHighpass.processSample (state.work[static_cast<std::size_t> (i)]);
            wet = state.toneLowpass.processSample (wet) * level;

            const auto dry = state.dry[static_cast<std::size_t> (i)];

            samples[i] = dry * (1.0f - wetWeight) + wet * wetWeight;
        }
    }
}

} // namespace apollo::dsp
