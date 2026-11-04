/*
    Undo and redo (Phase 18a, ADR-0119).

      - a control picked up and let go is one step, named after it; one let go
        where it was picked up is none; undo puts it back and redo puts it
        forward again, and a new edit forgets what could be redone;
      - gestures that overlap are one step, whatever order they end in, and
        an end that never began changes nothing;
      - automation and a MIDI controller are not edits: nothing is recorded,
        and an undo afterwards takes back only what the step itself moved;
      - a curve drawn, a macro named and a routing dropped are steps;
      - a preset loaded is a step, and undoing it brings back the sound that
        was there — parameters, name, macro names — and leaves the MIDI
        assignments as they are;
      - a wavetable loaded is a step once it lands, covering the slot and the
        switch it set and not a knob turned while it loaded; one removed is a
        step too;
      - a host loading state, or a program change, empties the history;
      - no more than `maxSteps` are kept;
      - the page's commands, and the message that answers them.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "MIDI/MidiControlManager.h"
#include "Parameters/ParameterDefinitions.h"
#include "Resources/PresetLibrary.h"
#include "State/EditHistory.h"
#include "State/MacroNames.h"
#include "State/PresetDocument.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

[[nodiscard]] float valueOf (ApolloAudioProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    return parameter != nullptr ? parameter->getValue() : -1.0f;
}

[[nodiscard]] juce::String gesture (const juce::String& id, const juce::String& state)
{
    return "{\"type\":\"gesture\",\"version\":1,\"id\":\"" + id + "\",\"state\":\"" + state + "\"}";
}

[[nodiscard]] juce::String set (const juce::String& id, float value)
{
    return "{\"type\":\"setParameter\",\"version\":1,\"id\":\"" + id + "\",\"normalizedValue\":" + juce::String (value) + "}";
}

/** Turns a control on the page from wherever it is to @p value, as a knob
    does: picked up, moved, let go. */
void turn (ui::ParameterBridge& bridge, const juce::String& id, float value)
{
    (void) bridge.handleMessage (gesture (id, "begin"));
    (void) bridge.handleMessage (set (id, value * 0.5f));
    (void) bridge.handleMessage (set (id, value));
    (void) bridge.handleMessage (gesture (id, "end"));
}

[[nodiscard]] juce::String command (const juce::String& type)
{
    return "{\"type\":\"" + type + "\",\"version\":1}";
}

void settle (ApolloAudioProcessor& processor)
{
    processor.getUserContent().waitUntilIdle();
    processor.getUserContent().flushDeliveries();
}

/** A preset library with nothing of the user's in it: the factory sounds,
    which are compiled in, are all there is. */
struct FactoryOnlyLibrary
{
    juce::File root { juce::File::createTempFile ("ApolloEditHistory") };
    resources::PresetLibrary library;

    FactoryOnlyLibrary()
    {
        root.deleteFile();
        root.createDirectory();

        resources::PresetLocations locations;
        locations.userDirectory = root.getChildFile ("User");
        locations.factoryDirectory = root.getChildFile ("Factory");
        locations.userDirectory.createDirectory();

        library.setLocations (locations);
        library.rescan();
        library.waitForScan();
        library.flushPendingNotification();
    }

    ~FactoryOnlyLibrary()
    {
        library.waitForScan();
        root.deleteRecursively();
    }

    [[nodiscard]] int idOf (const juce::String& name) const
    {
        for (const auto& entry : library.getIndex().entries)
            if (entry.name == name)
                return entry.id;

        return 0;
    }
};

class EditHistoryTests final : public juce::UnitTest
{
public:
    EditHistoryTests()
        : juce::UnitTest ("Edit history", "State")
    {
    }

