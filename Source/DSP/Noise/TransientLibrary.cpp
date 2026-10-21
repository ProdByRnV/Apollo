#include "DSP/Noise/TransientLibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace apollo::dsp
{

namespace
{

constexpr double rate = TransientSample::referenceRate;
constexpr double twoPi = 2.0 * std::numbers::pi;

/** A seeded uniform source in [-1, 1), so every build holds the same samples. */
class Random
{
public:
    explicit Random (std::uint32_t seed) noexcept : state (seed | 1u) {}

    [[nodiscard]] double next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double> (static_cast<std::int32_t> (state)) / 2147483648.0;
    }

private:
    std::uint32_t state;
};

/** A band-pass with 0 dB at its centre (Bristow-Johnson's cookbook). */
class BandPass
{
public:
    BandPass (double centreHz, double q) noexcept
    {
        const auto w = twoPi * centreHz / rate;
        const auto alpha = std::sin (w) / (2.0 * q);
        const auto a0 = 1.0 + alpha;

        b0 = alpha / a0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    [[nodiscard]] double process (double x) noexcept
    {
        const auto y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

private:
    double b0 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
};

/** A one-pole low-pass. */
class LowPass
{
public:
    explicit LowPass (double cornerHz) noexcept : coefficient (std::exp (-twoPi * cornerHz / rate)) {}

    [[nodiscard]] double process (double x) noexcept
    {
        state = x + coefficient * (state - x);
        return state;
    }

private:
    double coefficient;
    double state = 0.0;
};

[[nodiscard]] int framesFor (double seconds) noexcept
{
    return static_cast<int> (seconds * rate);
}

/** A raised-cosine fade over the last tenth, so no transient ends on a step. */
void fadeOut (std::vector<double>& signal)
{
    const auto length = signal.size();
    const auto fade = std::max<std::size_t> (length / 10, static_cast<std::size_t> (framesFor (0.002)));

    for (std::size_t i = 0; i < fade && i < length; ++i)
    {
        const auto gain = 0.5 - 0.5 * std::cos (std::numbers::pi * static_cast<double> (i) / static_cast<double> (fade));
        signal[length - 1 - i] *= gain;
    }
}

// The four transients. Each is written as one channel and called once per
// channel with a different seed, so the noise in it is decorrelated between
// the two sides and anything tonal is shared.

/** A sub-millisecond burst of high-passed noise: the tick of a key. */
[[nodiscard]] std::vector<double> makeClick (std::uint32_t seed)
{
    Random random (seed);
    LowPass body (1000.0);
    std::vector<double> out (static_cast<std::size_t> (framesFor (0.015)));

    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const auto t = static_cast<double> (n) / rate;
        const auto x = random.next() * std::exp (-t / 0.0006);

        // The noise less its low-passed self: a one-pole high-pass at 1 kHz.
        out[n] = x - body.process (x);
    }

    return out;
}

/** A bright band of noise struck and left to ring: a plectrum's attack. */
[[nodiscard]] std::vector<double> makePick (std::uint32_t seed)
{
    Random random (seed);
    BandPass band (3500.0, 0.8);
    std::vector<double> out (static_cast<std::size_t> (framesFor (0.08)));

    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const auto t = static_cast<double> (n) / rate;
        const auto rise = std::min (1.0, t / 0.0002);
        out[n] = band.process (random.next() * rise * std::exp (-t / 0.006));
    }

    return out;
}

/** A sine falling from 180 Hz to 50, with a little noise at its front: the
    body of a drum, or a felt hammer. The sine is the same on both sides. */
[[nodiscard]] std::vector<double> makeThump (std::uint32_t seed)
{
    Random random (seed);
    LowPass dull (800.0);
    std::vector<double> out (static_cast<std::size_t> (framesFor (0.25)));
    double phase = 0.0;

    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const auto t = static_cast<double> (n) / rate;
        const auto frequency = 50.0 + 130.0 * std::exp (-t / 0.02);

        const auto tone = std::sin (phase) * std::exp (-t / 0.06);
        const auto knock = 0.3 * dull.process (random.next() * std::exp (-t / 0.004));

        out[n] = tone + knock;
        phase += twoPi * frequency / rate;
    }

    return out;
}

/** A narrow band of noise that swells and fades: the chiff at the start of a
    flute or an organ pipe. */
[[nodiscard]] std::vector<double> makeChiff (std::uint32_t seed)
{
    Random random (seed);
    BandPass band (1600.0, 3.0);
    std::vector<double> out (static_cast<std::size_t> (framesFor (0.3)));

    for (std::size_t n = 0; n < out.size(); ++n)
    {
        const auto t = static_cast<double> (n) / rate;
        const auto envelope = t < 0.008 ? t / 0.008 : std::exp (-(t - 0.008) / 0.05);
        out[n] = band.process (random.next() * envelope);
    }

    return out;
}

/** A Blackman-windowed sinc low-pass of @p taps taps, @p cutoff in cycles per
    sample, normalised to unity at DC. */
