/*
    The user's own content in a patch (Phase 13g): loading it, carrying it, and
    surviving everything that can go wrong with it.

    Against a whole processor and real files, because what is under test is
    the joins: a file read on one thread and published on another, audio
    written into a document by one instance and played by another, a preset
    that carries a sound and one that clears it.

    The two loader tests that were in WavetableResourceTests.cpp live here now,
    rewritten against the store that replaced the loader: a load that does not
    make anyone wait, and a failure that leaves the instrument playing.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Resources/EmbeddedAudio.h"
#include "Resources/FactoryPresets.h"
#include "Resources/NoiseSampleFile.h"
#include "Resources/UserContent.h"
#include "Resources/WavetableFile.h"
#include "State/PresetDocument.h"
#include "State/StateSerialization.h"

using namespace apollo;
using resources::ContentSlot;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

class Scratch
{
public:
    Scratch()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloUserContentTests")
                    .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~Scratch() { root.getParentDirectory().deleteRecursively(); }

    [[nodiscard]] juce::File child (const juce::String& name) const { return root.getChildFile (name); }

private:
    juce::File root;
};

[[nodiscard]] bool writeWav (const juce::File& file, const std::vector<std::vector<float>>& channels, double rate)
{
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    const auto writer = format.createWriterFor (stream,
                                                juce::AudioFormatWriterOptions {}
                                                    .withSampleRate (rate)
                                                    .withNumChannels (static_cast<int> (channels.size()))
                                                    .withBitsPerSample (24));

    if (writer == nullptr)
        return false;

    const auto length = static_cast<int> (channels[0].size());
    juce::AudioBuffer<float> buffer (static_cast<int> (channels.size()), length);

    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < length; ++i)
            buffer.setSample (c, i, channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]);

    return writer->writeFromAudioSampleBuffer (buffer, 0, length);
}

/** @p frames frames of a wave whose harmonics grow from frame to frame. */
[[nodiscard]] std::vector<float> tableSamples (int frames)
{
    std::vector<float> out (static_cast<std::size_t> (frames * 2048));

    for (int f = 0; f < frames; ++f)
        for (int i = 0; i < 2048; ++i)
        {
            const auto x = twoPi * i / 2048.0;
            const auto brightness = static_cast<double> (f + 1) / static_cast<double> (frames);
            out[static_cast<std::size_t> (f * 2048 + i)] =
                static_cast<float> (0.6 * std::sin (x) + 0.3 * brightness * std::sin (3.0 * x));
        }

    return out;
}

[[nodiscard]] std::vector<float> tone (int length)
{
    std::vector<float> out (static_cast<std::size_t> (length));

    for (int n = 0; n < length; ++n)
        out[static_cast<std::size_t> (n)] = 0.5f * static_cast<float> (std::sin (twoPi * 700.0 * n / 48000.0));

    return out;
}

void settle (ApolloAudioProcessor& processor)
{
    processor.getUserContent().waitUntilIdle();
    processor.getUserContent().flushDeliveries();
}

[[nodiscard]] float plainOf (ApolloAudioProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : -1.0f;
}

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

[[nodiscard]] juce::ValueTree itemFor (ApolloAudioProcessor& processor, ContentSlot slot)
{
    const auto content = processor.getValueTreeState().state.getChildWithName (
        resources::UserContent::contentTreeType);

    for (int i = 0; i < content.getNumChildren(); ++i)
        if (content.getChild (i).getProperty (resources::UserContent::slotProperty).toString()
            == resources::toToken (slot))
            return content.getChild (i);

    return {};
}

[[nodiscard]] const dsp::Wavetable* tableOf (ApolloAudioProcessor& processor, int oscillator)
{
    return &processor.getVoiceEngine().getWavetableLibrary().getTable (dsp::WavetableLibrary::userSlot (oscillator));
}

/** Oscillator 1 on USER with note 69 held, left channel. */
[[nodiscard]] std::vector<float> renderUser (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_wavetable", 4.0f);
    setPlain (processor, "filter1_type", 0.0f);
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    std::vector<float> rendered;

    for (int b = 0; b < 20; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
    }

    return rendered;
}

