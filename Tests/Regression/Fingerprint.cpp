#include "Regression/Fingerprint.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace apollo::regression
{

namespace
{

/** Amplitude to dBFS, floored. */
[[nodiscard]] float amplitudeToDb (double amplitude) noexcept
{
    if (! (amplitude > 0.0))
        return quietFloorDb;

    return std::max (quietFloorDb, static_cast<float> (20.0 * std::log10 (amplitude)));
}

/** Power to dBFS, floored. */
[[nodiscard]] float powerToDb (double power) noexcept
{
    if (! (power > 0.0))
        return quietFloorDb;

    return std::max (quietFloorDb, static_cast<float> (10.0 * std::log10 (power)));
}

[[nodiscard]] double meanSquareOf (const std::vector<float>& signal,
                                   std::size_t from,
                                   std::size_t to) noexcept
{
    if (to <= from)
        return 0.0;

    auto sum = 0.0;

    for (auto i = from; i < to; ++i)
    {
        const auto value = static_cast<double> (signal[i]);
        sum += value * value;
    }

    return sum / static_cast<double> (to - from);
}

/** The transform length used for the band estimate.

    4096 points at 48 kHz is a 12 Hz resolution and an 85 ms frame. The
    resolution is set by the narrowest band rather than by taste: a third-octave
    band at the bottom of the range spans 20 Hz to 25 Hz, so a coarser transform
    would have bands with no bins in them at all — which is not an empty band,
    it is a band that reports whatever its neighbour's leakage happened to be.
    The frame length is the other half of the same trade, and 85 ms is short
    enough that a render of a second and a half still averages thirty-odd of
    them.

    A render too short for that drops to whatever power of two fits, because a
    fingerprint of a brief case is still worth having.
*/
[[nodiscard]] int transformLengthFor (int numSamples) noexcept
{
    auto length = 4096;

    while (length > 256 && length > numSamples)
        length /= 2;

    return length;
}

/** Fowler-Noll-Vo over the raw bits. Exact, and only ever compared with itself. */
[[nodiscard]] std::uint64_t hashOf (const juce::AudioBuffer<float>& buffer) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            std::uint32_t bits = 0;
            std::memcpy (&bits, &data[i], sizeof (bits));

            for (int byte = 0; byte < 4; ++byte)
            {
                hash ^= static_cast<std::uint64_t> ((bits >> (byte * 8)) & 0xFFu);
                hash *= 1099511628211ull;
            }
        }
    }

    return hash;
}

} // namespace

