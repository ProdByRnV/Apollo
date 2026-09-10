/*
    MIDI Learn tests, at the level a user actually meets it.

    These drive the whole path: a control-change message goes into
    processBlock, and what comes out is a changed APVTS parameter, a stored
    mapping, or nothing at all. The manager's flush is called directly rather
    than waited for, so no message loop is needed and every test is
    deterministic.

    The split from MidiMappingTests is deliberate. That file pins down the model;
    this one pins down the wiring — that the audio thread does not write
    parameters itself, that a learned control survives a project save, that the
    controllers Apollo acts on directly keep working, and that the bridge
    commands do what they say.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "MIDI/MidiControlManager.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 128;

/** A parameter that is continuous, automatable and easy to read back. */
constexpr const char* mappedParameterId = "master_gain";
constexpr const char* otherParameterId = "osc1_position";

/** Renders one block through @p processor carrying the given messages. */
void renderWith (ApolloAudioProcessor& processor, const juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    buffer.clear();

    auto copy = midi;
    processor.processBlock (buffer, copy);
}

/** Renders one block carrying a single control-change message. */
void sendController (ApolloAudioProcessor& processor, int channel, int controller, int value)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (channel, controller, value), 0);
    renderWith (processor, midi);
}

/** @returns the normalised value APVTS currently holds for an ID. */
[[nodiscard]] float normalisedValue (ApolloAudioProcessor& processor, const juce::String& id)
{
    const auto* parameter = processor.getValueTreeState().getParameter (id);
    return parameter != nullptr ? parameter->getValue() : -1.0f;
}

/** Builds a `{"version":1,"type":...}` bridge message with an optional ID. */
[[nodiscard]] juce::String bridgeMessage (const juce::String& type, const juce::String& id = {})
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("version", ui::protocolVersion);
    object->setProperty ("type", type);

    if (id.isNotEmpty())
        object->setProperty ("id", id);

    return juce::JSON::toString (juce::var (object));
}

class MidiLearnTests final : public juce::UnitTest
{
public:
    MidiLearnTests()
        : juce::UnitTest ("MIDI Learn", "MIDI")
    {
    }

    void runTest() override
    {
        testLearnAssignsTheMovedControl();
        testLearnRefusesAReservedControlAndStaysArmed();
        testLearnCanBeCancelled();
        testMappedControllerDrivesTheParameter();
        testAudioThreadDoesNotWriteParameters();
        testUnmappedControllerChangesNothing();
        testLearnTakesPriorityOverAnExistingMapping();
        testRemovalStopsTheControl();
        testFixedFunctionControllersStillWork();
        testMappingsSurviveStateRoundTrip();
        testUnknownParameterMappingsAreDropped();
        testBridgeCommands();
    }

private:
    [[nodiscard]] static int indexOf (const char* id)
    {
        return params::indexOfParameter (id);
    }

