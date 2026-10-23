/*
    Program changes (Phase 13h-2): a preset chosen by number, from a MIDI
    program change or from the host's program list.

    Through whole processors, because the claims are about the instrument: that
    a program change reaching processBlock loads nothing on the audio thread and
    the right preset on the message thread; that bank 0 is the built-ins and
    the host's list, and the banks after it the rest of the library; that a
    number with nothing behind it, or a switched-off setting, leaves the sound
    alone; and that the host's idea of the current program survives a project.

    The test runner has no message loop, so the message thread's half is run by
    calling `service()`, as the timer would.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "Resources/FactoryPresets.h"
#include "Resources/ProgramChanges.h"
#include "State/PresetDocument.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 256;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

[[nodiscard]] float plainOf (ApolloAudioProcessor& processor, const juce::String& id)
{
    return processor.getValueTreeState().getRawParameterValue (id)->load();
}

[[nodiscard]] juce::String nameOf (ApolloAudioProcessor& processor)
{
    return presets::getMetadata (processor.getValueTreeState()).name;
}

/** Plays one block carrying @p messages, all at its start. */
void play (ApolloAudioProcessor& processor, std::initializer_list<juce::MidiMessage> messages)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();

    juce::MidiBuffer midi;

    for (const auto& message : messages)
        midi.addEvent (message, 0);

    processor.processBlock (buffer, midi);
}

/** The position of the built-in called @p name, or -1. */
[[nodiscard]] int builtInNumber (const juce::String& name)
{
    const auto builtIn = resources::factoryPresets();

    for (std::size_t i = 0; i < builtIn.size(); ++i)
        if (resources::metadataOf (builtIn[i]).name == name)
            return static_cast<int> (i);

    return -1;
}

/** A user library in a scratch folder, with the presets named in it. */
class ScratchLibrary
{
public:
    explicit ScratchLibrary (ApolloAudioProcessor& processor)
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloProgramChangeTests")
                    .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();

        resources::PresetLocations locations;
        locations.userDirectory = root.getChildFile ("User");
        locations.factoryDirectory = root.getChildFile ("Factory");
        (void) locations.userDirectory.createDirectory();

        processor.getPresetLibrary().setLocations (locations);
        userDirectory = locations.userDirectory;
    }

    ~ScratchLibrary() { root.getParentDirectory().deleteRecursively(); }

    /** Saves the processor's current sound as @p name, with the cutoff at
        @p cutoffHz so each preset can be told apart. */
    void save (ApolloAudioProcessor& processor, const juce::String& name, float cutoffHz)
    {
        setPlain (processor, "filter1_cutoff", cutoffHz);

        presets::Metadata metadata;
        metadata.name = name;

        juce::File written;
        (void) resources::savePreset (userDirectory, name,
                                      presets::write (processor.getValueTreeState(), metadata), written);
    }

private:
    juce::File root;
    juce::File userDirectory;
};

class ProgramChangesTests final : public juce::UnitTest
{
public:
    ProgramChangesTests()
        : juce::UnitTest ("Program changes", "Resources")
    {
    }

    void runTest() override
    {
        testTheHostSeesTheBuiltIns();
        testTheHostChoosesAProgram();
        testAMidiProgramChange();
        testTheLatestWins();
        testAUserBank();
        testNothingThere();
        testTheSetting();
        testTheProgramSurvivesAProject();
    }

private:
    void testTheHostSeesTheBuiltIns()
    {
        beginTest ("The host is shown the built-in sounds as programs, in their compiled order");

        ApolloAudioProcessor processor;

        const auto builtIn = resources::factoryPresets();

        expectEquals (processor.getNumPrograms(), static_cast<int> (builtIn.size()));
        expect (processor.getNumPrograms() > 1, "one program would give the host no list at all");

        for (std::size_t i = 0; i < builtIn.size(); ++i)
            expectEquals (processor.getProgramName (static_cast<int> (i)), resources::metadataOf (builtIn[i]).name);

        expectEquals (processor.getProgramName (0), juce::String ("Init"), "program 0 is the empty case");
        expectEquals (processor.getProgramName (-1), juce::String());
        expectEquals (processor.getProgramName (processor.getNumPrograms()), juce::String());

        expectEquals (processor.getCurrentProgram(), 0, "a new instance is Init");
    }

