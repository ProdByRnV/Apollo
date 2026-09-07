#include "DSP/Oversampling/Oversampler.h"

#include <algorithm>

namespace apollo::dsp
{

namespace
{

/** Tap counts for the two stages.

    Both are odd, as a halfband must be, but their centres have opposite parity:
    79 taps centres on index 39 and 81 taps on index 40. That is deliberate. A
    stage contributes `centre` samples of round-trip delay measured at its own
    input rate, so the second stage — which runs at twice the base rate —
    contributes `centre / 2` base samples. An odd centre there would put the
    total on a half sample, which cannot be reported to a host or used to align
    a dry path. An even one keeps it whole.

    The counts are otherwise a quality/cost choice, and they were chosen against a
    stated goal rather than picked: a halfband's transition band straddles the
    base rate's Nyquist, and it has to be narrow enough that the passband is
    still flat at the top of hearing. At 48 kHz with 2x oversampling these place
    the passband edge near 20 kHz and the stopband edge near 28 kHz. A shorter
    filter began rolling off around 19 kHz, which is inside the audible band.

    The tests measure the edges this actually produces rather than restating
    these numbers as a claim.
*/
constexpr int stage1Taps = 79;
constexpr int stage2Taps = 81;

/** Stopband attenuation asked of the Kaiser design.

    Chosen against the aliasing budget the wavetable tests already enforce, which
    is -60 dBc. Asking for 100 dB leaves the conversion filters far enough below
    that budget that the oversampler is not the thing setting it.
*/
constexpr double stopbandDb = 100.0;

} // namespace

void Oversampler::prepare (Factor factor, int maxBlockSize)
{
    oversamplingFactor = factor;
    preparedBlockSize = maxBlockSize > 0 ? maxBlockSize : 0;

    if (factor == Factor::none || preparedBlockSize == 0)
    {
        latencySamples = 0;

        // Sized even when bypassed. The pass-through path hands the caller a
        // writable buffer like every other factor, and allocating that lazily on
        // first use would put a heap allocation on the audio thread.
        buffer2x.assign (static_cast<std::size_t> (preparedBlockSize), 0.0f);
        buffer4x.clear();
        return;
    }

    stage1Coefficients.design (stage1Taps, stopbandDb);

    stage1Up.prepare (stage1Coefficients, preparedBlockSize);
    stage1Down.prepare (stage1Coefficients, preparedBlockSize);

    buffer2x.assign (static_cast<std::size_t> (preparedBlockSize) * 2, 0.0f);

    // A stage's round trip costs `centre` samples at the rate it takes as input.
    latencySamples = stage1Coefficients.getCentre();

    if (factor == Factor::x4)
    {
        stage2Coefficients.design (stage2Taps, stopbandDb);

        stage2Up.prepare (stage2Coefficients, preparedBlockSize * 2);
        stage2Down.prepare (stage2Coefficients, preparedBlockSize * 2);

        buffer4x.assign (static_cast<std::size_t> (preparedBlockSize) * 4, 0.0f);

        // The second stage's input runs at twice the base rate, so its
        // contribution is halved on the way back to base-rate samples. The even
        // centre chosen above is what makes this division exact.
        latencySamples += stage2Coefficients.getCentre() / 2;
    }
    else
    {
        buffer4x.clear();
    }

    reset();
}

void Oversampler::reset() noexcept
{
    stage1Up.reset();
    stage1Down.reset();
    stage2Up.reset();
    stage2Down.reset();

    std::fill (buffer2x.begin(), buffer2x.end(), 0.0f);
    std::fill (buffer4x.begin(), buffer4x.end(), 0.0f);
}

float* Oversampler::upsample (const float* input, int numSamples) noexcept
{
    if (input == nullptr || numSamples <= 0 || numSamples > preparedBlockSize)
        return nullptr;

    if (oversamplingFactor == Factor::none)
    {
        // Pass-through still hands back a writable buffer, so the caller's
        // in-place processing works identically at every factor. buffer2x is
        // reused rather than a third buffer added, and prepare sized it.
        if (buffer2x.size() < static_cast<std::size_t> (numSamples))
            return nullptr;

        std::copy (input, input + numSamples, buffer2x.begin());
        return buffer2x.data();
    }

    stage1Up.process (input, buffer2x.data(), numSamples);

    if (oversamplingFactor == Factor::x2)
        return buffer2x.data();

    stage2Up.process (buffer2x.data(), buffer4x.data(), numSamples * 2);
    return buffer4x.data();
}

void Oversampler::downsample (float* output, int numSamples) noexcept
{
    if (output == nullptr || numSamples <= 0 || numSamples > preparedBlockSize)
        return;

    if (oversamplingFactor == Factor::none)
    {
        if (buffer2x.size() >= static_cast<std::size_t> (numSamples))
            std::copy (buffer2x.begin(), buffer2x.begin() + numSamples, output);

        return;
    }

    if (oversamplingFactor == Factor::x4)
        stage2Down.process (buffer4x.data(), buffer2x.data(), numSamples * 2);

    stage1Down.process (buffer2x.data(), output, numSamples);
}

} // namespace apollo::dsp
