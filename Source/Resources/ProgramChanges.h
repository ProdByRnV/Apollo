#pragma once

/*
    Program changes: choosing a preset by number, from a MIDI controller or from
    the host (Phase 13h-2, PRD §34).

    TWO ROUTES, ONE NUMBERING. A VST3 host never passes a MIDI program change to
    a plugin as MIDI: it translates one into the plugin's program list, if the
    plugin has one, and drops it otherwise. A standalone, or any host that
    passes MIDI through, delivers the message itself. So Apollo has both — a
    program list for the host, and a MIDI handler — and they count the same
    way, so program 8 is Wire Pluck whichever route it came by.

    - **Bank 0 is the built-in sounds**, in the order they are compiled in:
      program 0 is Init. It is the only bank a host's program list shows,
      because a host caches that list and the built-ins are the only presets
      guaranteed to be there, in the same order, on every machine.
    - **Banks 1 and up are the rest of the library**, 128 to a bank, in the
      order the browser lists them. Bank select is controller 0 (the MSB);
      controller 32 is not used, because most controllers that send a bank at
      all send that one.

    A NUMBER WITH NOTHING BEHIND IT CHANGES NOTHING. The sound stays as it was,
    and the interface says the slot was empty.

    NOTHING IS LOADED ON THE AUDIO THREAD. The audio thread only remembers
    which program was asked for, in one atomic; the load happens on the message
    thread, from a timer, through exactly the function the browser uses
    (`applyPreset`). The latest request wins: two program changes inside one
    timer tick load the second.

    Whether MIDI program changes are obeyed at all is `midi_program_change`, a
    setting of the instrument rather than of the sound: saved with the project,
    never carried by a preset, and not automatable (ADR-0045). The host's own
    program list is not subject to it, because choosing a program there is
    something the user did on purpose.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

#include "Resources/PresetLibrary.h"

namespace apollo::resources
{

class ProgramChanges final : private juce::Timer
{
public:
    /** Programs in one bank: the seven bits a program change carries. */
    static constexpr int programsPerBank = 128;

    /** @param onApplied called on the message thread after a preset has been
                         applied, to do what any wholesale replacement needs. */
    ProgramChanges (juce::AudioProcessorValueTreeState& apvts,
                    PresetLibrary& library,
                    std::function<void()> onApplied);

    ~ProgramChanges() override;

    // The host's program list ------------------------------------------------

    /** How many programs the host is shown: the built-in sounds. */
    [[nodiscard]] static int getNumHostPrograms() noexcept;

    [[nodiscard]] static juce::String getHostProgramName (int index);

    /** The built-in the current sound was last loaded as, or 0.

        Worked out again whenever the state is replaced, from the name, author
        and category the state carries, so a project reopens on the program it
        was saved on and a host re-applying that program finds nothing to do.
        Any thread.
    */
    [[nodiscard]] int getCurrentProgram() const noexcept { return currentProgram.load(); }

    /** The host chose a program. Loaded now on the message thread, or on the
        next tick from any other. Out-of-range numbers are ignored. */
    void selectHostProgram (int index);

    // MIDI --------------------------------------------------------------------

    /** Reads bank selects and program changes. AUDIO THREAD; allocates nothing.

        @param obeyProgramChanges the `midi_program_change` setting. Bank
                                  selects are remembered either way, so turning
                                  the setting on mid-song finds the right bank.
    */
    void handleMidi (const juce::MidiMessage& message, bool obeyProgramChanges) noexcept;

    // Message thread ---------------------------------------------------------

    /** Loads whatever has been asked for since the last call. The timer calls
        this; tests, which have no message loop, call it themselves. */
    void service();

    /** The state was replaced, by whatever means. Works out the current program
        again, and tells the host if it moved. */
    void stateReplaced();

    /** What the last program change did, for the interface. */
    struct Event
    {
        /** Bumped by every event, so a reader can tell a new one from the last. */
        int serial = 0;

        /** LOADED, LOAD_FAILED or PROGRAM_EMPTY. */
        juce::String token;

        juce::String message;
    };

    [[nodiscard]] const Event& getLastEvent() const noexcept { return lastEvent; }

    /** Called on the message thread when `getCurrentProgram` changes, so the
        processor can tell the host. */
    std::function<void()> onCurrentProgramChanged;

private:
    void timerCallback() override;

    /** A request is a bank and a program, packed; this is "none". */
    static constexpr int noRequest = -1;

    [[nodiscard]] static int pack (int bank, int program) noexcept { return bank * programsPerBank + program; }

    void load (int bank, int program);

    /** The program the state now stands for: the built-in it is, or the one
        the project saved, or else the one already current. */
    [[nodiscard]] int programForState() const;
    void report (const juce::String& token, const juce::String& message);
    void setCurrentProgram (int program);

    juce::AudioProcessorValueTreeState& apvts;
    PresetLibrary& library;
    std::function<void()> onApplied;

    std::atomic<int> pendingRequest { noRequest };
    std::atomic<int> currentProgram { 0 };

    /** The bank the next program change is in. Written and read only on the
        audio thread. */
    int bank = 0;

    /** A request for a user bank that is waiting for the library's first scan,
        which nothing starts until something needs it. Message thread. */
    int waitingForScan = noRequest;

    Event lastEvent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramChanges)
};

} // namespace apollo::resources
