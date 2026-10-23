/*
    The format a patch carries the user's audio in (Phase 13g).

    Two claims. It is lossless at 24 bits, for the shapes it will meet: a smooth
    wavetable, noise, and a step. And it is safe to decode whatever a document
    contains: every way of being wrong is refused, and a stream damaged at
    random is refused or held to the readers' rules — never a crash, never a
    value out of range.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "Resources/EmbeddedAudio.h"
#include "Resources/NoiseSampleFile.h"
#include "Resources/WavetableFile.h"

using namespace apollo::resources;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

[[nodiscard]] juce::MemoryBlock bytesOf (const juce::String& text)
{
    juce::MemoryOutputStream out;
    (void) juce::Base64::convertFromBase64 (out, text);
    return out.getMemoryBlock();
}

[[nodiscard]] juce::String textOf (const juce::MemoryBlock& block)
{
    return juce::Base64::toBase64 (block.getData(), block.getSize());
}

/** Every sample of @p reader, channel by channel. */
[[nodiscard]] std::vector<std::vector<float>> readAll (juce::AudioFormatReader& reader)
{
    const auto length = static_cast<int> (reader.lengthInSamples);
    juce::AudioBuffer<float> buffer (static_cast<int> (reader.numChannels), length);
    (void) reader.read (&buffer, 0, length, 0, true, true);

    std::vector<std::vector<float>> out;

    for (int c = 0; c < buffer.getNumChannels(); ++c)
        out.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

    return out;
}

class EmbeddedAudioTests final : public juce::UnitTest
{
public:
    EmbeddedAudioTests()
        : juce::UnitTest ("Embedded audio", "Resources")
    {
    }

    void runTest() override
    {
        testLossless();
        testCompresses();
        testRefusals();
        testRandomDamage();
    }

private:
    void testLossless()
    {
        beginTest ("Audio comes back exactly, to 24 bits, scaled to full scale");

        // A wavetable louder than full scale, noise in stereo, and a step: the
        // three things each prediction order exists for.
        std::vector<double> table (2048 * 4);
        for (std::size_t i = 0; i < table.size(); ++i)
            table[i] = 1.7 * std::sin (twoPi * static_cast<double> (i) / 2048.0);

        juce::Random random (11);
        std::vector<double> left (9600), right (9600);
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = random.nextDouble() * 2.0 - 1.0;
            right[i] = (random.nextDouble() * 2.0 - 1.0) * 0.25;
        }

        std::vector<double> step (1000, -0.5);
        for (std::size_t i = 500; i < step.size(); ++i)
            step[i] = 0.5;

        struct Case { const char* name; std::vector<std::vector<double>> channels; double rate; };

