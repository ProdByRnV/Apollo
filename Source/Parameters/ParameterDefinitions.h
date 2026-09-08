#pragma once

/*
    The authoritative Apollo parameter registry.

    UI_BINDINGS.md §3 requires one authoritative native definition system, with
    the documented registry generated from it rather than maintained by hand.
    This is that system.

    The definitions are deliberately plain data with no JUCE dependency:

      - the registry can be validated (unique IDs, conventions, defaults in
        range) without constructing an AudioProcessor;
      - the same data drives the APVTS layout, the bridge metadata sent to the
        frontend, and the preset system, so those three cannot disagree;
      - it keeps the contract readable in one place, which matters because
        parameter IDs are permanent (Docs/PARAMETER-CONVENTIONS.md §1).

    Adding a parameter: see Docs/PARAMETER-CONVENTIONS.md §6.
*/

#include <array>
#include <cstddef>
#include <string_view>

namespace apollo::params
{

/** How a parameter's value is quantised and presented. */
enum class ParameterType
{
    floatingPoint, ///< Continuous value.
    integer,       ///< Whole numbers across an inclusive range.
    choice         ///< A fixed set of named options.
};

/** The unit a parameter's plain value is expressed in.

    An enum rather than a display string, so the frontend owns formatting and
    localisation, and so unit-aware behaviour (dB versus linear interpolation,
    say) can key off it.
*/
enum class ParameterUnit
{
    none,
    hertz,
    milliseconds,
    decibels,
    normalised, ///< A unitless 0-1 quantity, usually presented as a percentage.
    voices,
    semitones,
    cents,
    octaves
};

/** A single parameter's complete contract.

    The fields are exactly what UI_BINDINGS.md §3 requires each definition to
    specify: stable ID, display name, type, user-facing range, default, unit,
    step, skew, automation capability, smoothing and modulation capability.
*/
struct ParameterDefinition
{
    /** Stable, permanent identifier. Must satisfy ParameterId.h. */
    std::string_view id;

    /** Human-readable name for hosts and the UI. May change; the ID may not. */
    std::string_view name;

    ParameterType type;
    ParameterUnit unit;

    /** Inclusive plain-value range and default. */
    float minimum;
    float maximum;
    float defaultValue;

    /** JUCE-style skew factor for the plain <-> normalised mapping.

        1.0 is linear. Below 1.0 gives finer resolution near the minimum, which
        is what frequency and time controls need to feel musical instead of
        bunching everything useful into the first few degrees of travel.
    */
    float skew = 1.0f;

    /** Plain-value quantisation step. 0 means continuous.

        Discrete parameters must preserve their step count across the bridge
        (UI_BINDINGS.md §4); an integer control is never a rounded float.
    */
    float stepSize = 0.0f;

    /** Exposed to host automation. */
    bool automatable = true;

    /** Can be targeted by the modulation matrix (Phase 5). */
    bool modulatable = false;

