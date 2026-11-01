#pragma once

/*
    The names the user gives the four macros (Phase 16c, ADR-0115).

    A name is not a parameter: nothing automates it and nothing on the audio
    thread reads it. It says what a macro is for — "Wobble", "Brightness" — and
    so it belongs to the sound, and lives where the drawn LFO curves do: as a
    child of the state tree. A host project saves it, a preset carries it, a
    preset without names puts every macro back to "Macro 1" to "Macro 4", and an
    older Apollo reading the document ignores an element it does not know.

        <MACRONAMES>
          <MACRO macro="2" name="Wobble"/>
        </MACRONAMES>

    ONLY MACROS THAT HAVE A NAME ARE WRITTEN. An unnamed macro is the empty
    string here and "Macro N" wherever a person reads it, so a patch nobody
    named saves exactly the document it saved before this existed.

    THE HOST'S NAME DOES NOT CHANGE. A DAW lists the macro as "Macro 1" whatever
    it is called here: a host parameter's name is fixed for the life of the
    plugin, and hosts that do re-read one do not agree on when.

    THREADS. Writers — the bridge renaming a macro, a state load restoring the
    names — run on the message thread, or on whatever thread a host calls
    `setStateInformation` from, and are serialised by a lock. The audio thread
    never touches this.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <mutex>

namespace apollo::state
{

/** Macros that can be named. */
inline constexpr int numMacroNames = 4;

/** The longest name a macro may have, in characters. Long enough for
    "Filter Sweep" or "Reverb Space", short enough to sit under a knob. */
inline constexpr int maxMacroNameLength = 16;

class MacroNameStore
{
public:
    explicit MacroNameStore (juce::AudioProcessorValueTreeState& stateToUse);

    MacroNameStore (const MacroNameStore&) = delete;
    MacroNameStore& operator= (const MacroNameStore&) = delete;

    /** Names macro @p macroIndex (0-based) and writes the name into the state
        tree. An empty name, or the macro's own default ("Macro 2" for the
        second), takes the name away.

        @returns false, changing nothing, if the index is out of range or the
                 name is longer than `maxMacroNameLength` once cleaned. Refused
                 rather than cut: the page limits what can be typed, so a name
                 that needed cutting is a page defect.
    */
    [[nodiscard]] bool set (int macroIndex, const juce::String& name);

    /** @returns the name macro @p macroIndex was given, or an empty string if
        it has none or the index is out of range. */
    [[nodiscard]] juce::String get (int macroIndex) const;

    /** @returns what a person reads for macro @p macroIndex: its name, or
        "Macro N" when it has none. */
    [[nodiscard]] juce::String displayName (int macroIndex) const;

    /** Rebuilds every name from the state tree, after a host or preset load
        replaced it. A macro missing from the tree has no name; one whose name
        is too long is cut to fit, as a preset's comment is — the sound is still
        a good sound, and refusing it over a label would punish the user for
        something they cannot see.

        @returns how many macros have a name.
    */
    int restoreFromState();

    /** Called after `set` changes a name, on the thread that called it. The
        processor uses it to tell the host the project has changed, since no
        parameter moved to tell it. */
    std::function<void()> onEdited;

    /** Every run of whitespace or control characters becomes one space, and
        the ends are trimmed: a name is one line of text, whatever was pasted. */
    [[nodiscard]] static juce::String clean (const juce::String& text);

    /** "Macro 1" to "Macro 4", for an index from 0. */
    [[nodiscard]] static juce::String defaultName (int macroIndex);

    /** Element and property names. Part of the serialized contract. */
    static constexpr const char* namesTreeType = "MACRONAMES";
    static constexpr const char* macroTreeType = "MACRO";
    static constexpr const char* macroProperty = "macro";
    static constexpr const char* nameProperty = "name";

private:
    /** Rewrites `<MACRONAMES>` from `names`. Lock held. */
    void writeToState();

    /** @p name cleaned, and empty if it is the default for @p macroIndex. */
    [[nodiscard]] static juce::String normalise (int macroIndex, const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;

    mutable std::mutex lock;

    std::array<juce::String, static_cast<std::size_t> (numMacroNames)> names;
};

} // namespace apollo::state
