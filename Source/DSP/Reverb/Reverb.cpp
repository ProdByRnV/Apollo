#include "DSP/Reverb/Reverb.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double controlRampSeconds = 0.02;

/** Pre-delay is ramped slowly for the same reason the delay's time is: moving
    it moves a read pointer, and a read pointer that jumps is a click.
*/
constexpr double preDelayRampSeconds = 0.1;

constexpr float denormalFloor = 1.0e-18f;

constexpr double pi = 3.14159265358979323846;

/** The rate the base lengths below are quoted at. */
constexpr double referenceSampleRate = 48000.0;

/** Base line lengths, in samples at 48 kHz, for each mode.

    Mutually prime, which is the whole point: lengths sharing a factor put their
    echoes on top of each other at the common multiple, and a reverb whose modes
    line up is a reverb with a pitch. The room set runs 26 to 57 ms and the hall
    set 52 to 119 ms, which is the physical difference between the two — a hall
    is not a room with a longer decay, it is a room whose walls are further away.
*/
constexpr int roomLengths[] { 1237, 1381, 1607, 1811, 2053, 2273, 2503, 2749 };
constexpr int hallLengths[] { 2477, 2963, 3413, 3853, 4327, 4783, 5233, 5711 };

/** Diffusion delays, in samples at 48 kHz, per channel.

    Short, mutually prime, and different for the two channels so the left and
    right inputs are smeared differently — which is what stops a centred sound
    from producing an identical tail on both sides.
*/
constexpr int diffusionLengths[2][Reverb::diffusionStages] {
    { 142, 379, 107, 277 },
    { 167, 331, 127, 293 },
};

constexpr float diffusionCoefficients[Reverb::diffusionStages] { 0.75f, 0.75f, 0.625f, 0.625f };

/** Gain applied to the input on its way into the network, and to each line on
    its way out.

    Both are 1/sqrt(4), because four lines carry each channel's input and four
    are summed into each output. That makes the pair energy-preserving for the
    decorrelated tail, which is what the steady-state level should follow.

    It also fixes a level problem that is easy to miss and unpleasant to meet:
    on the *first* pass every line carrying a given channel holds the same
    signal, so the output tap sums four correlated copies. Without this scaling
    that sum reaches twice the input — a reverb that is louder than the sound
    that caused it, which is not what any room does. With it the worst case is
    exactly unity, and `Tests/DSP/ReverbTests.cpp` asserts it.
*/
constexpr float injectionScale = 0.5f;

[[nodiscard]] float flush (float sample) noexcept
{
    return std::abs (sample) < denormalFloor ? 0.0f : sample;
}

/** An in-place fast Walsh-Hadamard transform over eight values.

    This is the feedback matrix. Written as butterflies rather than as an 8x8
    multiply because it is the same result for 24 additions instead of 64
    multiply-adds, and because the structure makes the property that matters
    visible: it is orthogonal, so it preserves energy exactly, and the only thing
    that changes the loop's energy is the decay gain applied afterwards.

    The 1/sqrt(8) scaling is what makes it orthonormal rather than merely
    orthogonal — without it the network would gain 8x per pass.
*/
void hadamard (std::array<float, Reverb::lineCount>& values) noexcept
{
    for (int step = 1; step < Reverb::lineCount; step <<= 1)
    {
        for (int i = 0; i < Reverb::lineCount; i += step * 2)
        {
            for (int j = i; j < i + step; ++j)
            {
                const auto a = values[static_cast<std::size_t> (j)];
                const auto b = values[static_cast<std::size_t> (j + step)];

                values[static_cast<std::size_t> (j)] = a + b;
                values[static_cast<std::size_t> (j + step)] = a - b;
            }
        }
    }

    constexpr float normalisation = 0.35355339059f; // 1 / sqrt(8)

    for (auto& value : values)
        value *= normalisation;
}

} // namespace

int Reverb::baseLength (Mode mode, int index) noexcept
{
    const auto clamped = index < 0 ? 0 : (index >= lineCount ? lineCount - 1 : index);

    return mode == Mode::room ? roomLengths[clamped] : hallLengths[clamped];
}

float Reverb::getLineLength (int index) const noexcept
{
    const auto clamped = index < 0 ? 0 : (index >= lineCount ? lineCount - 1 : index);

    return lineLengths[static_cast<std::size_t> (clamped)];
}

double Reverb::getTailSeconds() const noexcept
{
    return static_cast<double> (settings.decaySeconds)
         + static_cast<double> (settings.preDelayMs) * 0.001;
}