    void runTest() override
    {
        testAGestureIsAStep();
        testOverlappingGestures();
        testAutomationIsNotAnEdit();
        testTheRestOfTheSound();
        testAPresetLoad();
        testContent();
        testOutsideLoadsEmptyIt();
        testTheLimit();
        testTheCommands();
    }

private:
    void testAGestureIsAStep()
    {
        beginTest ("A control picked up and let go is one step, which undoes and redoes");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);

        const auto start = valueOf (processor, "filter1_cutoff");

        turn (bridge, "filter1_cutoff", 0.25f);
        expectEquals (history.numUndoable(), 1);
        expectEquals (history.undoName(), juce::String ("Filter 1 Cutoff"));

        // Picked up and put down where it was: nothing happened.
        turn (bridge, "filter1_cutoff", 0.25f);
        expectEquals (history.numUndoable(), 1, "a control let go where it was is not a step");

        expect (history.undo());
        expectEquals (valueOf (processor, "filter1_cutoff"), start);
        expect (! history.canUndo());
        expectEquals (history.redoName(), juce::String ("Filter 1 Cutoff"));

        expect (history.redo());
        expectWithinAbsoluteError (valueOf (processor, "filter1_cutoff"), 0.25f, 1.0e-3f);
        expect (! history.canRedo());

        // A new edit after an undo forgets what could have been redone.
        expect (history.undo());
        turn (bridge, "filter1_resonance", 0.6f);
        expect (! history.canRedo(), "a new edit must forget the redo");
        expectEquals (valueOf (processor, "filter1_cutoff"), start);

        expect (! history.redo(), "nothing to redo is refused");

