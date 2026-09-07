#include "DSP/Oscillators/Wavetable.h"

#include <cmath>

namespace apollo::dsp
{

int Wavetable::selectMipLevel (double frequencyHz, double sampleRate) noexcept
{
    if (! (frequencyHz > 0.0) || ! (sampleRate > 0.0))
        return numMipLevels - 1;

    // The most harmonics that still fit below Nyquist at this pitch.
    const auto allowedHarmonics = (sampleRate * 0.5) / frequencyHz;

    for (int level = 0; level < numMipLevels; ++level)
        if (static_cast<double> (harmonicsAtLevel (level)) <= allowedHarmonics)
            return level;

    // Above roughly Nyquist/2 even a single harmonic would alias; the sine level
    // is the safest thing left, and the caller has bigger problems.
    return numMipLevels - 1;
}

void Wavetable::setSize (int newNumFrames)
{
    numFrames = newNumFrames > 0 ? newNumFrames : 0;

    levels.assign (static_cast<std::size_t> (numMipLevels), {});

    if (numFrames == 0)
        return;

    for (int level = 0; level < numMipLevels; ++level)
    {
        const auto samples = static_cast<std::size_t> (samplesAtLevel (level))
                           * static_cast<std::size_t> (numFrames);

        levels[static_cast<std::size_t> (level)].assign (samples, 0.0f);
    }
}

std::size_t Wavetable::offsetOf (int level, int frameIndex) const noexcept
{
    return static_cast<std::size_t> (frameIndex) * static_cast<std::size_t> (samplesAtLevel (level));
}

float* Wavetable::getWritePointer (int level, int frameIndex) noexcept
{
    if (level < 0 || level >= numMipLevels || frameIndex < 0 || frameIndex >= numFrames)
        return nullptr;

    return levels[static_cast<std::size_t> (level)].data() + offsetOf (level, frameIndex);
}

const float* Wavetable::getReadPointer (int level, int frameIndex) const noexcept
{
    if (level < 0 || level >= numMipLevels || frameIndex < 0 || frameIndex >= numFrames)
        return nullptr;

    return levels[static_cast<std::size_t> (level)].data() + offsetOf (level, frameIndex);
}

float Wavetable::getSample (int level, int frameIndex, double phase) const noexcept
{
    const auto* frame = getReadPointer (level, frameIndex);

    if (frame == nullptr)
        return 0.0f;

    const auto size = samplesAtLevel (level);
    const auto position = phase * static_cast<double> (size);

    auto index = static_cast<int> (position);
    const auto fraction = position - static_cast<double> (index);

    // Guards against a phase of exactly 1.0, or a denormal drifting negative.
    index %= size;

    if (index < 0)
        index += size;

    // 4-point cubic Hermite. The table is periodic, so the neighbours wrap
    // rather than clamp — clamping would flatten the waveform at the wrap point
    // and put a discontinuity in every cycle.
    const auto wrap = [size] (int i) noexcept
    {
        i %= size;
        return i < 0 ? i + size : i;
    };

    const auto y0 = static_cast<double> (frame[wrap (index - 1)]);
    const auto y1 = static_cast<double> (frame[index]);
    const auto y2 = static_cast<double> (frame[wrap (index + 1)]);
    const auto y3 = static_cast<double> (frame[wrap (index + 2)]);

    const auto c0 = y1;
    const auto c1 = 0.5 * (y2 - y0);
    const auto c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const auto c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);

    return static_cast<float> (((c3 * fraction + c2) * fraction + c1) * fraction + c0);
}

float Wavetable::getSampleAtPosition (int level, double framePosition, double phase) const noexcept
{
    if (numFrames <= 0)
        return 0.0f;

    if (numFrames == 1)
        return getSample (level, 0, phase);

    const auto maxFrame = static_cast<double> (numFrames - 1);
    const auto clamped = framePosition < 0.0 ? 0.0 : (framePosition > maxFrame ? maxFrame : framePosition);

    const auto lowerFrame = static_cast<int> (clamped);
    const auto upperFrame = lowerFrame + 1 < numFrames ? lowerFrame + 1 : lowerFrame;
    const auto blend = static_cast<float> (clamped - static_cast<double> (lowerFrame));

    const auto lower = getSample (level, lowerFrame, phase);
    const auto upper = getSample (level, upperFrame, phase);

    return lower + (upper - lower) * blend;
}

} // namespace apollo::dsp
