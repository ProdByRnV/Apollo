#include "DSP/Utilities/Fft.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

/** Reorders @p samples so a decimation-in-time transform can run in place.

    Index n moves to the index whose bits are n's reversed. The counter below
    increments a bit-reversed number directly — adding one from the top, with
    the carry travelling downwards — which avoids reversing each index.
*/
void bitReverse (std::vector<std::complex<double>>& samples) noexcept
{
    const auto size = samples.size();

    std::size_t reversed = 0;

    for (std::size_t i = 1; i < size; ++i)
    {
        auto bit = size >> 1;

        for (; (reversed & bit) != 0; bit >>= 1)
            reversed &= ~bit;

        reversed |= bit;

        if (i < reversed)
            std::swap (samples[i], samples[reversed]);
    }
}

} // namespace

void forwardTransform (std::vector<std::complex<double>>& samples)
{
    const auto size = samples.size();

    if (! isTransformSize (size))
        return;

    bitReverse (samples);

    for (std::size_t span = 2; span <= size; span <<= 1)
    {
        const auto half = span / 2;

        for (std::size_t start = 0; start < size; start += span)
        {
            for (std::size_t offset = 0; offset < half; ++offset)
            {
                // Computed rather than carried forward by repeated
                // multiplication: the recurrence is quicker and accumulates
                // error along the stage, which lands hardest on the low
                // harmonics that carry most of a waveform.
                const auto angle = -twoPi * static_cast<double> (offset)
                                 / static_cast<double> (span);

                const std::complex<double> twiddle { std::cos (angle), std::sin (angle) };

                const auto even = samples[start + offset];
                const auto odd = twiddle * samples[start + offset + half];

                samples[start + offset] = even + odd;
                samples[start + offset + half] = even - odd;
            }
        }
    }
}

std::vector<std::complex<double>> analyseCycle (const std::vector<double>& samples,
                                                int numHarmonics)
{
    if (numHarmonics <= 0 || ! isTransformSize (samples.size()))
        return {};

    std::vector<std::complex<double>> spectrum (samples.size());

    for (std::size_t i = 0; i < samples.size(); ++i)
        spectrum[i] = { samples[i], 0.0 };

    forwardTransform (spectrum);

    const auto size = static_cast<double> (samples.size());

    // Only half the transform holds distinct harmonics; the rest is the mirror
    // image a real signal always produces. Asking for more than that is not an
    // error — a caller wants a fixed-length spectrum — so the excess comes back
    // silent rather than as the reflection, which would be audible nonsense.
    const auto available = static_cast<int> (samples.size() / 2);
    const auto count = static_cast<std::size_t> (numHarmonics);

    std::vector<std::complex<double>> harmonics (count);

    for (std::size_t k = 1; k <= count; ++k)
    {
        if (static_cast<int> (k) >= available)
            break;

        const auto bin = spectrum[k];

        // Real input: x[n] = a0 + sum a_k cos + b_k sin, with
        // a_k = 2 Re(X[k]) / N and b_k = -2 Im(X[k]) / N. The DC term is
        // dropped deliberately — see the header.
        harmonics[k - 1] = { 2.0 * bin.real() / size, -2.0 * bin.imag() / size };
    }

    return harmonics;
}

} // namespace apollo::dsp