        for (const auto& c : { Case { "table", { table }, 48000.0 },
                               Case { "noise", { left, right }, 44100.0 },
                               Case { "step", { step }, 48000.0 } })
        {
            const auto text = encodeAudio (c.channels, c.rate);
            expect (text.isNotEmpty(), c.name);

            auto reader = decodeAudio (text);
            expect (reader != nullptr, c.name);

            if (reader == nullptr)
                continue;

            expectEquals (static_cast<int> (reader->numChannels), static_cast<int> (c.channels.size()));
            expectEquals (reader->sampleRate, c.rate);
            expectEquals (static_cast<int> (reader->lengthInSamples), static_cast<int> (c.channels[0].size()));

            auto peak = 0.0;
            for (const auto& channel : c.channels)
                for (const auto value : channel)
                    peak = std::max (peak, std::abs (value));

            const auto read = readAll (*reader);
            auto worst = 0.0;

            for (std::size_t ch = 0; ch < read.size(); ++ch)
                for (std::size_t i = 0; i < read[ch].size(); ++i)
                    worst = std::max (worst, std::abs (static_cast<double> (read[ch][i]) - c.channels[ch][i] / peak));

            logMessage (juce::String ("  ") + c.name + ": worst error " + juce::String (worst, 10));
            expect (worst <= 1.0 / 8388607.0, juce::String (c.name) + " is not 24-bit exact");
        }
    }

    void testCompresses()
    {
        beginTest ("A smooth table shrinks; noise stays about the size of 24-bit audio");

        std::vector<double> table (2048 * 64);
        for (std::size_t i = 0; i < table.size(); ++i)
        {
            const auto frame = static_cast<double> (i / 2048) / 64.0;
            const auto x = twoPi * static_cast<double> (i % 2048) / 2048.0;
            table[i] = std::sin (x) + frame * 0.3 * std::sin (5.0 * x);
        }

        juce::Random random (3);
        std::vector<double> noise (48000);
        for (auto& value : noise)
            value = random.nextDouble() * 2.0 - 1.0;

        const auto tableBytes = bytesOf (encodeAudio ({ table }, 48000.0)).getSize();
        const auto noiseBytes = bytesOf (encodeAudio ({ noise }, 48000.0)).getSize();

        const auto tableRatio = static_cast<double> (tableBytes) / static_cast<double> (table.size() * 3);
        const auto noiseRatio = static_cast<double> (noiseBytes) / static_cast<double> (noise.size() * 3);

        logMessage ("  a 64-frame table: " + juce::String (tableRatio * 100.0, 1)
                    + " % of 24-bit PCM; noise: " + juce::String (noiseRatio * 100.0, 1) + " %");

        expect (tableRatio < 0.5, "a smooth table did not compress");
        expect (noiseRatio < 1.2, "noise grew well past its own size");
    }

    void testRefusals()
    {
        beginTest ("Everything that is not this format, or is damaged, is refused");

        std::vector<double> signal (4096);
        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] = std::sin (static_cast<double> (i) * 0.01);

        const auto good = bytesOf (encodeAudio ({ signal }, 48000.0));
        expect (decodeAudio (textOf (good)) != nullptr);

        const auto with = [&good] (std::size_t offset, std::uint8_t value)
        {
            auto copy = good;
            static_cast<std::uint8_t*> (copy.getData())[offset] = value;
            return textOf (copy);
        };

        const auto withU32 = [&good] (std::size_t offset, std::uint32_t value)
        {
            auto copy = good;
            auto* bytes = static_cast<std::uint8_t*> (copy.getData());
            for (int b = 0; b < 4; ++b)
                bytes[offset + static_cast<std::size_t> (b)] = static_cast<std::uint8_t> (value >> (8 * b));
            return textOf (copy);
        };

        expect (decodeAudio ({}) == nullptr, "empty");
        expect (decodeAudio ("!!! not base64 !!!") == nullptr, "not base64");
        expect (decodeAudio (juce::Base64::toBase64 ("short")) == nullptr, "shorter than a header");
        expect (decodeAudio (with (0, 'X')) == nullptr, "the wrong magic");
        expect (decodeAudio (with (4, 1)) == nullptr, "version 1, which was FLAC");
        expect (decodeAudio (with (4, 3)) == nullptr, "a version from the future");
        expect (decodeAudio (with (5, 0)) == nullptr, "no channels");
        expect (decodeAudio (with (5, 3)) == nullptr, "three channels");
        expect (decodeAudio (withU32 (6, 0)) == nullptr, "a rate of zero");
        expect (decodeAudio (withU32 (6, 10000000)) == nullptr, "a rate of ten megahertz");
        expect (decodeAudio (withU32 (10, 0)) == nullptr, "no frames");
        expect (decodeAudio (withU32 (10, 0xffffffffu)) == nullptr, "four billion frames");
        expect (decodeAudio (withU32 (10, 4095)) == nullptr, "fewer frames than the stream holds");
        expect (decodeAudio (withU32 (10, 4097)) == nullptr, "more frames than the stream holds");
        expect (decodeAudio (with (14, 3)) == nullptr, "a prediction order that does not exist");

        auto truncated = good;
        truncated.setSize (good.getSize() - 7);
        expect (decodeAudio (textOf (truncated)) == nullptr, "a stream cut short");

        expect (decodeAudio (juce::String::repeatedString ("A", maximumEmbeddedAudioChars + 4)) == nullptr,
                "text past the bound must be refused before it is decoded");

        beginTest ("A stream that inflates to far more than its header allows is refused at the ceiling");

        // A valid header for one frame of one channel, then a megabyte of
        // zeros compressed to almost nothing.
        juce::MemoryOutputStream bomb;
        bomb.write ("APAU", 4);
        bomb.writeByte (2);
        bomb.writeByte (1);
        for (const auto value : { 48000u, 1u })
            for (int b = 0; b < 4; ++b)
                bomb.writeByte (static_cast<char> ((value >> (8 * b)) & 0xffu));
        bomb.writeByte (0);

        {
            juce::GZIPCompressorOutputStream zip (bomb, 9);
            const std::vector<char> zeros (1024 * 1024, 0);
            zip.write (zeros.data(), zeros.size());
        }

        expect (bomb.getDataSize() < 4096, "the bomb should be small");
        expect (decodeAudio (juce::Base64::toBase64 (bomb.getData(), bomb.getDataSize())) == nullptr);
    }

    void testRandomDamage()
    {
        beginTest ("Damage anywhere is refused, or yields audio the readers hold to their rules");

        std::vector<double> signal (2048 * 2);
        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] = std::sin (twoPi * static_cast<double> (i) / 2048.0);

        const auto good = bytesOf (encodeAudio ({ signal }, 48000.0));
        juce::Random random (2026);
        auto refused = 0, read = 0;

        for (int attempt = 0; attempt < 400; ++attempt)
        {
            auto damaged = good;
            auto* bytes = static_cast<std::uint8_t*> (damaged.getData());

            for (int flips = 1 + random.nextInt (4); flips > 0; --flips)
                bytes[static_cast<std::size_t> (random.nextInt (static_cast<int> (damaged.getSize())))]
                    ^= static_cast<std::uint8_t> (1 + random.nextInt (255));

            auto reader = decodeAudio (textOf (damaged));

            if (reader == nullptr)
            {
                ++refused;
                continue;
            }

            ++read;
            WavetableFrames frames;
            (void) readWavetable (*reader, frames);

            for (const auto& frame : frames)
                for (const auto value : frame)
                    expect (std::isfinite (value) && std::abs (value) <= 1.0, "a damaged stream decoded out of range");
        }

        logMessage ("  of 400 damaged streams: " + juce::String (refused) + " refused, "
                    + juce::String (read) + " read and checked");
        expect (refused > 0);
    }
};

EmbeddedAudioTests embeddedAudioTests;

} // namespace
