#pragma once

/*
    The noise source: white or pink noise, or a built-in transient, played at a
    rate the keyboard can set.

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

    TRANSIENTS (Phase 13d-2) are short samples from TransientLibrary.h, played
    once from each note's start rather than run for ever. A SAMPLE the user
    loaded (Phase 13g) is the same kind of object, built by the same
    makeTransient, and played the same way. Any sample type can LOOP instead:
    when it reaches its end it goes on from its start, and the interpolation
    reads across the seam rather than into silence. The rate plays them
    faster as well as slower, unlike a stream, and they are read from the mip
    level whose top still lands below Nyquist at that rate, with four-point
    Hermite interpolation. At their own pitch and the 48 kHz they were built
    at, every read lands on a sample and returns it exactly.

    DETERMINISM is the generator's: seeded explicitly, so a render reproduces.
    Reseeding also clears the pink filters and the interpolation, which is what
    makes a reset voice reproduce exactly.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O.
*/

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "DSP/Noise/NoiseGenerator.h"
#include "DSP/Noise/TransientLibrary.h"

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
    pink,

    // The built-in transients (TransientLibrary.h), appended in Phase 13d-2.
    click,
    pick,
    thump,
    chiff,

    // A sample the user loaded, appended in Phase 13g (ADR-0104).
    sample
};

inline constexpr int numNoiseTypes = 7;

/** True for the types that are a sample, played from each note's start,
    rather than a stream that runs for ever: the built-in transients and a
    loaded sample. */
[[nodiscard]] constexpr bool isTransient (NoiseType type) noexcept
{
    return static_cast<int> (type) >= static_cast<int> (NoiseType::click);
}

/** The transient library's index for a built-in transient type. Meaningless
    for `sample`, which is not in the library. */