        bridge.setEditHistory (nullptr);
    }

    void testOverlappingGestures()
    {
        beginTest ("Gestures that overlap are one step, whichever ends first");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);

        const auto frequency = valueOf (processor, "fx_eq_band1_freq");
        const auto gain = valueOf (processor, "fx_eq_band1_gain");

        // An equaliser handle: frequency and gain at once.
        (void) bridge.handleMessage (gesture ("fx_eq_band1_freq", "begin"));
        (void) bridge.handleMessage (gesture ("fx_eq_band1_gain", "begin"));
        (void) bridge.handleMessage (set ("fx_eq_band1_freq", 0.7f));
        (void) bridge.handleMessage (set ("fx_eq_band1_gain", 0.8f));
        (void) bridge.handleMessage (gesture ("fx_eq_band1_freq", "end"));

        expectEquals (history.numUndoable(), 0, "the step ends when the last gesture does");

        (void) bridge.handleMessage (gesture ("fx_eq_band1_gain", "end"));
        expectEquals (history.numUndoable(), 1);
        expect (history.undoName().endsWith ("and 1 other"), history.undoName());

        // An end nobody began.
        (void) bridge.handleMessage (gesture ("filter1_cutoff", "end"));
        expectEquals (history.numUndoable(), 1);

        expect (history.undo());
        expectEquals (valueOf (processor, "fx_eq_band1_freq"), frequency);
        expectEquals (valueOf (processor, "fx_eq_band1_gain"), gain);

        // A drag that moved only one of the two is named after that one.
        (void) bridge.handleMessage (gesture ("fx_eq_band1_freq", "begin"));
        (void) bridge.handleMessage (gesture ("fx_eq_band1_gain", "begin"));
        (void) bridge.handleMessage (set ("fx_eq_band1_freq", 0.3f));
        (void) bridge.handleMessage (gesture ("fx_eq_band1_gain", "end"));
        (void) bridge.handleMessage (gesture ("fx_eq_band1_freq", "end"));
        expectEquals (history.undoName(), juce::String ("EQ Band 1 Frequency"));

        bridge.setEditHistory (nullptr);
    }

    void testAutomationIsNotAnEdit()
    {
        beginTest ("Automation and a controller are not recorded, and an undo leaves what they moved");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);

        auto* resonance = processor.getValueTreeState().getParameter ("filter1_resonance");

        // What a host's automation lane does.
        resonance->setValueNotifyingHost (0.9f);
        expect (! history.canUndo(), "automation is not an edit");

        // And a MIDI controller.
        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = params::indexOfParameter ("filter1_drive");
        expect (processor.getMidiControl().assign (mapping) == midi::AssignResult::added);
        expect (! history.canUndo(), "assigning a controller is not a change to the sound");

        const auto start = valueOf (processor, "filter1_cutoff");
        turn (bridge, "filter1_cutoff", 0.4f);

        resonance->setValueNotifyingHost (0.1f);

        expect (history.undo());
        expectEquals (valueOf (processor, "filter1_cutoff"), start);
        expectWithinAbsoluteError (valueOf (processor, "filter1_resonance"), 0.1f, 1.0e-3f, "the undo must not touch what automation moved");
        expectEquals (static_cast<int> (processor.getMidiControl().getMappings().size()), 1);

        bridge.setEditHistory (nullptr);
    }

    void testTheRestOfTheSound()
    {
        beginTest ("A curve drawn, a macro named and a routing dropped are steps");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);
        bridge.setLfoCurves (&processor.getLfoCurves());
        bridge.setMacroNames (&processor.getMacroNames());

        // A macro named, and named again.
        (void) bridge.handleMessage (R"({"type":"setMacroName","version":1,"macro":2,"name":"Wobble"})");
        (void) bridge.handleMessage (R"({"type":"setMacroName","version":1,"macro":2,"name":"Wub"})");
        expectEquals (history.numUndoable(), 2);
        expectEquals (history.undoName(), juce::String ("Macro 2 name"));

        // The same name again changes nothing.
        (void) bridge.handleMessage (R"({"type":"setMacroName","version":1,"macro":2,"name":"Wub"})");
        expectEquals (history.numUndoable(), 2);

        expect (history.undo());
        expectEquals (processor.getMacroNames().get (1), juce::String ("Wobble"));
        expect (history.undo());
        expectEquals (processor.getMacroNames().get (1), juce::String());
        expect (history.redo());
        expectEquals (processor.getMacroNames().get (1), juce::String ("Wobble"));

        // A curve drawn.
        const auto drawn = processor.getLfoCurves().get (0);
        (void) bridge.handleMessage (R"({"type":"setLfoCurve","version":1,"lfo":1,"points":[[0,0,0],[0.5,1,0],[1,0,0]]})");
        expectEquals (history.undoName(), juce::String ("LFO 1 curve"));
        expect (! (processor.getLfoCurves().get (0) == drawn));

        // Undoing the curve leaves the macro's name alone: a step puts back
        // only what it changed.
        expect (history.undo());
        expect (processor.getLfoCurves().get (0) == drawn, "the curve must come back");
        expectEquals (processor.getMacroNames().get (1), juce::String ("Wobble"));

        // A routing dropped onto a control: source, destination and depth
        // are one step.
        const auto source = valueOf (processor, "mod01_source");
        (void) bridge.handleMessage (R"({"type":"modulationAssign","version":1,"source":2,"id":"filter1_cutoff"})");
        expectEquals (history.undoName(), juce::String ("Add routing"));
        expect (valueOf (processor, "mod01_source") != source);

        expect (history.undo());
        expectEquals (valueOf (processor, "mod01_source"), source);

        bridge.setLfoCurves (nullptr);
        bridge.setMacroNames (nullptr);
        bridge.setEditHistory (nullptr);
    }

    void testAPresetLoad()
    {
        beginTest ("A preset loaded is a step, and undoing it brings back the sound that was there");

        FactoryOnlyLibrary presets;

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);
        bridge.setPresetLibrary (&presets.library);
        bridge.setMacroNames (&processor.getMacroNames());
        bridge.onStateReplaced = [&processor] { processor.notifyStateReplaced(); };

        // The sound being worked on: an edit, and a named macro.
        turn (bridge, "filter1_cutoff", 0.123f);
        (void) bridge.handleMessage (R"({"type":"setMacroName","version":1,"macro":1,"name":"Mine"})");

        midi::Mapping mapping;
        mapping.address.controller = 21;
        mapping.parameterIndex = params::indexOfParameter ("filter1_resonance");
        expect (processor.getMidiControl().assign (mapping) == midi::AssignResult::added);

        const auto bell = presets.idOf ("Glass Bell");
        expect (bell > 0, "the factory library must list Glass Bell");

        (void) bridge.handleMessage ("{\"type\":\"loadPreset\",\"version\":1,\"preset\":" + juce::String (bell) + "}");
        expectEquals (presets::getMetadata (processor.getValueTreeState()).name, juce::String ("Glass Bell"));
        expectEquals (history.undoName(), juce::String ("Load Glass Bell"));
        expectEquals (processor.getMacroNames().get (0), juce::String());

        const auto bellCutoff = valueOf (processor, "filter1_cutoff");
        const auto reloads = processor.getStateReloadCounter();

        expect (history.undo());
        expectWithinAbsoluteError (valueOf (processor, "filter1_cutoff"), 0.123f, 1.0e-3f);
        expectEquals (processor.getMacroNames().get (0), juce::String ("Mine"));
        expect (presets::getMetadata (processor.getValueTreeState()).name != "Glass Bell");
        expect (processor.getStateReloadCounter() != reloads, "the window must be told to resynchronise");

        // The controller is still assigned: a setting of the instrument, not
        // part of the sound.
        expectEquals (static_cast<int> (processor.getMidiControl().getMappings().size()), 1);

        expect (history.redo());
        expectEquals (valueOf (processor, "filter1_cutoff"), bellCutoff);
        expectEquals (presets::getMetadata (processor.getValueTreeState()).name, juce::String ("Glass Bell"));
        expectEquals (static_cast<int> (processor.getMidiControl().getMappings().size()), 1);

        bridge.onStateReplaced = {};
        bridge.setMacroNames (nullptr);
        bridge.setPresetLibrary (nullptr);
        bridge.setEditHistory (nullptr);
    }

    void testContent()
    {
        beginTest ("A wavetable loaded is a step once it lands, and only it");

        ApolloAudioProcessor processor;
        processor.getContentLibrary().setLocations ({});

        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);
        bridge.setUserContent (&processor.getUserContent(), &processor.getContentLibrary());
        processor.getContentLibrary().waitForScan();

        const auto table = valueOf (processor, "osc2_wavetable");

        (void) bridge.handleMessage (R"({"type":"loadBuiltInTable","version":1,"slot":"osc2","table":3})");
        expect (! history.canUndo(), "not a step until it lands");

        // A knob turned while it loads is its own step.
        turn (bridge, "osc2_position", 0.8f);
        expectEquals (history.numUndoable(), 1);

        settle (processor);
        expectEquals (history.numUndoable(), 2);
        expectEquals (history.undoName(), juce::String ("Load Organ"));
        expect (processor.getUserContent().getInfo (resources::ContentSlot::osc2).loaded);

        expect (history.undo());
        settle (processor);
        expect (! processor.getUserContent().getInfo (resources::ContentSlot::osc2).loaded, "the table must go");
        expectEquals (valueOf (processor, "osc2_wavetable"), table, "and the switch it set");
        expectWithinAbsoluteError (valueOf (processor, "osc2_position"), 0.8f, 1.0e-3f, "but not the knob turned while it loaded");

        expect (history.redo());
        settle (processor);
        expect (processor.getUserContent().getInfo (resources::ContentSlot::osc2).loaded, "redo must bring it back");
        expectEquals (processor.getUserContent().getInfo (resources::ContentSlot::osc2).source.name, juce::String ("Organ"));

        (void) bridge.handleMessage (R"({"type":"clearContent","version":1,"slot":"osc2"})");
        expectEquals (history.undoName(), juce::String ("Remove Organ"));

        expect (history.undo());
        settle (processor);
        expect (processor.getUserContent().getInfo (resources::ContentSlot::osc2).loaded);

        bridge.setUserContent (nullptr, nullptr);
        bridge.setEditHistory (nullptr);
    }

    void testOutsideLoadsEmptyIt()
    {
        beginTest ("A host loading state, or a program change, empties the history");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        turn (bridge, "filter1_cutoff", 0.3f);
        expect (history.canUndo());

        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        expect (! history.canUndo(), "a project load must empty the history");

        turn (bridge, "filter1_cutoff", 0.3f);
        expect (history.undo());
        expect (history.canRedo());

        processor.setCurrentProgram (1);
        processor.getProgramChanges().service();
        expect (! history.canUndo() && ! history.canRedo(), "a program change must empty the history");

        // Refused state changes nothing, and leaves the history alone.
        turn (bridge, "filter1_cutoff", 0.6f);
        const char junk[] = "not Apollo";
        processor.setStateInformation (junk, static_cast<int> (sizeof (junk)));
        expect (history.canUndo());

        bridge.setEditHistory (nullptr);
    }

    void testTheLimit()
    {
        beginTest ("No more than maxSteps steps are kept, the oldest going first");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);

        for (int i = 0; i < state::EditHistory::maxSteps + 20; ++i)
            turn (bridge, "filter1_cutoff", static_cast<float> (i % 2 == 0 ? 0.2 : 0.7));

        expectEquals (history.numUndoable(), state::EditHistory::maxSteps);

        while (history.undo())
            ;

        // Twenty steps fell off the front, so this is where the twenty-first
        // turn started: the twentieth turn's value.
        expectWithinAbsoluteError (valueOf (processor, "filter1_cutoff"), 0.7f, 1.0e-3f);

        bridge.setEditHistory (nullptr);
    }

    void testTheCommands()
    {
        beginTest ("The page asks to undo and redo, and is told what it can");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::StringArray sent;
        bridge.setOutboundHandler ([&sent] (const juce::String& message) { sent.add (message); });

        // No history attached: answered, never an error.
        expect (bridge.handleMessage (command ("undo")).contains ("\"history\""));

        bridge.setEditHistory (&history);

        juce::var reply;
        expect (juce::JSON::parse (bridge.handleMessage (command ("requestHistory")), reply).wasOk());
        expectEquals (reply["type"].toString(), juce::String ("history"));
        expectEquals (reply["undo"].toString(), juce::String());

        // The last history message pushed, or a void var if there was none.
        const auto lastPushed = [&sent]
        {
            for (int i = sent.size(); --i >= 0;)
            {
                juce::var message;

                if (juce::JSON::parse (sent[i], message).wasOk() && message["type"].toString() == "history")
                    return message;
            }

            return juce::var();
        };

        turn (bridge, "filter1_cutoff", 0.4f);

        // Pushed, without the page asking.
        expectEquals (lastPushed()["undo"].toString(), juce::String ("Filter 1 Cutoff"), "an edit must push the history");

        expectEquals (bridge.handleMessage (command ("undo")), juce::String());
        expectEquals (lastPushed()["redo"].toString(), juce::String ("Filter 1 Cutoff"));
        expectEquals (lastPushed()["undo"].toString(), juce::String());

        expectEquals (bridge.handleMessage (command ("redo")), juce::String());

        // Nothing to redo: answered with the history as it is.
        expect (juce::JSON::parse (bridge.handleMessage (command ("redo")), reply).wasOk());
        expectEquals (reply["undo"].toString(), juce::String ("Filter 1 Cutoff"));
        expectEquals (reply["redo"].toString(), juce::String());

        expect (! ui::parseMessage (R"({"type":"undo","version":2})").ok, "the version is checked");

        bridge.setEditHistory (nullptr);
        bridge.setOutboundHandler ({});
    }
};

static EditHistoryTests editHistoryTests;

} // namespace
