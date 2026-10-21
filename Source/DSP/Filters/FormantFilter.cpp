#include "DSP/Filters/FormantFilter.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

struct Vowel
{
    std::array<float, FormantCoefficients::numFormants> frequency;  // hertz
    std::array<float, FormantCoefficients::numFormants> level;      // decibels
    std::array<float, FormantCoefficients::numFormants> bandwidth;  // hertz
};

/** Tenor formants (Csound's formant table), in the order the cutoff visits
    them: U, O, A, E, I. */
constexpr std::array<Vowel, 5> vowels {{
    { { 350.0f, 600.0f, 2700.0f }, { 0.0f, -20.0f, -17.0f }, { 40.0f, 60.0f, 100.0f } },
    { { 400.0f, 800.0f, 2600.0f }, { 0.0f, -10.0f, -12.0f }, { 40.0f, 80.0f, 100.0f } },
    { { 650.0f, 1080.0f, 2650.0f }, { 0.0f, -6.0f, -7.0f }, { 80.0f, 90.0f, 120.0f } },
    { { 400.0f, 1700.0f, 2600.0f }, { 0.0f, -14.0f, -12.0f }, { 70.0f, 80.0f, 100.0f } },
    { { 290.0f, 1870.0f, 2800.0f }, { 0.0f, -15.0f, -18.0f }, { 40.0f, 90.0f, 100.0f } },
}};

/** U's cutoff, and the distance between vowels. */
constexpr float firstVowelHz = 160.0f;
constexpr float octavesPerVowel = 1.5f;

/** The Q at which the formants are the table's own width. */
constexpr float tableQ = 1.414f;

/** Makeup gain. Three narrow bands pass a small part of a rich waveform: a
    110 Hz saw measured 10.6 to 13.3 dB quieter through every vowel than through
    no filter. +12 dB puts each vowel within 1.5 dB of it, so choosing the
    formant type changes the vowel and not the level (CombFormantTests). */
constexpr float makeupGain = 4.0f;

} // namespace

float FormantCoefficients::vowelPosition (float cutoffHz) noexcept
{
    // Positive test first, so NaN and anything below the first vowel land on U.
    if (! (cutoffHz > firstVowelHz))
        return 0.0f;

    const auto position = std::log2 (cutoffHz / firstVowelHz) / octavesPerVowel;
    const auto last = static_cast<float> (vowels.size() - 1);

    return position < last ? position : last;
}

void FormantCoefficients::set (float cutoffHz, float q, double sampleRate) noexcept
{
    const auto position = vowelPosition (cutoffHz);

    auto lower = static_cast<std::size_t> (position);

    if (lower >= vowels.size() - 1)
        lower = vowels.size() - 2;

    const auto t = position - static_cast<float> (lower);
    const auto& from = vowels[lower];
    const auto& to = vowels[lower + 1];

    // NaN reaches the state-variable filter's own clamp, which sends it to the
    // widest setting rather than into a coefficient.
    const auto narrowing = q / tableQ;

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const auto frequency = from.frequency[i] * std::exp2 (t * std::log2 (to.frequency[i] / from.frequency[i]));
        const auto levelDb = from.level[i] + t * (to.level[i] - from.level[i]);
        const auto bandwidth = from.bandwidth[i] + t * (to.bandwidth[i] - from.bandwidth[i]);

        bands[i].set (frequency, frequency / bandwidth * narrowing, sampleRate);

        // The band-pass peaks at Q, so multiplying by the damping (1 / Q)
        // puts each formant's centre at exactly its level.
        gains[i] = std::pow (10.0f, levelDb / 20.0f) * bands[i].k * makeupGain;
    }
}

void FormantFilter::reset() noexcept
{
    for (auto& band : bands)
        band.reset();
}

void FormantFilter::setCoefficients (const FormantCoefficients& newCoefficients) noexcept
{
    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        bands[i].setMode (StateVariableFilter::Mode::bandpass);
        bands[i].setCoefficients (newCoefficients.bands[i]);
    }

    gains = newCoefficients.gains;
}

float FormantFilter::processSample (float input) noexcept
{
    auto output = 0.0f;

    for (std::size_t i = 0; i < bands.size(); ++i)
        output += gains[i] * bands[i].processSample (input);

    return output;
}

} // namespace apollo::dsp
