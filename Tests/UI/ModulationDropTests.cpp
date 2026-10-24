/*
    Drag-and-drop modulation (Phase 13h-4, ADR-0108).

    The page says which source was dropped on which control; everything that
    decides anything happens here. So this is where it is tested: which
    controls are destinations, which slot a drop takes, what it refuses, that
    the slot written is the slot the engine plays, and that the page is told
    which knobs accept a drop by the engine rather than by a table of its own.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <set>
#include <string_view>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ModulationTargets.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "UI/BridgeProtocol.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;

juce::String slotId (int slot, const char* part)
{
    return "mod" + juce::String (slot).paddedLeft ('0', 2) + "_" + part;
}

juce::String assign (int source, const juce::String& id)
{
    return R"({"type":"modulationAssign","version":1,"source":)" + juce::String (source)
         + R"(,"id":")" + id + R"("})";
}

juce::String clear (int slot)
{
    return R"({"type":"modulationClear","version":1,"slot":)" + juce::String (slot) + "}";
}

/** The parsed reply, held whole so that a property read from it cannot
    outlive the object it came from. */
struct Reply
{
    juce::var parsed;

    explicit Reply (const juce::String& json) { (void) juce::JSON::parse (json, parsed); }

    [[nodiscard]] juce::String text (const char* property) const { return parsed.getProperty (property, {}).toString(); }
    [[nodiscard]] int number (const char* property) const { return static_cast<int> (parsed.getProperty (property, -99)); }
};

class ModulationDropTests final : public juce::UnitTest
{
public:
    ModulationDropTests()
        : juce::UnitTest ("Drag-and-drop modulation", "UI")
    {
    }

    void runTest() override
    {
        testTheTableNamesRealDestinations();
        testWhichSlotADropTakes();
        testMalformedDropsAreRefused();
        testADropThroughTheBridge();
        testClearingASlot();
        testTheMetadataNamesTheDestinations();
        testADropIsHeard();
    }

private:
    void testTheTableNamesRealDestinations()
    {
        beginTest ("Every destination with a control of its own is in the table, once");

        std::set<int> destinations;
        std::set<std::string_view> ids;

        for (const auto& target : params::modulationTargets)
        {
            const auto id = params::toJuceString (target.parameterId);

            expect (params::findParameter (target.parameterId) != nullptr, id + ": not a parameter");
            expect (destinations.insert (static_cast<int> (target.destination)).second, id + ": destination twice");
            expect (ids.insert (target.parameterId).second, id + ": control twice");

            expect (params::destinationForParameter (target.parameterId) == target.destination, id);
            expect (params::parameterForDestination (target.destination) == target.parameterId, id);
        }

        for (int d = 1; d < static_cast<int> (dsp::ModDestination::count); ++d)
        {
            const auto destination = static_cast<dsp::ModDestination> (d);
            const auto hasNoControl = destination == dsp::ModDestination::allPitch
                                   || destination == dsp::ModDestination::amplitude;

            expect (destinations.count (d) == (hasNoControl ? 0u : 1u),
                    "destination " + juce::String (d));
        }

        expect (params::destinationForParameter ("osc1_detune") == dsp::ModDestination::none);
        expect (params::destinationForParameter ("mod01_depth") == dsp::ModDestination::none);
        expect (params::destinationForParameter ("") == dsp::ModDestination::none);
        expect (params::parameterForDestination (dsp::ModDestination::allPitch).empty());
        expect (params::parameterForDestination (dsp::ModDestination::none).empty());

        expect (params::sourceName (dsp::ModSource::lfo1) == "LFO 1");
        expect (params::sourceName (dsp::ModSource::macro4) == "Macro 4");
        expect (params::sourceName (dsp::ModSource::count).empty());
    }

