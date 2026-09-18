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

bool Wavetable::resolveTap (int level, double phase, Tap& tap) noexcept
{
    if (level < 0 || level >= numMipLevels)
        return false;

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
        return false;

    const auto size = samplesAtLevel (level);

    const auto wrappedPhase = phase - std::floor (phase);
    const auto position = wrappedPhase * static_cast<double> (size);

    auto index = static_cast<int> (position);
    tap.fraction = position - static_cast<double> (index);

    // Belt and braces against a phase of exactly 1.0 surviving the wrap through
    // rounding, which would index one past the end.
    if (index >= size)
        index = size - 1;

    if (index < 0)
        index = 0;

    tap.index = index;
    tap.mask = size - 1;

    return true;
}

namespace
{

/** 4-point cubic Hermite over four samples and a fraction between y1 and y2.

    Written as one function taking doubles rather than inlined at each call site
    so that the one-frame and two-frame read paths cannot drift apart: they must
    agree exactly where a blend is degenerate, and a test asserts that they do.
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

float Wavetable::readFrame (const float* frame, const Tap& tap) noexcept
{
    const auto mask = tap.mask;
    const auto index = tap.index;

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
    const auto y0 = static_cast<double> (frame[(index + mask) & mask]);
    const auto y1 = static_cast<double> (frame[index]);
    const auto y2 = static_cast<double> (frame[(index + 1) & mask]);
    const auto y3 = static_cast<double> (frame[(index + 2) & mask]);

    return static_cast<float> (hermite (y0, y1, y2, y3, tap.fraction));
}

float Wavetable::readBlendedFrames (const float* lower, const float* upper,
                                    const Tap& tap, double blend) noexcept
{
    const auto mask = tap.mask;
    const auto index = tap.index;

    const auto i0 = (index + mask) & mask;
    const auto i1 = index;
    const auto i2 = (index + 1) & mask;
    const auto i3 = (index + 2) & mask;

    // Crossfade the four pairs of points, then interpolate once. Hermite is
    // linear in its samples, so this is the same value the old two-cubic form
    // produced, for half the index arithmetic and one cubic instead of two.
    const auto mix = [blend] (float a, float b) noexcept
    {
        const auto lo = static_cast<double> (a);
        return lo + (static_cast<double> (b) - lo) * blend;
    };

    return static_cast<float> (hermite (mix (lower[i0], upper[i0]),
                                        mix (lower[i1], upper[i1]),
                                        mix (lower[i2], upper[i2]),
                                        mix (lower[i3], upper[i3]),
                                        tap.fraction));
}

float Wavetable::getSample (int level, int frameIndex, double phase) const noexcept
{
    const auto* frame = getReadPointer (level, frameIndex);

    if (frame == nullptr)
        return 0.0f;

    Tap tap;

    if (! resolveTap (level, phase, tap))
        return 0.0f;

    return readFrame (frame, tap);
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
    const auto blend = clamped - static_cast<double> (lowerFrame);

    const auto* lower = getReadPointer (level, lowerFrame);

    if (lower == nullptr)
        return 0.0f;

    Tap tap;

    if (! resolveTap (level, phase, tap))
        return 0.0f;

    // The last frame has no successor to blend towards, and a blend of exactly
    // zero would read the second frame only to multiply it by nothing. Both are
    // the one-frame path, which also keeps the two forms bit-identical where
    // they are meant to agree.
    if (lowerFrame + 1 >= numFrames || blend == 0.0)
        return readFrame (lower, tap);

    // Frames of a level live back to back in one buffer, so the next frame is a
    // pointer offset rather than a second bounds-checked lookup.
    return readBlendedFrames (lower, lower + samplesAtLevel (level), tap, blend);
}

} // namespace apollo::dsp
