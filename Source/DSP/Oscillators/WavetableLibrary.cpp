#include "DSP/Oscillators/WavetableLibrary.h"

#include <cmath>
#include <vector>

namespace apollo::dsp
{

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

/** Harmonic amplitudes describing one waveform.

    Index k holds the amplitude of harmonic k + 1. Sine phase throughout, which
    is what makes the classic shapes come out with their textbook spectra.
*/
using HarmonicSeries = std::vector<double>;

/** Ideal sawtooth: every harmonic, amplitude 1/k. */
[[nodiscard]] HarmonicSeries makeSaw (int numHarmonics)
{
    HarmonicSeries series (static_cast<std::size_t> (numHarmonics), 0.0);

    for (int k = 1; k <= numHarmonics; ++k)
        series[static_cast<std::size_t> (k - 1)] = 1.0 / static_cast<double> (k);

    return series;
}

/** Ideal square: odd harmonics only, amplitude 1/k. */
[[nodiscard]] HarmonicSeries makeSquare (int numHarmonics)
{
    HarmonicSeries series (static_cast<std::size_t> (numHarmonics), 0.0);

    for (int k = 1; k <= numHarmonics; k += 2)
        series[static_cast<std::size_t> (k - 1)] = 1.0 / static_cast<double> (k);

    return series;
}

/** Ideal triangle: odd harmonics, amplitude 1/k^2, alternating sign. */
[[nodiscard]] HarmonicSeries makeTriangle (int numHarmonics)
{
    HarmonicSeries series (static_cast<std::size_t> (numHarmonics), 0.0);
    double sign = 1.0;

    for (int k = 1; k <= numHarmonics; k += 2)
    {
        series[static_cast<std::size_t> (k - 1)] = sign / (static_cast<double> (k) * static_cast<double> (k));
        sign = -sign;
    }

    return series;
}

/** A single harmonic: a pure sine. */
[[nodiscard]] HarmonicSeries makeSine (int numHarmonics)
{
    HarmonicSeries series (static_cast<std::size_t> (numHarmonics), 0.0);

    if (numHarmonics > 0)
        series[0] = 1.0;

    return series;
}

/** Linear blend between two harmonic series, used to morph across frames. */
[[nodiscard]] HarmonicSeries blend (const HarmonicSeries& a, const HarmonicSeries& b, double amount)
{
    HarmonicSeries result (a.size(), 0.0);

    for (std::size_t i = 0; i < a.size(); ++i)
        result[i] = a[i] * (1.0 - amount) + b[i] * amount;

    return result;
}

/** @returns the largest absolute value in a buffer. */
[[nodiscard]] float peakOf (const float* data, int numSamples) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (data[i]));

    return peak;
}

/** Scales a buffer in place. */
void applyGain (float* data, int numSamples, float gain) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        data[i] *= gain;
}

/** Renders one frame at one mip level from a harmonic series.

    Only the harmonics the level is allowed to keep are summed, which is the
    band-limiting: the rest are never generated.

    A shared sine lookup table makes this an integer index and a multiply-add
    per harmonic per sample rather than a transcendental call, which is the
    difference between generation taking milliseconds and taking seconds.
*/
void renderFrame (float* destination,
                  int numSamples,
                  int maxHarmonics,
                  const HarmonicSeries& series,
                  const std::vector<double>& sineTable)
{
    const auto tableSize = static_cast<int> (sineTable.size());
    const auto harmonicLimit = std::min (maxHarmonics, static_cast<int> (series.size()));

    for (int i = 0; i < numSamples; ++i)
        destination[i] = 0.0f;

    for (int k = 1; k <= harmonicLimit; ++k)
    {
        const auto amplitude = series[static_cast<std::size_t> (k - 1)];

        if (amplitude == 0.0)
            continue;

        // Index steps by (k * tableSize / numSamples) per output sample, all in
        // integers so the phase cannot drift across the cycle.
        const auto step = k * (tableSize / numSamples);
        int index = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            destination[i] += static_cast<float> (amplitude * sineTable[static_cast<std::size_t> (index)]);

            index += step;

            if (index >= tableSize)
                index -= tableSize;
        }
    }
}

/** Fills every mip level and frame of a table by morphing between two shapes. */
void buildMorphTable (Wavetable& table,
                      const HarmonicSeries& startShape,
                      const HarmonicSeries& endShape,
                      const std::vector<double>& sineTable)
{
    table.setSize (WavetableLibrary::framesPerTable);

    for (int frame = 0; frame < WavetableLibrary::framesPerTable; ++frame)
    {
        const auto amount = WavetableLibrary::framesPerTable > 1
                              ? static_cast<double> (frame)
                                    / static_cast<double> (WavetableLibrary::framesPerTable - 1)
                              : 0.0;

        const auto series = blend (startShape, endShape, amount);

        // Every level is rendered first, then all of them are scaled by a single
        // shared factor.
        //
        // The factor must come from the *loudest* level, not from the most
        // detailed one. Removing harmonics does not simply lower the peak: a
        // partial mix can peak higher with fewer harmonics than with all of
        // them, because the harmonics that cancelled the fundamental's crest
        // are the ones that were removed. Normalising against level 0 therefore
        // let the reduced levels reach 1.097 — measured — and a voice could
        // exceed the amplitude the engine's gain staging assumes.
        //
        // One shared factor is still essential: normalising levels
        // independently would boost the duller ones and make a note change
        // loudness as it crossed an octave boundary.
        float loudestPeak = 0.0f;

        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            auto* data = table.getWritePointer (level, frame);
            const auto samples = Wavetable::samplesAtLevel (level);

            renderFrame (data, samples, Wavetable::harmonicsAtLevel (level), series, sineTable);

            loudestPeak = std::max (loudestPeak, peakOf (data, samples));
        }

        if (loudestPeak > 0.0f)
        {
            const auto gain = 1.0f / loudestPeak;

            for (int level = 0; level < Wavetable::numMipLevels; ++level)
                applyGain (table.getWritePointer (level, frame),
                           Wavetable::samplesAtLevel (level),
                           gain);
        }
    }
}

} // namespace

WavetableLibrary::WavetableLibrary()
{
    // One cycle of sine at the top level's resolution, shared by every render.
    std::vector<double> sineTable (static_cast<std::size_t> (Wavetable::topLevelSamples), 0.0);

    for (std::size_t i = 0; i < sineTable.size(); ++i)
        sineTable[i] = std::sin (twoPi * static_cast<double> (i)
                                 / static_cast<double> (sineTable.size()));

    constexpr int harmonics = Wavetable::topLevelHarmonics;

    const auto sine = makeSine (harmonics);
    const auto triangle = makeTriangle (harmonics);
    const auto saw = makeSaw (harmonics);
    const auto square = makeSquare (harmonics);

    // Placeholder factory content: four morphs across the classic shapes, chosen
    // to give the scanning and spectral tests something well-defined to measure.
    buildMorphTable (tables[0], sine, saw, sineTable);
    buildMorphTable (tables[1], sine, square, sineTable);
    buildMorphTable (tables[2], triangle, saw, sineTable);
    buildMorphTable (tables[3], saw, square, sineTable);
}

const Wavetable& WavetableLibrary::getTable (int index) const noexcept
{
    const auto clamped = index < 0 ? 0 : (index >= numTables ? numTables - 1 : index);
    return tables[static_cast<std::size_t> (clamped)];
}

} // namespace apollo::dsp
