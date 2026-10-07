/*
    Which MIDI inputs a standalone Apollo opens for itself.

    The rule exists because JUCE's standalone wrapper opens none on desktop, so
    a keyboard plugged into the machine did nothing at all until somebody found
    the settings dialog (ADR-0081). It lives in a header with no JUCE in it so
    that it can be tested on a machine with no MIDI hardware — and so that the
    half of it that is easy to get wrong, leaving a user's decision alone, is
    tested rather than assumed.
*/

#include <juce_core/juce_core.h>

#include "MIDI/MidiDeviceAdoption.h"

#include <string>
#include <vector>

using namespace apollo::midi;

namespace
{

juce::String describe (const std::vector<std::string>& devices)
{
    juce::StringArray names;

    for (const auto& device : devices)
        names.add (juce::String (device));

    return names.isEmpty() ? "nothing" : names.joinIntoString (", ");
}

class MidiDeviceAdoptionTests final : public juce::UnitTest
{
public:
    MidiDeviceAdoptionTests() : juce::UnitTest ("MIDI device adoption", "MIDI") {}

    void runTest() override
    {
        testAKeyboardPluggedInIsOpened();
        testADeviceAlreadyOpenIsLeftAlone();
        testADeviceSwitchedOffStaysOff();
        testAnUnpluggedDeviceIsForgottenAndComesBack();
        testNothingToDo();
        testSeveralAtOnce();
        testEmptyIdentifiersAreIgnored();
    }

private:
    void testAKeyboardPluggedInIsOpened()
    {
        beginTest ("A device the application has never seen is opened");

        // The case that was reported: a keyboard connected to the machine, an
        // application that ignored it.
        const auto adoption = adoptMidiInputs ({ "MPK mini IV" }, {});

        expectEquals (static_cast<int> (adoption.toOpen.size()), 1, describe (adoption.toOpen));
        expectEquals (juce::String (adoption.toOpen.front()), juce::String ("MPK mini IV"));
        expect (adoption.toForget.empty());
    }

    void testADeviceAlreadyOpenIsLeftAlone()
    {
        beginTest ("A device that is still there and has been seen is not touched again");

        const auto adoption = adoptMidiInputs ({ "MPK mini IV" }, { "MPK mini IV" });

        expect (adoption.isEmpty(), "Nothing to open and nothing to forget");
    }

    void testADeviceSwitchedOffStaysOff()
    {
        beginTest ("A device the user switched off is not switched back on");

        // The half of the rule that matters. Apollo does not ask whether a seen
        // device is currently enabled, precisely so that a user who unticked it
        // in the settings dialog keeps their decision: it has been seen, so it
        // is left alone, whatever its state.
        //
        // Without this, the half-second timer would undo that tick for ever.
        const std::vector<std::string> seen { "MPK mini IV", "Cymatics Pinch" };
        const auto adoption = adoptMidiInputs ({ "MPK mini IV", "Cymatics Pinch" }, seen);

        expect (adoption.isEmpty(),
                "Neither is reopened: " + describe (adoption.toOpen));
    }

    void testAnUnpluggedDeviceIsForgottenAndComesBack()
    {
        beginTest ("A device that is unplugged is forgotten, and opens again when it returns");

        const auto unplugged = adoptMidiInputs ({}, { "MPK mini IV" });

        expectEquals (static_cast<int> (unplugged.toForget.size()), 1);
        expectEquals (juce::String (unplugged.toForget.front()), juce::String ("MPK mini IV"));
        expect (unplugged.toOpen.empty());

        // Having been forgotten, plugging it back in is a device seen for the
        // first time — which is what makes replugging work rather than being
        // the one way to end up with a dead keyboard.
        const auto returned = adoptMidiInputs ({ "MPK mini IV" }, {});

        expectEquals (static_cast<int> (returned.toOpen.size()), 1);
    }

    void testNothingToDo()
    {
        beginTest ("No devices at all is not an event");

        expect (adoptMidiInputs ({}, {}).isEmpty());
    }

    void testSeveralAtOnce()
    {
        beginTest ("A keyboard and a control surface are both opened, and a third is forgotten");

        // Nothing about the rule prefers one kind of device: Apollo does not
        // know which of these is a keyboard and must not guess (CLAUDE.md §46).
        const auto adoption = adoptMidiInputs ({ "MPK mini IV", "Launch Control XL" },
                                               { "Old Interface" });

        expectEquals (static_cast<int> (adoption.toOpen.size()), 2, describe (adoption.toOpen));
        expectEquals (static_cast<int> (adoption.toForget.size()), 1, describe (adoption.toForget));
        expectEquals (juce::String (adoption.toForget.front()), juce::String ("Old Interface"));
    }

    void testEmptyIdentifiersAreIgnored()
    {
        beginTest ("An empty identifier is not a device");

        // A driver that reports a device with no identifier would otherwise be
        // "opened" on every tick, for ever, since it can never be recorded as
        // seen in any way that matches.
        const auto adoption = adoptMidiInputs ({ "", "MPK mini IV" }, {});

        expectEquals (static_cast<int> (adoption.toOpen.size()), 1, describe (adoption.toOpen));
        expectEquals (juce::String (adoption.toOpen.front()), juce::String ("MPK mini IV"));
    }
};

static MidiDeviceAdoptionTests midiDeviceAdoptionTests;

} // namespace
