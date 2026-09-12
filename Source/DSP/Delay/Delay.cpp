#include "DSP/Delay/Delay.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

/** Ramp for the controls that are removing a step rather than being a glide. */
constexpr double controlRampSeconds = 0.02;

/** Ramp for the delay time itself. See `delaySamples` in the header. */
constexpr double timeRampSeconds = 0.1;

/** Anything smaller than this is flushed to zero on its way back into the loop.

    A feedback path is where denormals live: a repeat decays towards zero and,
    once it is small enough, every multiply lands in the denormal range where
    some processors take orders of magnitude longer (CLAUDE.md §37). The plugin
    also runs with denormals flushed by the host callback, but this class is
    reachable without that — from a test, from an offline renderer — and a tail
    that costs a hundred times more in one caller than another is not a property
    worth having.
*/
constexpr float denormalFloor = 1.0e-18f;

[[nodiscard]] float flush (float sample) noexcept
{
    return std::abs (sample) < denormalFloor ? 0.0f : sample;
}

} // namespace

double Delay::beatsFor (Division division) noexcept
{
    switch (division)
    {
        case Division::whole:            return 4.0;
        case Division::halfDotted:       return 3.0;
        case Division::half:             return 2.0;
        case Division::halfTriplet:      return 4.0 / 3.0;
        case Division::quarterDotted:    return 1.5;
        case Division::quarterTriplet:   return 2.0 / 3.0;
        case Division::eighthDotted:     return 0.75;
        case Division::eighth:           return 0.5;
        case Division::eighthTriplet:    return 1.0 / 3.0;
        case Division::sixteenthDotted:  return 0.375;
        case Division::sixteenth:        return 0.25;
        case Division::sixteenthTriplet: return 1.0 / 6.0;
        case Division::thirtySecond:     return 0.125;

        case Division::quarter:
        default:
            return 1.0;
    }
}

double Delay::getDelaySeconds() const noexcept
{
    const auto seconds = settings.tempoSynced
                           ? beatsFor (settings.division) * 60.0 / tempoBpm
                           : static_cast<double> (settings.timeMs) * 0.001;

    // Clamped rather than wrapped: a whole note at 40 BPM is six seconds, and a
    // delay that silently became a different note value would be worse than one
    // that stops getting longer.
    return seconds > maximumDelaySeconds ? maximumDelaySeconds : seconds;
}

void Delay::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;

    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    maximumDelaySamples = static_cast<int> (maximumDelaySeconds * preparedSampleRate);

    for (auto& channel : channelState)
        channel.line.prepare (maximumDelaySamples);

    refreshFilters();

    delaySamples.reset (preparedSampleRate, timeRampSeconds);
    feedbackGain.reset (preparedSampleRate, controlRampSeconds);
    mix.reset (preparedSampleRate, controlRampSeconds);

    delaySamples.setCurrentAndTargetValue (
        static_cast<float> (getDelaySeconds() * preparedSampleRate));
    feedbackGain.setCurrentAndTargetValue (settings.feedback * maximumFeedback);
    mix.setCurrentAndTargetValue (settings.mix);
}

void Delay::reset() noexcept
{
    for (auto& channel : channelState)
    {
        channel.line.reset();
        channel.damping.reset();
        channel.lowCut.reset();
    }

    // The time is not ramped from wherever it was: a reset is a transport jump
    // or a device change, and gliding to the current time from the last one
    // would be an audible artefact of something the user did not do.
    delaySamples.setCurrentAndTargetValue (
        static_cast<float> (getDelaySeconds() * preparedSampleRate));
}

void Delay::refreshTime() noexcept
{
    if (preparedSampleRate <= 0.0)
        return;

    delaySamples.setTargetValue (static_cast<float> (getDelaySeconds() * preparedSampleRate));
}

void Delay::refreshFilters() noexcept
{
    dampingActive = settings.dampingHz < dampingOpenHz;
    lowCutActive = settings.lowCutHz > lowCutFlatHz;

    dampingCoefficients.set (settings.dampingHz, 0.707f, preparedSampleRate);
    lowCutCoefficients.set (settings.lowCutHz, 0.707f, preparedSampleRate);

    for (auto& channel : channelState)
    {
        channel.damping.setCoefficients (dampingCoefficients);
        channel.damping.setMode (dampingActive ? StateVariableFilter::Mode::lowpass
                                               : StateVariableFilter::Mode::off);

        channel.lowCut.setCoefficients (lowCutCoefficients);
        channel.lowCut.setMode (lowCutActive ? StateVariableFilter::Mode::highpass
                                             : StateVariableFilter::Mode::off);
    }
}

