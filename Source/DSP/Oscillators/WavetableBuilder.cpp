#include "DSP/Oscillators/WavetableBuilder.h"

#include "DSP/Utilities/Fft.h"

#include <algorithm>
#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

/** Below this, a harmonic cannot change a float sample and is not rendered.

    The frames are normalised to about unit peak and stored as `float`, whose
    resolution near 1.0 is around 1e-7. A harmonic a hundred times smaller than
    that contributes nothing that can be represented, so summing it is work with
    no observable result.
*/
constexpr double negligible = 1.0e-9;

/** One cycle of sine at the top level's resolution.

    Built once and shared by every harmonic of every frame of every level, which
    turns the inner loop into an integer index and a multiply-add rather than a
    transcendental call. That is the difference between building the library
    taking milliseconds and taking seconds.
*/
[[nodiscard]] const std::vector<double>& sineTable()
{
    static const std::vector<double> table = []
    {
        std::vector<double> values (static_cast<std::size_t> (Wavetable::topLevelSamples), 0.0);

        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = std::sin (twoPi * static_cast<double> (i)
                                  / static_cast<double> (values.size()));

        return values;
    }();

    return table;
}

/** Renders one frame at one mip level.

    Only the harmonics the level is allowed to keep are summed, which is the
    band-limiting: the rest are never generated.
*/
void renderFrame (float* destination,
                  int numSamples,
                  int maxHarmonics,
                  const FrameSpectrum& spectrum)
{
    const auto& sine = sineTable();
    const auto tableSize = static_cast<int> (sine.size());
    const auto harmonicLimit = std::min (maxHarmonics, static_cast<int> (spectrum.size()));

    // A quarter of a cycle ahead of sine is cosine, which is how the cosine
    // term is taken from the same table rather than from a second one.
    const auto quarterCycle = tableSize / 4;

    for (int i = 0; i < numSamples; ++i)
        destination[i] = 0.0f;

    for (int k = 1; k <= harmonicLimit; ++k)
    {
        const auto harmonic = spectrum[static_cast<std::size_t> (k - 1)];

        const auto hasSine = std::abs (harmonic.sine) > negligible;
        const auto hasCosine = std::abs (harmonic.cosine) > negligible;

        // NEGLIGIBLE RATHER THAN ZERO, and this is most of the cost of building
        // the library. A spectrum written as a formula rarely contains an exact
        // zero: the swept table's rolloff is 1/(1 + (k/knee)^6), which at its
        // closed end leaves the hundredth harmonic at about 1e-12 — a value
        // that cannot be represented in the float samples being written, and
        // was costing a full pass over the frame to add nothing.
        //
        // The threshold is below the resolution of the destination, so this is
        // skipping work that provably has no effect rather than approximating.
        if (! hasSine && ! hasCosine)
            continue;

        // The index steps by (k * tableSize / numSamples) per output sample,
        // all in integers so the phase cannot drift across the cycle.
        const auto step = k * (tableSize / numSamples);

        int sineIndex = 0;
        int cosineIndex = quarterCycle;

        // Three loops rather than one with two multiplies in it. Every built-in
        // table but the folded one is purely sine or purely cosine phase, so
        // the common case does half the work.
        if (hasSine && ! hasCosine)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                destination[i] += static_cast<float> (
                    harmonic.sine * sine[static_cast<std::size_t> (sineIndex)]);

                sineIndex += step;

                if (sineIndex >= tableSize)
                    sineIndex -= tableSize;
            }
        }
        else if (hasCosine && ! hasSine)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                destination[i] += static_cast<float> (
                    harmonic.cosine * sine[static_cast<std::size_t> (cosineIndex)]);

                cosineIndex += step;

                if (cosineIndex >= tableSize)
                    cosineIndex -= tableSize;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto value = harmonic.sine * sine[static_cast<std::size_t> (sineIndex)]
                                 + harmonic.cosine * sine[static_cast<std::size_t> (cosineIndex)];

                destination[i] += static_cast<float> (value);

                sineIndex += step;
                cosineIndex += step;

                if (sineIndex >= tableSize)
                    sineIndex -= tableSize;

                if (cosineIndex >= tableSize)
                    cosineIndex -= tableSize;
            }
        }
    }
}

/** @returns the largest absolute value in a buffer. */
[[nodiscard]] float peakOf (const float* data, int numSamples) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (data[i]));

    return peak;
}

void applyGain (float* data, int numSamples, float gain) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        data[i] *= gain;
}

} // namespace

FrameSpectrum makeSilentSpectrum (int numHarmonics)
{
    return FrameSpectrum (static_cast<std::size_t> (std::max (0, numHarmonics)));
}

TableSpectrum analyseFrames (const std::vector<std::vector<double>>& frames, int numHarmonics)
{
    TableSpectrum spectrum;
    spectrum.reserve (frames.size());

    for (const auto& frame : frames)
    {
        const auto analysed = analyseCycle (frame, numHarmonics);

        auto frameSpectrum = makeSilentSpectrum (numHarmonics);

        for (std::size_t k = 0; k < frameSpectrum.size() && k < analysed.size(); ++k)
            frameSpectrum[k] = { analysed[k].real(), analysed[k].imag() };

        spectrum.push_back (std::move (frameSpectrum));
    }

    return spectrum;
}

void buildWavetable (Wavetable& table, const TableSpectrum& spectrum)
{
    const auto numFrames = static_cast<int> (spectrum.size());

    table.setSize (numFrames);

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const auto& frameSpectrum = spectrum[static_cast<std::size_t> (frame)];

        float loudestPeak = 0.0f;

        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            auto* data = table.getWritePointer (level, frame);

            if (data == nullptr)
                continue;

            const auto samples = Wavetable::samplesAtLevel (level);

            renderFrame (data, samples, Wavetable::harmonicsAtLevel (level), frameSpectrum);

            loudestPeak = std::max (loudestPeak, peakOf (data, samples));
        }

        // A silent frame stays silent. Scaling it by 1/0 would be the one way a
        // table could carry a value the oscillator cannot use.
        if (loudestPeak <= 0.0f)
            continue;

        const auto gain = 1.0f / loudestPeak;

        for (int level = 0; level < Wavetable::numMipLevels; ++level)
            if (auto* data = table.getWritePointer (level, frame))
                applyGain (data, Wavetable::samplesAtLevel (level), gain);
    }
}

} // namespace apollo::dsp