[[nodiscard]] std::vector<double> lowPassKernel (int taps, double cutoff)
{
    std::vector<double> kernel (static_cast<std::size_t> (taps));
    const auto centre = static_cast<double> (taps - 1) / 2.0;
    double sum = 0.0;

    for (int i = 0; i < taps; ++i)
    {
        const auto x = static_cast<double> (i) - centre;
        const auto sinc = x == 0.0 ? 2.0 * cutoff : std::sin (twoPi * cutoff * x) / (std::numbers::pi * x);
        const auto phase = twoPi * static_cast<double> (i) / static_cast<double> (taps - 1);
        const auto window = 0.42 - 0.5 * std::cos (phase) + 0.08 * std::cos (2.0 * phase);

        kernel[static_cast<std::size_t> (i)] = sinc * window;
        sum += sinc * window;
    }

    for (auto& tap : kernel)
        tap /= sum;

    return kernel;
}

/** Low-passes @p input with @p kernel, centred so nothing is delayed, keeping
    every @p step-th output. Outside the input is silence. */
[[nodiscard]] std::vector<double> filter (const std::vector<double>& input,
                                          const std::vector<double>& kernel,
                                          std::size_t step)
{
    const auto half = static_cast<std::ptrdiff_t> (kernel.size() / 2);
    const auto length = static_cast<std::ptrdiff_t> (input.size());
    std::vector<double> output ((input.size() + step - 1) / step);

    for (std::size_t m = 0; m < output.size(); ++m)
    {
        const auto centre = static_cast<std::ptrdiff_t> (m * step);
        double sum = 0.0;

        for (std::ptrdiff_t j = 0; j < static_cast<std::ptrdiff_t> (kernel.size()); ++j)
        {
            const auto index = centre + j - half;

            if (index >= 0 && index < length)
                sum += kernel[static_cast<std::size_t> (j)] * input[static_cast<std::size_t> (index)];
        }

        output[m] = sum;
    }

    return output;
}

/** One channel's six levels, before normalising. */
[[nodiscard]] std::array<std::vector<double>, TransientSample::numLevels>
    buildLevels (const std::vector<double>& source)
{
    // Level 0 keeps everything below 20 kHz of the 48 kHz synthesis and is
    // stored at that rate. Each level after halves the band and the rate: a
    // low-pass passing the lower 10/24 of the old rate and stopping at its
    // quarter, then every other sample. The narrower transition is why those
    // kernels are twice as long.
    static const auto topKernel = lowPassKernel (127, 22000.0 / rate);
    static const auto halvingKernel = lowPassKernel (255, 11000.0 / rate);

    std::array<std::vector<double>, TransientSample::numLevels> levels;
    levels[0] = filter (source, topKernel, 1);

    for (std::size_t k = 1; k < levels.size(); ++k)
        levels[k] = filter (levels[k - 1], halvingKernel, 2);

    return levels;
}

[[nodiscard]] TransientSample build (std::vector<double> (*make) (std::uint32_t), std::uint32_t seed)
{
    auto left = make (seed);
    auto right = make (seed ^ 0x9E3779B9u);

    fadeOut (left);
    fadeOut (right);

    return makeTransient (left, right);
}

} // namespace

TransientSample makeTransient (const std::vector<double>& left, const std::vector<double>& right)
{
    const auto leftLevels = buildLevels (left);
    const auto rightLevels = buildLevels (right);

    // Normalised so the loudest sample of level 0, on either side, is 1: the
    // same peak as the white noise, so a transient at full level is no louder
    // at its peak than the noise it replaces.
    double peak = 0.0;

    for (const auto* level : { &leftLevels[0], &rightLevels[0] })
        for (const auto sample : *level)
            peak = std::max (peak, std::abs (sample));

    const auto scale = peak > 0.0 ? 1.0 / peak : 1.0;

    TransientSample sample;

    for (std::size_t k = 0; k < sample.levels.size(); ++k)
    {
        const auto convert = [scale] (const std::vector<double>& source)
        {
            std::vector<float> converted (source.size());

            for (std::size_t i = 0; i < source.size(); ++i)
                converted[i] = static_cast<float> (source[i] * scale);

            return converted;
        };

        sample.levels[k].left = convert (leftLevels[k]);
        sample.levels[k].right = convert (rightLevels[k]);
    }

    return sample;
}

TransientLibrary::TransientLibrary()
{
    samples[0] = build (makeClick, 0x1A2B3C4Du);
    samples[1] = build (makePick, 0x5E6F7081u);
    samples[2] = build (makeThump, 0x92A3B4C5u);
    samples[3] = build (makeChiff, 0xD6E7F809u);
}

const TransientLibrary& TransientLibrary::shared()
{
    // A function-local static, so the work happens the first time an engine is
    // made rather than during static initialisation, and C++ guarantees one
    // thread does it.
    static const TransientLibrary library;
    return library;
}

const TransientSample& TransientLibrary::get (int index) const noexcept
{
    const auto clamped = index < 0 ? 0 : (index >= numTransients ? numTransients - 1 : index);
    return samples[static_cast<std::size_t> (clamped)];
}

} // namespace apollo::dsp
