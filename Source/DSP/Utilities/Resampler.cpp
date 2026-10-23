#include "DSP/Utilities/Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace apollo::dsp
{

namespace
{

constexpr double pi = 3.14159265358979323846;

/** Zero crossings of the sinc either side of the centre. */
constexpr int zeroCrossings = 24;

/** Kaiser beta: about -86 dB in the stopband. */
constexpr double kaiserBeta = 8.6;

/** Table entries per zero crossing. Linear interpolation between entries this
    close is far below the window's own stopband. */
constexpr int tableResolution = 512;

/** The cutoff, as a fraction of the lower Nyquist. The rest is the transition
    band, which ends at that Nyquist. */
constexpr double cutoffFraction = 0.94;

/** The modified Bessel function of the first kind, order zero, by its series. */
[[nodiscard]] double besselI0 (double x)
{
    auto sum = 1.0;
    auto term = 1.0;
    const auto quarterSquare = x * x / 4.0;

    for (int k = 1; k < 64; ++k)
    {
        term *= quarterSquare / (static_cast<double> (k) * static_cast<double> (k));
        sum += term;

        if (term < sum * 1.0e-17)
            break;
    }

    return sum;
}

/** The windowed sinc against distance in zero crossings, 0 to zeroCrossings,
    sampled `tableResolution` times per crossing, with one entry past the end. */
[[nodiscard]] const std::vector<double>& kernelTable()
{
    static const auto table = []
    {
        const auto size = static_cast<std::size_t> (zeroCrossings * tableResolution + 2);
        std::vector<double> values (size, 0.0);
        const auto denominator = besselI0 (kaiserBeta);

        for (std::size_t i = 0; i + 1 < size; ++i)
        {
            const auto x = static_cast<double> (i) / static_cast<double> (tableResolution);

            if (x > static_cast<double> (zeroCrossings))
                break;

            const auto sinc = x == 0.0 ? 1.0 : std::sin (pi * x) / (pi * x);
            const auto ratio = x / static_cast<double> (zeroCrossings);
            const auto window = besselI0 (kaiserBeta * std::sqrt (std::max (0.0, 1.0 - ratio * ratio))) / denominator;

            values[i] = sinc * window;
        }

        return values;
    }();

    return table;
}

[[nodiscard]] double kernelAt (const std::vector<double>& table, double crossings) noexcept
{
    const auto x = std::abs (crossings) * static_cast<double> (tableResolution);
    const auto index = static_cast<std::size_t> (x);

    if (index + 1 >= table.size())
        return 0.0;

    const auto fraction = x - static_cast<double> (index);
    return table[index] + (table[index + 1] - table[index]) * fraction;
}

} // namespace

std::vector<double> resample (const std::vector<double>& input, double inputRate, double outputRate)
{
    if (! (inputRate > 0.0) || ! (outputRate > 0.0) || ! std::isfinite (inputRate) || ! std::isfinite (outputRate))
        return {};

    if (inputRate == outputRate)
        return input;

    const auto outputLength = static_cast<std::size_t> (
        std::floor (static_cast<double> (input.size()) * outputRate / inputRate));

    std::vector<double> output (outputLength, 0.0);

    if (input.empty() || outputLength == 0)
        return output;

    const auto& table = kernelTable();

    // The cutoff in cycles per input sample. Below the lower Nyquist, whichever
    // side that is on.
    const auto cutoff = cutoffFraction * 0.5 * std::min (inputRate, outputRate) / inputRate;

    // Input samples per zero crossing of the kernel, and how far it reaches.
    const auto samplesPerCrossing = 1.0 / (2.0 * cutoff);
    const auto reach = static_cast<double> (zeroCrossings) * samplesPerCrossing;
    const auto gain = 2.0 * cutoff;

    const auto step = inputRate / outputRate;
    const auto last = static_cast<std::ptrdiff_t> (input.size()) - 1;

    for (std::size_t n = 0; n < outputLength; ++n)
    {
        const auto centre = static_cast<double> (n) * step;
        const auto first = std::max<std::ptrdiff_t> (0, static_cast<std::ptrdiff_t> (std::ceil (centre - reach)));
        const auto end = std::min<std::ptrdiff_t> (last, static_cast<std::ptrdiff_t> (std::floor (centre + reach)));

        auto sum = 0.0;

        for (auto i = first; i <= end; ++i)
        {
            const auto distance = (static_cast<double> (i) - centre) / samplesPerCrossing;
            sum += input[static_cast<std::size_t> (i)] * kernelAt (table, distance);
        }

        output[n] = sum * gain;
    }

    return output;
}

} // namespace apollo::dsp
