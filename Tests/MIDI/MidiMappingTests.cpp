/*
    MIDI mapping model tests.

    Everything here is exercised against the JUCE-free core in
    Source/MIDI/MidiMapping.h: no processor, no APVTS, no message loop and no
    MIDI device. That is the reason the model was kept separate from the manager
    that drives it — the conflict policy and the value scaling are where the
    behaviour a user notices actually lives, and they are cheap to pin down
    exhaustively when nothing has to be constructed to reach them.

    The publication channel is tested here too, including the case that matters:
    a producer that has published more tables than the ring holds.
*/

#include <juce_core/juce_core.h>

#include "MIDI/MidiMapping.h"
#include "Parameters/ParameterDefinitions.h"

#include <limits>

using namespace apollo;

namespace
{

/** @returns the @p index'th assignable controller number.

    Counting from 2 and stepping over the reserved set, so a test that wants a
    great many distinct controllers does not have to know where the holes are.
*/
[[nodiscard]] int assignableController (int index)
{
    int controller = 2;

    for (int remaining = index; remaining > 0; --remaining)
    {
        do
            ++controller;
        while (midi::isReservedController (controller));
    }

    return controller;
}

/** @returns a mapping of a controller to a parameter, with the full range. */
[[nodiscard]] midi::Mapping makeMapping (int controller, int parameterIndex,
                                         int channel = midi::omniChannel)
{
    midi::Mapping mapping;
    mapping.address.controller = controller;
    mapping.address.channel = channel;
    mapping.parameterIndex = parameterIndex;
    return mapping;
}

class MidiMappingTests final : public juce::UnitTest
{
public:
    MidiMappingTests()
        : juce::UnitTest ("MIDI mapping model", "MIDI")
    {
    }

    void runTest() override
    {
        testValidation();
        testReservedControllers();
        testValueScaling();
        testAssignAndLookup();
        testBijectionIsEnforced();
        testUnchangedIsReported();
        testChannelSpecificBeatsOmni();
        testRemoval();
        testCapacity();
        testPublicationChannel();
        testPublicationRingOverflow();
    }

private:
    static constexpr int aParameter = 0;
    static constexpr int anotherParameter = 1;

    void testValidation()
    {
        beginTest ("Addresses and mappings validate their own ranges");

        midi::MappingTable table;

        expect (! succeeded (table.assign (makeMapping (-1, aParameter))),
                "a negative controller must be refused");
        expect (! succeeded (table.assign (makeMapping (128, aParameter))),
                "controller 128 is outside the 0-127 range");
        expect (! succeeded (table.assign (makeMapping (7, aParameter, 17))),
                "channel 17 does not exist");
        expect (! succeeded (table.assign (makeMapping (7, aParameter, -1))),
                "a negative channel must be refused");
        expect (! succeeded (table.assign (makeMapping (7, -1))),
                "a negative parameter index must be refused");
        expect (! succeeded (table.assign (
                    makeMapping (7, static_cast<int> (params::parameterCount())))),
                "a parameter index past the registry must be refused");

        expectEquals (table.size(), 0, "no refusal may leave anything behind");

        // The scaling ends arrive from the frontend and are the one part of a
        // mapping that can be an arbitrary number.
        auto nonFinite = makeMapping (7, aParameter);
        nonFinite.maximum = std::numeric_limits<float>::infinity();
        expectEquals (static_cast<int> (table.assign (nonFinite)),
                      static_cast<int> (midi::AssignResult::rejectedInvalidRange),
                      "an infinite range end must be refused, not clamped");

        auto outOfRange = makeMapping (7, aParameter);
        outOfRange.minimum = -0.5f;
        expectEquals (static_cast<int> (table.assign (outOfRange)),
                      static_cast<int> (midi::AssignResult::rejectedInvalidRange),
                      "a normalised end outside 0-1 must be refused");

        expectEquals (table.size(), 0);

        // Every channel in the legal range, plus omni, must be accepted.
        for (int channel = midi::firstChannel; channel <= midi::lastChannel; ++channel)
            expect (midi::isValidChannel (channel));

        expect (midi::isValidChannel (midi::omniChannel));
        expect (! midi::isValidChannel (midi::lastChannel + 1));
    }

