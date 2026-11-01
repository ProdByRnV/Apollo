/*
    Macro names (Phase 16c, ADR-0115): kept, saved, loaded, and set from the
    page.

      - a name set on the store is the name read back and written into the
        state tree; an empty name or the macro's own default takes it away; a
        name too long, or for a macro that does not exist, is refused unchanged;
      - a name is one line: runs of whitespace and control characters become
        one space, and the ends are trimmed;
      - renaming tells whoever is listening exactly once, and only when
        something changed — that is what marks a host's project as unsaved;
      - an unnamed patch writes no names at all, so its document is the one it
        always was;
      - a host save and load carries the names; a preset carries them too, and
        a preset without them puts "Macro 1" to "Macro 4" back;
      - a hand-edited document is cleaned and cut rather than refused;
      - the page's command renames a macro and is answered with all four; a
        name that is not one is refused with its own error and changes nothing.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "Resources/FactoryPresets.h"
#include "State/MacroNames.h"
#include "State/PresetDocument.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

[[nodiscard]] juce::String setNameMessage (const juce::String& macro, const juce::String& name)
{
    return "{\"type\":\"setMacroName\",\"version\":1,\"macro\":" + macro + ",\"name\":" + name + "}";
}

[[nodiscard]] juce::ValueTree namesIn (ApolloAudioProcessor& processor)
{
    return processor.getValueTreeState().state.getChildWithName (state::MacroNameStore::namesTreeType);
}

class MacroNameTests final : public juce::UnitTest
{
public:
    MacroNameTests()
        : juce::UnitTest ("Macro names", "State")
    {
    }

    void runTest() override
    {
        testTheStore();
        testANameIsOneLine();
        testRenamingIsAnnounced();
        testAnUnnamedPatchWritesNothing();
        testAHostSaveCarriesTheNames();
        testAPresetCarriesTheNames();
        testAHandEditedDocument();
        testTheCommands();
        testTheParser();
    }

private:
    void testTheStore()
    {
        beginTest ("A name set is the name read back, and in the state tree");

        ApolloAudioProcessor processor;
        auto& store = processor.getMacroNames();

        for (int i = 0; i < state::numMacroNames; ++i)
        {
            expect (store.get (i).isEmpty(), "a fresh macro has a name");
            expectEquals (store.displayName (i), "Macro " + juce::String (i + 1));
        }

        expect (store.set (1, "Wobble"));
        expectEquals (store.get (1), juce::String ("Wobble"));
        expectEquals (store.displayName (1), juce::String ("Wobble"));
        expect (store.get (0).isEmpty(), "naming macro 2 named macro 1");

        const auto stored = namesIn (processor);
        expect (stored.isValid() && stored.getNumChildren() == 1, "the tree does not hold exactly the one name");
        expectEquals (static_cast<int> (stored.getChild (0).getProperty (state::MacroNameStore::macroProperty)), 2);
        expectEquals (stored.getChild (0).getProperty (state::MacroNameStore::nameProperty).toString(),
                      juce::String ("Wobble"));

        // Exactly the limit is a name; one more is not.
        const juce::String sixteen ("Sixteen chars ok");
        expectEquals (sixteen.length(), state::maxMacroNameLength);
        expect (store.set (2, sixteen));
        expect (! store.set (2, sixteen + "!"), "a name over the limit was accepted");
        expectEquals (store.get (2), sixteen, "a refused name changed the stored one");

        // Characters, not bytes: sixteen two-byte letters fit.
        const auto umlauts = juce::String::fromUTF8 ("\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6"
                                                     "\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6\xc3\xb6");
        expectEquals (umlauts.length(), 16);
        expect (store.set (3, umlauts), "sixteen non-ASCII letters were refused");

        expect (! store.set (4, "Nope"), "a macro that does not exist was named");
        expect (! store.set (-1, "Nope"));

        // Taking a name away: by emptying it, or by giving it its own default.
        expect (store.set (1, ""));
        expect (store.get (1).isEmpty());
        expect (store.set (2, "Macro 3"));
        expect (store.get (2).isEmpty(), "the default name was stored as a name");
        expectEquals (store.displayName (2), juce::String ("Macro 3"));

        // Another macro's default is a perfectly good name.
        expect (store.set (0, "Macro 4"));
        expectEquals (store.get (0), juce::String ("Macro 4"));
    }

    void testANameIsOneLine()
    {
        beginTest ("A name is one line, trimmed");

        using Store = state::MacroNameStore;
        expectEquals (Store::clean ("  Filter\t\tSweep \n"), juce::String ("Filter Sweep"));
        expectEquals (Store::clean ("Line\r\nbreak"), juce::String ("Line break"));
        expectEquals (Store::clean (juce::String::fromUTF8 ("a\xe2\x80\xa8" "b")), juce::String ("a b"),
                      "a line separator survived");
        expectEquals (Store::clean ("\t \n"), juce::String());
        expectEquals (Store::clean ("Wobble"), juce::String ("Wobble"));

        ApolloAudioProcessor processor;
        expect (processor.getMacroNames().set (0, "   Macro 1   "));
        expect (processor.getMacroNames().get (0).isEmpty(), "the default with spaces round it became a name");

        // Measured after cleaning: spaces round a name at the limit cost nothing.
        expect (processor.getMacroNames().set (1, "   Sixteen chars ok   "));
        expectEquals (processor.getMacroNames().get (1), juce::String ("Sixteen chars ok"));
    }

    void testRenamingIsAnnounced()
    {
        beginTest ("Renaming is announced once, and only when something changed");

        ApolloAudioProcessor processor;
        state::MacroNameStore store (processor.getValueTreeState());

        auto calls = 0;
        store.onEdited = [&calls] { ++calls; };

        expect (store.set (0, "Wobble"));
        expectEquals (calls, 1);

        expect (store.set (0, "Wobble"));
        expectEquals (calls, 1, "the same name again was announced");

        expect (! store.set (0, "Far too long for a macro"));
        expectEquals (calls, 1, "a refused name was announced");

        expect (store.set (0, ""));
        expectEquals (calls, 2, "taking the name away was not announced");

        (void) store.restoreFromState();
        expectEquals (calls, 2, "a state load was announced as an edit");
    }

    void testAnUnnamedPatchWritesNothing()
    {
        beginTest ("A patch with no names saves no names at all");

        ApolloAudioProcessor processor;
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
        expect (xml != nullptr && xml->getChildByName (state::MacroNameStore::namesTreeType) == nullptr,
                "an unnamed patch wrote names");

        // And naming one and taking the name away is the same as never naming.
        expect (processor.getMacroNames().set (3, "Space"));
        expect (processor.getMacroNames().set (3, ""));
        expect (! namesIn (processor).isValid());
    }

    void testAHostSaveCarriesTheNames()
    {
        beginTest ("A host save and load carries the names");

        juce::MemoryBlock saved;

        {
            ApolloAudioProcessor processor;
            expect (processor.getMacroNames().set (0, "Brightness"));
            expect (processor.getMacroNames().set (3, "Space"));
            processor.getStateInformation (saved);
        }

        ApolloAudioProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expectEquals (restored.getMacroNames().get (0), juce::String ("Brightness"));
        expectEquals (restored.getMacroNames().get (3), juce::String ("Space"));
        expect (restored.getMacroNames().get (1).isEmpty());

        // A project saved before names existed takes them away rather than
        // leaving the last project's labels on this one's macros.
        juce::MemoryBlock plain;
        ApolloAudioProcessor { }.getStateInformation (plain);

        restored.setStateInformation (plain.getData(), static_cast<int> (plain.getSize()));
        expect (restored.getMacroNames().get (0).isEmpty(), "an older project kept a name");
        expectEquals (restored.getMacroNames().displayName (0), juce::String ("Macro 1"));
    }

    void testAPresetCarriesTheNames()
    {
        beginTest ("A preset carries the names, and one without puts the defaults back");

        ApolloAudioProcessor processor;
        expect (processor.getMacroNames().set (2, "Grit"));

        presets::Metadata metadata;
        metadata.name = "Named";
        const auto text = presets::write (processor.getValueTreeState(), metadata);
        expect (text.contains (state::MacroNameStore::namesTreeType), "the preset left the names out");

        ApolloAudioProcessor other;
        expect (presets::read (other.getValueTreeState(), text) == state::StateLoadResult::ok);
        other.notifyStateReplaced();
        expectEquals (other.getMacroNames().get (2), juce::String ("Grit"), "the preset's name did not load");

        // A built-in carries no names, so loading one clears them.
        const auto factory = resources::factoryPresets();
        expect (! factory.empty());
        expect (presets::read (other.getValueTreeState(), resources::render (factory.front()))
                == state::StateLoadResult::ok);
        other.notifyStateReplaced();
        expect (other.getMacroNames().get (2).isEmpty(), "a factory preset kept a name");
    }

    void testAHandEditedDocument()
    {
        beginTest ("A hand-edited document is cleaned and cut rather than refused");

        ApolloAudioProcessor processor;
        auto root = processor.getValueTreeState().state;

        juce::ValueTree names (state::MacroNameStore::namesTreeType);

        const auto add = [&names] (const juce::var& macro, const juce::String& text)
        {
            juce::ValueTree entry (state::MacroNameStore::macroTreeType);
            entry.setProperty (state::MacroNameStore::macroProperty, macro, nullptr);
            entry.setProperty (state::MacroNameStore::nameProperty, text, nullptr);
            names.appendChild (entry, nullptr);
        };

        add (1, "Fifteen letters and then some");
        add (2, "\tTabbed\n\nname ");
        add (3, "Macro 3");
        add (9, "No such macro");
        add (4, juce::String::repeatedString ("x", 100000));

        root.appendChild (names, nullptr);
        expectEquals (processor.getMacroNames().restoreFromState(), 3);

        const auto first = processor.getMacroNames().get (0);
        expectEquals (first.length(), state::maxMacroNameLength - 1, "a long name was not cut to fit");
        expectEquals (first, juce::String ("Fifteen letters"), "the cut left a trailing space");
        expectEquals (processor.getMacroNames().get (1), juce::String ("Tabbed name"));
        expect (processor.getMacroNames().get (2).isEmpty(), "a default name was kept as a name");
        expectEquals (processor.getMacroNames().get (3).length(), state::maxMacroNameLength);

        // What the tree holds afterwards is what was accepted.
        const auto rewritten = namesIn (processor);
        expectEquals (rewritten.getNumChildren(), 3);
    }

    void testTheCommands()
    {
        beginTest ("The page renames a macro and is answered with all four; a bad name changes nothing");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setMacroNames (&processor.getMacroNames());

        const auto reply = bridge.handleMessage (setNameMessage ("2", "\"Wobble\""));

        juce::var parsed;
        expect (juce::JSON::parse (reply, parsed).wasOk());
        expectEquals (parsed["type"].toString(), juce::String ("macroNames"));
        expectEquals (static_cast<int> (parsed["maxLength"]), state::maxMacroNameLength);

        const auto* names = parsed["names"].getArray();
        expect (names != nullptr && names->size() == 4, "the answer is not all four names");

        if (names != nullptr && names->size() == 4)
        {
            expectEquals (names->getReference (0).toString(), juce::String());
            expectEquals (names->getReference (1).toString(), juce::String ("Wobble"));
        }

        expectEquals (processor.getMacroNames().get (1), juce::String ("Wobble"));

        const auto refused = bridge.handleMessage (setNameMessage ("2", "\"Seventeen chars!!\""));
        expect (refused.contains ("INVALID_MACRO_NAME"), "a long name was not refused by name");
        expectEquals (processor.getMacroNames().get (1), juce::String ("Wobble"), "a refused name changed the stored one");

        expect (bridge.handleMessage ("{\"type\":\"requestMacroNames\",\"version\":1}").contains ("\"macroNames\""));

        // Without a store, the commands are refused rather than ignored.
        ui::ParameterBridge headless (processor.getValueTreeState());
        expect (headless.handleMessage ("{\"type\":\"requestMacroNames\",\"version\":1}").contains ("\"error\""));
    }

    void testTheParser()
    {
        beginTest ("The parser refuses anything that is not a name for a macro that exists");

        expect (ui::parseMessage (setNameMessage ("1", "\"Wobble\"")).ok);
        expect (ui::parseMessage (setNameMessage ("4", "\"\"")).ok, "an empty name, which takes one away, was refused");

        const auto refusedAs = [this] (const juce::String& message, ui::BridgeErrorCode code, const juce::String& what)
        {
            const auto result = ui::parseMessage (message);
            expect (! result.ok && result.error == code, what);
        };

        using ui::BridgeErrorCode;
        refusedAs (setNameMessage ("0", "\"A\""), BridgeErrorCode::invalidMacroName, "macro 0");
        refusedAs (setNameMessage ("5", "\"A\""), BridgeErrorCode::invalidMacroName, "macro 5");
        refusedAs (setNameMessage ("1.5", "\"A\""), BridgeErrorCode::invalidMacroName, "macro 1.5");
        refusedAs (setNameMessage ("1", "\"Seventeen chars!!\""), BridgeErrorCode::invalidMacroName, "17 characters");
        refusedAs (setNameMessage ("1", "7"), BridgeErrorCode::malformedMessage, "a number for a name");
        refusedAs (setNameMessage ("\"1\"", "\"A\""), BridgeErrorCode::malformedMessage, "a string for a macro");
        refusedAs ("{\"type\":\"setMacroName\",\"version\":1,\"macro\":1}", BridgeErrorCode::malformedMessage, "no name");

        // What is measured is what would be kept.
        const auto padded = ui::parseMessage (setNameMessage ("1", "\"  Sixteen chars ok \\t\""));
        expect (padded.ok, "spaces round a name at the limit made it too long");
        expectEquals (padded.command.macroName, juce::String ("Sixteen chars ok"));
    }
};

static MacroNameTests macroNameTests;

} // namespace