    /** Needs smoothing to avoid audible discontinuities.

        Chosen per parameter rather than applied blindly (CLAUDE.md §36):
        smoothing a discrete selector would only blur a switch meant to be
        instantaneous.
    */
    bool smoothed = false;
};

/** The registry.

    Grew from the ten parameters documented in UI_BINDINGS.md §3 as the phases
    that implement them landed: `osc1_position` in Phase 4a with the wavetable
    engine, and the source section in Phase 4b. The remaining envelope, LFO and
    effect parameters are added by their own phases, so no ID ships before the
    DSP that gives it meaning.
*/
inline constexpr std::array<ParameterDefinition, 32> parameterDefinitions { {
    // Oscillator 1 -----------------------------------------------------------
    { "osc1_wavetable", "Osc 1 Wavetable",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 3.0f, 0.0f,
      1.0f, 1.0f,
      /* automatable */ true, /* modulatable */ false, /* smoothed */ false },

    // Added in Phase 4, when the wavetable engine gave it something to mean.
    // PRD §8.1 requires a wavetable position control; UI_BINDINGS.md §3's
    // initial registry predates the oscillator and does not list one. Following
    // Docs/PARAMETER-CONVENTIONS.md §6: the ID is new rather than a reuse, and
    // is permanent from here.
    { "osc1_position", "Osc 1 Position",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      /* automatable */ true, /* modulatable */ true, /* smoothed */ true },

    { "osc1_unison", "Osc 1 Unison",
      ParameterType::integer, ParameterUnit::voices,
      1.0f, 16.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "osc1_detune", "Osc 1 Detune",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.2f,
      1.0f, 0.0f,
      true, true, true },

    // Added in Phase 4b with the unison stack and the source mixer. Detune
    // without a stereo spread is a mono chorus, and an oscillator without a
    // level and a balance cannot be mixed against oscillator 2, the sub or the
    // noise — so these three arrive with the DSP that gives them meaning
    // (Docs/PARAMETER-CONVENTIONS.md §6).
    { "osc1_spread", "Osc 1 Spread",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.5f,
      1.0f, 0.0f,
      true, true, true },

    { "osc1_level", "Osc 1 Level",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 1.0f,
      1.0f, 0.0f,
      true, true, true },

    // Bipolar, so the default sits at the exact centre of the control's travel
    // rather than at one end of a 0-1 range that has to be re-centred for
    // display.
    { "osc1_pan", "Osc 1 Pan",
      ParameterType::floatingPoint, ParameterUnit::none,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // Oscillator 2 -----------------------------------------------------------
    // Structurally identical to oscillator 1 apart from its tuning controls and
    // its default level. It is silent by default: adding a second oscillator
    // must not change how an existing patch sounds, and layering is a decision
    // the user makes rather than one Apollo makes for them.
    { "osc2_wavetable", "Osc 2 Wavetable",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 3.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "osc2_position", "Osc 2 Position",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    { "osc2_unison", "Osc 2 Unison",
      ParameterType::integer, ParameterUnit::voices,
      1.0f, 16.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "osc2_detune", "Osc 2 Detune",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.2f,
      1.0f, 0.0f,
      true, true, true },

    { "osc2_spread", "Osc 2 Spread",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.5f,
      1.0f, 0.0f,
      true, true, true },

    { "osc2_level", "Osc 2 Level",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    { "osc2_pan", "Osc 2 Pan",
      ParameterType::floatingPoint, ParameterUnit::none,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // Two octaves either way covers the intervals a second oscillator is
    // actually used for — octaves, fifths, and the occasional detuned third —
    // without offering transpositions that would put the oscillator outside the
    // range its wavetable mipmap is built for.
    { "osc2_semitones", "Osc 2 Semitones",
      ParameterType::integer, ParameterUnit::semitones,
      -24.0f, 24.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    // Separate from the semitone control rather than folded into one continuous
    // range: a coarse control that snaps to intervals and a fine control that
    // does not are two different gestures, and merging them makes both worse.
    { "osc2_fine", "Osc 2 Fine",
      ParameterType::floatingPoint, ParameterUnit::cents,
      -100.0f, 100.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // Sub oscillator ---------------------------------------------------------
    { "sub_level", "Sub Level",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    { "sub_octave", "Sub Octave",
      ParameterType::integer, ParameterUnit::octaves,
      -2.0f, -1.0f, -1.0f,
      1.0f, 1.0f,
      true, false, false },

    // Noise ------------------------------------------------------------------
    { "noise_level", "Noise Level",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // Envelope 1 — amplitude ------------------------------------------------
    // Added in Phase 5a with the DAHDSR generator that gives them meaning. Only
    // envelope 1 is registered: it shapes the voice amplitude, which is a
    // destination that exists today. Envelopes 2-4 have nowhere to send their
    // output until the modulation matrix, so their IDs ship with it
    // (Docs/PARAMETER-CONVENTIONS.md §6).
    //
    // Times are in milliseconds with a strong skew, because envelope times are
    // used logarithmically: the difference between 5 ms and 50 ms matters far
    // more than the difference between 5 s and 5.05 s, and a linear control
    // would spend most of its travel in the range nobody adjusts.
    { "env1_delay", "Env 1 Delay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env1_attack", "Env 1 Attack",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 5.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env1_hold", "Env 1 Hold",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env1_decay", "Env 1 Decay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 100.0f,
      0.25f, 0.0f,
      true, true, false },

    // Defaults to full, which is what keeps the Phase 3 gain staging valid: a
    // held note sits at exactly the level every headroom measurement assumed
    // (ADR-0025).
    { "env1_sustain", "Env 1 Sustain",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 1.0f,
      1.0f, 0.0f,
      true, true, false },

    { "env1_release", "Env 1 Release",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 50.0f,
      0.25f, 0.0f,
      true, true, false },

    // Bipolar curve tension. Positive is the analog shape — quick off the mark,
    // easing into the target — which is why the default is positive rather than
    // linear: a linear amplitude release sounds like a fade-out rather than
    // like an instrument stopping. PRD §15.1 asks for one tension control;
    // per-stage curves are listed there as a later option.
    { "env1_curve", "Env 1 Curve",
      ParameterType::floatingPoint, ParameterUnit::none,
      -1.0f, 1.0f, 0.5f,
      1.0f, 0.0f,
      true, true, false },

    // Filter -----------------------------------------------------------------
    // Un-indexed because UI_BINDINGS.md §3 defines them that way, predating the
    // second filter the PRD specifies. Whether they gain an index is a Phase 5
    // decision recorded in Docs/PARAMETER-CONVENTIONS.md §3; once shipped, the
    // ID is permanent either way.
    { "filter_cutoff", "Filter Cutoff",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      20.0f, 20000.0f, 20000.0f,
      // Places 1 kHz near the centre of the control's travel.
      0.2298f, 0.0f,
      true, true, true },

    { "filter_resonance", "Filter Resonance",
      ParameterType::floatingPoint, ParameterUnit::none,
      0.1f, 10.0f, 0.707f,
      // Places Q = 1 near the centre. 0.707 is Butterworth.
      0.2890f, 0.0f,
      true, true, true },

    { "filter_drive", "Filter Drive",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // Effects ----------------------------------------------------------------
    { "fx_distortion_mix", "Distortion Mix",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    { "fx_delay_time", "Delay Time",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      1.0f, 2000.0f, 500.0f,
      // Places 250 ms near the centre of the control's travel.
      0.3327f, 0.0f,
      true, true, true },

    // Master -----------------------------------------------------------------
    { "master_gain", "Master Gain",
      ParameterType::floatingPoint, ParameterUnit::decibels,
      -60.0f, 6.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true }
} };

/** @returns the definition with this ID, or nullptr if there is none.

    A linear scan over a small fixed array: bounded work, no allocation, safe to
    call from any context.
*/
[[nodiscard]] constexpr const ParameterDefinition* findParameter (std::string_view id) noexcept
{
    for (const auto& definition : parameterDefinitions)
        if (definition.id == id)
            return &definition;

    return nullptr;
}

/** @returns the number of registered parameters. */
[[nodiscard]] constexpr std::size_t parameterCount() noexcept
{
    return parameterDefinitions.size();
}

/** @returns a short, stable token for a unit, for bridge metadata.

    Not localised or decorated: the frontend owns presentation.
*/
[[nodiscard]] constexpr std::string_view toString (ParameterUnit unit) noexcept
{
    switch (unit)
    {
        case ParameterUnit::none:         return "";
        case ParameterUnit::hertz:        return "Hz";
        case ParameterUnit::milliseconds: return "ms";
        case ParameterUnit::decibels:     return "dB";
        case ParameterUnit::normalised:   return "%";
        case ParameterUnit::voices:       return "voices";
        case ParameterUnit::semitones:    return "st";
        case ParameterUnit::cents:        return "cents";
        case ParameterUnit::octaves:      return "oct";
    }

    return "";
}

/** @returns a short, stable token for a parameter type, for bridge metadata. */
[[nodiscard]] constexpr std::string_view toString (ParameterType type) noexcept
{
    switch (type)
    {
        case ParameterType::floatingPoint: return "float";
        case ParameterType::integer:       return "int";
        case ParameterType::choice:        return "choice";
    }

    return "float";
}

} // namespace apollo::params
