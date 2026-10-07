#pragma once

/*
    Which output device the standalone application plays through.

    JUCE's standalone wrapper saves the device it was last using and reopens it
    on the next launch. That is right for somebody who chose an interface, and
    wrong for everybody else: plug a laptop into speakers, Windows moves its
    default to the new endpoint, and Apollo goes on playing out of the laptop
    because the endpoint it saved still exists. Reported exactly that way
    (ADR-0082).

    The rule:

      > **Apollo plays through the system's default output, until you choose
      > something else. Choosing the current default again goes back to
      > following it.**

    Which needs one distinction to be made honestly: a device that is not the
    default because *the user picked it* must be left alone for ever, while a
    device that is not the default because *the default moved* must be
    abandoned. The two look identical in a snapshot, so Apollo remembers which
    default it adopted and compares.

    Plain strings and no JUCE, so every branch can be tested without an audio
    device of any kind, and nothing here knows the name of a device, a vendor
    or a connector (CLAUDE.md §46).
*/

#include <string>

namespace apollo::audio
{

/** What Apollo remembers between ticks, and across launches. */
struct OutputFollowing
{
    /** The device the user chose. Empty while Apollo is following the system
        default, which is the state it starts in.
    */
    std::string chosenByUser;

    /** The system default Apollo last adopted. This is what lets a default
        that moved be told apart from a device a user selected.
    */
    std::string adoptedDefault;

    [[nodiscard]] bool operator== (const OutputFollowing& other) const noexcept
    {
        return chosenByUser == other.chosenByUser && adoptedDefault == other.adoptedDefault;
    }
};

/** What to do about the output device, and what to remember afterwards. */
struct OutputDecision
{
    bool switchDevice = false;
    std::string switchTo;
    OutputFollowing state;
};

/** @param current        the device the application is playing through now
    @param systemDefault  the device the operating system currently calls default
    @param remembered     what Apollo recorded last time

    @returns whether to move, and the state to remember.
*/
[[nodiscard]] inline OutputDecision followSystemDefault (const std::string& current,
                                                         const std::string& systemDefault,
                                                         const OutputFollowing& remembered)
{
    OutputDecision decision;
    decision.state = remembered;

    // Nothing is known about the system's preference, so nothing is decided.
    // This is a real case: a machine with no audio device at all, and the
    // moment during start-up before a device type has been scanned.
    if (systemDefault.empty())
        return decision;

    if (! remembered.chosenByUser.empty())
    {
        // The user has chosen. The only thing that can change that is the user
        // choosing again — which is visible as the device no longer being the
        // one they picked.
        if (current == remembered.chosenByUser)
            return decision;

        if (current == systemDefault)
        {
            // They picked the default back. Apollo follows the system again,
            // which is how somebody undoes a choice without a setting to undo.
            decision.state.chosenByUser.clear();
            decision.state.adoptedDefault = systemDefault;
        }
        else
        {
            decision.state.chosenByUser = current;
        }

        return decision;
    }

    if (current == systemDefault)
    {
        // Already where the system wants it. Recorded, so that the next time
        // the default moves it can be recognised as having moved.
        decision.state.adoptedDefault = systemDefault;
        return decision;
    }

    // Not on the default, and the user has not chosen. Either the default has
    // moved under Apollo, or this is the first look and the saved device comes
    // from a build that never followed anything — both mean: go to the default.
    if (remembered.adoptedDefault.empty() || current == remembered.adoptedDefault)
    {
        decision.switchDevice = true;
        decision.switchTo = systemDefault;
        decision.state.adoptedDefault = systemDefault;
        return decision;
    }

    // Neither the default nor what Apollo adopted: the user changed it in the
    // settings dialog while Apollo was following. That is a choice.
    decision.state.chosenByUser = current;
    return decision;
}

} // namespace apollo::audio