    void testWhichSlotADropTakes()
    {
        beginTest ("A drop takes the first empty slot, never a half-made one, and never doubles a routing");

        using dsp::ModDestination;
        using dsp::ModSource;
        using params::DropOutcome;

        dsp::ModulationRouting routing;

        auto plan = params::planDrop (routing, ModSource::lfo1, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::assigned && plan.slot == 0);

        routing.slots[0] = { ModSource::lfo1, ModDestination::filter1Cutoff, 0.4f, ModSource::none };

        plan = params::planDrop (routing, ModSource::lfo1, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::alreadyRouted && plan.slot == 0, "the same pairing again");

        plan = params::planDrop (routing, ModSource::lfo2, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::assigned && plan.slot == 1, "another source");

        // Somebody's unfinished routings: a source with nowhere to go, and a
        // destination with nothing to move it.
        routing.slots[1] = { ModSource::envelope2, ModDestination::none, 0.0f, ModSource::none };
        routing.slots[2] = { ModSource::none, ModDestination::osc1Level, 0.5f, ModSource::none };

        plan = params::planDrop (routing, ModSource::lfo2, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::assigned && plan.slot == 3, "half-made slots are not free");

        // A pairing with no depth moves nothing, and a drop brings it back.
        routing.slots[0].depth = 0.0f;
        plan = params::planDrop (routing, ModSource::lfo1, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::revived && plan.slot == 0);

        // A different via is still the same pairing: a second slot would add
        // depth that nothing on screen shows.
        routing.slots[0] = { ModSource::lfo1, ModDestination::filter1Cutoff, -0.3f, ModSource::modWheel };
        plan = params::planDrop (routing, ModSource::lfo1, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::alreadyRouted && plan.slot == 0);

        for (auto& slot : routing.slots)
            if (slot.source == ModSource::none && slot.destination == ModDestination::none)
                slot = { ModSource::velocity, ModDestination::osc2Pan, 0.1f, ModSource::none };

        plan = params::planDrop (routing, ModSource::lfo3, ModDestination::filter1Cutoff);
        expect (plan.outcome == DropOutcome::matrixFull && plan.slot == -1, "a full matrix");

        plan = params::planDrop (routing, ModSource::velocity, ModDestination::osc2Pan);
        expect (plan.outcome == DropOutcome::alreadyRouted && plan.slot == 3,
                "a full matrix still finds a pairing it holds");
    }

    void testMalformedDropsAreRefused()
    {
        beginTest ("Drops that are not routings are refused before anything is read");

        const auto parsed = ui::parseMessage (assign (5, "filter1_cutoff"));
        expect (parsed.ok);
        expect (parsed.command.type == ui::BridgeCommandType::modulationAssign);
        expect (parsed.command.modulationSource == 5);
        expect (parsed.command.parameterId == "filter1_cutoff");

        const auto refusedWith = [this] (const juce::String& json, ui::BridgeErrorCode code)
        {
            const auto result = ui::parseMessage (json);
            expect (! result.ok && result.error == code, json);
        };

        using ui::BridgeErrorCode;

        refusedWith (assign (5, "osc1_detune"), BridgeErrorCode::invalidModulation);
        refusedWith (assign (5, "mod01_depth"), BridgeErrorCode::invalidModulation);
        refusedWith (assign (5, "no_such_thing"), BridgeErrorCode::unknownParameter);
        refusedWith (assign (0, "filter1_cutoff"), BridgeErrorCode::invalidModulation);
        refusedWith (assign (20, "filter1_cutoff"), BridgeErrorCode::invalidModulation);
        refusedWith (assign (-1, "filter1_cutoff"), BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationAssign","version":1,"source":5.5,"id":"filter1_cutoff"})",
                     BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationAssign","version":1,"source":"5","id":"filter1_cutoff"})",
                     BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationAssign","version":1,"id":"filter1_cutoff"})",
                     BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationAssign","version":1,"source":5})",
                     BridgeErrorCode::malformedMessage);

        const auto cleared = ui::parseMessage (clear (16));
        expect (cleared.ok && cleared.command.type == ui::BridgeCommandType::modulationClear);
        expect (cleared.command.modulationSlot == 15, "slots are numbered from 1 on the wire");

        refusedWith (clear (0), BridgeErrorCode::invalidModulation);
        refusedWith (clear (17), BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationClear","version":1,"slot":2.5})", BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationClear","version":1,"slot":null})", BridgeErrorCode::invalidModulation);
        refusedWith (R"({"type":"modulationClear","version":1})", BridgeErrorCode::invalidModulation);
    }

