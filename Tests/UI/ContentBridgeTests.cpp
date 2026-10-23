/*
    The user's content across the bridge (Phase 13g).

    The protocol half: the commands the page can send, and everything it might
    send instead that must be refused. The bridge half: a whole processor, its
    content store and a library pointed at a scratch folder, driven through
    the bridge the way the page drives it — and nothing the page is sent names
    a path.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "UI/BridgeProtocol.h"
#include "UI/ParameterBridge.h"

using namespace apollo;
using namespace apollo::ui;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

class Scratch
{
public:
    Scratch()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloContentBridgeTests")
                    .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~Scratch() { root.getParentDirectory().deleteRecursively(); }

    [[nodiscard]] juce::File child (const juce::String& path) const { return root.getChildFile (path); }

    [[nodiscard]] resources::ContentLocations locations() const
    {
        return { child ("user/Wavetables"), child ("user/Noise"), child ("factory/Wavetables"), child ("factory/Noise") };
    }

private:
    juce::File root;
};

[[nodiscard]] bool writeWav (const juce::File& file, int length)
{
    (void) file.getParentDirectory().createDirectory();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    const auto writer = format.createWriterFor (stream,
                                                juce::AudioFormatWriterOptions {}
                                                    .withSampleRate (48000.0)
                                                    .withNumChannels (1)
                                                    .withBitsPerSample (24));

    if (writer == nullptr)
        return false;

    juce::AudioBuffer<float> buffer (1, length);

    for (int i = 0; i < length; ++i)
        buffer.setSample (0, i, 0.5f * static_cast<float> (std::sin (twoPi * i / 2048.0)));

    return writer->writeFromAudioSampleBuffer (buffer, 0, length);
}

[[nodiscard]] juce::var parse (const juce::String& json)
{
    return juce::JSON::parse (json);
}

class ContentBridgeTests final : public juce::UnitTest
{
public:
    ContentBridgeTests()
        : juce::UnitTest ("Content across the bridge", "UI")
    {
    }

    void runTest() override
    {
        testCommandsParse();
        testWhatIsRefused();
        testThroughTheBridge();
    }

private:
    void testCommandsParse()
    {
        beginTest ("Every content command is accepted in its proper form");

        const auto request = parseMessage (R"({"type":"requestContent","version":1})");
        expect (request.ok && request.command.type == BridgeCommandType::requestContent);

        expect (parseMessage (R"({"type":"rescanContent","version":1})").command.type
                == BridgeCommandType::rescanContent);

        const auto load = parseMessage (R"({"type":"loadContent","version":1,"slot":"osc2","entry":12})");
        expect (load.ok);
        expect (load.command.type == BridgeCommandType::loadContent);
        expect (load.command.contentSlot == resources::ContentSlot::osc2);
        expectEquals (load.command.contentId, 12);

        for (const auto& [type, expected] : std::vector<std::pair<juce::String, BridgeCommandType>> {
                 { "chooseContentFile", BridgeCommandType::chooseContentFile },
                 { "clearContent", BridgeCommandType::clearContent },
                 { "saveContentToLibrary", BridgeCommandType::saveContentToLibrary } })
        {
            const auto parsed = parseMessage (R"({"type":")" + type + R"(","version":1,"slot":"noise"})");
            expect (parsed.ok, type);
            expect (parsed.command.type == expected, type);
            expect (parsed.command.contentSlot == resources::ContentSlot::noise, type);
        }

        const auto reveal = parseMessage (R"({"type":"revealContentFolder","version":1,"kind":"noise"})");
        expect (reveal.ok && reveal.command.contentKind == resources::ContentKind::noise);
    }

    void testWhatIsRefused()
    {
        beginTest ("A slot that is not one, a path, or an entry that could not exist is refused by name");

        const auto errorOf = [] (const juce::String& json) { return parseMessage (json).error; };

        expect (errorOf (R"({"type":"clearContent","version":1,"slot":"osc3"})") == BridgeErrorCode::invalidContentSlot);
        expect (errorOf (R"({"type":"clearContent","version":1})") == BridgeErrorCode::invalidContentSlot);
        expect (errorOf (R"({"type":"chooseContentFile","version":1,"slot":1})") == BridgeErrorCode::invalidContentSlot);
        expect (errorOf (R"({"type":"revealContentFolder","version":1,"kind":"C:/Windows"})")
                == BridgeErrorCode::invalidContentSlot);

        for (const auto* entry : { "0", "-1", "1.5", "1e30", "99999999" })
            expect (errorOf (juce::String (R"({"type":"loadContent","version":1,"slot":"osc1","entry":)") + entry + "}")
                        == BridgeErrorCode::unknownContent,
                    juce::String ("entry ") + entry);

        expect (errorOf (R"({"type":"loadContent","version":1,"slot":"osc1","entry":"C:/table.wav"})")
                == BridgeErrorCode::malformedMessage, "a path is not an entry");
        expect (errorOf (R"({"type":"loadContent","version":1,"slot":"osc1"})") == BridgeErrorCode::malformedMessage);

        expectEquals (toToken (BridgeErrorCode::unknownContent), juce::String ("UNKNOWN_CONTENT"));
        expectEquals (toToken (BridgeErrorCode::invalidContentSlot), juce::String ("INVALID_CONTENT_SLOT"));
    }

    void testThroughTheBridge()
    {
        beginTest ("The page loads library content by number, and is told what every slot holds");

        Scratch scratch;
        const auto where = scratch.locations();
        expect (writeWav (where.userWavetables.getChildFile ("Basses/Growl.wav"), 2048 * 8));
        expect (writeWav (where.userNoise.getChildFile ("Breath.wav"), 4800));

        ApolloAudioProcessor processor;
        processor.getContentLibrary().setLocations (where);

        ParameterBridge bridge (processor.getValueTreeState());

        std::vector<juce::String> pushed;
        bridge.setOutboundHandler ([&pushed] (const juce::String& message) { pushed.push_back (message); });

        bridge.setUserContent (&processor.getUserContent(), &processor.getContentLibrary());
        processor.getContentLibrary().waitForScan();
        processor.getContentLibrary().flushPendingNotification();

        const auto index = parse (bridge.createContentIndex());
        expectEquals (index["type"].toString(), juce::String ("contentIndex"));
        expectEquals (index["wavetables"].size(), 1);
        expectEquals (index["noise"].size(), 1);

        const auto growl = static_cast<int> (index["wavetables"][0]["id"]);
        const auto breath = static_cast<int> (index["noise"][0]["id"]);
        expectEquals (static_cast<int> (index["wavetables"][0]["frames"]), 8);
        expectEquals (index["wavetables"][0]["bank"].toString(), juce::String ("Basses"));

        // A sample is not a wavetable.
        const auto wrongKind = parse (bridge.handleMessage (
            R"({"type":"loadContent","version":1,"slot":"osc1","entry":)" + juce::String (breath) + "}"));
        expectEquals (wrongKind["code"].toString(), juce::String ("UNKNOWN_CONTENT"));

        expect (bridge.handleMessage (
                    R"({"type":"loadContent","version":1,"slot":"osc1","entry":)" + juce::String (growl) + "}")
                    .isEmpty());

        processor.getUserContent().waitUntilIdle();
        processor.getUserContent().flushDeliveries();

        const auto last = parse (pushed.back());
        expectEquals (last["type"].toString(), juce::String ("contentSlots"));
        expectEquals (last["status"].toString(), juce::String ("LOADED"));
        expectEquals (last["slot"].toString(), juce::String ("osc1"));

        const auto osc1 = last["slots"][0];
        expect (static_cast<bool> (osc1["loaded"]));
        expectEquals (osc1["name"].toString(), juce::String ("Growl"));
        expectEquals (osc1["origin"].toString(), juce::String ("library"));
        expectEquals (static_cast<int> (osc1["entry"]), growl, "the slot names the row it came from");

        beginTest ("Save to library writes the patch's audio as a file the browser then lists");

        processor.getUserContent().loadFile (resources::ContentSlot::noise,
                                             where.userNoise.getChildFile ("Breath.wav"),
                                             { "Breath (from elsewhere)", resources::ContentOrigin::file, {} });
        processor.getUserContent().waitUntilIdle();
        processor.getUserContent().flushDeliveries();

        const auto saved = parse (bridge.handleMessage (R"({"type":"saveContentToLibrary","version":1,"slot":"noise"})"));
        expectEquals (saved["status"].toString(), juce::String ("SAVED"));
        expect (where.userNoise.getChildFile ("Breath (from elsewhere).wav").existsAsFile());

        processor.getContentLibrary().waitForScan();
        processor.getContentLibrary().flushPendingNotification();

        const auto slots = parse (bridge.createContentSlots ("CURRENT", {}, {}));
        const auto noise = slots["slots"][2];
        expectEquals (noise["origin"].toString(), juce::String ("library"));
        expect (static_cast<int> (noise["entry"]) > 0, "the saved file is the row the slot came from");

        beginTest ("Asking for a dialog or a folder goes where the editor says, and clearing empties");

        resources::ContentSlot chosen = resources::ContentSlot::osc1;
        bridge.onChooseContentFile = [&chosen] (resources::ContentSlot slot) { chosen = slot; };
        expect (bridge.handleMessage (R"({"type":"chooseContentFile","version":1,"slot":"osc2"})").isEmpty());
        expect (chosen == resources::ContentSlot::osc2);

        auto revealed = 0;
        bridge.onRevealContentFolder = [&revealed] (resources::ContentKind) { ++revealed; };
        expect (bridge.handleMessage (R"({"type":"revealContentFolder","version":1,"kind":"wavetable"})").isEmpty());
        expectEquals (revealed, 1);

        expect (bridge.handleMessage (R"({"type":"clearContent","version":1,"slot":"osc1"})").isEmpty());
        expectEquals (parse (pushed.back())["status"].toString(), juce::String ("CLEARED"));
        expect (! processor.getUserContent().getInfo (resources::ContentSlot::osc1).loaded);

        beginTest ("Nothing the page is sent about content names a path");

        pushed.push_back (bridge.createContentIndex());
        pushed.push_back (bridge.createContentSlots ("CURRENT", {}, {}));

        const auto scratchPath = where.userWavetables.getParentDirectory().getFullPathName();

        for (const auto& message : pushed)
        {
            expect (! message.contains (scratchPath), "a message carries the library's location");
            expect (! message.contains (".wav"), "a message carries a file name: " + message.substring (0, 120));
            expect (! message.containsChar ('\\'));
        }

        bridge.setUserContent (nullptr, nullptr);
        expectEquals (parse (bridge.handleMessage (R"({"type":"requestContent","version":1})"))["code"].toString(),
                      juce::String ("UNKNOWN_MESSAGE_TYPE"), "with nothing attached the commands are refused");
    }
};

ContentBridgeTests contentBridgeTests;

} // namespace