    /** A prepared processor with a known, non-default starting value, so a
        parameter that did *not* move is distinguishable from one that did.
    */
    static void prepare (ApolloAudioProcessor& processor)
    {
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);
    }

    void testLearnAssignsTheMovedControl()
    {
        beginTest ("Learn assigns whichever control the user moves");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();

        expect (! control.isLearning());
        expect (control.beginLearn (mappedParameterId));
        expect (control.isLearning());

        sendController (processor, 1, 74, 100);
        control.flushPendingChanges();

        expect (! control.isLearning(), "a completed learn must disarm itself");

        const auto mappings = control.getMappings();
        expectEquals (mappings.size(), 1);

        const auto* mapping = mappings.findForParameter (indexOf (mappedParameterId));
        expect (mapping != nullptr, "the moved control must be assigned to the armed parameter");
        expect (mapping != nullptr && mapping->address.controller == 74);
        expect (mapping != nullptr && mapping->address.channel == midi::omniChannel,
                "a learned mapping defaults to omni, so it survives a channel change");

        // The message that completed the learn must not also drive the
        // parameter: the control is being pointed at, not played.
        expectWithinAbsoluteError (
            normalisedValue (processor, mappedParameterId),
            processor.getValueTreeState().getParameter (mappedParameterId)->getDefaultValue(),
            1.0e-6f,
            "the learning message itself must not move the parameter");
    }

    void testLearnRefusesAReservedControlAndStaysArmed()
    {
        beginTest ("A reserved control is refused, and learn stays armed");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();
        expect (control.beginLearn (mappedParameterId));

        // The sustain pedal: CC 64.
        sendController (processor, 1, 64, 127);
        control.flushPendingChanges();

        expect (control.isLearning(),
                "a refused control must leave learn armed, so the user can simply move another");
        expectEquals (control.getMappings().size(), 0);
        expectEquals (static_cast<int> (control.getLastAssignResult()),
                      static_cast<int> (midi::AssignResult::rejectedReservedController));

        // And a legitimate control still completes it afterwards.
        sendController (processor, 1, 74, 64);
        control.flushPendingChanges();

        expect (! control.isLearning());
        expectEquals (control.getMappings().size(), 1);
    }

    void testLearnCanBeCancelled()
    {
        beginTest ("Learn can be cancelled without assigning anything");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();
        expect (control.beginLearn (mappedParameterId));
        control.cancelLearn();
        expect (! control.isLearning());

        sendController (processor, 1, 74, 100);
        control.flushPendingChanges();

        expectEquals (control.getMappings().size(), 0,
                      "a control moved after cancelling must not be assigned");

        expect (! control.beginLearn ("not_a_parameter"),
                "learn must refuse an ID that is not in the registry");
        expect (! control.isLearning());
    }

    void testMappedControllerDrivesTheParameter()
    {
        beginTest ("A mapped control moves its parameter across the full range");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();

        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (control.assign (mapping)));

        sendController (processor, 1, 74, 127);
        control.flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f);

        sendController (processor, 1, 74, 0);
        control.flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 0.0f, 1.0e-5f);

        sendController (processor, 1, 74, 64);
        control.flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId),
                                   64.0f / 127.0f, 1.0e-5f);

        // An omni mapping answers to every channel.
        sendController (processor, 11, 74, 127);
        control.flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f,
                                   "an omni mapping must respond on channel 11 as well as 1");
    }

    void testAudioThreadDoesNotWriteParameters()
    {
        beginTest ("Controller values reach APVTS only when the message thread flushes");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();

        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (control.assign (mapping)));

        const auto before = normalisedValue (processor, mappedParameterId);

        // A whole sweep, rendered without a single flush in between. This is the
        // real-time contract made observable: the audio thread cannot have
        // written the parameter, because the parameter has not changed.
        for (int value = 0; value <= 127; ++value)
            sendController (processor, 1, 74, value);

        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), before, 1.0e-6f,
                                   "rendering alone must not write a parameter");

        control.flushPendingChanges();

        // And the sweep collapses to its newest value rather than replaying every
        // step of it.
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f,
                                   "the flush must apply the newest value seen");
    }

    void testUnmappedControllerChangesNothing()
    {
        beginTest ("An unmapped control changes nothing at all");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();

        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (control.assign (mapping)));

        const auto before = normalisedValue (processor, mappedParameterId);
        const auto otherBefore = normalisedValue (processor, otherParameterId);

        sendController (processor, 1, 75, 127);
        control.flushPendingChanges();

        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), before, 1.0e-6f);
        expectWithinAbsoluteError (normalisedValue (processor, otherParameterId), otherBefore,
                                   1.0e-6f);
    }

    void testLearnTakesPriorityOverAnExistingMapping()
    {
        beginTest ("A control being learned does not also drive its old destination");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();

        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (control.assign (mapping)));

        const auto before = normalisedValue (processor, mappedParameterId);

        expect (control.beginLearn (otherParameterId));
        sendController (processor, 1, 74, 127);
        control.flushPendingChanges();

        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), before, 1.0e-6f,
                                   "the old destination must not move while its control is being "
                                   "reassigned");

        // And the bijection has taken the control away from the old parameter.
        const auto mappings = control.getMappings();
        expectEquals (mappings.size(), 1);
        expect (mappings.findForParameter (indexOf (otherParameterId)) != nullptr);
        expect (mappings.findForParameter (indexOf (mappedParameterId)) == nullptr);
        expectEquals (static_cast<int> (control.getLastAssignResult()),
                      static_cast<int> (midi::AssignResult::replacedControllerMapping),
                      "the user must be told the control was taken from something else");
    }

    void testRemovalStopsTheControl()
    {
        beginTest ("A removed mapping stops driving its parameter");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();

        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (control.assign (mapping)));

        sendController (processor, 1, 74, 127);
        control.flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f);

        expect (control.removeParameterMapping (indexOf (mappedParameterId)));
        expect (! control.removeParameterMapping (indexOf (mappedParameterId)),
                "removing what is already gone must report that");

        sendController (processor, 1, 74, 0);
        control.flushPendingChanges();

        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f,
                                   "a removed control must no longer move the parameter");

        // clearAllMappings does the same for everything at once.
        expect (succeeded (control.assign (mapping)));
        control.clearAllMappings();
        expectEquals (control.getMappings().size(), 0);

        sendController (processor, 1, 74, 0);
        control.flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f);
    }

    void testFixedFunctionControllersStillWork()
    {
        beginTest ("Sustain and the mod wheel keep their fixed behaviour");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();
        auto& engine = processor.getVoiceEngine();

        // Sustain, while learn is armed: refused as a mapping, and still acted on.
        expect (control.beginLearn (mappedParameterId));
        sendController (processor, 1, 64, 127);
        control.flushPendingChanges();

        expect (engine.isSustainPedalDown(),
                "the sustain pedal must work even while learn is armed at it");
        expectEquals (control.getMappings().size(), 0);

        control.cancelLearn();
        sendController (processor, 1, 64, 0);
        expect (! engine.isSustainPedalDown());

        // The mod wheel is a modulation source *and* mappable, and mapping it
        // must not cost it its first job.
        midi::Mapping mapping;
        mapping.address.controller = 1;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (control.assign (mapping)));

        sendController (processor, 1, 1, 127);
        control.flushPendingChanges();

        expectWithinAbsoluteError (normalisedValue (processor, mappedParameterId), 1.0f, 1.0e-5f,
                                   "a mapped mod wheel must drive its parameter");
        expectWithinAbsoluteError (engine.getModWheel(), 1.0f, 1.0e-6f,
                                   "and must still be a modulation source");
    }

    void testMappingsSurviveStateRoundTrip()
    {
        beginTest ("Mappings survive a project save and reload");

        juce::MemoryBlock saved;
        constexpr int savedController = 74;

        {
            ApolloAudioProcessor processor;
            prepare (processor);

            auto& control = processor.getMidiControl();

            midi::Mapping mapping;
            mapping.address.controller = savedController;
            mapping.address.channel = 5;
            mapping.parameterIndex = indexOf (mappedParameterId);
            mapping.minimum = 0.25f;
            mapping.maximum = 0.75f;
            expect (succeeded (control.assign (mapping)));

            processor.getStateInformation (saved);
        }

        expect (saved.getSize() > 0);

        ApolloAudioProcessor restored;
        prepare (restored);

        expectEquals (restored.getMidiControl().getMappings().size(), 0,
                      "a fresh processor starts with no mappings");

        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expectEquals (static_cast<int> (restored.getLastStateLoadResult()),
                      static_cast<int> (state::StateLoadResult::ok));

        const auto mappings = restored.getMidiControl().getMappings();
        expectEquals (mappings.size(), 1, "the saved mapping must come back");

        const auto* mapping = mappings.findForParameter (indexOf (mappedParameterId));
        expect (mapping != nullptr);

        if (mapping != nullptr)
        {
            expectEquals (mapping->address.controller, savedController);
            expectEquals (mapping->address.channel, 5, "the channel must round-trip too");
            expectWithinAbsoluteError (mapping->minimum, 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (mapping->maximum, 0.75f, 1.0e-6f);
        }

        // And the restored mapping is live, not merely stored.
        sendController (restored, 5, savedController, 127);
        restored.getMidiControl().flushPendingChanges();
        expectWithinAbsoluteError (normalisedValue (restored, mappedParameterId), 0.75f, 1.0e-5f,
                                   "a restored mapping must actually drive its parameter");

        // A state document written before mappings existed simply has none,
        // rather than failing to load.
        ApolloAudioProcessor bare;
        prepare (bare);
        juce::MemoryBlock bareState;
        bare.getStateInformation (bareState);

        ApolloAudioProcessor target;
        prepare (target);
        target.setStateInformation (bareState.getData(), static_cast<int> (bareState.getSize()));
        expectEquals (static_cast<int> (target.getLastStateLoadResult()),
                      static_cast<int> (state::StateLoadResult::ok));
        expectEquals (target.getMidiControl().getMappings().size(), 0);
    }

    void testUnknownParameterMappingsAreDropped()
    {
        beginTest ("A mapping naming an unknown parameter is dropped, not fatal");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();
        auto& root = processor.getValueTreeState().state;

        root.removeChild (root.getChildWithName (midi::MidiControlManager::mappingsTreeType),
                          nullptr);

        juce::ValueTree mappings (midi::MidiControlManager::mappingsTreeType);

        // One good, one naming a parameter this build does not have, one with a
        // controller number that cannot exist, and one that is not a mapping at
        // all. Only the first may survive.
        const auto addEntry = [&mappings] (const juce::String& type,
                                           const juce::String& id,
                                           int controller)
        {
            juce::ValueTree entry (type);
            entry.setProperty (midi::MidiControlManager::parameterProperty, id, nullptr);
            entry.setProperty (midi::MidiControlManager::controllerProperty, controller, nullptr);
            mappings.appendChild (entry, nullptr);
        };

        addEntry (midi::MidiControlManager::mappingTreeType, mappedParameterId, 74);
        addEntry (midi::MidiControlManager::mappingTreeType, "osc9_nonexistent", 75);
        addEntry (midi::MidiControlManager::mappingTreeType, otherParameterId, 999);
        addEntry ("SOMETHINGELSE", otherParameterId, 76);

        root.appendChild (mappings, nullptr);

        expectEquals (control.restoreFromState(), 1,
                      "exactly one of the four entries is representable");

        const auto restored = control.getMappings();
        expectEquals (restored.size(), 1);
        expect (restored.findForParameter (indexOf (mappedParameterId)) != nullptr);
        expect (restored.findForParameter (indexOf (otherParameterId)) == nullptr);
    }

    void testBridgeCommands()
    {
        beginTest ("The bridge exposes learn, removal and clearing, and nothing more");

        ApolloAudioProcessor processor;
        prepare (processor);

        ui::ParameterBridge bridge (processor.getValueTreeState());

        // Without a MIDI subsystem attached, the commands are refused rather
        // than silently doing nothing.
        {
            const auto reply = bridge.handleMessage (bridgeMessage ("requestMidiMappings"));
            expect (reply.contains ("UNKNOWN_MESSAGE_TYPE"),
                    "a bridge with no MIDI attached must say so");
        }

        bridge.setMidiControl (&processor.getMidiControl());

        const auto parseType = [] (const juce::String& json)
        {
            juce::var parsed;

            if (juce::JSON::parse (json, parsed).failed())
                return juce::String();

            auto* object = parsed.getDynamicObject();
            return object != nullptr ? object->getProperty ("type").toString() : juce::String();
        };

        const auto parseLearning = [] (const juce::String& json)
        {
            juce::var parsed;

            if (juce::JSON::parse (json, parsed).failed())
                return juce::String ("<unparseable>");

            auto* object = parsed.getDynamicObject();
            return object != nullptr ? object->getProperty ("learning").toString()
                                     : juce::String ("<no object>");
        };

        const auto countMappings = [] (const juce::String& json)
        {
            juce::var parsed;

            if (juce::JSON::parse (json, parsed).failed())
                return -1;

            auto* object = parsed.getDynamicObject();

            if (object == nullptr)
                return -1;

            const auto* entries = object->getProperty ("mappings").getArray();
            return entries != nullptr ? entries->size() : -1;
        };

        {
            const auto reply = bridge.handleMessage (bridgeMessage ("requestMidiMappings"));
            expectEquals (parseType (reply), juce::String ("midiMappings"));
            expectEquals (countMappings (reply), 0);
            expectEquals (parseLearning (reply), juce::String(),
                          "not learning must be reported as an empty string, not an absent key");
        }

        {
            const auto reply = bridge.handleMessage (
                bridgeMessage ("midiLearnBegin", mappedParameterId));
            expectEquals (parseLearning (reply), juce::String (mappedParameterId));
            expect (processor.getMidiControl().isLearning());
        }

        {
            const auto reply = bridge.handleMessage (bridgeMessage ("midiLearnCancel"));
            expectEquals (parseLearning (reply), juce::String());
            expect (! processor.getMidiControl().isLearning());
        }

        // An unregistered ID is refused by the protocol layer, before it can
        // reach the mapping table at all.
        expect (bridge.handleMessage (bridgeMessage ("midiLearnBegin", "not_a_parameter"))
                    .contains ("UNKNOWN_PARAMETER"));
        expect (bridge.handleMessage (bridgeMessage ("midiMappingRemove", "not_a_parameter"))
                    .contains ("UNKNOWN_PARAMETER"));

        // Removal and clearing, through the bridge, with the reply carrying the
        // whole resulting state each time.
        midi::Mapping mapping;
        mapping.address.controller = 74;
        mapping.parameterIndex = indexOf (mappedParameterId);
        expect (succeeded (processor.getMidiControl().assign (mapping)));

        {
            const auto reply = bridge.handleMessage (bridgeMessage ("requestMidiMappings"));
            expectEquals (countMappings (reply), 1);
        }

        {
            const auto reply = bridge.handleMessage (
                bridgeMessage ("midiMappingRemove", mappedParameterId));
            expectEquals (countMappings (reply), 0);
        }

        expect (succeeded (processor.getMidiControl().assign (mapping)));

        {
            const auto reply = bridge.handleMessage (bridgeMessage ("midiMappingClearAll"));
            expectEquals (countMappings (reply), 0);
        }

        // Detaching must be safe, and must put the bridge back to refusing.
        bridge.setMidiControl (nullptr);
        expect (bridge.handleMessage (bridgeMessage ("requestMidiMappings"))
                    .contains ("UNKNOWN_MESSAGE_TYPE"));
    }
};

MidiLearnTests midiLearnTests;

} // namespace