void Reverb::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;

    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    sampleRateScale = preparedSampleRate / referenceSampleRate;

    // Sized for the longest line the controls can ask for: the hall set at
    // maximum size. The read position moves inside this buffer when size
    // changes, so nothing is ever reallocated while audio is running.
    const auto longestBase = static_cast<double> (hallLengths[lineCount - 1]);

    maximumLineSamples = static_cast<int> (longestBase * sampleRateScale
                                           * static_cast<double> (maximumSizeScale)) + 2;

    maximumPreDelaySamples = static_cast<int> (maximumPreDelaySeconds * preparedSampleRate) + 2;

    for (auto& line : lines)
        line.prepare (maximumLineSamples);

    for (std::size_t channel = 0; channel < diffusers.size(); ++channel)
    {
        preDelay[channel].prepare (maximumPreDelaySamples);

        for (int stage = 0; stage < diffusionStages; ++stage)
        {
            const auto length = static_cast<int> (
                static_cast<double> (diffusionLengths[channel][stage]) * sampleRateScale);

            auto& stageFilter = diffusers[channel][static_cast<std::size_t> (stage)];

            stageFilter.prepare (length > 1 ? length : 1);
            stageFilter.setCoefficient (diffusionCoefficients[stage]);
        }
    }

    refreshLengths();
    refreshDamping();

    preDelaySamples.reset (preparedSampleRate, preDelayRampSeconds);
    width.reset (preparedSampleRate, controlRampSeconds);
    mix.reset (preparedSampleRate, controlRampSeconds);

    preDelaySamples.setCurrentAndTargetValue (
        static_cast<float> (static_cast<double> (settings.preDelayMs) * 0.001 * preparedSampleRate));
    width.setCurrentAndTargetValue (settings.width);
    mix.setCurrentAndTargetValue (settings.mix);
}

void Reverb::reset() noexcept
{
    for (auto& line : lines)
        line.reset();

    for (auto& damper : dampers)
        damper.state = 0.0f;

    for (auto& channel : diffusers)
        for (auto& stage : channel)
            stage.reset();

    for (auto& line : preDelay)
        line.reset();
}

void Reverb::refreshLengths() noexcept
{
    const auto scale = static_cast<double> (minimumSizeScale)
                     + static_cast<double> (settings.size)
                           * static_cast<double> (maximumSizeScale - minimumSizeScale);

    for (int i = 0; i < lineCount; ++i)
    {
        const auto length = static_cast<double> (baseLength (settings.mode, i)) * sampleRateScale * scale;

        lineLengths[static_cast<std::size_t> (i)] =
            static_cast<float> (length < 1.0 ? 1.0 : length);
    }

    refreshDecay();
}

void Reverb::refreshDecay() noexcept
{
    if (preparedSampleRate <= 0.0)
        return;

    const auto decay = settings.decaySeconds > 0.01f ? settings.decaySeconds : 0.01f;

    for (int i = 0; i < lineCount; ++i)
    {
        // The standard RT60 relation: a line of length L, traversed with gain g,
        // has fallen 60 dB after RT60 seconds when g = 10^(-3L / (RT60 * fs)).
        // Deriving each line's gain from its own length is what makes every line
        // decay at the same rate *in time* even though they are different
        // lengths — without it, short lines would die first and the tail would
        // change character as it faded.
        const auto seconds = static_cast<double> (lineLengths[static_cast<std::size_t> (i)])
                           / preparedSampleRate;

        const auto gain = std::pow (10.0, -3.0 * seconds / static_cast<double> (decay));

        // Below one by construction, which together with the orthogonal mix is
        // the whole stability argument.
        decayGains[static_cast<std::size_t> (i)] =
            static_cast<float> (gain > 0.999 ? 0.999 : gain);
    }
}

void Reverb::refreshDamping() noexcept
{
    dampingActive = settings.dampingHz < dampingOpenHz;

    if (preparedSampleRate <= 0.0)
    {
        dampingCoefficient = 1.0f;
        return;
    }

    const auto cutoff = static_cast<double> (settings.dampingHz);

    // One-pole coefficient: 1 - exp(-2 pi fc / fs). At the top of the range this
    // approaches 1, which is a filter that does nothing — which is why the
    // switch above exists rather than relying on the arithmetic to be
    // transparent.
    const auto coefficient = 1.0 - std::exp (-2.0 * pi * cutoff / preparedSampleRate);

    dampingCoefficient = static_cast<float> (coefficient > 1.0 ? 1.0 : (coefficient < 0.0 ? 0.0 : coefficient));
}

void Reverb::setSettings (const Settings& newSettings) noexcept
{
    if (newSettings == settings)
        return;

    const auto lengthsChanged = newSettings.mode != settings.mode
                             || newSettings.size != settings.size;
    const auto decayChanged = newSettings.decaySeconds != settings.decaySeconds;
    const auto dampingChanged = newSettings.dampingHz != settings.dampingHz;

    settings = newSettings;

    if (lengthsChanged)
        refreshLengths();     // which refreshes the decay gains too
    else if (decayChanged)
        refreshDecay();

    if (dampingChanged)
        refreshDamping();

    preDelaySamples.setTargetValue (
        static_cast<float> (static_cast<double> (settings.preDelayMs) * 0.001 * preparedSampleRate));
    width.setTargetValue (settings.width);
    mix.setTargetValue (settings.mix);
}

