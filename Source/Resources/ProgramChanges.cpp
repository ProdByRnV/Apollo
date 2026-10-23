#include "Resources/ProgramChanges.h"

#include "Resources/FactoryPresets.h"
#include "State/PresetDocument.h"

#include <utility>
#include <vector>

namespace apollo::resources
{

namespace
{

/** How often the message thread looks for a request. A program change is a
    performance gesture, and thirty times a second is well inside the time it
    takes to notice a sound has changed. */
constexpr int serviceRateHz = 30;

[[nodiscard]] bool onMessageThread()
{
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();

    return manager != nullptr && manager->isThisTheMessageThread();
}

/** "bank 1, program 12", as the message carried them. */
[[nodiscard]] juce::String describeNumber (int bank, int program)
{
    return "bank " + juce::String (bank) + ", program " + juce::String (program);
}

} // namespace

ProgramChanges::ProgramChanges (juce::AudioProcessorValueTreeState& state,
                                PresetLibrary& presetLibrary,
                                std::function<void()> appliedHandler)
    : apvts (state),
      library (presetLibrary),
      onApplied (std::move (appliedHandler))
{
    startTimerHz (serviceRateHz);
}

ProgramChanges::~ProgramChanges()
{
    stopTimer();
}

int ProgramChanges::getNumHostPrograms() noexcept
{
    return static_cast<int> (factoryPresets().size());
}

juce::String ProgramChanges::getHostProgramName (int index)
{
    const auto builtIn = factoryPresets();

    if (index < 0 || static_cast<std::size_t> (index) >= builtIn.size())
        return {};

    return metadataOf (builtIn[static_cast<std::size_t> (index)]).name;
}

void ProgramChanges::selectHostProgram (int index)
{
    if (index < 0 || index >= getNumHostPrograms())
        return;

    pendingRequest.store (pack (0, index));

    // A host expects the program it chose to be the current one when the call
    // returns, and on the message thread it can be.
    if (onMessageThread())
        service();
}

void ProgramChanges::handleMidi (const juce::MidiMessage& message, bool obeyProgramChanges) noexcept
{
    if (message.isController())
    {
        if (message.getControllerNumber() == 0)
            bank = message.getControllerValue();

        return;
    }

    if (message.isProgramChange() && obeyProgramChanges)
        pendingRequest.store (pack (bank, message.getProgramChangeNumber()));
}

void ProgramChanges::timerCallback()
{
    service();
}

void ProgramChanges::service()
{
    const auto request = pendingRequest.exchange (noRequest);

    if (request != noRequest)
    {
        // A newer request supersedes one still waiting for the library.
        waitingForScan = noRequest;
        load (request / programsPerBank, request % programsPerBank);
        return;
    }

    if (waitingForScan != noRequest && library.getCompletedScans() > 0)
    {
        const auto waiting = std::exchange (waitingForScan, noRequest);
        load (waiting / programsPerBank, waiting % programsPerBank);
    }
}

void ProgramChanges::load (int requestedBank, int program)
{
    PresetEntry entry;

    if (requestedBank == 0)
    {
        if (program >= getNumHostPrograms())
        {
            report ("PROGRAM_EMPTY", "Nothing is stored at " + describeNumber (requestedBank, program)
                                         + ". The sound has not changed.");
            return;
        }

        entry.builtIn = program;
        entry.name = getHostProgramName (program);
    }
    else
    {
        // THE LIBRARY IS NOT SCANNED UNTIL SOMETHING NEEDS IT (PresetLibrary),
        // and with no editor open nothing has. The request waits for the scan
        // rather than finding an empty list and saying the slot is empty.
        if (library.getCompletedScans() == 0)
        {
            if (! library.isScanning())
                library.rescan();

            waitingForScan = pack (requestedBank, program);
            return;
        }

        const auto index = library.getIndex();

        std::vector<const PresetEntry*> rest;

        for (const auto& candidate : index.entries)
            if (candidate.builtIn < 0)
                rest.push_back (&candidate);

        const auto position = static_cast<std::size_t> ((requestedBank - 1) * programsPerBank + program);

        if (position >= rest.size())
        {
            report ("PROGRAM_EMPTY", "Nothing is stored at " + describeNumber (requestedBank, program)
                                         + ". The sound has not changed.");
            return;
        }

        entry = *rest[position];
    }

    auto failedToRead = false;

    if (const auto applied = applyPreset (apvts, entry, failedToRead); applied != state::StateLoadResult::ok)
    {
        report ("LOAD_FAILED", "Program change: " + entry.name
                                   + (failedToRead ? " could not be read: " : " could not be loaded: ")
                                   + state::describe (applied));
        return;
    }

    if (onApplied)
        onApplied();

    report ("LOADED", "Program change: loaded " + entry.name + ".");
}

void ProgramChanges::stateReplaced()
{
    setCurrentProgram (programForState());
}

int ProgramChanges::programForState() const
{
    // A sound that is a built-in is that program, whatever else the document
    // says: the browser may have loaded it, and the host should show it.
    const auto metadata = presets::getMetadata (apvts);
    const auto builtIn = factoryPresets();

    for (std::size_t i = 0; i < builtIn.size(); ++i)
    {
        const auto candidate = metadataOf (builtIn[i]);

        if (candidate.name == metadata.name && candidate.author == metadata.author
            && candidate.category == metadata.category)
            return static_cast<int> (i);
    }

    // OTHERWISE THE PROJECT SAYS. A host stores the program it was shown, and
    // one that set it again after reopening a project would, if Apollo had
    // forgotten it, load that built-in over the user's sound. So the number is
    // saved with the project and read back here.
    // Text once it has been through a document, and untrusted like the rest of
    // one: only a plain number in range is taken.
    const auto stored = apvts.state.getProperty (juce::Identifier (state::hostProgramProperty)).toString();

    if (stored.isNotEmpty() && stored.length() <= 3 && stored.containsOnly ("0123456789"))
    {
        const auto program = stored.getIntValue();

        if (program < getNumHostPrograms())
            return program;
    }

    // A document that says nothing (a preset, or a project from before 13h-2)
    // leaves the program where it was. Moving the host's display to Init would
    // claim a sound the user is not hearing.
    return currentProgram.load();
}

void ProgramChanges::setCurrentProgram (int program)
{
    // Written into the state every time, not only when it moves: a document
    // that replaced the tree may have taken the old value with it.
    apvts.state.setProperty (juce::Identifier (state::hostProgramProperty), program, nullptr);

    if (currentProgram.exchange (program) != program && onCurrentProgramChanged)
        onCurrentProgramChanged();
}

void ProgramChanges::report (const juce::String& token, const juce::String& message)
{
    lastEvent.serial += 1;
    lastEvent.token = token;
    lastEvent.message = message;
}

} // namespace apollo::resources
