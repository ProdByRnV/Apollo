#include "StandaloneOutputDevice.h"

#include "Audio/OutputDeviceFollowing.h"

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <string>
#include <utility>

namespace apollo::standalone
{

namespace
{

/** Where what Apollo remembers is kept.

    In the standalone's own settings file, beside the device setup JUCE saves
    there, so a choice survives a restart the way the choice itself does.
*/
constexpr const char* chosenKey = "apolloChosenOutputDevice";
constexpr const char* adoptedKey = "apolloAdoptedDefaultOutput";

/** Keeps the standalone on the system's default output device.

    JUCE's standalone wrapper reopens the device it saved. That is right for
    somebody who chose an interface and wrong for everybody else: plugging a
    laptop into speakers moves the operating system's default to the new
    endpoint while the old one still exists, so the application goes on playing
    out of the laptop (ADR-0082).

    The decision is in Audio/OutputDeviceFollowing.h, where it is tested without
    an audio device. This reads the two names it needs and applies the answer.

    **A plugin reaches none of it.** There is no `StandalonePluginHolder` in a
    plugin build, so every tick returns on its first line — which is the only
    acceptable behaviour there: the device belongs to the host.
*/
class OutputDeviceFollower final : private juce::Timer,
                                   private juce::DeletedAtShutdown
{
public:
    OutputDeviceFollower()
    {
        startTimer (pollIntervalMs);
    }

    ~OutputDeviceFollower() override
    {
        stopTimer();
    }

private:
    /** Slower than the MIDI watcher: switching an audio device interrupts the
        audio, so this should not be eager, and a second between plugging a
        cable in and hearing the instrument move is not a delay anybody would
        notice.
    */
    static constexpr int pollIntervalMs = 1000;

    void timerCallback() override
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();

        if (holder == nullptr)
            return;

        auto* deviceType = holder->deviceManager.getCurrentDeviceTypeObject();

        if (deviceType == nullptr)
            return;

        // What the system calls default, as the device type reports it. Not
        // rescanned here: the device manager rescans when the system tells it
        // the list changed, and scanning on a timer would be work for nothing
        // several times a minute.
        const auto names = deviceType->getDeviceNames (false);
        const auto defaultIndex = deviceType->getDefaultDeviceIndex (false);
        const auto systemDefault = juce::isPositiveAndBelow (defaultIndex, names.size())
                                     ? names[defaultIndex]
                                     : juce::String();

        const auto setup = holder->deviceManager.getAudioDeviceSetup();
        const auto current = setup.outputDeviceName;

        const auto decision = apollo::audio::followSystemDefault (current.toStdString(),
                                                                  systemDefault.toStdString(),
                                                                  read (*holder));

        if (! decision.switchDevice)
        {
            write (*holder, decision.state);
            return;
        }

        // A device that would not open is not tried again until the system's
        // answer changes. Held in memory rather than in the settings: an
        // endpoint another application had exclusively is usually free by the
        // next run, and a failure is not something to inherit.
        if (decision.switchTo == refused)
            return;

        auto wanted = setup;
        wanted.outputDeviceName = juce::String (decision.switchTo);

        // Persisted, which is what the `true` is for. Apollo tracks whether a
        // device was the user's choice in its own two keys; JUCE's flag only
        // decides whether the device is written to the settings file. Passing
        // false left the file naming the device Apollo had just moved away
        // from, so the next launch opened the wrong one and corrected itself a
        // second later, audibly.
        const auto error = holder->deviceManager.setAudioDeviceSetup (wanted, true);

        if (error.isNotEmpty())
        {
            // The device refused to open — in use exclusively, or gone again
            // between the two lines above.
            //
            // The state is deliberately NOT written here. Recording the
            // adoption of a device Apollo is not playing through would make the
            // next tick see a device that is neither the default nor what was
            // adopted, and that is what a user's own choice looks like — so a
            // refused device would silently become "the user chose this".
            // The settings dialog keeps working either way (CLAUDE.md §33).
            refused = decision.switchTo;
            juce::Logger::writeToLog ("Apollo: could not open the system's default output device: " + error);
            return;
        }

        refused.clear();
        write (*holder, decision.state);
    }

    /** A device that would not open, so that it is not retried every second. */
    std::string refused;

    static apollo::audio::OutputFollowing read (const juce::StandalonePluginHolder& holder)
    {
        apollo::audio::OutputFollowing state;

        if (holder.settings == nullptr)
            return state;

        state.chosenByUser = holder.settings->getValue (chosenKey).toStdString();
        state.adoptedDefault = holder.settings->getValue (adoptedKey).toStdString();
        return state;
    }

    static void write (juce::StandalonePluginHolder& holder, const apollo::audio::OutputFollowing& state)
    {
        if (holder.settings == nullptr)
            return;

        if (read (holder) == state)
            return;

        holder.settings->setValue (chosenKey, juce::String (state.chosenByUser));
        holder.settings->setValue (adoptedKey, juce::String (state.adoptedDefault));
    }
};

} // namespace

void startOutputDeviceFollowing()
{
    static auto started = false;

    if (std::exchange (started, true))
        return;

    // Owned by JUCE's shutdown, which is why nothing is kept here.
    new OutputDeviceFollower();
}

} // namespace apollo::standalone
