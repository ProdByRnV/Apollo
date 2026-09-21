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

Wavetable::Reader Wavetable::makeReader (int level, double framePosition) const noexcept
{
    Reader reader;

    if (numFrames <= 0 || level < 0 || level >= numMipLevels)
        return reader;

    const auto size = samplesAtLevel (level);

    // A non-finite frame position would otherwise reach the cast below, and
    // converting a NaN to int is undefined behaviour exactly as converting an
    // out-of-range double is (see `read`). Neither comparison in the clamp is
    // true for a NaN, so it would pass straight through.
    const auto requested = std::isfinite (framePosition) ? framePosition : 0.0;

    auto lowerIndex = 0;
    auto blend = 0.0;

    if (numFrames > 1)
    {
        const auto maxFrame = static_cast<double> (numFrames - 1);
        const auto clamped = requested < 0.0 ? 0.0 : (requested > maxFrame ? maxFrame : requested);

        lowerIndex = static_cast<int> (clamped);
        blend = clamped - static_cast<double> (lowerIndex);
    }

    const auto* base = levels[static_cast<std::size_t> (level)].data() + offsetOf (level, lowerIndex);

    reader.lowerFrame = base;

    // Frames of a level live back to back in one buffer, so the next frame is a
    // pointer offset rather than a second bounds-checked lookup. Pointing the
    // upper frame at the lower one is how a degenerate blend is expressed: the
    // last frame has no successor, and a blend of exactly zero would read the
    // second frame only to multiply it by nothing.
    reader.upperFrame = (lowerIndex + 1 < numFrames && blend != 0.0) ? base + size : base;

    reader.blend = blend;
    reader.size = size;
    reader.mask = size - 1;

    return reader;
}

float Wavetable::getSample (int level, int frameIndex, double phase) const noexcept
{
    // Bounds-checked here rather than in makeReader, which *clamps* a frame
    // position because that is what scanning wants. Asking for a frame that
    // does not exist is a different thing from scanning past the end, and it
    // produces silence.
    if (frameIndex < 0 || frameIndex >= numFrames)
        return 0.0f;

    return makeReader (level, static_cast<double> (frameIndex)).read (phase);
}

float Wavetable::getSampleAtPosition (int level, double framePosition, double phase) const noexcept
{
    return makeReader (level, framePosition).read (phase);
}

} // namespace apollo::dsp
