#include "Analysis/SignalAnalysis.h"

#include <algorithm>
#include <cmath>

namespace apollo::analysis
{

namespace
{

/** @returns the base-two logarithm of @p size, or 0 if it is not a power of two. */
[[nodiscard]] int transformOrderOf (int size) noexcept
{
    if (size < 2 || (size & (size - 1)) != 0)
        return 0;

    auto order = 0;

    for (auto remaining = size; remaining > 1; remaining >>= 1)
        ++order;

    return order;
}

} // namespace

std::size_t Spectrum::binFor (double frequencyHz) const noexcept
{
    if (size <= 0 || sampleRate <= 0.0)
        return 0;

    const auto bin = static_cast<long long> (
        std::llround (frequencyHz * static_cast<double> (size) / sampleRate));

    if (bin < 0)
        return 0;

    const auto highest = static_cast<long long> (decibels.size()) - 1;

    return static_cast<std::size_t> (std::min (bin, std::max<long long> (highest, 0)));
}

float Spectrum::levelAt (double frequencyHz) const noexcept
{
    if (decibels.empty())
        return -200.0f;

    return decibels[binFor (frequencyHz)];
}

double Spectrum::peakFrequency() const noexcept
{
    if (decibels.size() < 3 || size <= 0 || sampleRate <= 0.0)
        return 0.0;

    // From bin 1: DC is not a pitch, and a tiny offset would otherwise win.
    std::size_t loudest = 1;

    for (std::size_t bin = 1; bin < decibels.size(); ++bin)
        if (decibels[bin] > decibels[loudest])
            loudest = bin;

    const auto centre = static_cast<double> (loudest);

    if (loudest == 0 || loudest + 1 >= decibels.size())
        return centre * sampleRate / static_cast<double> (size);

    // A parabola through the peak bin and its two neighbours, in decibels.
    // Without this the answer is quantised to the bin spacing, which at a
    // 16384-point transform and 48 kHz is nearly 3 Hz — enough to report a
    // perfectly tuned A4 as thirteen cents out.
    const auto left = static_cast<double> (decibels[loudest - 1]);
    const auto peak = static_cast<double> (decibels[loudest]);
    const auto right = static_cast<double> (decibels[loudest + 1]);

    const auto denominator = left - 2.0 * peak + right;

    const auto offset = std::abs (denominator) > 1.0e-12
                          ? 0.5 * (left - right) / denominator
                          : 0.0;

    return (centre + offset) * sampleRate / static_cast<double> (size);
}

Spectrum analyse (const std::vector<float>& samples, int size, double sampleRate)
{
    Spectrum spectrum;
    spectrum.sampleRate = sampleRate;
    spectrum.size = size;

    const auto order = transformOrderOf (size);

    if (order == 0 || static_cast<int> (samples.size()) < size)
        return spectrum;

    // Twice the length, because JUCE's frequency-only transform writes the
    // magnitudes into the first half and uses the second half as workspace.
    std::vector<float> working (static_cast<std::size_t> (size) * 2, 0.0f);

    std::copy (samples.begin(), samples.begin() + size, working.begin());

    juce::dsp::WindowingFunction<float> window (
        static_cast<std::size_t> (size),
        juce::dsp::WindowingFunction<float>::blackmanHarris);

    window.multiplyWithWindowingTable (working.data(), static_cast<std::size_t> (size));

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (working.data());

    const auto bins = static_cast<std::size_t> (size / 2);

    auto loudest = 0.0f;

    for (std::size_t bin = 1; bin < bins; ++bin)
        loudest = std::max (loudest, working[bin]);

    spectrum.decibels.assign (bins, -200.0f);

    if (loudest <= 0.0f)
        return spectrum;

    for (std::size_t bin = 0; bin < bins; ++bin)
        spectrum.decibels[bin] = static_cast<float> (
            toDecibels (static_cast<double> (working[bin]) / static_cast<double> (loudest)));

    return spectrum;
}

float peakOf (const std::vector<float>& samples) noexcept
{
    auto peak = 0.0f;

    for (const auto sample : samples)
        peak = std::max (peak, std::abs (sample));

    return peak;
}

double rmsOf (const std::vector<float>& samples) noexcept
{
    if (samples.empty())
        return 0.0;

    auto sum = 0.0;

    for (const auto sample : samples)
        sum += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (sum / static_cast<double> (samples.size()));
}

double dcOffsetOf (const std::vector<float>& samples) noexcept
{
    if (samples.empty())
        return 0.0;

    auto sum = 0.0;

    for (const auto sample : samples)
        sum += static_cast<double> (sample);

    return sum / static_cast<double> (samples.size());
}

bool allFinite (const std::vector<float>& samples) noexcept
{
    return std::all_of (samples.begin(), samples.end(),
                        [] (float sample) { return std::isfinite (sample); });
}

double toDecibels (double value, double floorDb) noexcept
{
    const auto magnitude = std::abs (value);

    if (magnitude <= 0.0)
        return floorDb;

    return std::max (floorDb, 20.0 * std::log10 (magnitude));
}

double totalHarmonicDistortionPlusNoise (const std::vector<float>& samples,
                                         double fundamentalHz,
                                         double sampleRate,
                                         int size)
{
    const auto order = transformOrderOf (size);

    if (order == 0 || static_cast<int> (samples.size()) < size || fundamentalHz <= 0.0)
        return 0.0;

    std::vector<float> working (static_cast<std::size_t> (size) * 2, 0.0f);

    std::copy (samples.begin(), samples.begin() + size, working.begin());

    juce::dsp::WindowingFunction<float> window (
        static_cast<std::size_t> (size),
        juce::dsp::WindowingFunction<float>::blackmanHarris);

    window.multiplyWithWindowingTable (working.data(), static_cast<std::size_t> (size));

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (working.data());

    const auto bins = static_cast<std::size_t> (size / 2);
    const auto binsPerHz = static_cast<double> (size) / sampleRate;

    const auto fundamentalBin = static_cast<std::size_t> (std::llround (fundamentalHz * binsPerHz));

    // The window spreads a tone over a few bins, so the fundamental is taken as
    // a small span rather than a single bin. Counting only the centre bin would
    // put the window's own skirts into the distortion figure and report a
    // perfect sine as several per cent distorted.
    constexpr std::size_t spread = 4;

    auto totalEnergy = 0.0;
    auto fundamentalEnergy = 0.0;

    for (std::size_t bin = 1; bin < bins; ++bin)
    {
        const auto magnitude = static_cast<double> (working[bin]);
        const auto energy = magnitude * magnitude;

        totalEnergy += energy;

        const auto distance = bin > fundamentalBin ? bin - fundamentalBin : fundamentalBin - bin;

        if (distance <= spread)
            fundamentalEnergy += energy;
    }

    if (totalEnergy <= 0.0)
        return 0.0;

    const auto rest = std::max (0.0, totalEnergy - fundamentalEnergy);

    return std::sqrt (rest / totalEnergy);
}

float worstInharmonicLevel (const Spectrum& spectrum, double fundamentalHz, double tolerance)
{
    if (spectrum.decibels.empty() || fundamentalHz <= 0.0 || spectrum.sampleRate <= 0.0)
        return -200.0f;

    const auto hzPerBin = spectrum.sampleRate / static_cast<double> (spectrum.size);

    auto worst = -200.0f;

    // From bin 1: DC is measured separately, as an offset rather than as a tone.
    for (std::size_t bin = 1; bin < spectrum.decibels.size(); ++bin)
    {
        const auto frequency = static_cast<double> (bin) * hzPerBin;
        const auto ratio = frequency / fundamentalHz;
        const auto nearestHarmonic = std::round (ratio);

        // Below the fundamental there is no harmonic to be near, so everything
        // there is inharmonic by definition — which is where the worst aliases
        // usually land.
        if (nearestHarmonic >= 1.0 && std::abs (ratio - nearestHarmonic) <= tolerance)
            continue;

        worst = std::max (worst, spectrum.decibels[bin]);
    }

    return worst;
}

} // namespace apollo::analysis