    void testADropThroughTheBridge()
    {
        beginTest ("A drop writes the slot the engine reads, and says which");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        const auto plain = [&apvts] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

        // Something left in an empty slot's via is not carried into a routing.
        if (auto* via = apvts.getParameter (slotId (1, "via")))
            via->setValueNotifyingHost (via->convertTo0to1 (11.0f));

        Reply first (bridge.handleMessage (assign (5, "filter1_cutoff")));
        expectEquals (first.text ("type"), juce::String ("modulationRouting"));
        expectEquals (first.text ("status"), juce::String ("ASSIGNED"));
        expectEquals (first.number ("slot"), 1);
        expect (first.text ("statusMessage").contains ("LFO 1 → Filter 1 Cutoff"), first.text ("statusMessage"));
        expect (first.text ("statusMessage").contains ("+25%"), first.text ("statusMessage"));

        expectEquals (plain (slotId (1, "source")), 5.0f);
        expectEquals (plain (slotId (1, "destination")), 12.0f);
        expectWithinAbsoluteError (plain (slotId (1, "depth")), params::dropDepth, 1.0e-6f);
        expectEquals (plain (slotId (1, "via")), 0.0f);

        // The same drop again changes nothing.
        if (auto* depth = apvts.getParameter (slotId (1, "depth")))
            depth->setValueNotifyingHost (depth->convertTo0to1 (-0.6f));

        Reply again (bridge.handleMessage (assign (5, "filter1_cutoff")));
        expectEquals (again.text ("status"), juce::String ("ALREADY_ROUTED"));
        expectEquals (again.number ("slot"), 1);
        expect (again.text ("statusMessage").contains ("-60%"), again.text ("statusMessage"));
        expectWithinAbsoluteError (plain (slotId (1, "depth")), -0.6f, 1.0e-6f);

        // A half-made slot is passed over.
        if (auto* source = apvts.getParameter (slotId (2, "source")))
            source->setValueNotifyingHost (source->convertTo0to1 (2.0f));

        Reply second (bridge.handleMessage (assign (16, "osc2_pan")));
        expectEquals (second.number ("slot"), 3);
        expect (second.text ("statusMessage").contains ("Macro 1 → Osc 2 Pan"), second.text ("statusMessage"));
        expectEquals (plain (slotId (3, "source")), 16.0f);
        expectEquals (plain (slotId (3, "destination")), 9.0f);
        expectEquals (plain (slotId (2, "destination")), 0.0f, "the half-made slot was left alone");

        // A pairing with no depth is brought back rather than doubled.
        if (auto* depth = apvts.getParameter (slotId (3, "depth")))
            depth->setValueNotifyingHost (depth->convertTo0to1 (0.0f));

        Reply revived (bridge.handleMessage (assign (16, "osc2_pan")));
        expectEquals (revived.text ("status"), juce::String ("ASSIGNED"));
        expectEquals (revived.number ("slot"), 3);
        expectWithinAbsoluteError (plain (slotId (3, "depth")), params::dropDepth, 1.0e-6f);

        // Filled to the last slot, then refused.
        for (int slot = 4; slot <= dsp::maxModulationSlots; ++slot)
        {
            Reply filled (bridge.handleMessage (assign (9, params::toJuceString (params::modulationTargets[static_cast<std::size_t> (slot)].parameterId))));
            expectEquals (filled.number ("slot"), slot);
        }

        Reply full (bridge.handleMessage (assign (6, "filter2_cutoff")));
        expectEquals (full.text ("status"), juce::String ("MATRIX_FULL"));
        expectEquals (full.number ("slot"), 0);
        expect (full.text ("statusMessage").contains ("All 16 slots"), full.text ("statusMessage"));

        // Nothing a drop says names a path or a parameter id.
        for (const auto* reply : { &first, &again, &second, &revived, &full })
            expect (! reply->text ("statusMessage").contains ("_"), reply->text ("statusMessage"));
    }

