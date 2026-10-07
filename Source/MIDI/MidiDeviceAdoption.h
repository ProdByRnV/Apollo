#pragma once

/*
    Which MIDI inputs the standalone application opens for itself.

    A plugin is handed MIDI by its host. The standalone has to go and find it,
    and JUCE's standalone wrapper does not: on desktop it opens no MIDI input at
    all until somebody ticks one in Options -> Audio/MIDI Settings. A keyboard
    plugged into the machine therefore does nothing, with no indication of why
    — which is exactly how it was reported (ADR-0081).

    The rule, which is the whole of the decision and is why it is here rather
    than inside the timer that applies it:

      - A device seen for the first time is **opened**. Plugging a keyboard in
        and playing it is what a person expects to work.
      - A device that has gone away is **forgotten**, so that plugging it back
        in opens it again.
      - A device that is still there and has been seen before is **left alone**.
        This is the half that matters: without it, a user who deliberately
        unticks a device in the settings dialog would have it switched back on
        half a second later, for ever.

    Plain strings and no JUCE, so the rule can be tested without a machine that
    has any MIDI hardware attached to it (CLAUDE.md §46 — nothing here assumes a
    particular controller, or that one exists at all).
*/

#include <algorithm>
#include <string>
#include <vector>

namespace apollo::midi
{

/** What to do about the MIDI inputs the system is currently offering. */
struct DeviceAdoption
{
    /** Devices to open: present now, never seen before. */
    std::vector<std::string> toOpen;

    /** Devices to stop remembering: seen before, not present now. */
    std::vector<std::string> toForget;

    [[nodiscard]] bool isEmpty() const noexcept { return toOpen.empty() && toForget.empty(); }
};

/** @param available   identifiers the system is offering right now
    @param alreadySeen identifiers this application has opened at some point

    @returns what to open and what to forget. Devices in both lists are left
             alone, which is what preserves a user's decision to switch one off.
*/
[[nodiscard]] inline DeviceAdoption adoptMidiInputs (const std::vector<std::string>& available,
                                                     const std::vector<std::string>& alreadySeen)
{
    const auto contains = [] (const std::vector<std::string>& haystack, const std::string& needle)
    {
        return std::find (haystack.begin(), haystack.end(), needle) != haystack.end();
    };

    DeviceAdoption adoption;

    for (const auto& device : available)
        if (! device.empty() && ! contains (alreadySeen, device))
            adoption.toOpen.push_back (device);

    for (const auto& device : alreadySeen)
        if (! contains (available, device))
            adoption.toForget.push_back (device);

    return adoption;
}

} // namespace apollo::midi
