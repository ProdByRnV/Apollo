/*
    Reading a user's noise sample (Phase 13g).

    Against real files written into a scratch directory: a sample is accepted
    at any rate and comes back at 48 kHz on both sides, and every way a file
    can fail to be one is refused by name rather than played.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "Resources/NoiseSampleFile.h"

using namespace apollo::resources;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

class Scratch
{
public:
    Scratch()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloNoiseSampleTests")
                    .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~Scratch() { root.getParentDirectory().deleteRecursively(); }

    [[nodiscard]] juce::File child (const juce::String& name) const { return root.getChildFile (name); }

private:
    juce::File root;
};

/** Writes @p channels to @p file as a WAV at @p rate, @p bits deep (32 is
    floating point). */
[[nodiscard]] bool writeWav (const juce::File& file, const std::vector<std::vector<float>>& channels,
                             double rate, int bits = 24)
{
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    const auto writer = format.createWriterFor (stream,
                                                juce::AudioFormatWriterOptions {}
                                                    .withSampleRate (rate)
                                                    .withNumChannels (static_cast<int> (channels.size()))
                                                    .withBitsPerSample (bits)
                                                    .withSampleFormat (bits == 32
                                                        ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                        : juce::AudioFormatWriterOptions::SampleFormat::integral));

    if (writer == nullptr)
        return false;

    const auto length = channels.empty() ? 0 : static_cast<int> (channels[0].size());
    juce::AudioBuffer<float> buffer (static_cast<int> (channels.size()), length);

    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < length; ++i)
            buffer.setSample (c, i, channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]);

    return writer->writeFromAudioSampleBuffer (buffer, 0, length);
}

[[nodiscard]] std::vector<float> tone (double frequency, double rate, int length, float level = 0.5f)
{
    std::vector<float> out (static_cast<std::size_t> (length));

    for (int n = 0; n < length; ++n)
        out[static_cast<std::size_t> (n)] = level * static_cast<float> (std::sin (twoPi * frequency * n / rate));

    return out;
}

class NoiseSampleFileTests final : public juce::UnitTest
{
public:
    NoiseSampleFileTests()
        : juce::UnitTest ("Noise sample files", "Resources")
    {
    }

    void runTest() override
    {
        Scratch scratch;

        beginTest ("A mono sample at another rate comes back on both sides at 48 kHz");
        {
            const auto file = scratch.child ("tone.wav");
            expect (writeWav (file, { tone (1000.0, 44100.0, 44100) }, 44100.0));

            NoiseSampleAudio audio;
            expect (readNoiseSampleFile (file, audio) == NoiseSampleLoadResult::ok);
            expectEquals (static_cast<int> (audio.left.size()), 48000);
            expect (audio.left == audio.right, "mono must play the same on both sides");
            expectWithinAbsoluteError (audio.seconds(), 1.0, 1.0e-9);

            // Still a 1 kHz tone at half scale, a quarter of the way in.
            const auto n = 12000;
            expectWithinAbsoluteError (audio.left[static_cast<std::size_t> (n)],
                                       0.5 * std::sin (twoPi * 1000.0 * n / 48000.0), 2.0e-3);
        }

        beginTest ("A stereo sample keeps its two sides");
        {
            const auto file = scratch.child ("stereo.wav");
            expect (writeWav (file, { tone (500.0, 48000.0, 4800), tone (500.0, 48000.0, 4800, -0.25f) }, 48000.0));

            NoiseSampleAudio audio;
            expect (readNoiseSampleFile (file, audio) == NoiseSampleLoadResult::ok);
            expectEquals (static_cast<int> (audio.left.size()), 4800);
            expectWithinAbsoluteError (audio.right[100], -0.5 * audio.left[100], 1.0e-6);
        }

        beginTest ("Every way a file can fail to be a noise sample is refused by name");
        {
            NoiseSampleAudio audio;

            expect (readNoiseSampleFile (scratch.child ("absent.wav"), audio) == NoiseSampleLoadResult::missing);

            const auto photograph = scratch.child ("photograph.wav");
            expect (photograph.replaceWithText ("not audio at all, just some text with the wrong extension"));
            expect (readNoiseSampleFile (photograph, audio) == NoiseSampleLoadResult::unreadable);

            const auto empty = scratch.child ("empty.wav");
            expect (writeWav (empty, { {} }, 48000.0));
            expect (readNoiseSampleFile (empty, audio) == NoiseSampleLoadResult::empty);

            const auto silent = scratch.child ("silent.wav");
            expect (writeWav (silent, { std::vector<float> (4800, 0.0f) }, 48000.0));
            expect (readNoiseSampleFile (silent, audio) == NoiseSampleLoadResult::silent);

            // Eleven seconds, at a low rate so the file stays small.
            const auto longFile = scratch.child ("long.wav");
            expect (writeWav (longFile, { tone (100.0, 8000.0, 88000) }, 8000.0, 16));
            expect (readNoiseSampleFile (longFile, audio) == NoiseSampleLoadResult::tooLong);

            auto poisoned = tone (100.0, 48000.0, 4800);
            poisoned[1000] = std::numeric_limits<float>::infinity();
            const auto poisonedFile = scratch.child ("poisoned.wav");
            expect (writeWav (poisonedFile, { poisoned }, 48000.0, 32));
            expect (readNoiseSampleFile (poisonedFile, audio) == NoiseSampleLoadResult::notFinite);

            expect (audio.left.empty() && audio.right.empty(), "a refusal must leave nothing behind");

            for (const auto result : { NoiseSampleLoadResult::missing, NoiseSampleLoadResult::tooLarge,
                                       NoiseSampleLoadResult::unreadable, NoiseSampleLoadResult::empty,
                                       NoiseSampleLoadResult::tooLong, NoiseSampleLoadResult::notFinite,
                                       NoiseSampleLoadResult::silent })
            {
                const auto text = describe (result);
                expect (text.isNotEmpty() && ! text.containsChar ('/') && ! text.containsChar ('\\'));
            }
        }

        beginTest ("Exactly ten seconds is allowed");
        {
            const auto file = scratch.child ("ten.wav");
            expect (writeWav (file, { tone (100.0, 8000.0, 80000) }, 8000.0, 16));

            NoiseSampleAudio audio;
            expect (readNoiseSampleFile (file, audio) == NoiseSampleLoadResult::ok);
            expectEquals (static_cast<int> (audio.left.size()), 480000);
        }
    }
};

NoiseSampleFileTests noiseSampleFileTests;

} // namespace