    void testClearingASlot()
    {
        beginTest ("Clearing a slot empties it, says what it did, and frees it for the next drop");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        const auto plain = [&apvts] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

        (void) bridge.handleMessage (assign (1, "osc1_level"));
        (void) bridge.handleMessage (assign (12, "filter2_resonance"));

        if (auto* via = apvts.getParameter (slotId (2, "via")))
            via->setValueNotifyingHost (via->convertTo0to1 (11.0f));

        Reply cleared (bridge.handleMessage (clear (2)));
        expectEquals (cleared.text ("status"), juce::String ("CLEARED"));
        expectEquals (cleared.number ("slot"), 2);
        expect (cleared.text ("statusMessage").contains ("Pitch Bend no longer moves Filter 2 Resonance"),
                cleared.text ("statusMessage"));

        for (const auto* part : { "source", "destination", "depth", "via" })
            expectEquals (plain (slotId (2, part)), 0.0f, part);

        expectEquals (plain (slotId (1, "source")), 1.0f, "the other slot is untouched");

        Reply emptyAgain (bridge.handleMessage (clear (2)));
        expectEquals (emptyAgain.text ("statusMessage"), juce::String ("Slot 2 is empty."));

        Reply next (bridge.handleMessage (assign (7, "sub_level")));
        expectEquals (next.number ("slot"), 2);
    }

    void testTheMetadataNamesTheDestinations()
    {
        beginTest ("The parameter metadata says which control each destination is");

        juce::var parsed;
        expect (juce::JSON::parse (ui::makeParameterMetadataMessage(), parsed).wasOk());

        const auto* entries = parsed.getProperty ("parameters", {}).getArray();
        expect (entries != nullptr);

        if (entries == nullptr)
            return;

        int named = 0;

        for (const auto& entry : *entries)
        {
            const auto id = entry.getProperty ("id", {}).toString();
            const auto destination = entry.getProperty ("destination", -1);

            expect (destination.isInt(), id + ": no destination field");
            expectEquals (static_cast<int> (destination),
                          static_cast<int> (params::destinationForParameter (id.toStdString())), id);

            if (static_cast<int> (destination) != 0)
                ++named;
        }

        expectEquals (named, static_cast<int> (params::modulationTargets.size()));
    }

    void testADropIsHeard()
    {
        beginTest ("A routing made by a drop moves the sound");

        // Velocity to oscillator 1's pan: a held note leans right by a known
        // amount, which a level or a cutoff would not show as plainly.
        const auto render = [] (bool drop)
        {
            ApolloAudioProcessor processor;
            ui::ParameterBridge bridge (processor.getValueTreeState());

            if (drop)
                (void) bridge.handleMessage (assign (9, "osc1_pan"));

            constexpr int blockSize = 256;
            processor.setRateAndBufferSizeDetails (testSampleRate, blockSize);
            processor.prepareToPlay (testSampleRate, blockSize);

            juce::AudioBuffer<float> block (2, blockSize);
            double left = 0.0;
            double right = 0.0;

            for (int b = 0; b < 80; ++b)
            {
                block.clear();
                juce::MidiBuffer midi;

                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);

                processor.processBlock (block, midi);

                if (b >= 20)
                {
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const auto l = static_cast<double> (block.getSample (0, i));
                        const auto r = static_cast<double> (block.getSample (1, i));
                        left += l * l;
                        right += r * r;
                    }
                }
            }

            processor.releaseResources();
            return std::pair { left, right };
        };

        const auto [plainLeft, plainRight] = render (false);
        const auto [droppedLeft, droppedRight] = render (true);

        expect (plainLeft > 0.0 && droppedLeft > 0.0, "both renders sound");
        expectWithinAbsoluteError (plainRight / plainLeft, 1.0, 0.01, "centred without the routing");
        expect (droppedRight / droppedLeft > 1.2,
                "leaning right with it: " + juce::String (droppedRight / droppedLeft, 3));
    }
};

static ModulationDropTests modulationDropTests;

} // namespace