[[nodiscard]] constexpr int transientIndex (NoiseType type) noexcept
{
    return static_cast<int> (type) - static_cast<int> (NoiseType::click);
}

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

    /** The rate the output runs at, which a transient's playback is relative
        to. A stream does not care. */
    void prepare (double sampleRate) noexcept
    {
        outputRate = sampleRate > 0.0 ? sampleRate : TransientSample::referenceRate;
        updateStep();
    }

    /** Seeds the stream and clears everything that shapes it, and stops a
        transient that is playing. */
    void setSeed (std::uint32_t seed) noexcept
    {
        generator.setSeed (seed);
        pinkLeft.reset();
        pinkRight.reset();
        interpolating = false;
        phase = 0.0f;
        playing = false;
    }

    /** @param newTransient  the sample for a transient type, from the shared
                             library; ignored for a stream. A transient type
                             without one is silent rather than undefined.
    */
    void setType (NoiseType newType, const TransientSample* newTransient = nullptr) noexcept
    {
        const auto index = static_cast<int> (newType);
        const auto clamped = static_cast<NoiseType> (index < 0 ? 0 : (index >= numNoiseTypes ? numNoiseTypes - 1 : index));
        const auto* sample = isTransient (clamped) ? newTransient : nullptr;

        if (clamped == type && sample == transient)
            return;

        type = clamped;
        transient = sample;

        // The values being interpolated between belong to the old type, and a
        // sample of the old type stops; the new one starts with the next note.
        interpolating = false;
        playing = false;
        updateStep();
    }

    [[nodiscard]] NoiseType getType() const noexcept { return type; }

    /** True plays a sample type round and round while the note sounds; false,
        the default, plays it once from the note's start. A stream ignores it. */
    void setLoop (bool shouldLoop) noexcept { looping = shouldLoop; }

    [[nodiscard]] bool isLooping() const noexcept { return looping; }

    /** The playback rate from key tracking: 1 is the noise's own.

        A stream below 1 is drawn less often and interpolated; at 1 or above it
        is drawn once a sample. A transient is played at exactly this rate,
        faster as well as slower. Anything that is not a positive finite number
        is taken as 1.
    */
    void setRate (float newRate) noexcept
    {
        pitch = (newRate > 0.0f && std::isfinite (newRate)) ? newRate : 1.0f;
        rate = pitch < 1.0f ? pitch : 1.0f;
        updateStep();
    }

    [[nodiscard]] float getRate() const noexcept { return rate; }

    /** Starts the transient from its beginning. Does nothing for a stream,
        which runs freely across notes. */
    void trigger() noexcept
    {
        if (transient != nullptr)
        {
            position = 0.0;
            playing = true;
        }
    }

    /** False only for a transient that has finished, whose output is silence
        until the next trigger, so a caller may skip it. */
    [[nodiscard]] bool isSounding() const noexcept
    {
        return transient == nullptr || playing;
    }

    /** The mip level a transient is being read from: 0 is the full band. */
    [[nodiscard]] int getTransientLevel() const noexcept { return level; }

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
    /** Pink, or interpolated, or both, or a transient. */
    APOLLO_NOINLINE void addShapedStereoSample (float& left, float& right, float gainLeft, float gainRight) noexcept
    {
        if (isTransient (type))
        {
            addTransientSample (left, right, gainLeft, gainRight);
            return;
        }

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

    void addTransientSample (float& left, float& right, float gainLeft, float gainRight) noexcept
    {
        if (! playing || transient == nullptr)
            return;

        // Level k holds every 2^k-th frame of level 0's timeline, so a position
        // in level-0 frames divides down to it exactly.
        const auto& source = transient->levels[static_cast<std::size_t> (level)];
        const auto x = position / static_cast<double> (1 << level);
        const auto whole = static_cast<std::ptrdiff_t> (x);
        const auto fraction = static_cast<float> (x - static_cast<double> (whole));

        left += hermite (source.left, whole, fraction, looping) * gainLeft;
        right += hermite (source.right, whole, fraction, looping) * gainRight;

        position += step;

        const auto length = static_cast<double> (transient->length());

        if (position >= length)
        {
            if (looping && length > 0.0)
                position = std::fmod (position, length);
            else
                playing = false;
        }
    }

    /** Four-point cubic Hermite. Either side of the sample it reads silence, or
        for a loop, the other end of it. */
    [[nodiscard]] static float hermite (const std::vector<float>& samples, std::ptrdiff_t index, float t,
                                        bool wrap) noexcept
    {
        const auto size = static_cast<std::ptrdiff_t> (samples.size());
        const auto at = [&samples, size, wrap] (std::ptrdiff_t i)
        {
            if (i >= 0 && i < size)
                return samples[static_cast<std::size_t> (i)];

            if (! wrap || size == 0)
                return 0.0f;

            const auto wrapped = ((i % size) + size) % size;
            return samples[static_cast<std::size_t> (wrapped)];
        };

        const auto y0 = at (index - 1);
        const auto y1 = at (index);
        const auto y2 = at (index + 1);
        const auto y3 = at (index + 2);

        const auto c1 = 0.5f * (y2 - y0);
        const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    /** A transient's step through level 0 per output sample, and the level to
        read: the first whose top, played at this pitch, lands at or below
        Nyquist. */
    void updateStep() noexcept
    {
        step = static_cast<double> (pitch) * TransientSample::referenceRate / outputRate;

        auto top = TransientSample::referenceBandwidth * static_cast<double> (pitch);
        level = 0;

        while (level < TransientSample::numLevels - 1 && top > 0.5 * outputRate)
        {
            top *= 0.5;
            ++level;
        }
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
    float pitch = 1.0f;

    const TransientSample* transient = nullptr;
    double outputRate = TransientSample::referenceRate;
    double position = 0.0;
    double step = 1.0;
    int level = 0;
    bool playing = false;
    bool looping = false;

    bool interpolating = false;
    float phase = 0.0f;
    float currentLeft = 0.0f;
    float currentRight = 0.0f;
    float nextLeft = 0.0f;
    float nextRight = 0.0f;
};

} // namespace apollo::dsp
