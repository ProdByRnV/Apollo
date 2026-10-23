/*
    The wavetable and noise libraries (Phase 13g): what a scan finds, how it is
    numbered and described, how content is found again, and how a patch's
    content is written back into the library.

    Against real folders in a scratch directory, for the reason the preset
    library's tests use real ones.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Resources/ContentLibrary.h"
#include "Resources/EmbeddedAudio.h"
#include "Resources/WavetableFile.h"

using namespace apollo::resources;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

class Scratch
{
public:
    Scratch()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloContentLibraryTests")
                    .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~Scratch() { root.getParentDirectory().deleteRecursively(); }

    [[nodiscard]] juce::File child (const juce::String& path) const { return root.getChildFile (path); }

    [[nodiscard]] ContentLocations locations() const
    {
        return { child ("user/Wavetables"), child ("user/Noise"), child ("factory/Wavetables"), child ("factory/Noise") };
    }

private:
    juce::File root;
};

[[nodiscard]] bool writeWav (const juce::File& file, int length, double rate = 48000.0, int channels = 1)
{
    (void) file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    const auto writer = format.createWriterFor (stream,
                                                juce::AudioFormatWriterOptions {}
                                                    .withSampleRate (rate)
                                                    .withNumChannels (channels)
                                                    .withBitsPerSample (16));

    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer (channels, length);

    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < length; ++i)
            buffer.setSample (c, i, 0.5f * static_cast<float> (std::sin (twoPi * i / 2048.0)));

    return writer->writeFromAudioSampleBuffer (buffer, 0, length);
}

class ContentLibraryTests final : public juce::UnitTest
{
public:
    ContentLibraryTests()
        : juce::UnitTest ("Content library", "Resources")
    {
    }

    void runTest() override
    {
        testScan();
        testFindingContentAgain();
        testSaveIntoTheLibrary();
        testTheLibraryObject();
    }

private:
    void testScan()
    {
        beginTest ("A scan finds both kinds in both roots, banks as folders, numbered from 1");

        Scratch scratch;
        const auto where = scratch.locations();

        expect (writeWav (where.userWavetables.getChildFile ("Basses/Growl.wav"), 2048 * 64));
        expect (writeWav (where.userWavetables.getChildFile ("Air.wav"), 2048 * 4));
        expect (writeWav (where.userWavetables.getChildFile ("Ragged.wav"), 3000));
        expect (writeWav (where.factoryWavetables.getChildFile ("Factory One.wav"), 2048));
        expect (writeWav (where.userNoise.getChildFile ("Breath.wav"), 24000, 48000.0, 2));
        expect (writeWav (where.userNoise.getChildFile ("Long/Rain.wav"), 8000 * 11, 8000.0));
        expect (where.userWavetables.getChildFile ("Notes.txt").replaceWithText ("not content"));
        expect (where.userWavetables.getChildFile ("Photo.wav").replaceWithText ("not audio"));

        const auto index = scanContent (where);

        expectEquals (static_cast<int> (index.entries.size()), 7, "the text file is not content");
        expect (! index.truncated);

        for (std::size_t i = 0; i < index.entries.size(); ++i)
            expectEquals (index.entries[i].id, static_cast<int> (i) + 1);

        // Wavetables first; the user's own before the factory's; banks and
        // names in order.
        const auto names = [&index]
        {
            juce::StringArray out;
            for (const auto& entry : index.entries)
                out.add (entry.name);
            return out;
        }();

        expectEquals (names.joinIntoString (","),
                      juce::String ("Air,Photo,Ragged,Growl,Factory One,Breath,Rain"));

        const auto* growl = findContent (index, 4);
        expect (growl != nullptr);
        expectEquals (growl->bank, juce::String ("Basses"));
        expectEquals (growl->libraryPath, juce::String ("Basses/Growl.wav"));
        expectEquals (growl->frames, 64);
        expect (growl->problem.isEmpty());
        expect (! growl->factory);

        expectEquals (findContent (index, 1)->frames, 4);
        expectEquals (findContent (index, 1)->bank, juce::String());
        expect (findContent (index, 2)->problem.isNotEmpty(), "a photograph is not a wavetable");
        expectEquals (findContent (index, 3)->problem, describe (WavetableLoadResult::wrongFrameLength));
        expect (findContent (index, 5)->factory);

        const auto* breath = findContent (index, 6);
        expect (breath->kind == ContentKind::noise);
        expectWithinAbsoluteError (breath->seconds, 0.5, 1.0e-9);
        expect (breath->problem.isEmpty());
        expect (findContent (index, 7)->problem.isNotEmpty(), "eleven seconds is too long");

        expect (findContent (index, 0) == nullptr);
        expect (findContent (index, 8) == nullptr);

        beginTest ("Missing folders are not an error");
        expect (scanContent ({}).entries.empty());
        expect (scanContent ({ scratch.child ("nowhere"), {}, {}, {} }).entries.empty());
    }

    void testFindingContentAgain()
    {
        beginTest ("Content is found again by where it came from, and a chosen file is classified");

        Scratch scratch;
        const auto where = scratch.locations();
        expect (writeWav (where.userWavetables.getChildFile ("Basses/Growl.wav"), 2048));
        expect (writeWav (where.factoryWavetables.getChildFile ("Basses/Growl.wav"), 2048));

        const auto index = scanContent (where);

        const auto user = sourceFor (where, ContentKind::wavetable, where.userWavetables.getChildFile ("Basses/Growl.wav"));
        expect (user.origin == ContentOrigin::library);
        expectEquals (user.libraryPath, juce::String ("Basses/Growl.wav"));
        expectEquals (user.name, juce::String ("Growl"));

        const auto factory = sourceFor (where, ContentKind::wavetable,
                                        where.factoryWavetables.getChildFile ("Basses/Growl.wav"));
        expect (factory.origin == ContentOrigin::factoryLibrary);

        const auto elsewhere = sourceFor (where, ContentKind::wavetable, scratch.child ("Desktop/Thing.wav"));
        expect (elsewhere.origin == ContentOrigin::file);
        expect (elsewhere.libraryPath.isEmpty(), "a file outside the library keeps no path");

        // A wavetable folder is not where noise is found.
        expect (sourceFor (where, ContentKind::noise, where.userWavetables.getChildFile ("Basses/Growl.wav")).origin
                == ContentOrigin::file);

        const auto* foundUser = findContent (index, ContentKind::wavetable, user);
        const auto* foundFactory = findContent (index, ContentKind::wavetable, factory);
        expect (foundUser != nullptr && ! foundUser->factory);
        expect (foundFactory != nullptr && foundFactory->factory);
        expect (findContent (index, ContentKind::wavetable, elsewhere) == nullptr);
        expect (findContent (index, ContentKind::noise, user) == nullptr);
    }

    void testSaveIntoTheLibrary()
    {
        beginTest ("A patch's content is written into the library as a WAV, never over another");

        Scratch scratch;
        const auto folder = scratch.child ("user/Wavetables");

        std::vector<double> frames (2048 * 3);

        for (std::size_t i = 0; i < frames.size(); ++i)
            frames[i] = std::sin (twoPi * static_cast<double> (i) / 2048.0);

        const auto audio = encodeAudio ({ frames }, 48000.0);

        juce::File first, second;
        expect (saveContent (audio, ContentKind::wavetable, "Growl", folder, first) == ContentSaveResult::ok);
        expect (saveContent (audio, ContentKind::wavetable, "Growl", folder, second) == ContentSaveResult::ok);

        expectEquals (first.getFileName(), juce::String ("Growl.wav"));
        expectEquals (second.getFileName(), juce::String ("Growl (2).wav"));

        WavetableFrames read;
        expect (readWavetableFile (second, read) == WavetableLoadResult::ok);
        expectEquals (static_cast<int> (read.size()), 3);
        expectWithinAbsoluteError (read[1][512], 0.999, 1.0e-5);

        // A name that is not one still saves, as something.
        juce::File named;
        expect (saveContent (audio, ContentKind::wavetable, "../../", folder, named) == ContentSaveResult::ok);
        expect (named.isAChildOf (folder), "a name must not climb out of the library");

        juce::File none;
        expect (saveContent ({}, ContentKind::wavetable, "X", folder, none) == ContentSaveResult::nothingToSave);
        expect (saveContent ("not audio", ContentKind::wavetable, "X", folder, none) == ContentSaveResult::unreadable);
    }

    void testTheLibraryObject()
    {
        beginTest ("The library scans off the caller's thread and says when it has");

        Scratch scratch;
        const auto where = scratch.locations();
        expect (writeWav (where.userNoise.getChildFile ("Breath.wav"), 4800));

        ContentLibrary library;
        library.setLocations (where);

        auto updates = 0;
        library.onIndexUpdated = [&updates] { ++updates; };

        library.rescan();
        library.waitForScan();
        expectEquals (updates, 0, "delivered on the message thread, not the scan's");

        library.flushPendingNotification();
        expectEquals (updates, 1);
        expectEquals (static_cast<int> (library.getIndex().entries.size()), 1);
    }
};

ContentLibraryTests contentLibraryTests;

} // namespace