void Delay::setSettings (const Settings& newSettings) noexcept
{
    if (newSettings == settings)
        return;

    const auto filtersChanged = newSettings.dampingHz != settings.dampingHz
                             || newSettings.lowCutHz != settings.lowCutHz;

    settings = newSettings;

    refreshTime();

    feedbackGain.setTargetValue (settings.feedback * maximumFeedback);
    mix.setTargetValue (settings.mix);

    if (filtersChanged)
        refreshFilters();
}

void Delay::setTempo (double bpm) noexcept
{
    const auto usable = bpm > 1.0 && bpm < 1000.0 ? bpm : fallbackBpm;

    if (usable == tempoBpm)
        return;

    tempoBpm = usable;

    // Only a synced delay cares, and a free one must not be dragged around by a
    // tempo map it is not following.
    if (settings.tempoSynced)
        refreshTime();
}

double Delay::getTailSeconds() const noexcept
{
    const auto time = getDelaySeconds();
    const auto gain = static_cast<double> (settings.feedback * maximumFeedback);

    if (! (gain > 0.0))
        return time;

    // How many repeats it takes for the loop gain to reach -60 dB. The damping
    // filter makes the real tail shorter than this, which is the right way for
    // the estimate to be wrong: a host that renders a little too much silence
    // has lost nothing, one that truncates the tail has lost the ending.
    const auto repeats = std::log (0.001) / std::log (gain);
    const auto tail = time * repeats;

    constexpr double longestReportedTail = 30.0;

    return tail > longestReportedTail ? longestReportedTail : tail;
}

void Delay::processBypassed (float* const* channels, int numChannels, int numSamples) noexcept
{
    (void) channels;
    (void) numChannels;

    if (numSamples <= 0)
        return;

    // The audio passes through untouched — the delay adds no latency, so there
    // is nothing to compensate — but the lines are fed silence rather than left
    // holding what was in them. Otherwise switching the effect back in would
    // replay whatever was playing when it was switched out, seconds later and
    // without warning.
    for (int i = 0; i < numSamples; ++i)
    {
        channelState[0].line.write (0.0f);
        channelState[1].line.write (0.0f);
    }
}

void Delay::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0 || preparedSampleRate <= 0.0)
        return;

    auto* left = channels[0];

    if (left == nullptr)
        return;

    const auto stereo = numChannels >= 2 && channels[1] != nullptr;
    auto* right = stereo ? channels[1] : nullptr;

    auto& stateLeft = channelState[0];
    auto& stateRight = channelState[1];

    // Both channels are advanced inside one loop over samples rather than one
    // loop per channel, and not only for tidiness: in ping-pong the two lines
    // read each other, so a channel-at-a-time pass would have to keep a block's
    // worth of the other channel's output to feed itself with.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto delay = delaySamples.getNextValue();
        const auto feedback = feedbackGain.getNextValue();
        const auto wetAmount = mix.getNextValue();

        const auto dryLeft = left[i];
        const auto dryRight = stereo ? right[i] : 0.0f;

        const auto wetLeft = stateLeft.line.read (delay);
        const auto wetRight = stereo ? stateRight.line.read (delay) : 0.0f;

        // Filtered on the way back in, so each repeat is darker than the one
        // before it rather than every repeat being equally dark.
        const auto loopLeft = flush (stateLeft.damping.processSample (
            stateLeft.lowCut.processSample (wetLeft)));

        const auto loopRight = stereo
                                 ? flush (stateRight.damping.processSample (
                                       stateRight.lowCut.processSample (wetRight)))
                                 : 0.0f;

        if (stereo && settings.pingPong)
        {
            // Crossed: what left played comes back on the right, and vice
            // versa, which is what makes a repeat alternate sides.
            stateLeft.line.write (dryLeft + loopRight * feedback);
            stateRight.line.write (dryRight + loopLeft * feedback);
        }
        else
        {
            stateLeft.line.write (dryLeft + loopLeft * feedback);

            if (stereo)
                stateRight.line.write (dryRight + loopRight * feedback);
        }

        left[i] = dryLeft + (wetLeft - dryLeft) * wetAmount;

        if (stereo)
            right[i] = dryRight + (wetRight - dryRight) * wetAmount;
    }
}

} // namespace apollo::dsp