void Reverb::processBypassed (float* const* channels, int numChannels, int numSamples) noexcept
{
    (void) channels;
    (void) numChannels;

    if (numSamples <= 0)
        return;

    // Silence into the network rather than nothing at all, so a reverb switched
    // out and back in does not resume a tail from whenever it was switched out.
    //
    // Every stage of it, and the diffusers in particular: they are the one part
    // that is easy to forget, because they are in front of the network rather
    // than inside it. Left holding the last thing they heard, they would pour it
    // into the network on the first block after the effect came back — which is
    // exactly what this was written to prevent, arriving by a different route.
    std::array<float, lineCount> taps {};

    for (int i = 0; i < numSamples; ++i)
    {
        for (auto& line : preDelay)
            line.write (0.0f);

        for (auto& channel : diffusers)
            for (auto& stage : channel)
                (void) stage.processSample (0.0f);

        for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            auto& line = lines[static_cast<std::size_t> (lineIndex)];

            const auto tapped = line.read (lineLengths[static_cast<std::size_t> (lineIndex)]);

            taps[static_cast<std::size_t> (lineIndex)] =
                dampingActive
                  ? dampers[static_cast<std::size_t> (lineIndex)].process (tapped, dampingCoefficient)
                  : tapped;
        }

        hadamard (taps);

        for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex)
            lines[static_cast<std::size_t> (lineIndex)].write (
                flush (taps[static_cast<std::size_t> (lineIndex)]
                       * decayGains[static_cast<std::size_t> (lineIndex)]));
    }
}

void Reverb::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numSamples <= 0 || preparedSampleRate <= 0.0)
        return;

    auto* left = channels[0];

    if (left == nullptr)
        return;

    const auto stereo = numChannels >= 2 && channels[1] != nullptr;
    auto* right = stereo ? channels[1] : nullptr;

    std::array<float, lineCount> taps {};

    for (int i = 0; i < numSamples; ++i)
    {
        const auto dryLeft = left[i];
        const auto dryRight = stereo ? right[i] : dryLeft;

        const auto gap = preDelaySamples.getNextValue();
        const auto spread = width.getNextValue();
        const auto wetAmount = mix.getNextValue();

        preDelay[0].write (dryLeft);
        preDelay[1].write (dryRight);

        auto injectLeft = preDelay[0].read (gap);
        auto injectRight = preDelay[1].read (gap);

        for (int stage = 0; stage < diffusionStages; ++stage)
        {
            injectLeft = diffusers[0][static_cast<std::size_t> (stage)].processSample (injectLeft);
            injectRight = diffusers[1][static_cast<std::size_t> (stage)].processSample (injectRight);
        }

        // Read every line, damp what came out, and remember it for the mix.
        for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            auto& line = lines[static_cast<std::size_t> (lineIndex)];

            const auto tapped = line.read (lineLengths[static_cast<std::size_t> (lineIndex)]);

            taps[static_cast<std::size_t> (lineIndex)] =
                dampingActive
                  ? dampers[static_cast<std::size_t> (lineIndex)].process (tapped, dampingCoefficient)
                  : tapped;
        }

        // The output is taken *before* the mix, from what the lines actually
        // hold: even lines to the left, odd to the right. Taking it after the
        // matrix would sum eight copies of nearly the same thing and lose the
        // decorrelation the network exists to create.
        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;

        for (int lineIndex = 0; lineIndex < lineCount; lineIndex += 2)
        {
            wetLeft += taps[static_cast<std::size_t> (lineIndex)];
            wetRight += taps[static_cast<std::size_t> (lineIndex + 1)];
        }

        wetLeft *= injectionScale;
        wetRight *= injectionScale;

        hadamard (taps);

        for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            // The input is injected into half the lines on each side, so the two
            // channels enter the network at different places and the tail is
            // stereo from the first reflection rather than from the output taps
            // alone.
            const auto injection = lineIndex < lineCount / 2 ? injectLeft : injectRight;

            lines[static_cast<std::size_t> (lineIndex)].write (
                flush (taps[static_cast<std::size_t> (lineIndex)]
                           * decayGains[static_cast<std::size_t> (lineIndex)]
                       + injection * injectionScale));
        }

        // Width as a mid/side balance: at zero the tail collapses to the centre,
        // at one it is what the network produced.
        const auto mid = (wetLeft + wetRight) * 0.5f;
        const auto side = (wetLeft - wetRight) * 0.5f;

        const auto outLeft = mid + side * spread;
        const auto outRight = mid - side * spread;

        left[i] = dryLeft + (outLeft - dryLeft) * wetAmount;

        if (stereo)
            right[i] = dryRight + (outRight - dryRight) * wetAmount;
    }
}

} // namespace apollo::dsp
