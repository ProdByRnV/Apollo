#pragma once

/*
    Sample-rate oversampling for one channel, for use around nonlinear stages.

    WHAT THIS IS FOR. A nonlinear function generates harmonics the input did not
    contain. Feed a 5 kHz sine into a hard clipper at 48 kHz and its ninth
    harmonic lands at 45 kHz, which is above Nyquist and folds back to 3 kHz as
    an inharmonic tone that no filter downstream can remove. Running the
    nonlinearity at a higher rate moves the fold-back point up, and the
    downsampler's filter removes what is now above the original Nyquist before it
    ever aliases.

    WHAT THIS IS NOT FOR. Linear processing does not need it. A filter, a gain, a
    pan, a mixer and a delay produce no new frequencies, so oversampling them
    costs CPU and buys nothing (CLAUDE.md §23, ARCHITECTURE.md §2.3). Apollo's
    oscillators are a specific case worth stating plainly: they are band-limited
    at the source by the wavetable mipmap (ADR-0020), so they are not oversampled
    either. Docs/OVERSAMPLING.md records where it is and is not applied, and why.

    USAGE. The caller owns its own signal and works in place on the buffer this
    hands back:

        auto* wide = oversampler.upsample (input, numSamples);

        for (int i = 0; i < oversampler.getOversampledLength (numSamples); ++i)
            wide[i] = shape (wide[i]);

        oversampler.downsample (output, numSamples);

    Factor::none is a real, supported setting rather than a special case the
    caller has to branch around: it passes the signal through, reports zero
    latency, and lets a quality control switch oversampling off without the
    surrounding code changing shape.

    LATENCY. Linear-phase filters delay the signal, and the round trip through
    this class delays it by `getLatencySamples()` at the base rate — a whole
    number, which is why the two cascaded stages are designed with opposite
    centre-tap parity (ADR-0028). A stage that mixes a dry path against an
    oversampled wet path must delay the dry path by that amount, or the two will
    comb rather than blend.

    REAL-TIME CONTRACT: `prepare` allocates and must run before audio starts.
    `upsample`, `downsample` and `reset` are callable from the audio thread and
    allocate nothing, lock nothing and perform no I/O.
*/

#include "DSP/Oversampling/HalfbandFilter.h"

#include <vector>

namespace apollo::dsp
{

class Oversampler
{
public:
    enum class Factor
    {
        none = 1, ///< Pass-through. Zero latency, zero cost.
        x2 = 2,   ///< The baseline for nonlinear stages.
        x4 = 4    ///< Where measurement shows 2x is not enough.
    };

    Oversampler() = default;

    /** Designs the filters and sizes the buffers. Allocates; call while stopped.

        @param factor        oversampling ratio.
        @param maxBlockSize  largest block that will be passed to `upsample`.
    */
    void prepare (Factor factor, int maxBlockSize);

    /** Clears the filter state without reallocating. Audio-thread safe. */
    void reset() noexcept;

    [[nodiscard]] Factor getFactor() const noexcept { return oversamplingFactor; }

    [[nodiscard]] int getFactorAsInt() const noexcept { return static_cast<int> (oversamplingFactor); }

    /** @returns the number of oversampled samples @p numSamples produces. */
    [[nodiscard]] int getOversampledLength (int numSamples) const noexcept
    {
        return numSamples * getFactorAsInt();
    }

    /** @returns the round-trip delay in samples at the base rate.

        Whole samples by construction. Zero for Factor::none.
    */
    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples; }

    /** Upsamples @p numSamples and @returns the writable oversampled buffer.

        The buffer holds `getOversampledLength (numSamples)` samples and belongs
        to this object: it stays valid until the next call, and the caller is
        expected to modify it in place. Returns nullptr if the object is not
        prepared or @p numSamples exceeds the prepared maximum, so a caller that
        ignores the contract gets a null pointer rather than a buffer overrun.
    */
    [[nodiscard]] float* upsample (const float* input, int numSamples) noexcept;

    /** Downsamples the buffer last returned by `upsample` into @p output. */
    void downsample (float* output, int numSamples) noexcept;

private:
    Factor oversamplingFactor = Factor::none;

    int preparedBlockSize = 0;
    int latencySamples = 0;

    // Two independent designs. The first stage's centre tap sits at an odd
    // index and the second's at an even one, which is what makes the 4x round
    // trip land on a whole number of base-rate samples rather than on a half.
    HalfbandCoefficients stage1Coefficients;
    HalfbandCoefficients stage2Coefficients;

    HalfbandUpsampler stage1Up;
    HalfbandUpsampler stage2Up;
    HalfbandDownsampler stage1Down;
    HalfbandDownsampler stage2Down;

    std::vector<float> buffer2x;
    std::vector<float> buffer4x;
};

} // namespace apollo::dsp