Fingerprint fingerprintOf (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    Fingerprint print;

    print.midDb.fill (quietFloorDb);
    print.sideDb.fill (quietFloorDb);
    print.bandDb.fill (quietFloorDb);

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() <= 0 || sampleRate <= 0.0)
        return print;

    const auto* left = buffer.getReadPointer (0);
    const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;

    const auto count = static_cast<std::size_t> (numSamples);

    std::vector<float> mid (count);
    std::vector<float> side (count);

    auto peak = 0.0;

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto index = static_cast<int> (i);
        const auto l = static_cast<double> (left[index]);
        const auto r = static_cast<double> (right[index]);

        mid[i] = static_cast<float> (0.5 * (l + r));
        side[i] = static_cast<float> (0.5 * (l - r));

        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
    }

    print.peakDb = amplitudeToDb (peak);

    // RMS of the pair, which is the level a meter would show.
    const auto midMeanSquare = meanSquareOf (mid, 0, count);
    print.rmsDb = amplitudeToDb (std::sqrt (midMeanSquare + meanSquareOf (side, 0, count)));

    //==========================================================================
    // The envelope: sixteen equal slices of the render.

    for (int segment = 0; segment < segmentCount; ++segment)
    {
        const auto slices = static_cast<std::size_t> (segmentCount);
        const auto from = count * static_cast<std::size_t> (segment) / slices;
        const auto to = count * static_cast<std::size_t> (segment + 1) / slices;

        const auto index = static_cast<std::size_t> (segment);

        print.midDb[index] = amplitudeToDb (std::sqrt (meanSquareOf (mid, from, to)));
        print.sideDb[index] = amplitudeToDb (std::sqrt (meanSquareOf (side, from, to)));
    }

    //==========================================================================
    // The timbre: an averaged spectrum of the mid signal, gathered into bands.

    const auto length = transformLengthFor (numSamples);
    const auto bins = length / 2;

    juce::dsp::FFT fft (static_cast<int> (std::log2 (static_cast<double> (length)) + 0.5));

    std::vector<float> window (static_cast<std::size_t> (length));

    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.data(),
        static_cast<std::size_t> (length),
        juce::dsp::WindowingFunction<float>::hann,
        false);

    std::vector<double> power (static_cast<std::size_t> (bins) + 1, 0.0);
    std::vector<float> frame (static_cast<std::size_t> (length) * 2);

    const auto hop = length / 2;
    auto frames = 0;

    for (int start = 0; start + length <= numSamples; start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);

        for (int i = 0; i < length; ++i)
            frame[static_cast<std::size_t> (i)] =
                mid[static_cast<std::size_t> (start + i)] * window[static_cast<std::size_t> (i)];

        fft.performFrequencyOnlyForwardTransform (frame.data());

        for (int bin = 0; bin <= bins; ++bin)
        {
            const auto magnitude = static_cast<double> (frame[static_cast<std::size_t> (bin)]);
            power[static_cast<std::size_t> (bin)] += magnitude * magnitude;
        }

        ++frames;
    }

    if (frames == 0)
        return print;

    auto total = 0.0;

    for (const auto value : power)
        total += value;

    // Scaled so the bands sum to the mean square of the mid signal. The
    // transform's own normalisation, the window's power and the overlap all
    // cancel out of that, which is the point: the numbers are levels rather
    // than whatever this particular FFT happens to return.
    const auto scale = total > 0.0 ? midMeanSquare / total : 0.0;

    const auto nyquist = sampleRate * 0.5;

    std::array<double, static_cast<std::size_t> (bandCount) + 1> edges {};

    for (int edge = 0; edge <= bandCount; ++edge)
        edges[static_cast<std::size_t> (edge)] =
            lowestBandHz * std::pow (nyquist / lowestBandHz,
                                     static_cast<double> (edge) / static_cast<double> (bandCount));

    std::array<double, static_cast<std::size_t> (bandCount)> gathered {};
    gathered.fill (0.0);

    for (int bin = 0; bin <= bins; ++bin)
    {
        const auto frequency = static_cast<double> (bin) * sampleRate / static_cast<double> (length);

        // Everything below the lowest edge — DC included — belongs to the first
        // band. A synthesiser's sub-20 Hz content is not a band anybody reads
        // separately, but it is not something to throw away either.
        auto band = 0;

        while (band + 1 < bandCount && frequency >= edges[static_cast<std::size_t> (band + 1)])
            ++band;

        gathered[static_cast<std::size_t> (band)] += power[static_cast<std::size_t> (bin)];
    }

    for (int band = 0; band < bandCount; ++band)
        print.bandDb[static_cast<std::size_t> (band)] =
            powerToDb (scale * gathered[static_cast<std::size_t> (band)]);

    print.checksum = hashOf (buffer);

    return print;
}

std::vector<Difference> compare (const Fingerprint& golden,
                                 const Fingerprint& measured,
                                 const Tolerances& tolerances)
{
    std::vector<Difference> differences;

    const auto check = [&differences] (const std::string& what, float a, float b, float allowed)
    {
        // Both clamped, so two silences agree rather than two arbitrary
        // sub-audible numbers being asked to.
        const auto first = std::max (a, quietFloorDb);
        const auto second = std::max (b, quietFloorDb);
        const auto delta = std::abs (second - first);

        if (delta > allowed)
            differences.push_back ({ what, first, second, second - first });
    };

    check ("peak", golden.peakDb, measured.peakDb, tolerances.levelDb);
    check ("rms", golden.rmsDb, measured.rmsDb, tolerances.levelDb);

    for (int segment = 0; segment < segmentCount; ++segment)
    {
        const auto index = static_cast<std::size_t> (segment);
        const auto where = "/" + std::to_string (segment);

        check ("mid" + where, golden.midDb[index], measured.midDb[index], tolerances.levelDb);
        check ("side" + where, golden.sideDb[index], measured.sideDb[index], tolerances.levelDb);
    }

    for (int band = 0; band < bandCount; ++band)
    {
        const auto index = static_cast<std::size_t> (band);

        check ("band/" + std::to_string (band),
               golden.bandDb[index], measured.bandDb[index], tolerances.bandDb);
    }

    return differences;
}

float worstDifference (const Fingerprint& golden, const Fingerprint& measured)
{
    // Every measurement compared with a tolerance of zero: whatever comes back
    // is the largest move, and its size against the real tolerance is how much
    // room the suite has before a legitimate build difference turns red.
    const auto everything = compare (golden, measured, { 0.0f, 0.0f });

    auto worst = 0.0f;

    for (const auto& difference : everything)
        worst = std::max (worst, std::abs (difference.delta));

    return worst;
}

} // namespace apollo::regression
