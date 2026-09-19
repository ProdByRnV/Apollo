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

namespace
{

/** 4-point cubic Hermite over four samples and a fraction between y1 and y2.

    One function rather than an expression inlined at each call site, so that
    the one-frame and two-frame read paths cannot drift apart: they must agree
    exactly where a blend is degenerate, and a test asserts that they do.
*/
[[nodiscard]] inline double hermite (double y0, double y1, double y2, double y3, double fraction) noexcept
{
    const auto c0 = y1;
    const auto c1 = 0.5 * (y2 - y0);
    const auto c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const auto c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);

    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

} // namespace

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

float Wavetable::Reader::read (double phase) const noexcept
{
    if (lowerFrame == nullptr)
        return 0.0f;

    // Phase is wrapped into [0, 1) *before* it is scaled, not after.
    //
    // Converting a double that exceeds INT_MAX to int is undefined behaviour,
    // not a wrap — and it does not announce itself: MSVC produced usable-looking
    // garbage while UBSan on Linux reported
    // "2.15456e+09 is outside the range of representable values of type 'int'".
    // Wrapping first bounds the value before the cast can see it, so the cast is
    // always in range by construction rather than by the caller's good manners.
    //
    // Non-finite phase is rejected outright: floor(inf) is inf and floor(NaN) is
    // NaN, either of which would put the same undefined cast right back.
    if (! std::isfinite (phase))
        return 0.0f;

    const auto wrappedPhase = phase - std::floor (phase);
    const auto position = wrappedPhase * static_cast<double> (size);

    auto index = static_cast<int> (position);
    const auto fraction = position - static_cast<double> (index);

    // Belt and braces against a phase of exactly 1.0 surviving the wrap through
    // rounding, which would index one past the end.
    if (index >= size)
        index = size - 1;

    if (index < 0)
        index = 0;

    // The table is periodic, so the interpolator's outer taps wrap rather than
    // clamp — clamping would flatten the waveform at the wrap point and put a
    // discontinuity in every cycle.
    //
    // Wrapped with a mask rather than a modulo, which is valid because every
    // frame size is a power of two (see Wavetable.h) and matters because this
    // was four integer divisions per frame, two frames per oscillator, 1088
    // oscillators, 48000 times a second (ADR-0070).
    //
    // `index - 1` is written as `index + mask` so that the expression never goes
    // negative: index - 1 + size == index + mask, since size == mask + 1. The
    // two agree on every two's-complement machine, but only one of them is
    // obviously right.
    const auto i0 = (index + mask) & mask;
    const auto i1 = index;
    const auto i2 = (index + 1) & mask;
    const auto i3 = (index + 2) & mask;

    if (upperFrame == lowerFrame)
        return static_cast<float> (hermite (static_cast<double> (lowerFrame[i0]),
                                            static_cast<double> (lowerFrame[i1]),
                                            static_cast<double> (lowerFrame[i2]),
                                            static_cast<double> (lowerFrame[i3]),
                                            fraction));

    // Crossfade the four pairs of points, then interpolate once. Hermite is
    // linear in its samples, so this is the same value two cubics and a
    // crossfade of their results would produce, for half the cubic (ADR-0070).
    const auto mix = [this] (float a, float b) noexcept
    {
        const auto lower = static_cast<double> (a);
        return lower + (static_cast<double> (b) - lower) * blend;
    };

    return static_cast<float> (hermite (mix (lowerFrame[i0], upperFrame[i0]),
                                        mix (lowerFrame[i1], upperFrame[i1]),
                                        mix (lowerFrame[i2], upperFrame[i2]),
                                        mix (lowerFrame[i3], upperFrame[i3]),
                                        fraction));
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