    void testReservedControllers()
    {
        beginTest ("Controllers with a fixed meaning cannot be mapped");

        midi::MappingTable table;

        expectEquals (static_cast<int> (table.assign (makeMapping (64, aParameter))),
                      static_cast<int> (midi::AssignResult::rejectedReservedController),
                      "CC 64 is the sustain pedal and Apollo acts on it directly");

        for (int controller = 120; controller <= 127; ++controller)
            expect (midi::isReservedController (controller),
                    "the channel-mode messages are commands, not controls");

        // RPN plumbing: parameter-number halves and data entries, which Apollo
        // acts on (ADR-0045) and which carry no control value of their own.
        for (const int controller : { 6, 38, 98, 99, 100, 101 })
        {
            expect (midi::isReservedController (controller),
                    "CC " + juce::String (controller) + " carries RPN plumbing");

            expectEquals (static_cast<int> (table.assign (makeMapping (controller, aParameter))),
                          static_cast<int> (midi::AssignResult::rejectedReservedController));
        }

        // CC 1 is deliberately mappable: the mod wheel is a modulation source in
        // the matrix, and mapping it as well is a legitimate request.
        expect (! midi::isReservedController (1));
        expect (succeeded (table.assign (makeMapping (1, aParameter))));

        expect (! midi::isReservedController (0));
        expect (! midi::isReservedController (119));
    }

