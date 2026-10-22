#include "Engine/SpectrumFrame.h"

#include <algorithm>
#include <cmath>

namespace apollo::engine
{

namespace
{

constexpr double lowestHz = 20.0;
constexpr double highestHz = 20000.0;
constexpr double pivotHz = 1000.0;
constexpr double twoPi = 6.283185307179586476925286766559;

/** The ratio between one point's frequency and the next. */
[[nodiscard]] double pointRatio() noexcept
{
    return std::pow (highestHz / lowestHz, 1.0 / static_cast<double> (spectrumPoints - 1));
}

} // namespace

double SpectrumAnalyser::frequencyOfPoint (int point) noexcept
{
    const auto clamped = std::clamp (point, 0, spectrumPoints - 1);
    return lowestHz * std::pow (pointRatio(), static_cast<double> (clamped));
}

SpectrumAnalyser::SpectrumAnalyser()
{
    constexpr auto size = static_cast<std::size_t> (spectrumTransformSize);

    transform.prepare (size);
    samples.resize (size);
    bins.resize (size);
    power.resize (size / 2 + 1);

    // Hann, periodic. Its coherent gain is a half: a sine of amplitude A lands
    // in its bin at A * (sum of the window) / 2, so that is what 0 dB is
    // measured against.
    window.resize (size);
    auto windowSum = 0.0;

    for (std::size_t i = 0; i < size; ++i)
    {
        window[i] = 0.5 - 0.5 * std::cos (twoPi * static_cast<double> (i) / static_cast<double> (size));
        windowSum += window[i];
    }

    normalisationDb = 20.0 * std::log10 (2.0 / windowSum);

    reset();
}

void SpectrumAnalyser::reset() noexcept
{
    smoothedInput.fill (spectrumFloorDb);
    smoothedOutput.fill (spectrumFloorDb);
}

bool SpectrumAnalyser::analyse (const telemetry::ScopeBuffer& tap, double sampleRate,
                                SpectrumFrame::Levels& levels)
{
    levels.fill (spectrumFloorDb);

    if (! tap.readWindow (samples.data(), spectrumTransformSize))
        return false;

    auto peak = 0.0f;

    for (const auto sample : samples)
        peak = std::max (peak, std::abs (sample));

    // Silence is the floor, not a transform of nothing: a transform of zeros
    // is a column of logarithms of zero, and the floor is what silence is
    // drawn as anyway.
    if (! (peak >= telemetry::scopeSilenceThreshold))
        return false;

    for (std::size_t i = 0; i < samples.size(); ++i)
        bins[i] = { static_cast<double> (samples[i]) * window[i], 0.0 };

    transform.perform (bins.data());

    for (std::size_t k = 0; k < power.size(); ++k)
        power[k] = std::norm (bins[k]);

    const auto binsPerHz = static_cast<double> (spectrumTransformSize) / sampleRate;
    const auto lastBin = static_cast<double> (power.size() - 1);
    const auto halfStep = std::sqrt (pointRatio());

    for (int point = 0; point < spectrumPoints; ++point)
    {
        const auto hz = frequencyOfPoint (point);
        const auto centre = hz * binsPerHz;

        // Above Nyquist there is nothing to show: the engine cannot produce it.
        if (centre >= lastBin)
            continue;

        const auto first = std::ceil ((hz / halfStep) * binsPerHz);
        const auto last = std::min (std::floor ((hz * halfStep) * binsPerHz), lastBin);

        auto value = 0.0;

        if (first <= last)
        {
            // Several bins in this point's share of the axis: the loudest.
            for (auto k = static_cast<std::size_t> (first); k <= static_cast<std::size_t> (last); ++k)
                value = std::max (value, power[k]);
        }
        else
        {
            // Narrower than a bin: between the two nearest.
            const auto below = static_cast<std::size_t> (std::floor (centre));
            const auto fraction = centre - std::floor (centre);
            value = power[below] + (power[below + 1] - power[below]) * fraction;
        }

        if (value <= 0.0)
            continue;

        const auto tilt = static_cast<double> (spectrumTiltDbPerOctave) * std::log2 (hz / pivotHz);
        const auto db = 10.0 * std::log10 (value) + normalisationDb + tilt;

        levels[static_cast<std::size_t> (point)] = std::max (spectrumFloorDb, static_cast<float> (db));
    }

    return true;
}

bool SpectrumAnalyser::build (const telemetry::TelemetryHub& hub, SpectrumFrame& frame)
{
    // MESSAGE THREAD.
    const auto sampleRate = hub.getSampleRate();

    frame.active = hub.isEqualiserPlaced() && sampleRate > 0.0;
    frame.silent = true;

    if (! frame.active)
    {
        // Nothing to draw, and nothing to carry into the next time there is:
        // an equaliser put back in the rack starts from the floor.
        reset();
        frame.input = smoothedInput;
        frame.output = smoothedOutput;
        return false;
    }

    const auto heardInput = analyse (hub.spectrum (telemetry::SpectrumTap::equaliserInput), sampleRate, rawInput);
    const auto heardOutput = analyse (hub.spectrum (telemetry::SpectrumTap::equaliserOutput), sampleRate, rawOutput);

    frame.silent = ! heardInput && ! heardOutput;

    const auto smooth = [] (const SpectrumFrame::Levels& raw, SpectrumFrame::Levels& smoothed)
    {
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] > smoothed[i])
                smoothed[i] += (raw[i] - smoothed[i]) * spectrumAttack;
            else
                smoothed[i] = std::max (raw[i], smoothed[i] - spectrumReleaseDbPerFrame);
        }
    };

    smooth (rawInput, smoothedInput);
    smooth (rawOutput, smoothedOutput);

    frame.input = smoothedInput;
    frame.output = smoothedOutput;
    return true;
}

} // namespace apollo::engine
