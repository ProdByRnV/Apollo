/*
    The interface size (18b): the file that remembers it, and the commands that
    change it.

    The window itself is the WebView editor's, which the headless suite does not
    build (ADR-0076); its size for each scale is in EditorSizingTests. What is
    here is everything around it that can go wrong without a screen: a settings
    file that is missing, damaged or somebody else's, and a page that asks for
    a size Apollo does not offer.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "UI/BridgeProtocol.h"
#include "UI/EditorSizing.h"
#include "UI/InterfaceSettings.h"
#include "UI/ParameterBridge.h"

using namespace apollo;
using namespace apollo::ui;

namespace
{

class InterfaceSettingsTests final : public juce::UnitTest
{
public:
    InterfaceSettingsTests() : juce::UnitTest ("Interface settings", "UI") {}

    void runTest() override
    {
        testASizeIsKept();
        testABadFileIsTheDefault();
        testTheProtocol();
        testThroughTheBridge();
    }

private:
    struct Scratch
    {
        juce::File root { juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("ApolloInterfaceSettingsTests")
                              .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))) };

        ~Scratch() { root.getParentDirectory().deleteRecursively(); }

        [[nodiscard]] juce::File file() const { return root.getChildFile ("Apollo/Interface.settings"); }
    };

    static juce::var parsed (const juce::String& message) { return juce::JSON::parse (message); }

    void testASizeIsKept()
    {
        beginTest ("A size chosen is the size the next window opens at, and only an offered one is kept");

        Scratch scratch;
        const auto file = scratch.file();

        expectEquals (loadInterfaceScale (file), defaultInterfaceScale, "nothing stored is the default");

        for (const auto scale : interfaceScales)
        {
            expect (saveInterfaceScale (file, scale), "the folder is made the first time");
            expectEquals (loadInterfaceScale (file), scale);
        }

        expect (saveInterfaceScale (file, 125));
        expect (! saveInterfaceScale (file, 101), "a size not offered is not written");
        expect (! saveInterfaceScale (file, 0));
        expectEquals (loadInterfaceScale (file), 125, "and the one before it is still there");

        expect (defaultInterfaceSettingsFile().getFileName() == "Interface.settings");
        expect (defaultInterfaceSettingsFile().getParentDirectory().getFileName() == "Apollo");
    }

    void testABadFileIsTheDefault()
    {
        beginTest ("A settings file that is damaged, foreign or enormous opens the window at the default size");

        Scratch scratch;
        const auto file = scratch.file();
        expect (file.getParentDirectory().createDirectory().wasOk());

        for (const auto* text : { "", "not xml at all", "<OTHER scale=\"150\"/>", "<APOLLO_INTERFACE/>",
                                  "<APOLLO_INTERFACE scale=\"\"/>", "<APOLLO_INTERFACE scale=\"101\"/>",
                                  "<APOLLO_INTERFACE scale=\"1.5\"/>", "<APOLLO_INTERFACE scale=\"150abc\"/>",
                                  "<APOLLO_INTERFACE scale=\"-150\"/>", "<APOLLO_INTERFACE scale=\"99999999999\"/>",
                                  "<APOLLO_INTERFACE scale=\"15" })
        {
            expect (file.replaceWithText (text));
            expectEquals (loadInterfaceScale (file), defaultInterfaceScale, juce::String ("for: ") + text);
        }

        // One that would have been read, buried in padding no settings file has.
        expect (file.replaceWithText ("<APOLLO_INTERFACE scale=\"150\"/>" + juce::String::repeatedString (" ", 20000)));
        expectEquals (loadInterfaceScale (file), defaultInterfaceScale, "an enormous file is not read");

        expect (file.replaceWithText ("<APOLLO_INTERFACE scale=\" 150 \"/>"));
        expectEquals (loadInterfaceScale (file), 150, "and a good one is");

        // A folder where the file should be.
        Scratch other;
        expect (other.file().createDirectory().wasOk());
        expectEquals (loadInterfaceScale (other.file()), defaultInterfaceScale);
        expect (! saveInterfaceScale (other.file(), 150), "and it cannot be written over");
    }

    void testTheProtocol()
    {
        beginTest ("The page can ask for an offered size and nothing else");

        const auto set = parseMessage (R"({"type":"setInterfaceScale","version":1,"scale":150})");
        expect (set.ok && set.command.type == BridgeCommandType::setInterfaceScale);
        expectEquals (set.command.interfaceScale, 150);

        expect (parseMessage (R"({"type":"requestInterfaceScale","version":1})").ok);

        for (const auto* scale : { "101", "150.5", "1.5", "0", "-150", "1e30", "\"150\"", "null", "[150]" })
            expect (parseMessage (juce::String (R"({"type":"setInterfaceScale","version":1,"scale":)") + scale + "}").error
                        == BridgeErrorCode::malformedMessage,
                    juce::String ("scale ") + scale);

        expect (parseMessage (R"({"type":"setInterfaceScale","version":1})").error == BridgeErrorCode::malformedMessage);

        const auto message = parsed (makeInterfaceScaleMessage (125));
        expectEquals (message["type"].toString(), juce::String ("interfaceScale"));
        expectEquals (static_cast<int> (message["scale"]), 125);

        const auto* offered = message["scales"].getArray();
        expect (offered != nullptr && offered->size() == static_cast<int> (interfaceScales.size()),
                "the page is sent the engine's list rather than keeping a copy");
    }

    void testThroughTheBridge()
    {
        beginTest ("A size chosen on the page reaches the editor, and a refused one changes nothing");

        ApolloAudioProcessor processor;
        ParameterBridge bridge (processor.getValueTreeState());

        const auto asked = parsed (bridge.handleMessage (R"({"type":"requestInterfaceScale","version":1})"));
        expectEquals (static_cast<int> (asked["scale"]), defaultInterfaceScale);

        // Without an editor the size is still taken and answered.
        const auto alone = parsed (bridge.handleMessage (R"({"type":"setInterfaceScale","version":1,"scale":85})"));
        expectEquals (static_cast<int> (alone["scale"]), 85);

        int told = 0;
        bridge.onInterfaceScaleChanged = [&told] (int scale) { told = scale; };

        const auto reply = parsed (bridge.handleMessage (R"({"type":"setInterfaceScale","version":1,"scale":175})"));
        expectEquals (static_cast<int> (reply["scale"]), 175, "the page draws what the engine answers");
        expectEquals (told, 175, "and the editor is told, to keep it and fit the window");

        told = 0;
        const auto refused = parsed (bridge.handleMessage (R"({"type":"setInterfaceScale","version":1,"scale":300})"));
        expectEquals (refused["type"].toString(), juce::String ("error"));
        expectEquals (told, 0, "a refused size goes nowhere");
        expectEquals (bridge.getInterfaceScale(), 175);

        // What the editor reads from the file goes through the same check.
        bridge.setInterfaceScale (333);
        expectEquals (bridge.getInterfaceScale(), 175);
        bridge.setInterfaceScale (110);
        expectEquals (bridge.getInterfaceScale(), 110);
    }
};

static InterfaceSettingsTests interfaceSettingsTests;

} // namespace