    void testTheHostChoosesAProgram()
    {
        beginTest ("A program the host chooses is loaded as the browser loads it, and the host is told");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        const auto wirePluck = builtInNumber ("Wire Pluck");
        expect (wirePluck > 0);

        const auto reloads = processor.getStateReloadCounter();

        processor.setCurrentProgram (wirePluck);
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Wire Pluck"));
        expectEquals (processor.getCurrentProgram(), wirePluck);
        expect (processor.getStateReloadCounter() != reloads, "an editor must be told to resynchronise");

        const auto& event = processor.getProgramChanges().getLastEvent();
        expectEquals (event.token, juce::String ("LOADED"));
        expect (event.message.contains ("Wire Pluck"));

        // Out of range is ignored, not clamped onto a sound nobody asked for.
        processor.setCurrentProgram (-1);
        processor.setCurrentProgram (processor.getNumPrograms());
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Wire Pluck"));
        expectEquals (processor.getCurrentProgram(), wirePluck);
    }

    void testAMidiProgramChange()
    {
        beginTest ("A MIDI program change loads nothing on the audio thread, then its built-in");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        const auto glassBell = builtInNumber ("Glass Bell");

        play (processor, { juce::MidiMessage::programChange (1, glassBell) });

        expectEquals (nameOf (processor), juce::String(), "the audio thread only remembers the request");

        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Glass Bell"));
        expectEquals (processor.getCurrentProgram(), glassBell,
                      "bank 0 counts the way the host's list does");

        // Any channel: Apollo listens on all of them, and so does this.
        play (processor, { juce::MidiMessage::programChange (11, 0) });
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Init"));

        beginTest ("A program change is obeyed while the instrument is bypassed");

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::programChange (1, glassBell), 0);
        processor.processBlockBypassed (buffer, midi);
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Glass Bell"));
    }

    void testTheLatestWins()
    {
        beginTest ("Two program changes before the message thread looks: the second is loaded, once");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        const auto before = processor.getProgramChanges().getLastEvent().serial;

        play (processor, { juce::MidiMessage::programChange (1, builtInNumber ("Glass Bell")),
                           juce::MidiMessage::programChange (1, builtInNumber ("Slow Bloom")) });
        processor.getProgramChanges().service();
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Slow Bloom"));
        expectEquals (processor.getProgramChanges().getLastEvent().serial, before + 1);
    }

    void testAUserBank()
    {
        beginTest ("Bank 1 is the rest of the library in the browser's order, scanned when first needed");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        ScratchLibrary scratch (processor);
        scratch.save (processor, "Alpha", 300.0f);
        scratch.save (processor, "Beta", 3000.0f);

        setPlain (processor, "filter1_cutoff", 12000.0f);

        auto& library = processor.getPresetLibrary();
        expectEquals (library.getCompletedScans(), 0, "nothing has scanned the library yet");

        // Bank 1, program 1: the second preset that is not a built-in.
        play (processor, { juce::MidiMessage::controllerEvent (1, 0, 1),
                           juce::MidiMessage::programChange (1, 1) });

        processor.getProgramChanges().service();
        expectEquals (nameOf (processor), juce::String(), "the request waits for the scan it started");

        library.waitForScan();
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Beta"));
        expectWithinAbsoluteError (plainOf (processor, "filter1_cutoff"), 3000.0f, 1.0f);

        // The bank is remembered: the next program change is in it too.
        play (processor, { juce::MidiMessage::programChange (1, 0) });
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Alpha"));

        // Not a built-in, so the host's program stays where it was.
        expectEquals (processor.getCurrentProgram(), 0);

        // And back to bank 0.
        play (processor, { juce::MidiMessage::controllerEvent (1, 0, 0),
                           juce::MidiMessage::programChange (1, builtInNumber ("Wire Pluck")) });
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Wire Pluck"));
    }

    void testNothingThere()
    {
        beginTest ("A number with nothing behind it leaves the sound alone and says so");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        ScratchLibrary scratch (processor);
        processor.getPresetLibrary().rescan();
        processor.getPresetLibrary().waitForScan();

        setPlain (processor, "filter1_cutoff", 1234.0f);
        const auto reloads = processor.getStateReloadCounter();

        for (const auto& [bank, program] : { std::pair { 0, 100 }, std::pair { 1, 0 }, std::pair { 5, 127 } })
        {
            play (processor, { juce::MidiMessage::controllerEvent (1, 0, bank),
                               juce::MidiMessage::programChange (1, program) });
            processor.getProgramChanges().service();

            const auto& event = processor.getProgramChanges().getLastEvent();

            expectEquals (event.token, juce::String ("PROGRAM_EMPTY"));
            expect (event.message.contains ("bank " + juce::String (bank) + ", program " + juce::String (program)),
                    event.message);
        }

        expectWithinAbsoluteError (plainOf (processor, "filter1_cutoff"), 1234.0f, 0.5f);
        expectEquals (processor.getStateReloadCounter(), reloads, "nothing was replaced");
    }

    void testTheSetting()
    {
        beginTest ("Switched off, a MIDI program change is ignored; the host's list is not");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        expectEquals (plainOf (processor, "midi_program_change"), 1.0f, "obeyed by default");

        setPlain (processor, "midi_program_change", 0.0f);

        const auto before = processor.getProgramChanges().getLastEvent().serial;

        play (processor, { juce::MidiMessage::programChange (1, builtInNumber ("Glass Bell")) });
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String());
        expectEquals (processor.getProgramChanges().getLastEvent().serial, before, "and nothing is reported");

        processor.setCurrentProgram (builtInNumber ("Glass Bell"));
        processor.getProgramChanges().service();

        expectEquals (nameOf (processor), juce::String ("Glass Bell"), "choosing in the host is deliberate");

        beginTest ("The setting belongs to the instrument: a preset neither carries nor changes it");

        expectEquals (plainOf (processor, "midi_program_change"), 0.0f, "a loaded preset left it off");

        presets::Metadata metadata;
        metadata.name = "Carrier";
        expect (! presets::write (processor.getValueTreeState(), metadata).contains ("midi_program_change"));

        juce::MemoryBlock project;
        processor.getStateInformation (project);

        ApolloAudioProcessor reopened;
        reopened.setStateInformation (project.getData(), static_cast<int> (project.getSize()));

        expectEquals (plainOf (reopened, "midi_program_change"), 0.0f, "but a project does");
    }

    void testTheProgramSurvivesAProject()
    {
        beginTest ("A project reopens on the program it was saved on, even edited");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        const auto squareKeys = builtInNumber ("Square Keys");

        processor.setCurrentProgram (squareKeys);
        processor.getProgramChanges().service();
        setPlain (processor, "filter1_cutoff", 777.0f);

        juce::MemoryBlock project;
        processor.getStateInformation (project);

        ApolloAudioProcessor reopened;
        auto changes = 0;
        reopened.getProgramChanges().onCurrentProgramChanged = [&changes] { ++changes; };

        reopened.setStateInformation (project.getData(), static_cast<int> (project.getSize()));

        expectEquals (reopened.getCurrentProgram(), squareKeys,
                      "a host re-applying the saved program must find it already current");
        expectEquals (changes, 1, "and is told the program moved");
        expectWithinAbsoluteError (plainOf (reopened, "filter1_cutoff"), 777.0f, 0.5f, "the edit survives");

        beginTest ("A project on a sound that is no built-in still reopens on its program");

        {
            ApolloAudioProcessor original;
            original.prepareToPlay (testSampleRate, blockSize);

            original.setCurrentProgram (squareKeys);
            original.getProgramChanges().service();

            // The user's own sound: renamed, so it matches no built-in.
            presets::Metadata own;
            own.name = "My Own Keys";
            presets::setMetadata (original.getValueTreeState(), own);

            expect (! presets::write (original.getValueTreeState(), own).contains (state::hostProgramProperty),
                    "a preset is a sound, and carries no host program");

            juce::MemoryBlock saved;
            original.getStateInformation (saved);

            ApolloAudioProcessor again;
            again.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

            expectEquals (again.getCurrentProgram(), squareKeys,
                          "else a host setting its stored program again would load Square Keys over it");
            expectEquals (nameOf (again), juce::String ("My Own Keys"));

            // A stored number that is not one is ignored.
            for (const auto* nonsense : { "-1", "10", "3.5", "abc", "99999999999" })
            {
                ApolloAudioProcessor hostile;
                hostile.getValueTreeState().state.setProperty (state::hostProgramProperty, nonsense, nullptr);
                hostile.notifyStateReplaced();

                expectEquals (hostile.getCurrentProgram(), 0, nonsense);
            }
        }

        beginTest ("A built-in chosen in the browser moves the host's program too");

        bool failedToRead = false;
        resources::PresetEntry entry;
        entry.builtIn = builtInNumber ("Dust Sweep");

        expect (resources::applyPreset (reopened.getValueTreeState(), entry, failedToRead)
                == state::StateLoadResult::ok);
        reopened.notifyStateReplaced();

        expectEquals (reopened.getCurrentProgram(), builtInNumber ("Dust Sweep"));
    }
};

ProgramChangesTests programChangesTests;

} // namespace
