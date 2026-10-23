#include "Resources/EmbeddedAudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace apollo::resources
{

namespace
{

constexpr std::array<char, 4> magic { 'A', 'P', 'A', 'U' };

/** Magic, version, channels, rate, frames. The orders follow, one a channel. */
constexpr std::size_t fixedHeaderBytes = 4 + 1 + 1 + 4 + 4;

/** Full scale of a 24-bit sample. Symmetric, so negation never overflows. */
constexpr std::int64_t fullScale = 8388607;

/** The highest rate a document may declare. Far above any real recording, and
    low enough that nothing derived from it overflows. */
constexpr std::uint32_t maximumRate = 768000;

/** The longest a residual's encoding can be: three predicted terms of a
    24-bit sample stay inside 27 bits, zigzagged into 28, which is four bytes
    of seven. Anything longer is damage. */
constexpr int maximumVarintBytes = 4;

constexpr int maximumOrder = 2;

void putU32 (juce::MemoryOutputStream& out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.writeByte (static_cast<char> ((value >> shift) & 0xffu));
}

[[nodiscard]] std::uint32_t getU32 (const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint32_t> (bytes[0])
         | (static_cast<std::uint32_t> (bytes[1]) << 8)
         | (static_cast<std::uint32_t> (bytes[2]) << 16)
         | (static_cast<std::uint32_t> (bytes[3]) << 24);
}

/** The residual of sample @p n of @p q under @p order: what is left after
    predicting it from the samples before it. Samples before the start are 0. */
[[nodiscard]] std::int64_t residualOf (const std::vector<std::int64_t>& q, std::size_t n, int order) noexcept
{
    const auto at = [&q, n] (std::size_t back) { return n >= back ? q[n - back] : std::int64_t { 0 }; };

    switch (order)
    {
        case 0:  return q[n];
        case 1:  return q[n] - at (1);
        default: return q[n] - 2 * at (1) + at (2);
    }
}

/** The order whose residuals are smallest in total. */
[[nodiscard]] int bestOrder (const std::vector<std::int64_t>& q) noexcept
{
    std::array<std::int64_t, maximumOrder + 1> totals {};

    for (std::size_t n = 0; n < q.size(); ++n)
        for (int order = 0; order <= maximumOrder; ++order)
            totals[static_cast<std::size_t> (order)] += std::abs (residualOf (q, n, order));

    return static_cast<int> (std::min_element (totals.begin(), totals.end()) - totals.begin());
}

void putVarint (juce::MemoryOutputStream& out, std::int64_t value)
{
    // Zigzag, so small negative residuals are small numbers too.
    auto zigzag = (static_cast<std::uint64_t> (value) << 1) ^ static_cast<std::uint64_t> (value >> 63);

    do
    {
        auto byte = static_cast<std::uint8_t> (zigzag & 0x7fu);
        zigzag >>= 7;

        if (zigzag != 0)
            byte = static_cast<std::uint8_t> (byte | 0x80u);

        out.writeByte (static_cast<char> (byte));
    }
    while (zigzag != 0);
}

/** Reads one residual from @p bytes at @p position. @returns false for a
    stream that ends early or a number longer than any residual can be. */
[[nodiscard]] bool getVarint (const std::vector<std::uint8_t>& bytes, std::size_t& position, std::int64_t& value) noexcept
{
    std::uint64_t zigzag = 0;

    for (int i = 0; i < maximumVarintBytes; ++i)
    {
        if (position >= bytes.size())
            return false;

        const auto byte = bytes[position++];
        zigzag |= static_cast<std::uint64_t> (byte & 0x7fu) << (7 * i);

        if ((byte & 0x80u) == 0)
        {
            value = static_cast<std::int64_t> (zigzag >> 1) ^ -static_cast<std::int64_t> (zigzag & 1u);
            return true;
        }
    }

    return false;
}

/** Hands decoded audio to the readers that hold files to their rules, so
    audio from a document and audio from a disk are checked by the same code. */
class DecodedAudioReader final : public juce::AudioFormatReader
{
public:
    DecodedAudioReader (juce::AudioBuffer<float>&& decoded, double rate)
        : juce::AudioFormatReader (nullptr, "Apollo embedded audio"),
          audio (std::move (decoded))
    {
        sampleRate = rate;
        bitsPerSample = 24;
        lengthInSamples = audio.getNumSamples();
        numChannels = static_cast<unsigned int> (audio.getNumChannels());
        usesFloatingPointData = true;
    }

    bool readSamples (int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                      juce::int64 startSampleInFile, int numSamples) override
    {
        for (int channel = 0; channel < numDestChannels; ++channel)
        {
            auto* destination = destChannels[channel];

            if (destination == nullptr)
                continue;

            // Floating-point data travels in the int buffers JUCE hands over,
            // as their bits: that is what usesFloatingPointData means.
            auto* floats = reinterpret_cast<float*> (destination) + startOffsetInDestBuffer;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto index = startSampleInFile + i;
                floats[i] = channel < audio.getNumChannels() && index >= 0 && index < audio.getNumSamples()
                              ? audio.getSample (channel, static_cast<int> (index))
                              : 0.0f;
            }
        }

        return true;
    }

private:
    juce::AudioBuffer<float> audio;
};

} // namespace

