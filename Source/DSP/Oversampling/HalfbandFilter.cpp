#include "DSP/Oversampling/HalfbandFilter.h"

#include <algorithm>
#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double pi = 3.14159265358979323846;

/** Zeroth-order modified Bessel function of the first kind.

    Needed only by the Kaiser window. The series converges quickly for the
    arguments a window uses, and the loop is bounded so a pathological beta
    cannot spin here forever.
*/
[[nodiscard]] double besselI0 (double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;

    for (int k = 1; k < 64; ++k)
    {
        const auto half = x / (2.0 * static_cast<double> (k));
        term *= half * half;
        sum += term;

        if (term < sum * 1.0e-17)
            break;
    }

    return sum;
}

/** sin(pi x) / (pi x), defined at zero. */
[[nodiscard]] double sinc (double x) noexcept
{
    if (x == 0.0)
        return 1.0;

    const auto arg = pi * x;
    return std::sin (arg) / arg;
}

/** Kaiser beta for a desired stopband attenuation, per Kaiser's own formula. */
[[nodiscard]] double kaiserBeta (double attenuationDb) noexcept
{
    if (attenuationDb > 50.0)
        return 0.1102 * (attenuationDb - 8.7);

    if (attenuationDb >= 21.0)
        return 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);

    return 0.0;
}

} // namespace

//==============================================================================

void HalfbandCoefficients::design (int numTaps, double stopbandDb)
{
    // A halfband filter is defined by its centre tap, so the length must be odd.
    // Rounding up rather than rejecting keeps a caller's "about 64 taps" working
    // instead of failing on a detail it should not have to know.
    if (numTaps < 7)
        numTaps = 7;

    if ((numTaps % 2) == 0)
        ++numTaps;

    centre = (numTaps - 1) / 2;

    impulse.assign (static_cast<std::size_t> (numTaps), 0.0f);

    const auto beta = kaiserBeta (stopbandDb);
    const auto denominator = besselI0 (beta);
    const auto half = static_cast<double> (centre);

    for (int n = 0; n < numTaps; ++n)
    {
        const auto offset = static_cast<double> (n) - half;

        // Ideal halfband: a lowpass at exactly a quarter of this filter's rate.
        // Written with a 0.5 scale so the coefficients sum to 0.5, which is the
        // gain a zero-stuffed upsampler needs doubled and a decimator needs as
        // it stands.
        //
        // Every even offset other than zero lands sinc() on a non-zero integer,
        // where sin(pi k) is zero. That is where the halfband zeros come from:
        // they are exact by construction, not the result of a design tolerance.
        const auto ideal = 0.5 * sinc (0.5 * offset);

        // Kaiser window.
        const auto ratio = offset / half;
        const auto inside = 1.0 - ratio * ratio;
        const auto window = denominator > 0.0
                              ? besselI0 (beta * std::sqrt (inside > 0.0 ? inside : 0.0)) / denominator
                              : 1.0;

        impulse[static_cast<std::size_t> (n)] = static_cast<float> (ideal * window);
    }

    // Force the structural zeros to be exactly zero. The windowed sinc evaluates
    // to something on the order of 1e-17 there rather than to nothing, and the
    // polyphase form below assumes they are absent. Making that assumption true
    // is cheaper than defending against it in the inner loop.
    for (int n = 0; n < numTaps; ++n)
        if (n != centre && ((n - centre) % 2) == 0)
            impulse[static_cast<std::size_t> (n)] = 0.0f;

    // The filtering branch: every tap whose parity differs from the centre's.
    // Those are the taps the zeros left behind, taken in order, and they form a
    // plain FIR running at the lower of the two rates.
    branchTaps.clear();

    for (int n = (centre % 2 == 0) ? 1 : 0; n < numTaps; n += 2)
        branchTaps.push_back (impulse[static_cast<std::size_t> (n)]);
}

//==============================================================================

void HalfbandUpsampler::prepare (const HalfbandCoefficients& coefficients, int maxInputSamples)
{
    coeffs = &coefficients;

    const auto taps = coefficients.getBranchTaps().size();
    history.assign (taps > 0 ? taps : 1, 0.0f);

    // The delay branch reproduces the centre tap alone, which is a pure delay of
    // half the centre index, rounded according to the centre's parity.
    delaySamples = coefficients.hasOddCentre() ? (coefficients.getCentre() - 1) / 2
                                               : coefficients.getCentre() / 2;

    delayLine.assign (static_cast<std::size_t> (delaySamples) + 1, 0.0f);

    static_cast<void> (maxInputSamples); // State is per-sample; block size only bounds the caller's buffer.

    reset();
}