    void testValueScaling()
    {
        beginTest ("Controller values scale across the assigned range");

        auto mapping = makeMapping (7, aParameter);

        expectWithinAbsoluteError (mapping.normalisedFor (0), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (mapping.normalisedFor (127), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (mapping.normalisedFor (64), 64.0f / 127.0f, 1.0e-6f);

        // Out-of-range controller values are clamped rather than extrapolated: a
        // malformed message must not produce a parameter value outside 0-1.
        expectWithinAbsoluteError (mapping.normalisedFor (-5), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (mapping.normalisedFor (999), 1.0f, 1.0e-6f);

        // A partial range.
        mapping.minimum = 0.25f;
        mapping.maximum = 0.75f;
        expectWithinAbsoluteError (mapping.normalisedFor (0), 0.25f, 1.0e-6f);
        expectWithinAbsoluteError (mapping.normalisedFor (127), 0.75f, 1.0e-6f);

        // Inversion is min > max, with no separate flag to get out of step.
        mapping.minimum = 1.0f;
        mapping.maximum = 0.0f;
        expectWithinAbsoluteError (mapping.normalisedFor (0), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (mapping.normalisedFor (127), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (mapping.normalisedFor (64), 1.0f - 64.0f / 127.0f, 1.0e-6f);
    }

    void testAssignAndLookup()
    {
        beginTest ("A mapping is found by controller and by parameter");

        midi::MappingTable table;

        expectEquals (static_cast<int> (table.assign (makeMapping (74, aParameter))),
                      static_cast<int> (midi::AssignResult::added));
        expectEquals (table.size(), 1);

        const auto* byMessage = table.findForMessage (1, 74);
        expect (byMessage != nullptr, "an omni mapping must match channel 1");
        expect (byMessage != nullptr && byMessage->parameterIndex == aParameter);

        expect (table.findForMessage (16, 74) != nullptr,
                "an omni mapping must match channel 16 just the same");
        expect (table.findForMessage (1, 75) == nullptr,
                "a controller with no mapping must not match one");

        const auto* byParameter = table.findForParameter (aParameter);
        expect (byParameter != nullptr);
        expect (byParameter != nullptr && byParameter->address.controller == 74);

        expect (table.findForParameter (anotherParameter) == nullptr);
    }

    void testBijectionIsEnforced()
    {
        beginTest ("One control drives one parameter, and one parameter has one control");

        midi::MappingTable table;

        expect (succeeded (table.assign (makeMapping (74, aParameter))));

        // Learning a second controller for the same parameter releases the first.
        expectEquals (static_cast<int> (table.assign (makeMapping (75, aParameter))),
                      static_cast<int> (midi::AssignResult::replacedParameterMapping));
        expectEquals (table.size(), 1, "the parameter must not end up with two controls");
        expect (table.findForMessage (1, 74) == nullptr,
                "the released controller must no longer drive anything");
        expect (table.findForMessage (1, 75) != nullptr);

        // Learning a controller another parameter uses takes it away from that one.
        expectEquals (static_cast<int> (table.assign (makeMapping (75, anotherParameter))),
                      static_cast<int> (midi::AssignResult::replacedControllerMapping));
        expectEquals (table.size(), 1);
        expect (table.findForParameter (aParameter) == nullptr);
        expect (table.findForParameter (anotherParameter) != nullptr);

        // Both at once: two existing mappings collapse into one.
        expect (succeeded (table.assign (makeMapping (80, aParameter))));
        expectEquals (table.size(), 2);
        expectEquals (static_cast<int> (table.assign (makeMapping (75, aParameter))),
                      static_cast<int> (midi::AssignResult::replacedBoth));
        expectEquals (table.size(), 1, "two mappings must have been released, not one");
    }

    void testUnchangedIsReported()
    {
        beginTest ("Reassigning an identical mapping reports that nothing changed");

        midi::MappingTable table;

        expectEquals (static_cast<int> (table.assign (makeMapping (74, aParameter))),
                      static_cast<int> (midi::AssignResult::added));
        expectEquals (static_cast<int> (table.assign (makeMapping (74, aParameter))),
                      static_cast<int> (midi::AssignResult::unchanged),
                      "an identical assignment must not report a replacement");
        expectEquals (table.size(), 1);
    }

    void testChannelSpecificBeatsOmni()
    {
        beginTest ("A channel-specific mapping wins over an omni one");

        midi::MappingTable table;

        expect (succeeded (table.assign (makeMapping (74, aParameter, midi::omniChannel))));
        expect (succeeded (table.assign (makeMapping (74, anotherParameter, 3))));

        expectEquals (table.size(), 2,
                      "an omni and a channel mapping on one controller are not a conflict");

        const auto* onThree = table.findForMessage (3, 74);
        expect (onThree != nullptr);
        expect (onThree != nullptr && onThree->parameterIndex == anotherParameter,
                "channel 3 must reach the channel-specific mapping");

        const auto* onFour = table.findForMessage (4, 74);
        expect (onFour != nullptr);
        expect (onFour != nullptr && onFour->parameterIndex == aParameter,
                "every other channel must fall through to the omni mapping");

        // And the same holds whichever order the two were learned in.
        midi::MappingTable reversed;
        expect (succeeded (reversed.assign (makeMapping (74, anotherParameter, 3))));
        expect (succeeded (reversed.assign (makeMapping (74, aParameter, midi::omniChannel))));

        const auto* stillOnThree = reversed.findForMessage (3, 74);
        expect (stillOnThree != nullptr && stillOnThree->parameterIndex == anotherParameter,
                "the result must not depend on the order the mappings arrived in");
    }

    void testRemoval()
    {
        beginTest ("Mappings can be removed individually and wholesale");

        midi::MappingTable table;

        expect (succeeded (table.assign (makeMapping (74, aParameter))));
        expect (succeeded (table.assign (makeMapping (75, anotherParameter))));
        expectEquals (table.size(), 2);

        expect (table.removeParameter (aParameter));
        expectEquals (table.size(), 1);
        expect (table.findForMessage (1, 74) == nullptr);
        expect (table.findForMessage (1, 75) != nullptr,
                "removing one mapping must not disturb another");

        expect (! table.removeParameter (aParameter),
                "removing what is not there must report that, not pretend it worked");

        midi::ControlAddress address;
        address.controller = 75;
        address.channel = midi::omniChannel;
        expect (table.removeAddress (address));
        expectEquals (table.size(), 0);

        expect (succeeded (table.assign (makeMapping (74, aParameter))));
        table.clear();
        expectEquals (table.size(), 0);
        expect (table.isEmpty());
    }

    void testCapacity()
    {
        beginTest ("The table fills to its capacity and refuses beyond it");

        midi::MappingTable table;

        // maxMappings distinct controllers on maxMappings distinct parameters.
        for (int i = 0; i < midi::maxMappings; ++i)
            expect (succeeded (table.assign (makeMapping (assignableController (i), i))),
                    "mapping " + juce::String (i) + " should have been accepted");

        expectEquals (table.size(), midi::maxMappings);
        expect (table.isFull());

        expectEquals (static_cast<int> (table.assign (makeMapping (assignableController (midi::maxMappings),midi::maxMappings))),
                      static_cast<int> (midi::AssignResult::rejectedTableFull),
                      "a full table must refuse rather than overwrite something");
        expectEquals (table.size(), midi::maxMappings);

        // A replacement still works when full, because it removes before it adds.
        expect (succeeded (table.assign (makeMapping (assignableController (midi::maxMappings),0))),
                "reassigning an existing parameter must work even at capacity");
        expectEquals (table.size(), midi::maxMappings);

        // Every mapping must still be findable: removal fills holes by moving
        // the last entry, and a scan that skipped one would be silent about it.
        for (int i = 1; i < midi::maxMappings; ++i)
            expect (table.findForParameter (i) != nullptr,
                    "parameter " + juce::String (i) + " lost its mapping");
    }

    void testPublicationChannel()
    {
        beginTest ("A published table reaches the consumer, and only when it changes");

        midi::MappingChannel channel;
        midi::MappingTable producer;
        midi::MappingTable consumer;

        expect (! channel.fetch (consumer),
                "an empty channel must report that nothing arrived");

        expect (succeeded (producer.assign (makeMapping (74, aParameter))));
        expect (channel.publish (producer));

        expect (channel.fetch (consumer), "the published table must arrive");
        expectEquals (consumer.size(), 1);
        expect (consumer.findForMessage (1, 74) != nullptr);

        expect (! channel.fetch (consumer),
                "a second fetch with nothing published must not report an update");

        // The consumer's copy is genuinely its own: further edits on the
        // producer must not be visible until they are published.
        expect (succeeded (producer.assign (makeMapping (75, anotherParameter))));
        expect (! channel.fetch (consumer));
        expectEquals (consumer.size(), 1, "an unpublished edit must not leak across");

        expect (channel.publish (producer));
        expect (channel.fetch (consumer));
        expectEquals (consumer.size(), 2);
    }

    void testPublicationRingOverflow()
    {
        beginTest ("A producer that outruns the ring is refused, not raced");

        midi::MappingChannel channel;
        midi::MappingTable producer;
        midi::MappingTable consumer;

        for (int i = 0; i < midi::MappingChannel::slotCount; ++i)
        {
            expect (succeeded (producer.assign (makeMapping (assignableController (i), i))));
            expect (channel.publish (producer),
                    "publication " + juce::String (i) + " should fit in the ring");
        }

        expect (succeeded (producer.assign (makeMapping (90, 20))));
        expect (! channel.publish (producer),
                "a full ring must refuse rather than overwrite a slot in flight");

        // Draining takes the newest of everything in flight, in one step.
        expect (channel.fetch (consumer));
        expectEquals (consumer.size(), midi::MappingChannel::slotCount,
                      "the consumer must end up with the newest published table");

        // And the refused publication succeeds once there is room again.
        expect (channel.publish (producer));
        expect (channel.fetch (consumer));
        expectEquals (consumer.size(), midi::MappingChannel::slotCount + 1);
    }
};

MidiMappingTests midiMappingTests;

} // namespace
