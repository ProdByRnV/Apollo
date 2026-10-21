#pragma once

/*
    The noise source: white or pink, played at a rate the keyboard can set.

    WHITE is the generator's stream as it always was (NoiseGenerator.h), and
    at the defaults it is literally that call, so a patch from before this file
    existed renders the same samples.

    PINK is Paul Kellet's "refined" filter: six one-pole low-passes and a
    two-tap term on the white stream, summed. Within about 0.05 dB of a -3 dB
    per octave slope from roughly 9 Hz to Nyquist at 44.1 kHz. Its poles are
    fixed rather than derived from the sample rate, so at 96 kHz the region it
    is exact over moves up an octave — the low corner to about 18 Hz — which
    leaves the audible band pink. It is scaled to white's RMS (0.3276, the
    filter's power gain worked out from its impulse response), so switching
    between the two moves the colour and not the level.

    RATE. Key tracking plays the noise the way a sampler plays a recording:
    slower below middle C and faster above it. Slower means the stream is drawn
    less often than once a sample and linearly interpolated, which darkens it —
    the interpolation is a low-pass whose corner follows the rate. Faster than
    once a sample has nothing to add, because noise drawn every sample is
    already as bright as the sample rate allows, so at a rate of 1 or more a
    value is simply drawn every sample.

    Interpolating between two values loses level — up to half the power for
    white noise, whose neighbours are unrelated, and much less for pink, whose
    neighbours are close. Each sample gets back exactly what its own point of
    the interpolation lost, from its type's neighbour correlation, so a tracked
    low note is darker but not quieter, and its level does not move.

    DETERMINISM is the generator's: seeded explicitly, so a render reproduces.
    Reseeding also clears the pink filters and the interpolation, which is what
    makes a reset voice reproduce exactly.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O.
*/

#include <cmath>
#include <cstdint>

#include "DSP/Noise/NoiseGenerator.h"

// Keeps a function out of its caller, where the caller is a hot loop that the
// function is rarely needed in. Every compiler Apollo builds with has one.
#ifndef APOLLO_NOINLINE
 #if defined (_MSC_VER)
  #define APOLLO_NOINLINE __declspec (noinline)
 #else
  #define APOLLO_NOINLINE __attribute__ ((noinline))
 #endif
#endif

namespace apollo::dsp
{

/** What the noise source plays. The order is the parameter's and is
    permanent: a type added later goes at the end. */
enum class NoiseType
{
    white = 0,
    pink
};

inline constexpr int numNoiseTypes = 2;

/** One channel of Kellet's pink filter. */
class PinkFilter
{
public:
    void reset() noexcept
    {
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
    }

    /** @returns the next pink value from one white value, at white's RMS. */
    [[nodiscard]] float process (float white) noexcept
    {
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;

        const auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;

        return pink * rmsScale;
    }

    /** The reciprocal of the filter's amplitude gain on white noise: the sum of
        its squared impulse response is 9.318. */
    static constexpr float rmsScale = 0.327597f;

private:
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
};

class NoiseSource
{
public:
    NoiseSource() = default;

    /** Seeds the stream and clears everything that shapes it. */
    void setSeed (std::uint32_t seed) noexcept
    {
        generator.setSeed (seed);
        pinkLeft.reset();
        pinkRight.reset();
        interpolating = false;
        phase = 0.0f;
    }

    void setType (NoiseType newType) noexcept
    {
        if (newType == type)
            return;

        type = (newType == NoiseType::pink) ? NoiseType::pink : NoiseType::white;

        // The values being interpolated between belong to the old type.
        interpolating = false;
    }

    [[nodiscard]] NoiseType getType() const noexcept { return type; }

    /** Draws per output sample, from key tracking: 1 is the stream's own rate.
        Below 1 the stream is drawn less often and interpolated; at 1 or above,
        and for anything that is not a positive finite number, once a sample.
    */
    void setRate (float newRate) noexcept
    {
        rate = (newRate > 0.0f && newRate < 1.0f) ? newRate : 1.0f;
    }

    [[nodiscard]] float getRate() const noexcept { return rate; }

    /** Adds the next stereo sample into @p left and @p right, scaling each
        channel by its own gain. */
    void addNextStereoSample (float& left, float& right, float gainLeft, float gainRight) noexcept
    {
        // Untracked white noise is the generator's own call, unchanged, so the
        // defaults render exactly what they rendered before. Only this is
        // inline. Everything else is a call, because inlined into the voice's
        // per-sample loop it made the default patch, which plays no noise at
        // all, measurably slower (ADR-0094).
        if (rate >= 1.0f && type == NoiseType::white)
        {
            interpolating = false;
            generator.addNextStereoSample (left, right, gainLeft, gainRight);
            return;
        }

        addShapedStereoSample (left, right, gainLeft, gainRight);
    }

    /** The correlation between neighbouring pink values, from Kellet's impulse
        response. White's neighbours are independent. */
    static constexpr float pinkNeighbourCorrelation = 0.823399f;

private:
    /** Pink, or interpolated, or both. */
    APOLLO_NOINLINE void addShapedStereoSample (float& left, float& right, float gainLeft, float gainRight) noexcept
    {
        if (rate >= 1.0f)
        {
            interpolating = false;

            left += pinkLeft.process (generator.nextLeft()) * gainLeft;
            right += pinkRight.process (generator.nextRight()) * gainRight;
            return;
        }

        if (! interpolating)
        {
            draw (currentLeft, currentRight);
            draw (nextLeft, nextRight);
            phase = 0.0f;
            interpolating = true;
        }
        else
        {
            phase += rate;

            // A rate below 1 never needs more than one new value per sample.
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                currentLeft = nextLeft;
                currentRight = nextRight;
                draw (nextLeft, nextRight);
            }
        }

        // Power lost to the interpolation, given back at this point of it.
        // Between two values with correlation r, a point a fraction t of the
        // way has variance (1 - t)^2 + t^2 + 2t(1 - t)r. Corrected per sample
        // rather than on average, because the average is only right if t
        // visits everywhere evenly, and it does not: an octave down, t is only
        // ever 0 or a half, and a rate just below 1 holds t near a half for
        // long enough to hear the level sag and recover.
        const auto t = phase;
        const auto u = 1.0f - t;
        const auto r = type == NoiseType::white ? 0.0f : pinkNeighbourCorrelation;
        const auto makeup = 1.0f / std::sqrt (u * u + t * t + 2.0f * t * u * r);

        left += (currentLeft + (nextLeft - currentLeft) * t) * makeup * gainLeft;
        right += (currentRight + (nextRight - currentRight) * t) * makeup * gainRight;
    }

    void draw (float& l, float& r) noexcept
    {
        if (type == NoiseType::white)
        {
            l = generator.nextLeft();
            r = generator.nextRight();
            return;
        }

        l = pinkLeft.process (generator.nextLeft());
        r = pinkRight.process (generator.nextRight());
    }

    NoiseGenerator generator;
    PinkFilter pinkLeft;
    PinkFilter pinkRight;

    NoiseType type = NoiseType::white;
    float rate = 1.0f;

    bool interpolating = false;
    float phase = 0.0f;
    float currentLeft = 0.0f;
    float currentRight = 0.0f;
    float nextLeft = 0.0f;
    float nextRight = 0.0f;
};

} // namespace apollo::dsp