void HalfbandUpsampler::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    historyPos = 0;
    delayPos = 0;
}

void HalfbandUpsampler::process (const float* input, float* output, int numInput) noexcept
{
    if (coeffs == nullptr || input == nullptr || output == nullptr || numInput <= 0)
        return;

    const auto& taps = coeffs->getBranchTaps();
    const auto numTaps = taps.size();

    if (numTaps == 0 || history.empty() || delayLine.empty())
        return;

    // The filtered branch drives one output phase and the delayed branch the
    // other. Which is which follows from where the centre tap sits.
    const auto firstIsFiltered = coeffs->hasOddCentre();

    for (int i = 0; i < numInput; ++i)
    {
        const auto x = input[i];

        history[historyPos] = x;

        // Doubled: zero-stuffing halves the signal's power, and the halfband's
        // coefficients sum to 0.5, so unity through the pair needs a factor of
        // two applied once, here.
        float accumulator = 0.0f;
        auto index = historyPos;

        for (std::size_t k = 0; k < numTaps; ++k)
        {
            accumulator += taps[k] * history[index];
            index = index == 0 ? history.size() - 1 : index - 1;
        }

        accumulator *= 2.0f;

        delayLine[delayPos] = x;
        const auto delayed = delayLine[(delayPos + 1) % delayLine.size()];

        const auto out = static_cast<std::size_t> (i) * 2;

        if (firstIsFiltered)
        {
            output[out] = accumulator;
            output[out + 1] = delayed;
        }
        else
        {
            output[out] = delayed;
            output[out + 1] = accumulator;
        }

        historyPos = (historyPos + 1) % history.size();
        delayPos = (delayPos + 1) % delayLine.size();
    }
}

//==============================================================================

void HalfbandDownsampler::prepare (const HalfbandCoefficients& coefficients, int maxOutputSamples)
{
    coeffs = &coefficients;

    const auto taps = coefficients.getBranchTaps().size();

    // An even centre puts the filtered branch one sample further back. The
    // history has to be long enough to still hold every tap after that shift.
    filterExtraDelay = coefficients.hasOddCentre() ? 0u : 1u;
    history.assign ((taps > 0 ? taps : 1) + filterExtraDelay, 0.0f);

    // One more than the upsampler's, and for the same reason mirrored: the
    // decimator reads the centre tap from the opposite side of the split.
    delaySamples = coefficients.hasOddCentre() ? (coefficients.getCentre() + 1) / 2
                                               : coefficients.getCentre() / 2;

    delayLine.assign (static_cast<std::size_t> (delaySamples) + 1, 0.0f);

    static_cast<void> (maxOutputSamples);

    reset();
}

void HalfbandDownsampler::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    historyPos = 0;
    delayPos = 0;
}

void HalfbandDownsampler::process (const float* input, float* output, int numOutput) noexcept
{
    if (coeffs == nullptr || input == nullptr || output == nullptr || numOutput <= 0)
        return;

    const auto& taps = coeffs->getBranchTaps();
    const auto numTaps = taps.size();

    if (numTaps == 0 || history.empty() || delayLine.empty())
        return;

    const auto firstIsDelayed = coeffs->hasOddCentre();

    for (int i = 0; i < numOutput; ++i)
    {
        const auto in = static_cast<std::size_t> (i) * 2;

        // Split the incoming pair into the two phases. The filtered branch and
        // the delayed branch each see one of them, mirroring the upsampler.
        const auto toDelay = firstIsDelayed ? input[in + 1] : input[in];
        const auto toFilter = firstIsDelayed ? input[in] : input[in + 1];

        history[historyPos] = toFilter;

        float accumulator = 0.0f;
        auto index = (historyPos + history.size() - filterExtraDelay) % history.size();

        for (std::size_t k = 0; k < numTaps; ++k)
        {
            accumulator += taps[k] * history[index];
            index = index == 0 ? history.size() - 1 : index - 1;
        }

        delayLine[delayPos] = toDelay;
        const auto delayed = delayLine[(delayPos + 1) % delayLine.size()];

        // The centre tap is 0.5, and the filtered branch already carries the
        // decimator's half. No doubling here: unity is the sum as it stands.
        output[i] = accumulator + 0.5f * delayed;

        historyPos = (historyPos + 1) % history.size();
        delayPos = (delayPos + 1) % delayLine.size();
    }
}

} // namespace apollo::dsp
