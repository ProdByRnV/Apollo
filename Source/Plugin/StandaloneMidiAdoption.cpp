#include "StandaloneMidiAdoption.h"

#include "MIDI/MidiDeviceAdoption.h"

// The standalone wrapper's own header, for the plugin holder that owns the
// application's device manager. It needs the plugin client's defines and
// juce_audio_utils ahead of it, which is the order JUCE's own standalone
// translation unit uses.
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace apollo::standalone
{

namespace
{

/** Keeps the standalone application's MIDI inputs open.

    JUCE's standalone wrapper has this ability and leaves it switched off on
    desktop: `StandalonePluginHolder`'s `shouldAutoOpenMidiDevices` defaults to
    false everywhere but iOS and Android, and the flag that changes it applies
    only to mobile. So the application opens no MIDI input at all until somebody
    ticks one in Options -> Audio/MIDI Settings, and a keyboard plugged into the
    machine does nothing with no indication of why (ADR-0081).

    This does that job beside JUCE's wrapper rather than replacing the whole
    standalone application to pass one `true`, and it adds a rule of its own
    about devices a user has switched off deliberately. The rule lives in
    MIDI/MidiDeviceAdoption.h, where it is tested without any MIDI hardware.

    **A plugin never reaches any of this.** There is no holder in a plugin
    build, so `getInstance()` is null and every tick returns immediately —
    which is the behaviour a plugin must have, since opening a controller for
    itself would take it away from the rest of the session.

    DeletedAtShutdown rather than a static: JUCE deletes these while the message
    thread is still running, which is where a Timer has to be stopped.
*/
class MidiDeviceAdopter final : private juce::Timer,
                                private juce::DeletedAtShutdown
{
public:
    MidiDeviceAdopter()
    {
        // The holder creates the plugin before it sets up its devices, and this
        // is constructed during that creation — so the first look happens on
        // the first tick rather than now.
        startTimer (pollIntervalMs);
    }

    ~MidiDeviceAdopter() override
    {
        stopTimer();
    }

private:
    /** JUCE polls at this rate for the same purpose. A keyboard plugged in
        mid-session should start working without the user doing anything, and
        half a second is below what anybody would call a delay.
    */
    static constexpr int pollIntervalMs = 500;

    void timerCallback() override
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();

        if (holder == nullptr)
            return;

        std::vector<std::string> available;

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            available.push_back (device.identifier.toStdString());

        const auto adoption = apollo::midi::adoptMidiInputs (available, seen);

        if (adoption.isEmpty())
            return;

        for (const auto& identifier : adoption.toOpen)
        {
            holder->deviceManager.setMidiInputDeviceEnabled (juce::String (identifier), true);
            seen.push_back (identifier);
        }

        for (const auto& identifier : adoption.toForget)
        {
            // Closed as well as forgotten: a device that has been unplugged is
            // not coming back on its own, and leaving it enabled would leave a
            // dead entry in the saved settings.
            holder->deviceManager.setMidiInputDeviceEnabled (juce::String (identifier), false);
            seen.erase (std::remove (seen.begin(), seen.end(), identifier), seen.end());
        }
    }

    /** Every identifier this application has opened, so that one the user
        switches off afterwards is not switched back on.
    */
    std::vector<std::string> seen;
};

} // namespace

void startMidiDeviceAdoption()
{
    static auto started = false;

    if (std::exchange (started, true))
        return;

    // Owned by JUCE's shutdown, which is why nothing is kept here.
    new MidiDeviceAdopter();
}

} // namespace apollo::standalone
