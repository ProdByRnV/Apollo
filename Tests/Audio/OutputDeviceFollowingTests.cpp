/*
    Which output device the standalone plays through.

    Written against the report that produced it: a laptop plugged into speakers
    through its headphone socket, Windows moving its default to the new
    endpoint, and Apollo going on playing out of the laptop (ADR-0082).

    Every case below is a sequence somebody can actually perform, and the ones
    that matter most are the two that look identical in a snapshot: a device
    that is not the default because the user chose it, and a device that is not
    the default because the default moved.
*/

#include <juce_core/juce_core.h>

#include "Audio/OutputDeviceFollowing.h"

#include <string>

using namespace apollo::audio;

namespace
{

// The two endpoints the machine this was reported on actually offers.
const std::string laptop { "Speakers (Realtek(R) Audio)" };
const std::string aux    { "Headphones (Realtek(R) Audio)" };
const std::string iface  { "Focusrite USB ASIO" };

class OutputDeviceFollowingTests final : public juce::UnitTest
{
public:
    OutputDeviceFollowingTests() : juce::UnitTest ("Output device following", "Audio") {}

    void runTest() override
    {
        testTheReportedCase();
        testFollowingAMovingDefault();
        testAChosenDeviceIsLeftAlone();
        testChoosingTheDefaultFollowsAgain();
        testAlreadyOnTheDefault();
        testNoSystemDefault();
        testAChosenDeviceThatDisappears();
        testASequenceOfPlugging();
    }

private:
    void testTheReportedCase()
    {
        beginTest ("A saved device from a build that followed nothing gives way to the default");

        // Apollo's settings held the laptop speakers, chosen by nothing but
        // JUCE saving whatever was in use; the aux is now the system default.
        // With nothing remembered, the default wins — otherwise the fix would
        // not fix the machine it was reported on.
        const auto decision = followSystemDefault (laptop, aux, {});

        expect (decision.switchDevice, "It moves");
        expectEquals (juce::String (decision.switchTo), juce::String (aux));
        expectEquals (juce::String (decision.state.adoptedDefault), juce::String (aux));
        expect (decision.state.chosenByUser.empty(), "Still following, not a choice");
    }

    void testFollowingAMovingDefault()
    {
        beginTest ("While following, Apollo goes where the default goes");

        OutputFollowing state;
        state.adoptedDefault = laptop;

        const auto plugged = followSystemDefault (laptop, aux, state);

        expect (plugged.switchDevice);
        expectEquals (juce::String (plugged.switchTo), juce::String (aux));

        // And back again when it is unplugged.
        const auto unplugged = followSystemDefault (aux, laptop, plugged.state);

        expect (unplugged.switchDevice);
        expectEquals (juce::String (unplugged.switchTo), juce::String (laptop));
    }

    void testAChosenDeviceIsLeftAlone()
    {
        beginTest ("A device the user picked survives the default moving");

        // Following the laptop; the user opens the dialog and picks an
        // interface. Apollo sees a device that is neither the default nor what
        // it adopted, which is what a choice looks like.
        OutputFollowing state;
        state.adoptedDefault = laptop;

        const auto chosen = followSystemDefault (iface, laptop, state);

        expect (! chosen.switchDevice, "It does not undo the choice");
        expectEquals (juce::String (chosen.state.chosenByUser), juce::String (iface));

        // Now the aux goes in and the system default moves. The choice stands.
        const auto afterPlugging = followSystemDefault (iface, aux, chosen.state);

        expect (! afterPlugging.switchDevice, "A chosen device is not taken away");
        expectEquals (juce::String (afterPlugging.state.chosenByUser), juce::String (iface));
    }

    void testChoosingTheDefaultFollowsAgain()
    {
        beginTest ("Picking the current default puts Apollo back to following it");

        // The only way out of a choice, and it needs no setting of its own.
        OutputFollowing state;
        state.chosenByUser = iface;
        state.adoptedDefault = laptop;

        const auto decision = followSystemDefault (aux, aux, state);

        expect (! decision.switchDevice, "Already there");
        expect (decision.state.chosenByUser.empty(), "The choice is released");
        expectEquals (juce::String (decision.state.adoptedDefault), juce::String (aux));

        // Which means the next move of the default is followed again.
        const auto next = followSystemDefault (aux, laptop, decision.state);

        expect (next.switchDevice);
        expectEquals (juce::String (next.switchTo), juce::String (laptop));
    }

    void testAlreadyOnTheDefault()
    {
        beginTest ("Being on the default is not an event, but it is remembered");

        const auto decision = followSystemDefault (aux, aux, {});

        expect (! decision.switchDevice);
        expectEquals (juce::String (decision.state.adoptedDefault), juce::String (aux),
                      "Remembered, so that the next move can be seen as a move");
    }

    void testNoSystemDefault()
    {
        beginTest ("A machine that reports no default is left alone");

        // Real: during start-up before a device type has been scanned, and on
        // a machine with no audio hardware at all.
        OutputFollowing state;
        state.adoptedDefault = laptop;

        const auto decision = followSystemDefault (laptop, "", state);

        expect (! decision.switchDevice);
        expect (decision.state == state, "Nothing is learned from no information");
    }

    void testAChosenDeviceThatDisappears()
    {
        beginTest ("When a chosen device is unplugged, Apollo takes what it is given");

        // JUCE falls back to something when a device vanishes. If what it fell
        // back to is the default, Apollo follows again; if it is not, that is
        // where the user is left, rather than being moved twice.
        OutputFollowing chosen;
        chosen.chosenByUser = iface;

        const auto fellBackToDefault = followSystemDefault (aux, aux, chosen);
        expect (fellBackToDefault.state.chosenByUser.empty(), "Following again");

        const auto fellBackElsewhere = followSystemDefault (laptop, aux, chosen);
        expect (! fellBackElsewhere.switchDevice);
        expectEquals (juce::String (fellBackElsewhere.state.chosenByUser), juce::String (laptop));
    }

    void testASequenceOfPlugging()
    {
        beginTest ("A whole session: plug in, unplug, choose, plug in, release");

        // The property is that Apollo is never moved away from a device the
        // user chose, and is never left behind a default it was following.
        OutputFollowing state;
        auto current = laptop;

        const auto step = [&] (const std::string& systemDefault)
        {
            const auto decision = followSystemDefault (current, systemDefault, state);
            state = decision.state;

            if (decision.switchDevice)
                current = decision.switchTo;

            return decision;
        };

        step (laptop);                                   // settles, following
        expect (current == laptop);

        step (aux);                                      // aux goes in
        expect (current == aux, "Followed to the aux");

        step (laptop);                                   // aux comes out
        expect (current == laptop, "Followed back");

        current = iface;                                 // the user picks an interface
        step (laptop);
        expectEquals (juce::String (state.chosenByUser), juce::String (iface));

        step (aux);                                      // aux goes in again
        expect (current == iface, "Left on the chosen interface");

        current = aux;                                   // the user picks the default
        step (aux);
        expect (state.chosenByUser.empty(), "Following again");

        step (laptop);                                   // aux out once more
        expect (current == laptop, "And following means following");
    }
};

static OutputDeviceFollowingTests outputDeviceFollowingTests;

} // namespace
