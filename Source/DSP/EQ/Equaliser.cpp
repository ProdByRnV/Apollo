#include "DSP/EQ/Equaliser.h"

#include <algorithm>
#include <cmath>

namespace apollo::dsp
{

namespace
{

/** Twenty milliseconds, the same ramp every other level in Apollo uses. */
constexpr double levelSmoothingSeconds = 0.02;

[[nodiscard]] float decibelsToGain (float decibels) noexcept
{
    return std::pow (10.0f, decibels * 0.05f);
}

} // namespace

Equaliser::Settings Equaliser::defaultSettings() noexcept
{
    Settings result;

    for (std::size_t band = 0; band < result.bands.size(); ++band)
    {
        result.bands[band].type = EqualiserBand::Type::peaking;
        result.bands[band].frequencyHz = defaultFrequencies[band];
        result.bands[band].gainDb = 0.0f;
        result.bands[band].bandwidthOctaves = 1.0f;
        result.bands[band].order = 1;
        result.bands[band].muted = false;
    }

    result.levelDb = 0.0f;

    return result;
}

Equaliser::Equaliser()
{
    settings = defaultSettings();

    for (std::size_t band = 0; band < bands.size(); ++band)
        bands[band].setSettings (settings.bands[band]);
}

void Equaliser::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize; // Nothing here is sized by the block; it works in place.

    preparedSampleRate = sampleRate;

    for (auto& band : bands)
        band.prepare (sampleRate);

    level.reset (sampleRate, levelSmoothingSeconds);
    level.setCurrentAndTargetValue (decibelsToGain (settings.levelDb));
}

void Equaliser::reset() noexcept
{
    for (auto& band : bands)
        band.reset();

    // Every smoothed control placed on its target, which is what reset means
    // across the whole rack (ADR-0060). This one already did; it is spelled with
    // the same verb as the other five so that a reader can see at a glance that
    // they agree.
    level.settle();
}

void Equaliser::setSettings (const Settings& newSettings) noexcept
{
    if (newSettings == settings)
        return;

    settings = newSettings;

    for (std::size_t band = 0; band < bands.size(); ++band)
        bands[band].setSettings (settings.bands[band]);

    level.setTargetValue (decibelsToGain (settings.levelDb));
}

const EqualiserBand& Equaliser::getBand (int index) const noexcept
{
    const auto clamped = std::clamp (index, 0, bandCount - 1);

    return bands[static_cast<std::size_t> (clamped)];
}

void Equaliser::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    // Seven calls per block, each of which decides for itself whether it has
    // anything to do. A band sitting at 0 dB returns immediately, so an
    // equaliser with two bands dialled in costs two bands.
    for (auto& band : bands)
        band.process (channels, numChannels, numSamples);

    // The trim is skipped outright at unity, which is where it sits until
    // someone moves it — and unity here means bit-exact, not nearly.
    if (! level.isSmoothing() && level.getCurrentValue() == 1.0f)
        return;

    const auto usableChannels = std::min (numChannels, maxEffectChannels);

    // The ramp is advanced once and replayed across the channels, so both sides
    // of a stereo signal are scaled by the same number on the same sample. A
    // per-channel ramp would drift the image while the trim moved.
    for (auto sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = level.getNextValue();

        for (auto channel = 0; channel < usableChannels; ++channel)
            if (channels[channel] != nullptr)
                channels[channel][sample] *= gain;
    }
}

double Equaliser::getTailSeconds() const noexcept
{
    auto longest = 0.0;

    for (const auto& band : bands)
    {
        const auto tail = band.getTailSeconds();
        longest = tail > longest ? tail : longest;
    }

    return longest;
}

double Equaliser::magnitudeDbAt (double hz) const noexcept
{
    auto total = static_cast<double> (settings.levelDb);

    for (const auto& band : bands)
        total += band.magnitudeDbAt (hz);

    return total;
}

double Equaliser::magnitudeDbAt (const Settings& settingsToUse, double sampleRate, double hz) noexcept
{
    auto total = static_cast<double> (settingsToUse.levelDb);

    for (const auto& band : settingsToUse.bands)
        total += EqualiserBand::magnitudeDbAt (band, sampleRate, hz);

    return total;
}

} // namespace apollo::dsp
