#include "DSP/EQ/Biquad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apollo::dsp
{

namespace
{

constexpr double pi = std::numbers::pi_v<double>;

/** Every designer ends the same way: divide through by a0 so that the section
    can be evaluated without a division per sample.

    a0 is a sum of positive quantities in every shape the cookbook defines, so it
    cannot be zero for any frequency and Q that reach here. The guard is for the
    case that cannot happen — a NaN arriving through a corrupt setting — and
    returns the identity, because an equaliser that briefly does nothing is
    recoverable and one that fills the buffer with NaN is not.
*/
[[nodiscard]] BiquadCoefficients normalise (double b0, double b1, double b2,
                                            double a0, double a1, double a2) noexcept
{
    if (! (a0 > 0.0) && ! (a0 < 0.0))
        return {};

    const auto inverse = 1.0 / a0;

    return { b0 * inverse, b1 * inverse, b2 * inverse, a1 * inverse, a2 * inverse };
}

/** The three quantities every cookbook formula is written in terms of. */
struct Intermediates
{
    double cosW0 = 1.0;
    double alpha = 0.0;

    /** sqrt(10^(gainDb/20)): the cookbook's `A`, which is the square root of the
        linear gain because a peaking section applies it once in the numerator
        and once in the denominator.
    */
    double a = 1.0;
};

[[nodiscard]] Intermediates intermediatesFor (double hz, double q, double gainDb,
                                              double sampleRate) noexcept
{
    const auto frequency = rbj::clampFrequency (hz, sampleRate);
    const auto quality = std::clamp (q, rbj::minimumQ, rbj::maximumQ);

    const auto w0 = 2.0 * pi * frequency / sampleRate;

    Intermediates result;

    result.cosW0 = std::cos (w0);
    result.alpha = std::sin (w0) / (2.0 * quality);
    result.a = std::pow (10.0, gainDb / 40.0);

    return result;
}

} // namespace

double BiquadCoefficients::magnitudeAt (double hz, double sampleRate) const noexcept
{
    if (! (sampleRate > 0.0))
        return 1.0;

    // z^-1 evaluated on the unit circle. The imaginary parts carry a minus sign
    // that the squared magnitude discards, so only the real cosines and the
    // sines' combination are needed.
    const auto w = 2.0 * pi * hz / sampleRate;

    const auto cos1 = std::cos (w);
    const auto sin1 = std::sin (w);
    const auto cos2 = std::cos (2.0 * w);
    const auto sin2 = std::sin (2.0 * w);

    const auto numeratorReal = b0 + b1 * cos1 + b2 * cos2;
    const auto numeratorImag = b1 * sin1 + b2 * sin2;

    const auto denominatorReal = 1.0 + a1 * cos1 + a2 * cos2;
    const auto denominatorImag = a1 * sin1 + a2 * sin2;

    const auto numerator = numeratorReal * numeratorReal + numeratorImag * numeratorImag;
    const auto denominator = denominatorReal * denominatorReal + denominatorImag * denominatorImag;

    if (! (denominator > 0.0))
        return 0.0;

    return std::sqrt (numerator / denominator);
}

namespace rbj
{

double clampFrequency (double hz, double sampleRate) noexcept
{
    if (! (sampleRate > 0.0))
        return minimumFrequency;

    const auto ceiling = sampleRate * maximumFrequencyFraction;

    // Written as two comparisons rather than std::clamp so that a NaN — which
    // fails both — falls through to the floor instead of propagating.
    if (hz > ceiling)
        return ceiling;

    if (hz > minimumFrequency)
        return hz;

    return minimumFrequency;
}

double qForBandwidth (double bandwidthOctaves, double hz, double sampleRate) noexcept
{
    const auto frequency = clampFrequency (hz, sampleRate);
    const auto w0 = 2.0 * pi * frequency / sampleRate;
    const auto sinW0 = std::sin (w0);

    // Bandwidth is only meaningful where the section has a sine to scale; at the
    // very top of the band the clamp above has already kept w0 clear of pi, so
    // this is the NaN guard rather than a real case.
    if (! (sinW0 > 0.0) || ! (bandwidthOctaves > 0.0))
        return maximumQ;

    // The cookbook's relation between bandwidth in octaves and alpha, solved for
    // Q. `w0 / sin(w0)` is the term that makes the answer depend on where in the
    // spectrum the band sits.
    const auto alpha = sinW0 * std::sinh (0.5 * std::numbers::ln2_v<double>
                                          * bandwidthOctaves * w0 / sinW0);

    if (! (alpha > 0.0))
        return maximumQ;

    return std::clamp (sinW0 / (2.0 * alpha), minimumQ, maximumQ);
}

BiquadCoefficients lowPass (double hz, double q, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, 0.0, sampleRate);
    const auto oneMinusCos = 1.0 - i.cosW0;

    return normalise (0.5 * oneMinusCos, oneMinusCos, 0.5 * oneMinusCos,
                      1.0 + i.alpha, -2.0 * i.cosW0, 1.0 - i.alpha);
}

BiquadCoefficients highPass (double hz, double q, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, 0.0, sampleRate);
    const auto onePlusCos = 1.0 + i.cosW0;

    return normalise (0.5 * onePlusCos, -onePlusCos, 0.5 * onePlusCos,
                      1.0 + i.alpha, -2.0 * i.cosW0, 1.0 - i.alpha);
}