class UserContentTests final : public juce::UnitTest
{
public:
    UserContentTests()
        : juce::UnitTest ("User content", "Resources")
    {
    }

    void runTest() override
    {
        testEmbeddedAudioRoundTrips();
        testEmbeddedAudioThatIsNotAudio();
        testLoadDoesNotWait();
        testUnusableFileChangesNothing();
        testNoiseSampleLoads();
        testNewerRequestWins();
        testHostSaveAndLoadCarryIt();
        testSameStateAgainCostsNothing();
        testDocumentWithoutContentEmptiesEverySlot();
        testPresetsCarryItAndFactoryPresetsClearIt();
        testUnreadableDocumentAudio();
        testLargestPatchFits();
        testClear();
        testDestroyedWhileLoading();
    }

private:
    void testEmbeddedAudioRoundTrips()
    {
        beginTest ("Audio stored in a patch comes back as it went in, to 24 bits");

        std::vector<double> signal (4096);

        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] = 1.7 * std::sin (twoPi * static_cast<double> (i) / 2048.0); // louder than full scale

        const auto text = resources::encodeAudio ({ signal }, 48000.0);
        expect (text.isNotEmpty());

        auto reader = resources::decodeAudio (text);
        expect (reader != nullptr);

        resources::WavetableFrames frames;
        expect (resources::readWavetable (*reader, frames) == resources::WavetableLoadResult::ok);
        expectEquals (static_cast<int> (frames.size()), 2);

        // Scaled to just inside full scale, so nothing clipped, and quantised
        // to 24 bits.
        const auto scale = 0.999 / 1.7;
        auto worst = 0.0;

        for (std::size_t i = 0; i < signal.size(); ++i)
            worst = std::max (worst, std::abs (frames[i / 2048][i % 2048] - signal[i] * scale));

        logMessage ("  worst difference after a round trip: " + juce::String (worst, 10));
        expect (worst < 2.0e-7, "the audio changed: " + juce::String (worst, 10));