juce::String encodeAudio (const std::vector<std::vector<double>>& channels, double rate)
{
    if (channels.empty() || channels.size() > 2 || channels[0].empty()
        || ! (rate > 0.0) || rate > static_cast<double> (maximumRate))
        return {};

    const auto length = channels[0].size();

    if (length > static_cast<std::size_t> (maximumEmbeddedAudioFrames))
        return {};

    for (const auto& channel : channels)
        if (channel.size() != length)
            return {};

    auto peak = 0.0;

    for (const auto& channel : channels)
        for (const auto value : channel)
            if (std::isfinite (value))
                peak = std::max (peak, std::abs (value));

    if (! (peak > 0.0))
        return {};

    juce::MemoryOutputStream document;

    document.write (magic.data(), magic.size());
    document.writeByte (static_cast<char> (embeddedAudioFormatVersion));
    document.writeByte (static_cast<char> (channels.size()));
    putU32 (document, static_cast<std::uint32_t> (std::lround (rate)));
    putU32 (document, static_cast<std::uint32_t> (length));

    // Full scale exactly: every bit used, nothing able to clip.
    std::vector<std::vector<std::int64_t>> quantised;

    for (const auto& channel : channels)
    {
        std::vector<std::int64_t> q (length);

        for (std::size_t i = 0; i < length; ++i)
        {
            const auto value = std::isfinite (channel[i]) ? channel[i] / peak : 0.0;
            q[i] = std::clamp (static_cast<std::int64_t> (std::llround (value * static_cast<double> (fullScale))),
                               -fullScale, fullScale);
        }

        quantised.push_back (std::move (q));
    }

    std::vector<int> orders;

    for (const auto& q : quantised)
    {
        orders.push_back (bestOrder (q));
        document.writeByte (static_cast<char> (orders.back()));
    }

    {
        juce::GZIPCompressorOutputStream compressed (document, 9);
        juce::MemoryOutputStream residuals;

        for (std::size_t c = 0; c < quantised.size(); ++c)
            for (std::size_t n = 0; n < length; ++n)
                putVarint (residuals, residualOf (quantised[c], n, orders[c]));

        compressed.write (residuals.getData(), residuals.getDataSize());
        compressed.flush();
    }

    return juce::Base64::toBase64 (document.getData(), document.getDataSize());
}

std::unique_ptr<juce::AudioFormatReader> decodeAudio (const juce::String& text)
{
    if (text.isEmpty() || text.length() > maximumEmbeddedAudioChars)
        return {};

    juce::MemoryOutputStream raw;

    if (! juce::Base64::convertFromBase64 (raw, text) || raw.getDataSize() < fixedHeaderBytes)
        return {};

    const auto* bytes = static_cast<const std::uint8_t*> (raw.getData());

    if (std::memcmp (bytes, magic.data(), magic.size()) != 0)
        return {};

    // Version 1 was FLAC, written only by a development build; refused rather
    // than guessed at.
    if (bytes[4] != static_cast<std::uint8_t> (embeddedAudioFormatVersion))
        return {};

    const auto channels = static_cast<int> (bytes[5]);
    const auto rate = getU32 (bytes + 6);
    const auto frames = getU32 (bytes + 10);

    if (channels < 1 || channels > 2 || rate == 0 || rate > maximumRate
        || frames == 0 || frames > static_cast<std::uint32_t> (maximumEmbeddedAudioFrames))
        return {};

    const auto headerBytes = fixedHeaderBytes + static_cast<std::size_t> (channels);

    if (raw.getDataSize() <= headerBytes)
        return {};

    std::array<int, 2> orders {};

    for (int c = 0; c < channels; ++c)
    {
        orders[static_cast<std::size_t> (c)] = bytes[fixedHeaderBytes + static_cast<std::size_t> (c)];

        if (orders[static_cast<std::size_t> (c)] > maximumOrder)
            return {};
    }

    // Inflated with a ceiling: the header says how many residuals there are,
    // and none can be longer than maximumVarintBytes, so a stream that keeps
    // giving past that is not this format — and a deliberately enormous one
    // costs the ceiling and no more.
    const auto ceiling = static_cast<std::size_t> (channels) * frames * static_cast<std::size_t> (maximumVarintBytes);

    juce::MemoryInputStream payload (bytes + headerBytes, raw.getDataSize() - headerBytes, false);
    juce::GZIPDecompressorInputStream inflater (payload);

    std::vector<std::uint8_t> residuals;
    std::array<std::uint8_t, 65536> chunk {};

    for (;;)
    {
        const auto got = inflater.read (chunk.data(), static_cast<int> (chunk.size()));

        if (got < 0)
            return {};

        if (got == 0)
            break;

        if (residuals.size() + static_cast<std::size_t> (got) > ceiling)
            return {};

        residuals.insert (residuals.end(), chunk.begin(), chunk.begin() + got);
    }

    juce::AudioBuffer<float> audio (channels, static_cast<int> (frames));
    std::size_t position = 0;

    for (int c = 0; c < channels; ++c)
    {
        const auto order = orders[static_cast<std::size_t> (c)];
        std::int64_t previous = 0, beforeThat = 0;

        for (std::uint32_t n = 0; n < frames; ++n)
        {
            std::int64_t residual = 0;

            if (! getVarint (residuals, position, residual))
                return {};

            const auto predicted = order == 0 ? 0 : order == 1 ? previous : 2 * previous - beforeThat;
            const auto sample = predicted + residual;

            // A sample outside 24 bits was never written by encodeAudio.
            if (sample < -fullScale || sample > fullScale)
                return {};

            audio.setSample (c, static_cast<int> (n), static_cast<float> (static_cast<double> (sample)
                                                                          / static_cast<double> (fullScale)));
            beforeThat = previous;
            previous = sample;
        }
    }

    // Everything accounted for, and nothing left over.
    if (position != residuals.size())
        return {};

    return std::make_unique<DecodedAudioReader> (std::move (audio), static_cast<double> (rate));
}

} // namespace apollo::resources
