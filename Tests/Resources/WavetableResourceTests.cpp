/*
    Wavetables that come from somewhere Apollo does not control.

    Two things are under test here and they fail in different ways:

      - the **reader**, which meets a disk and everything a disk can be — a file
        that is missing, locked, enormous, silent, truncated mid-frame, or a
        photograph wearing a `.wav`;
      - the **library slots**, which have to let a table be replaced while the
        audio thread is reading one, without freeing anything too early.

    What joins them — loading a file into an oscillator without anyone
    waiting, and leaving the instrument playing whatever happens — is the
    user content store's, and is tested with it (UserContentTests.cpp).

    These tests write real audio files, for the reason the preset tests write
    real presets: a reader tested against a mock filesystem is a reader tested
    against the filesystem somebody imagined. Each builds its files under the
    system temporary directory and removes them afterwards.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>
#include <vector>

#include "DSP/Oscillators/WavetableBuilder.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Utilities/Fft.h"
#include "Resources/WavetableFile.h"

using namespace apollo;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

/** A temporary directory that removes itself. */
class ScratchTree
{
public:
    explicit ScratchTree (const juce::String& label)
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloWavetableTests")
                    .getChildFile (label + "-"
                                   + juce::String (juce::Random::getSystemRandom()
                                                       .nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~ScratchTree() { root.deleteRecursively(); }

    [[nodiscard]] juce::File child (const juce::String& name) const
    {
        return root.getChildFile (name);
    }

private:
    juce::File root;

    JUCE_DECLARE_NON_COPYABLE (ScratchTree)
};

/** Writes @p samples to @p file as a mono WAV. */
[[nodiscard]] bool writeWav (const juce::File& file, const std::vector<float>& samples)
{
    file.deleteFile();

    juce::WavAudioFormat format;

    auto rawStream = std::make_unique<juce::FileOutputStream> (file);

    if (! rawStream->openedOk())
        return false;

    std::unique_ptr<juce::OutputStream> stream { std::move (rawStream) };

    // The writer takes the stream only if it succeeds, which is why this is the
    // overload taking a unique_ptr by reference rather than a raw pointer.
    const auto writer = format.createWriterFor (stream,
                                                juce::AudioFormatWriterOptions {}
                                                    .withSampleRate (48000.0)
                                                    .withNumChannels (1)
                                                    .withBitsPerSample (24));

    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer (1, static_cast<int> (samples.size()));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample (0, i, samples[static_cast<std::size_t> (i)]);

    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

/** @returns @p numFrames frames whose waveform opens from a sine to a saw. */
[[nodiscard]] std::vector<float> makeTableSamples (int numFrames)
{
    std::vector<float> samples;
    samples.reserve (static_cast<std::size_t> (numFrames * resources::wavetableFrameSamples));

    for (int frame = 0; frame < numFrames; ++frame)
    {
        const auto amount = numFrames > 1
                              ? static_cast<double> (frame) / static_cast<double> (numFrames - 1)
                              : 0.0;

        const auto partials = 1 + static_cast<int> (amount * 15.0);

        for (int i = 0; i < resources::wavetableFrameSamples; ++i)
        {
            auto value = 0.0;

            for (int k = 1; k <= partials; ++k)
                value += std::sin (twoPi * static_cast<double> (k) * static_cast<double> (i)
                                   / static_cast<double> (resources::wavetableFrameSamples))
                       / static_cast<double> (k);

            samples.push_back (static_cast<float> (value * 0.5));
        }
    }

    return samples;
}

class WavetableResourceTests final : public juce::UnitTest
{
public:
    WavetableResourceTests()
        : juce::UnitTest ("Wavetable resources", "Resources")
    {
    }

    void runTest() override
    {
        testReadsAWellFormedTable();
        testRefusesEverythingElse();
        testAnalysisReproducesTheWaveform();
        testPublishingReplacesWhatIsPlayed();
        testARetiredTableOutlivesTheBlockUsingIt();
    }

private:
    void testReadsAWellFormedTable()
    {
        beginTest ("A wavetable of whole frames reads back as those frames");

        ScratchTree tree { "good" };

        const auto file = tree.child ("table.wav");

        expect (writeWav (file, makeTableSamples (16)), "the test could not write its own file");

        resources::WavetableFrames frames;

        expect (resources::readWavetableFile (file, frames) == resources::WavetableLoadResult::ok);

        expectEquals (static_cast<int> (frames.size()), 16);

        for (const auto& frame : frames)
            expectEquals (static_cast<int> (frame.size()), resources::wavetableFrameSamples);

        // The first frame is a sine and the last is much richer, which is what
        // the file was written to contain — so the frames came back in order
        // rather than being shuffled or repeated.
        const auto firstSpectrum = dsp::analyseCycle (frames.front(), 8);
        const auto lastSpectrum = dsp::analyseCycle (frames.back(), 8);

        expect (std::abs (firstSpectrum[2]) < 0.01,
                "the first frame should be a sine, with no third harmonic");

        expect (std::abs (lastSpectrum[2]) > 0.1,
                "the last frame should be rich: third harmonic was "
                    + juce::String (std::abs (lastSpectrum[2])));
    }

    void testRefusesEverythingElse()
    {
        beginTest ("Every way a file can fail to be a wavetable is refused by name");

        ScratchTree tree { "bad" };

        resources::WavetableFrames frames;

        const auto check = [this, &frames] (const juce::File& file,
                                            resources::WavetableLoadResult expected,
                                            const juce::String& what)
        {
            const auto result = resources::readWavetableFile (file, frames);

            expect (result == expected,
                    what + ": expected " + resources::describe (expected) + " but got "
                        + resources::describe (result));

            expect (frames.empty(), what + ": a refusal must leave nothing behind");
        };

        check (tree.child ("nothing.wav"), resources::WavetableLoadResult::missing,
               "a file that is not there");

        // A text file wearing the extension. The decoder refuses it, which is
        // the path a renamed photograph takes too.
        const auto notAudio = tree.child ("photo.wav");
        notAudio.replaceWithText ("this is not a wavetable, it is a sentence");
        check (notAudio, resources::WavetableLoadResult::unreadable, "a file that is not audio");

        // Frames must be whole. Half a frame at the end is a table that would
        // play a discontinuity once per cycle.
        auto ragged = makeTableSamples (2);
        ragged.resize (ragged.size() - 100);

        const auto raggedFile = tree.child ("ragged.wav");
        expect (writeWav (raggedFile, ragged));
        check (raggedFile, resources::WavetableLoadResult::wrongFrameLength,
               "a table that stops mid-frame");

        // Silence has no waveform. It would normalise to nothing and play as
        // nothing, which is indistinguishable from a broken instrument.
        const auto silentFile = tree.child ("silent.wav");
        expect (writeWav (silentFile,
                          std::vector<float> (static_cast<std::size_t> (
                              resources::wavetableFrameSamples * 2), 0.0f)));
        check (silentFile, resources::WavetableLoadResult::silent, "a silent table");

        // Past the size bound, refused on the stat rather than by decoding it.
        const auto huge = tree.child ("huge.wav");
        {
            const juce::MemoryBlock padding (
                static_cast<std::size_t> (resources::maximumWavetableBytes) + 1024, true);

            expect (huge.replaceWithData (padding.getData(), padding.getSize()));
        }
        check (huge, resources::WavetableLoadResult::tooLarge, "a file far too large");

        // Every reason has something to say, and none of it names a path.
        for (const auto result : { resources::WavetableLoadResult::missing,
                                   resources::WavetableLoadResult::tooLarge,
                                   resources::WavetableLoadResult::unreadable,
                                   resources::WavetableLoadResult::empty,
                                   resources::WavetableLoadResult::wrongFrameLength,
                                   resources::WavetableLoadResult::tooManyFrames,
                                   resources::WavetableLoadResult::notFinite,
                                   resources::WavetableLoadResult::silent })
        {
            const auto text = resources::describe (result);

            expect (text.isNotEmpty(), "every refusal needs a sentence");
            expect (! text.containsChar ('\\') && ! text.contains ("/"),
                    "a refusal must not contain anything path-like: " + text);
        }
    }

    /** The claim that a loaded table is the waveform the user supplied. */
    void testAnalysisReproducesTheWaveform()
    {
        beginTest ("A table analysed and rebuilt is the shape it started as");

        ScratchTree tree { "roundtrip" };

        const auto file = tree.child ("table.wav");
        expect (writeWav (file, makeTableSamples (4)));

        resources::WavetableFrames frames;
        expect (resources::readWavetableFile (file, frames) == resources::WavetableLoadResult::ok);

        const auto spectrum = dsp::analyseFrames (frames, dsp::Wavetable::topLevelHarmonics);

        expectEquals (static_cast<int> (spectrum.size()), 4);

        dsp::Wavetable table;
        dsp::buildWavetable (table, spectrum);

        expectEquals (table.getNumFrames(), 4);

        // The built table is normalised, so the comparison is of shape rather
        // than of level: the correlation between what went in and what came
        // back has to be near perfect.
        for (int frame = 0; frame < table.getNumFrames(); ++frame)
        {
            const auto& original = frames[static_cast<std::size_t> (frame)];

            auto dot = 0.0;
            auto originalEnergy = 0.0;
            auto rebuiltEnergy = 0.0;

            for (int i = 0; i < resources::wavetableFrameSamples; ++i)
            {
                const auto phase = static_cast<double> (i)
                                 / static_cast<double> (resources::wavetableFrameSamples);

                const auto rebuilt = static_cast<double> (table.getSample (0, frame, phase));
                const auto source = original[static_cast<std::size_t> (i)];

                dot += rebuilt * source;
                originalEnergy += source * source;
                rebuiltEnergy += rebuilt * rebuilt;
            }

            const auto correlation = dot / std::sqrt (originalEnergy * rebuiltEnergy);

            expect (correlation > 0.999,
                    "frame " + juce::String (frame) + " came back as a different shape: "
                        + juce::String (correlation, 5));
        }
    }

    void testPublishingReplacesWhatIsPlayed()
    {
        beginTest ("A published table replaces the built-in, and the built-in comes back");

        dsp::WavetableLibrary library;

        const auto* builtIn = &library.getTable (0);

        expect (! library.isReplaced (0), "a fresh library is playing its own tables");

        // A table that is obviously not the built-in: a single frame of pure
        // second harmonic.
        auto replacement = std::make_unique<dsp::Wavetable>();
        {
            dsp::TableSpectrum spectrum { dsp::makeSilentSpectrum (16) };
            spectrum[0][1].sine = 1.0;

            dsp::buildWavetable (*replacement, spectrum);
        }

        const auto* published = replacement.get();

        expect (library.publish (0, std::move (replacement)));
        expect (library.isReplaced (0));
        expect (&library.getTable (0) == published, "the slot must play what was published");

        // Only that slot. A load into one oscillator's table must not disturb
        // the other three.
        for (int other = 1; other < dsp::WavetableLibrary::numTables; ++other)
            expect (! library.isReplaced (other), "slot " + juce::String (other) + " moved");

        library.restoreBuiltIn (0);

        expect (! library.isReplaced (0));
        expect (&library.getTable (0) == builtIn, "the built-in must be exactly the one before");

        // And nothing a caller can do makes a slot empty: an empty table is
        // refused rather than published, because it would silence the
        // oscillator.
        expect (! library.publish (0, std::make_unique<dsp::Wavetable>()),
                "an empty table must be refused");
        expect (! library.publish (-1, std::make_unique<dsp::Wavetable>()));
        expect (! library.publish (99, std::make_unique<dsp::Wavetable>()));
        expect (! library.publish (0, nullptr));
    }

    /** The lifetime rule, which is the part a sanitiser would otherwise find. */
    void testARetiredTableOutlivesTheBlockUsingIt()
    {
        beginTest ("A replaced table is kept until the audio thread has moved past it");

        dsp::WavetableLibrary library;

        const auto publishOne = [&library]
        {
            auto table = std::make_unique<dsp::Wavetable>();

            dsp::TableSpectrum spectrum { dsp::makeSilentSpectrum (8) };
            spectrum[0][0].sine = 1.0;

            dsp::buildWavetable (*table, spectrum);

            return library.publish (0, std::move (table));
        };

        expect (publishOne());
        expectEquals (library.getRetiredCount(), 0,
                      "replacing a built-in retires nothing: the built-in is kept anyway");

        // Replacing a *loaded* table is what retires one.
        expect (publishOne());
        expectEquals (library.getRetiredCount(), 1);

        // Nothing is freed while the audio thread might still be inside the
        // block that took the old pointer.
        expectEquals (library.collectRetired(), 0, "freed too early");
        expectEquals (library.getRetiredCount(), 1);

        library.beginBlock();

        expectEquals (library.collectRetired(), 0, "one block is not enough");

        library.beginBlock();

        expectEquals (library.collectRetired(), 1, "two blocks should be enough");
        expectEquals (library.getRetiredCount(), 0);

        // Going back to the built-in retires the loaded table the same way.
        // Two of them here, not one: the publish below retires the table still
        // live from earlier, and the restore then retires the one it published.
        expect (publishOne());
        library.restoreBuiltIn (0);

        expectEquals (library.getRetiredCount(), 2);

        library.beginBlock();
        library.beginBlock();

        expectEquals (library.collectRetired(), 2);
        expectEquals (library.getRetiredCount(), 0);

        // And the audio thread is told there is something new to pick up —
        // without which a published table would sit in its slot unplayed.
        expect (library.takeGenerationChange(), "a publish must be announced");
        expect (! library.takeGenerationChange(), "and announced only once");
    }
};

WavetableResourceTests wavetableResourceTests;

} // namespace
