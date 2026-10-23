#include "Resources/NoiseSampleFile.h"

#include "DSP/Utilities/Resampler.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace apollo::resources
{

juce::String describe (NoiseSampleLoadResult result)
{
    switch (result)
    {
        case NoiseSampleLoadResult::ok:
            return "The noise sample loaded.";
        case NoiseSampleLoadResult::missing:
            return "That noise sample is not there any more.";
        case NoiseSampleLoadResult::tooLarge:
            return "That file is far larger than a noise sample.";
        case NoiseSampleLoadResult::unreadable:
            return "That file could not be read as audio.";
        case NoiseSampleLoadResult::empty:
            return "That file has no audio in it.";
        case NoiseSampleLoadResult::tooLong:
            return "A noise sample can be at most ten seconds long.";
        case NoiseSampleLoadResult::notFinite:
            return "That sample contains values that are not numbers.";
        case NoiseSampleLoadResult::silent:
            return "That sample is silent.";
    }

    return "That file could not be read as a noise sample.";
}

NoiseSampleLoadResult readNoiseSample (juce::AudioFormatReader& reader, NoiseSampleAudio& destination)
{
    destination = {};

    const auto rate = reader.sampleRate;
    const auto length = reader.lengthInSamples;

    if (reader.numChannels == 0 || ! (rate > 0.0) || ! std::isfinite (rate))
        return NoiseSampleLoadResult::unreadable;

    if (length <= 0)
        return NoiseSampleLoadResult::empty;

    // Asked of the header, before a sample is decoded: an hour-long file costs
    // a division rather than an hour of decoding.
    if (static_cast<double> (length) / rate > maximumNoiseSampleSeconds + 1.0e-9)
        return NoiseSampleLoadResult::tooLong;

    const auto numSamples = static_cast<int> (length);
    const auto channels = std::min (2, static_cast<int> (reader.numChannels));

    juce::AudioBuffer<float> audio (channels, numSamples);

    if (! reader.read (&audio, 0, numSamples, 0, true, channels > 1))
        return NoiseSampleLoadResult::unreadable;

    std::vector<double> left (static_cast<std::size_t> (numSamples));
    std::vector<double> right (static_cast<std::size_t> (numSamples));

    auto loudest = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = static_cast<double> (audio.getSample (0, i));
        const auto r = static_cast<double> (audio.getSample (channels - 1, i));

        // Before anything is resampled, so an infinity cannot be smeared
        // across forty neighbours on its way to a voice (CLAUDE.md §34.2).
        if (! std::isfinite (l) || ! std::isfinite (r))
            return NoiseSampleLoadResult::notFinite;

        left[static_cast<std::size_t> (i)] = l;
        right[static_cast<std::size_t> (i)] = r;
        loudest = std::max ({ loudest, std::abs (l), std::abs (r) });
    }

    if (loudest <= 0.0)
        return NoiseSampleLoadResult::silent;

    destination.left = dsp::resample (left, rate, noiseSampleRate);
    destination.right = dsp::resample (right, rate, noiseSampleRate);

    // A file shorter than one sample at the new rate.
    if (destination.left.empty())
    {
        destination = {};
        return NoiseSampleLoadResult::empty;
    }

    return NoiseSampleLoadResult::ok;
}

NoiseSampleLoadResult readNoiseSampleFile (const juce::File& file, NoiseSampleAudio& destination)
{
    destination = {};

    if (! file.existsAsFile())
        return NoiseSampleLoadResult::missing;

    if (file.getSize() > maximumNoiseSampleBytes)
        return NoiseSampleLoadResult::tooLarge;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr)
        return NoiseSampleLoadResult::unreadable;

    return readNoiseSample (*reader, destination);
}

} // namespace apollo::resources