        // Two channels.
        std::vector<double> left (4800), right (4800);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.5 * std::sin (0.01 * static_cast<double> (i));
            right[i] = -0.25 * std::sin (0.01 * static_cast<double> (i));
        }

        auto stereo = resources::decodeAudio (resources::encodeAudio ({ left, right }, resources::noiseSampleRate));
        expect (stereo != nullptr);

        resources::NoiseSampleAudio audio;
        expect (resources::readNoiseSample (*stereo, audio) == resources::NoiseSampleLoadResult::ok);
        expectEquals (static_cast<int> (audio.left.size()), 4800);
        expectWithinAbsoluteError (audio.right[1000], -0.5 * audio.left[1000], 1.0e-6);
    }

    void testEmbeddedAudioThatIsNotAudio()
    {
        beginTest ("Audio in a document that is not audio is refused, and nothing crashes");

        expect (resources::decodeAudio ({}) == nullptr);
        expect (resources::decodeAudio ("!!!! not base64 !!!!") == nullptr);
        expect (resources::decodeAudio (juce::Base64::toBase64 ("plain text, not FLAC")) == nullptr);
        expect (resources::decodeAudio (juce::String::repeatedString ("A", resources::maximumEmbeddedAudioChars + 4))
                == nullptr, "an enormous text must be refused before it is decoded");

        // A stream cut in half: whatever it yields, it is held to the reader's
        // rules and never crashes.
        std::vector<double> signal (2048 * 8);

        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] = std::sin (twoPi * static_cast<double> (i) / 2048.0);

        juce::MemoryOutputStream bytes;
        expect (juce::Base64::convertFromBase64 (bytes, resources::encodeAudio ({ signal }, 48000.0)));

        const auto cut = juce::Base64::toBase64 (bytes.getData(), bytes.getDataSize() / 2);

        if (auto reader = resources::decodeAudio (cut))
        {
            resources::WavetableFrames frames;
            const auto result = resources::readWavetable (*reader, frames);

            for (const auto& frame : frames)
                for (const auto value : frame)
                    expect (std::isfinite (value));

            logMessage ("  a stream cut in half reads as: " + resources::describe (result));
        }

        expect (resources::encodeAudio ({}, 48000.0).isEmpty());
        expect (resources::encodeAudio ({ std::vector<double> (100, 0.0) }, 48000.0).isEmpty(),
                "silence has nothing to store");
    }

    void testLoadDoesNotWait()
    {
        beginTest ("A file becomes oscillator 1's table without the caller waiting, and is selected");

        Scratch scratch;
        const auto file = scratch.child ("Growl.wav");
        expect (writeWav (file, { tableSamples (8) }, 48000.0));

        ApolloAudioProcessor processor;
        auto& content = processor.getUserContent();

        std::vector<resources::ContentEvent> events;
        content.onChanged = [&events] (const resources::ContentEvent& event) { events.push_back (event); };

        const auto* fallback = tableOf (processor, 1);

        content.loadFile (ContentSlot::osc1, file, { "Growl", resources::ContentOrigin::library, "Basses/Growl.wav" });

        // Only LOADING so far: the result is delivered on the message thread,
        // which in this test is whenever it is flushed.
        expectEquals (static_cast<int> (events.size()), 1);
        expectEquals (events.back().token, juce::String ("LOADING"));
        expect (content.getInfo (ContentSlot::osc1).loading);

        settle (processor);

        expectEquals (events.back().token, juce::String ("LOADED"));
        expect (! events.back().message.containsChar ('/') && ! events.back().message.containsChar ('\\'),
                "a message must name no path");

        const auto& library = processor.getVoiceEngine().getWavetableLibrary();
        expect (library.isReplaced (dsp::WavetableLibrary::userSlot (1)));
        expect (tableOf (processor, 1) != fallback);
        expectEquals (tableOf (processor, 1)->getNumFrames(), 8);
        expect (! library.isReplaced (dsp::WavetableLibrary::userSlot (2)), "oscillator 2's slot moved");

        for (int builtIn = 0; builtIn < dsp::WavetableLibrary::numTables; ++builtIn)
            expect (! library.isReplaced (builtIn), "a built-in moved");

        expectEquals (plainOf (processor, "osc1_wavetable"), 4.0f, "loading must select USER");

        const auto info = content.getInfo (ContentSlot::osc1);
        expect (info.loaded && ! info.loading);
        expectEquals (info.frames, 8);
        expectEquals (info.source.name, juce::String ("Growl"));

        const auto item = itemFor (processor, ContentSlot::osc1);
        expect (item.isValid(), "the patch must carry the table");
        expectEquals (item.getProperty (resources::UserContent::pathProperty).toString(), juce::String ("Basses/Growl.wav"));
        expect (item.getProperty (resources::UserContent::audioProperty).toString().isNotEmpty());
        expect (! itemFor (processor, ContentSlot::osc2).isValid());
    }

    void testUnusableFileChangesNothing()
    {
        beginTest ("A file that cannot be used changes nothing, and says why by name");

        Scratch scratch;
        const auto good = scratch.child ("Good.wav");
        expect (writeWav (good, { tableSamples (4) }, 48000.0));

        ApolloAudioProcessor processor;
        auto& content = processor.getUserContent();

        resources::ContentEvent last;
        content.onChanged = [&last] (const resources::ContentEvent& event) { last = event; };

        content.loadFile (ContentSlot::osc1, good, {});
        settle (processor);

        const auto* before = tableOf (processor, 1);
        const auto stored = itemFor (processor, ContentSlot::osc1).getProperty (resources::UserContent::audioProperty).toString();
        setPlain (processor, "osc1_wavetable", 2.0f);

        for (const auto& [fileName, prepare] : std::vector<std::pair<juce::String, std::function<void (const juce::File&)>>> {
                 { "Gone.wav", [] (const juce::File&) {} },
                 { "Rubbish.wav", [] (const juce::File& f) { (void) f.replaceWithText ("not a wavetable"); } },
                 { "Ragged.wav", [] (const juce::File& f) { (void) writeWav (f, { std::vector<float> (3000, 0.3f) }, 48000.0); } } })
        {
            const auto file = scratch.child (fileName);
            prepare (file);

            content.loadFile (ContentSlot::osc1, file, {});
            settle (processor);

            expectEquals (last.token, juce::String ("LOAD_FAILED"), fileName);
            expect (last.message.startsWith (file.getFileNameWithoutExtension()), "the message names the file");
            expect (tableOf (processor, 1) == before, "a failed load replaced the table");
            expectEquals (itemFor (processor, ContentSlot::osc1).getProperty (resources::UserContent::audioProperty).toString(),
                          stored, "a failed load changed the patch");
            expectEquals (plainOf (processor, "osc1_wavetable"), 2.0f, "a failed load moved the choice");
            expect (! content.getInfo (ContentSlot::osc1).loading);
        }
    }

    void testNoiseSampleLoads()
    {
        beginTest ("A noise sample loads, at any rate, and selects SAMPLE");

        Scratch scratch;
        const auto file = scratch.child ("Breath.wav");
        expect (writeWav (file, { tone (22050), tone (22050) }, 44100.0));

        ApolloAudioProcessor processor;
        processor.getUserContent().loadFile (ContentSlot::noise, file, {});
        settle (processor);

        expect (processor.getVoiceEngine().getNoiseSampleSlot().isLoaded());
        expectEquals (plainOf (processor, "noise_type"), 6.0f);

        const auto info = processor.getUserContent().getInfo (ContentSlot::noise);
        expect (info.loaded);
        expectWithinAbsoluteError (info.seconds, 0.5, 1.0e-6);
        expectEquals (info.source.name, juce::String ("Breath"));
        expect (itemFor (processor, ContentSlot::noise).isValid());
    }

    void testNewerRequestWins()
    {
        beginTest ("A second choice supersedes the first, and only the second lands");

        Scratch scratch;
        const auto first = scratch.child ("First.wav");
        const auto second = scratch.child ("Second.wav");
        expect (writeWav (first, { tableSamples (64) }, 48000.0));
        expect (writeWav (second, { tableSamples (2) }, 48000.0));

        ApolloAudioProcessor processor;
        auto loaded = 0;
        processor.getUserContent().onChanged = [&loaded] (const resources::ContentEvent& event)
        {
            if (event.token == "LOADED")
                ++loaded;
        };

        processor.getUserContent().loadFile (ContentSlot::osc1, first, {});
        processor.getUserContent().loadFile (ContentSlot::osc1, second, {});
        settle (processor);

        expectEquals (loaded, 1);
        expectEquals (tableOf (processor, 1)->getNumFrames(), 2);
        expectEquals (processor.getUserContent().getInfo (ContentSlot::osc1).source.name, juce::String ("Second"));
    }

    void testHostSaveAndLoadCarryIt()
    {
        beginTest ("A host save carries the content, and another instance plays it");

        Scratch scratch;
        const auto tableFile = scratch.child ("Growl.wav");
        const auto sampleFile = scratch.child ("Breath.wav");
        expect (writeWav (tableFile, { tableSamples (16) }, 48000.0));
        expect (writeWav (sampleFile, { tone (4800) }, 48000.0));

        ApolloAudioProcessor original;
        original.getUserContent().loadFile (ContentSlot::osc1, tableFile, {});
        original.getUserContent().loadFile (ContentSlot::noise, sampleFile, {});
        settle (original);

        juce::MemoryBlock saved;
        original.getStateInformation (saved);
        logMessage ("  a project carrying a 16-frame table and a 0.1 s sample: "
                    + juce::String (static_cast<int> (saved.getSize() / 1024)) + " KB");

        ApolloAudioProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        expect (restored.getUserContent().getInfo (ContentSlot::osc1).loading, "a restore is a load, off this thread");
        settle (restored);

        expect (restored.getVoiceEngine().getWavetableLibrary().isReplaced (dsp::WavetableLibrary::userSlot (1)));
        expectEquals (tableOf (restored, 1)->getNumFrames(), 16);
        expect (restored.getVoiceEngine().getNoiseSampleSlot().isLoaded());
        expectEquals (restored.getUserContent().getInfo (ContentSlot::osc1).source.name, juce::String ("Growl"));
        expectEquals (plainOf (restored, "osc1_wavetable"), 4.0f);

        // They sound the same, to the 24 bits the patch keeps.
        const auto a = renderUser (original);
        const auto b = renderUser (restored);
        auto worst = 0.0f;

        for (std::size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));

        logMessage ("  worst difference between the two: " + juce::String (worst, 9));
        expect (worst < 1.0e-5f, "the restored table does not sound like the original");
    }

    void testSameStateAgainCostsNothing()
    {
        beginTest ("Restoring the state a slot already plays rebuilds nothing");

        Scratch scratch;
        const auto file = scratch.child ("Growl.wav");
        expect (writeWav (file, { tableSamples (16) }, 48000.0));

        ApolloAudioProcessor processor;
        processor.getUserContent().loadFile (ContentSlot::osc1, file, {});
        settle (processor);

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        const auto* before = tableOf (processor, 1);

        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        expect (! processor.getUserContent().getInfo (ContentSlot::osc1).loading);
        settle (processor);

        expect (tableOf (processor, 1) == before, "the same content was rebuilt");
    }

    void testDocumentWithoutContentEmptiesEverySlot()
    {
        beginTest ("A document without content empties every slot, which still sounds");

        Scratch scratch;
        const auto file = scratch.child ("Growl.wav");
        expect (writeWav (file, { tableSamples (4) }, 48000.0));

        ApolloAudioProcessor blank;
        juce::MemoryBlock plain;
        blank.getStateInformation (plain);
        expect (! blank.getValueTreeState().state.getChildWithName (resources::UserContent::contentTreeType).isValid(),
                "a patch with no content writes no element");

        ApolloAudioProcessor processor;
        processor.getUserContent().loadFile (ContentSlot::osc1, file, {});
        settle (processor);

        processor.setStateInformation (plain.getData(), static_cast<int> (plain.getSize()));
        settle (processor);

        expect (! processor.getVoiceEngine().getWavetableLibrary().isReplaced (dsp::WavetableLibrary::userSlot (1)));
        expect (! processor.getUserContent().getInfo (ContentSlot::osc1).loaded);
        expect (! tableOf (processor, 1)->isEmpty());
    }

    void testPresetsCarryItAndFactoryPresetsClearIt()
    {
        beginTest ("A preset carries the content; a factory preset clears it");

        Scratch scratch;
        const auto file = scratch.child ("Growl.wav");
        expect (writeWav (file, { tableSamples (8) }, 48000.0));

        ApolloAudioProcessor author;
        author.getUserContent().loadFile (ContentSlot::osc2, file, {});
        settle (author);

        const auto text = presets::write (author.getValueTreeState(), { "With a table", "Tester", "Lead", {} });
        expect (text.getNumBytesAsUTF8() < static_cast<std::size_t> (presets::maximumDocumentBytes));

        ApolloAudioProcessor listener;
        expect (presets::read (listener.getValueTreeState(), text) == state::StateLoadResult::ok);
        listener.notifyStateReplaced();
        settle (listener);

        expectEquals (tableOf (listener, 2)->getNumFrames(), 8);
        expectEquals (plainOf (listener, "osc2_wavetable"), 4.0f);

        const auto factory = resources::render (resources::factoryPresets().front());
        expect (presets::read (listener.getValueTreeState(), factory) == state::StateLoadResult::ok);
        listener.notifyStateReplaced();
        settle (listener);

        expect (! listener.getUserContent().getInfo (ContentSlot::osc2).loaded);
        expect (! listener.getVoiceEngine().getWavetableLibrary().isReplaced (dsp::WavetableLibrary::userSlot (2)));
    }

    void testUnreadableDocumentAudio()
    {
        beginTest ("Content in a document that cannot be read is refused, and the slot still sounds");

        ApolloAudioProcessor processor;

        auto state = processor.getValueTreeState().copyState();
        juce::ValueTree content { resources::UserContent::contentTreeType };
        juce::ValueTree item { resources::UserContent::itemTreeType };
        item.setProperty (resources::UserContent::slotProperty, "noise", nullptr);
        item.setProperty (resources::UserContent::nameProperty, "Broken", nullptr);
        item.setProperty (resources::UserContent::audioProperty, juce::Base64::toBase64 ("this is not FLAC"), nullptr);
        content.appendChild (item, nullptr);
        state.appendChild (content, nullptr);

        // A real document carries its version; without one the whole document
        // is refused before its content is looked at.
        state::stamp (state);

        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary (*state.createXml(), data);

        resources::ContentEvent last;
        processor.getUserContent().onChanged = [&last] (const resources::ContentEvent& event) { last = event; };

        processor.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
        settle (processor);

        expectEquals (last.token, juce::String ("RESTORE_FAILED"));
        logMessage ("  " + last.message);
        expect (! processor.getVoiceEngine().getNoiseSampleSlot().isLoaded());
        expect (! itemFor (processor, ContentSlot::noise).isValid(), "the patch still carries what cannot play");
    }

    void testLargestPatchFits()
    {
        beginTest ("The most content a patch can carry still fits in a preset");

        // The worst case: the largest table on both oscillators, and the
        // longest sample, of noise — which is what compresses worst.
        Scratch scratch;
        const auto table = scratch.child ("Huge.wav");
        const auto sample = scratch.child ("Rain.wav");
        expect (writeWav (table, { tableSamples (resources::maximumWavetableFrames) }, 48000.0));

        juce::Random random (7);
        std::vector<float> left (480000), right (480000);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = random.nextFloat() * 2.0f - 1.0f;
            right[i] = random.nextFloat() * 2.0f - 1.0f;
        }

        expect (writeWav (sample, { left, right }, 48000.0));

        ApolloAudioProcessor processor;
        processor.getUserContent().loadFile (ContentSlot::osc1, table, {});
        processor.getUserContent().loadFile (ContentSlot::osc2, table, {});
        processor.getUserContent().loadFile (ContentSlot::noise, sample, {});
        settle (processor);

        for (const auto slot : { ContentSlot::osc1, ContentSlot::osc2, ContentSlot::noise })
            expect (processor.getUserContent().getInfo (slot).loaded, resources::toToken (slot) + " did not load");

        const auto text = presets::write (processor.getValueTreeState(), { "Heavy", {}, {}, {} });
        const auto bytes = static_cast<int> (text.getNumBytesAsUTF8());

        logMessage ("  two 256-frame tables and ten seconds of stereo noise: "
                    + juce::String (bytes / 1024) + " KB of a " + juce::String (presets::maximumDocumentBytes / 1024)
                    + " KB bound");

        expect (bytes < presets::maximumDocumentBytes, "the largest patch does not fit");

        for (const auto slot : { ContentSlot::osc1, ContentSlot::noise })
            expect (itemFor (processor, slot).getProperty (resources::UserContent::audioProperty).toString().length()
                        < resources::maximumEmbeddedAudioChars,
                    "an item is past what a document may carry");
    }

    void testClear()
    {
        beginTest ("Clearing a slot puts its fallback back and takes it out of the patch");

        Scratch scratch;
        const auto file = scratch.child ("Growl.wav");
        expect (writeWav (file, { tableSamples (4) }, 48000.0));

        ApolloAudioProcessor processor;
        processor.getUserContent().loadFile (ContentSlot::osc1, file, {});
        settle (processor);

        processor.getUserContent().clear (ContentSlot::osc1);

        expect (! processor.getVoiceEngine().getWavetableLibrary().isReplaced (dsp::WavetableLibrary::userSlot (1)));
        expect (! processor.getUserContent().getInfo (ContentSlot::osc1).loaded);
        expect (! processor.getValueTreeState().state.getChildWithName (resources::UserContent::contentTreeType).isValid());
    }

    void testDestroyedWhileLoading()
    {
        beginTest ("An instance closed while a load is running closes cleanly");

        Scratch scratch;
        const auto file = scratch.child ("Big.wav");
        expect (writeWav (file, { tableSamples (128) }, 48000.0));

        {
            ApolloAudioProcessor processor;
            processor.getUserContent().loadFile (ContentSlot::osc1, file, {});
            processor.getUserContent().loadFile (ContentSlot::osc2, file, {});
        }

        expect (true);
    }
};

UserContentTests userContentTests;

} // namespace
