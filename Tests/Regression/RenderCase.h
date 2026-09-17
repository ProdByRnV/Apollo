#pragma once

/*
    The renders the regression suite keeps a golden for.

    A case is a complete recipe for a piece of audio: a starting patch, a list
    of parameters moved away from it, a sequence of notes, a sample rate, a
    block size and a length. Nothing else may affect the result, which is the
    property the whole of Phase 10b rests on — a golden that depends on the time
    of day, the machine's load or the order the tests ran in is not a golden.

    WHY THE FACTORY PRESETS ARE MOST OF THE SET. They are the sounds Apollo
    ships; they are the only sounds a user hears before making one of their own;
    and between them they touch nearly the whole engine — both oscillators,
    unison, the sub, the noise generator, both filters, four envelopes, LFOs,
    the modulation matrix and every effect in the rack. A regression suite built
    from synthetic patches would be a suite that tests what the author of the
    suite thought of. Building it from the shipped content means a change that
    alters what Apollo sounds like fails the build, which is the only definition
    of "regression" that matters to somebody using it.

    THE FOUR CASES THAT ARE NOT PRESETS exist because there are behaviours no
    preset exercises: several notes at once, a block size other than the usual
    one, a sample rate other than the usual one, and the modulation matrix
    driving something audibly. Each says in its own description what it is for.

    THESE ARE NOT PERFORMANCE MEASUREMENTS. How long a case takes to render is
    not recorded and not asserted; that is Phase 10c's work and it needs a
    measurement environment this suite does not have.
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <span>
#include <string_view>

namespace apollo::regression
{

/** A parameter moved away from where the case's starting patch left it.

    Plain values — hertz, milliseconds, decibels, or the integer a discrete
    control uses — the same convention the factory preset tables use, so the
    cases read the way the interface does.
*/
struct Setting
{
    std::string_view id;
    float value;
};

/** A note on or off, at a time measured from the start of the render. */
struct NoteEvent
{
    double seconds;
    int note;
    float velocity;
    bool on;
};

/** One reproducible render. */
struct RenderCase
{
    std::string_view name;

    /** What this case is in the set for. Read by a person, not by the suite. */
    std::string_view what;

    /** A factory preset to start from, or empty for the registry's defaults. */
    std::string_view preset;

    std::span<const Setting> settings;
    std::span<const NoteEvent> events;

    double sampleRate;
    int blockSize;
    double seconds;
};

/** Every case the suite renders. */
[[nodiscard]] std::span<const RenderCase> renderCases();

/** Renders one case into @p destination.

    @returns an empty string on success, or what went wrong — a preset name that
             is not in the library, a parameter id that is not registered, a
             value the parameter would not accept. Those are failures of the
             case table rather than of the instrument, and they are reported as
             themselves rather than as a mismatched fingerprint.
*/
[[nodiscard]] juce::String render (const RenderCase& renderCase, juce::AudioBuffer<float>& destination);

} // namespace apollo::regression