BiquadCoefficients bandPass (double hz, double q, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, 0.0, sampleRate);

    // The constant-peak-gain form: unity at the centre frequency whatever the
    // bandwidth, which is the one an equaliser wants. The constant-skirt form
    // would make a narrow band quieter than a wide one at its own centre.
    return normalise (i.alpha, 0.0, -i.alpha,
                      1.0 + i.alpha, -2.0 * i.cosW0, 1.0 - i.alpha);
}

BiquadCoefficients notch (double hz, double q, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, 0.0, sampleRate);

    return normalise (1.0, -2.0 * i.cosW0, 1.0,
                      1.0 + i.alpha, -2.0 * i.cosW0, 1.0 - i.alpha);
}

BiquadCoefficients peaking (double hz, double q, double gainDb, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, gainDb, sampleRate);

    // At 0 dB, A is 1 and the numerator becomes the denominator term for term:
    // the section is the identity exactly, not approximately. That is what lets
    // an untouched band be skipped rather than processed (see EqualiserBand).
    return normalise (1.0 + i.alpha * i.a, -2.0 * i.cosW0, 1.0 - i.alpha * i.a,
                      1.0 + i.alpha / i.a, -2.0 * i.cosW0, 1.0 - i.alpha / i.a);
}

BiquadCoefficients lowShelf (double hz, double q, double gainDb, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, gainDb, sampleRate);

    const auto aPlus = i.a + 1.0;
    const auto aMinus = i.a - 1.0;
    const auto twoRootA = 2.0 * std::sqrt (i.a) * i.alpha;

    return normalise (
        i.a * (aPlus - aMinus * i.cosW0 + twoRootA),
        2.0 * i.a * (aMinus - aPlus * i.cosW0),
        i.a * (aPlus - aMinus * i.cosW0 - twoRootA),
        aPlus + aMinus * i.cosW0 + twoRootA,
        -2.0 * (aMinus + aPlus * i.cosW0),
        aPlus + aMinus * i.cosW0 - twoRootA);
}

BiquadCoefficients highShelf (double hz, double q, double gainDb, double sampleRate) noexcept
{
    const auto i = intermediatesFor (hz, q, gainDb, sampleRate);

    const auto aPlus = i.a + 1.0;
    const auto aMinus = i.a - 1.0;
    const auto twoRootA = 2.0 * std::sqrt (i.a) * i.alpha;

    return normalise (
        i.a * (aPlus + aMinus * i.cosW0 + twoRootA),
        -2.0 * i.a * (aMinus + aPlus * i.cosW0),
        i.a * (aPlus + aMinus * i.cosW0 - twoRootA),
        aPlus - aMinus * i.cosW0 + twoRootA,
        2.0 * (aMinus - aPlus * i.cosW0),
        aPlus - aMinus * i.cosW0 - twoRootA);
}

} // namespace rbj

} // namespace apollo::dsp
