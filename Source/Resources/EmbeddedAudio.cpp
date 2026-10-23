#include "Resources/EmbeddedAudio.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apollo::resources
{

juce::String encodeAudio (const std::vector<std::vector<double>>& channels, double rate)
{
    if (channels.empty() || channels.size() > 2 || channels[0].empty() || ! (rate > 0.0))
        return {};

    const auto length = channels[0].size();

    for (const auto& channel : channels)
        if (channel.size() != length)
            return {};

    if (length > static_cast<std::size_t> (std::numeric_limits<int>::max()))
        return {};

    auto peak = 0.0;

    for (const auto& channel : channels)
        for (const auto value : channel)
            if (std::isfinite (value))
                peak = std::max (peak, std::abs (value));

    if (! (peak > 0.0))
        return {};

    // Full scale, just inside it, so the encoder's rounding cannot clip.
    const auto scale = 0.999 / peak;

    juce::AudioBuffer<float> buffer (static_cast<int> (channels.size()), static_cast<int> (length));

    for (std::size_t c = 0; c < channels.size(); ++c)
        for (std::size_t i = 0; i < length; ++i)
        {
            const auto value = channels[c][i];
            buffer.setSample (static_cast<int> (c), static_cast<int> (i),
                              std::isfinite (value) ? static_cast<float> (value * scale) : 0.0f);
        }

    juce::MemoryBlock encoded;

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (encoded, false);

        juce::FlacAudioFormat flac;
        auto writer = flac.createWriterFor (stream,
                                            juce::AudioFormatWriterOptions {}
                                                .withSampleRate (rate)
                                                .withNumChannels (buffer.getNumChannels())
                                                .withBitsPerSample (24));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
            return {};

        // The writer owns the stream; destroying it finishes the file.
    }

    if (encoded.isEmpty())
        return {};

    return juce::Base64::toBase64 (encoded.getData(), encoded.getSize());
}

std::unique_ptr<juce::AudioFormatReader> decodeAudio (const juce::String& text)
{
    if (text.isEmpty() || text.length() > maximumEmbeddedAudioChars)
        return {};

    juce::MemoryOutputStream bytes;

    if (! juce::Base64::convertFromBase64 (bytes, text))
        return {};

    if (bytes.getDataSize() == 0)
        return {};

    // Handed over raw, because ownership goes one way or the other inside the
    // call: on success the reader owns the stream, and on failure JUCE deletes
    // it. A smart pointer held here as well would delete it a second time.
    auto* stream = new juce::MemoryInputStream (bytes.getData(), bytes.getDataSize(), true);

    juce::FlacAudioFormat flac;
    return std::unique_ptr<juce::AudioFormatReader> { flac.createReaderFor (stream, true) };
}

} // namespace apollo::resources
