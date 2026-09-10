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
inline constexpr std::array<ParameterDefinition, 143> parameterDefinitions { {
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

    // Filters ----------------------------------------------------------------
    // Indexed from Phase 5b. These began life as un-indexed `filter_cutoff`,
    // `filter_resonance` and `filter_drive`, which predated the second filter
    // the PRD specifies. Keeping them would have left the instrument with
    // `filter_cutoff` beside `filter2_cutoff` for the rest of its life; renaming
    // them is a state migration rather than a rename, and schema version 2
    // performs it (ADR-0032). Apollo is pre-1.0 and unreleased, which is the
    // only window in which this is cheap (Docs/VERSIONING.md §2).
    //
    // Type is a discrete choice: 0 off, 1 lowpass, 2 highpass, 3 bandpass,
    // 4 notch. "Off" is a mode rather than a separate enable, so one control
    // cannot disagree with another about whether a filter is running.
    { "filter1_type", "Filter 1 Type",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 4.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "filter1_cutoff", "Filter 1 Cutoff",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      20.0f, 20000.0f, 20000.0f,
      // Places 1 kHz near the centre of the control's travel.
      0.2298f, 0.0f,
      true, true, true },

    { "filter1_resonance", "Filter 1 Resonance",
      ParameterType::floatingPoint, ParameterUnit::none,
      0.1f, 10.0f, 0.707f,
      // Places Q = 1 near the centre. 0.707 is Butterworth.
      0.2890f, 0.0f,
      true, true, true },

    { "filter1_drive", "Filter 1 Drive",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // Filter 2 is off by default, for the same reason oscillator 2 is silent:
    // adding it must not change how an existing patch sounds.
    { "filter2_type", "Filter 2 Type",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 4.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "filter2_cutoff", "Filter 2 Cutoff",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      20.0f, 20000.0f, 20000.0f,
      0.2298f, 0.0f,
      true, true, true },

    { "filter2_resonance", "Filter 2 Resonance",
      ParameterType::floatingPoint, ParameterUnit::none,
      0.1f, 10.0f, 0.707f,
      0.2890f, 0.0f,
      true, true, true },

    { "filter2_drive", "Filter 2 Drive",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, true },

    // 0 series, 1 parallel. Un-indexed because it describes how the two filters
    // are connected rather than belonging to either of them.
    { "filter_routing", "Filter Routing",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    // Envelopes 2-4 ----------------------------------------------------------
    // Added in Phase 5d, when the modulation matrix gave them destinations. They
    // are identical to envelope 1 apart from their defaults: envelope 1 shapes
    // amplitude and sustains at full, while a modulation envelope that sustained
    // at full would be a constant offset rather than a shape, so these decay to
    // nothing by default and do audibly nothing until they are routed.
    { "env2_delay", "Env 2 Delay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env2_attack", "Env 2 Attack",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 5.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env2_hold", "Env 2 Hold",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env2_decay", "Env 2 Decay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 300.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env2_sustain", "Env 2 Sustain",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "env2_release", "Env 2 Release",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 300.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env2_curve", "Env 2 Curve",
      ParameterType::floatingPoint, ParameterUnit::none,
      -1.0f, 1.0f, 0.5f,
      1.0f, 0.0f,
      true, true, false },

    { "env3_delay", "Env 3 Delay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env3_attack", "Env 3 Attack",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 5.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env3_hold", "Env 3 Hold",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env3_decay", "Env 3 Decay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 300.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env3_sustain", "Env 3 Sustain",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "env3_release", "Env 3 Release",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 300.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env3_curve", "Env 3 Curve",
      ParameterType::floatingPoint, ParameterUnit::none,
      -1.0f, 1.0f, 0.5f,
      1.0f, 0.0f,
      true, true, false },

    { "env4_delay", "Env 4 Delay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env4_attack", "Env 4 Attack",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 5.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env4_hold", "Env 4 Hold",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 2000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env4_decay", "Env 4 Decay",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 300.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env4_sustain", "Env 4 Sustain",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "env4_release", "Env 4 Release",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 300.0f,
      0.25f, 0.0f,
      true, true, false },

    { "env4_curve", "Env 4 Curve",
      ParameterType::floatingPoint, ParameterUnit::none,
      -1.0f, 1.0f, 0.5f,
      1.0f, 0.0f,
      true, true, false },

    // LFOs 1-4 ---------------------------------------------------------------
    // Shape is a discrete choice: 0 sine, 1 triangle, 2 saw, 3 reverse saw,
    // 4 square, 5 sample and hold, 6 step. The order matches dsp::LfoShape and,
    // like every identifier here, is permanent once released.
    //
    // Rate is skewed hard because LFO rates are used logarithmically: the
    // difference between 0.5 and 1 Hz matters far more than between 300 and
    // 400 Hz, and a linear control would spend most of its travel above the
    // range anybody adjusts.
    { "lfo1_shape", "LFO 1 Shape",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 6.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo1_rate", "LFO 1 Rate",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      0.01f, 400.0f, 1.0f,
      0.13f, 0.0f,
      true, true, true },

    { "lfo1_phase", "LFO 1 Phase",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo1_retrigger", "LFO 1 Retrigger",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo1_fade", "LFO 1 Fade In",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "lfo1_smoothing", "LFO 1 Smoothing",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo1_polarity", "LFO 1 Polarity",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo1_steps", "LFO 1 Steps",
      ParameterType::integer, ParameterUnit::none,
      2.0f, 32.0f, 8.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo2_shape", "LFO 2 Shape",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 6.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo2_rate", "LFO 2 Rate",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      0.01f, 400.0f, 1.0f,
      0.13f, 0.0f,
      true, true, true },

    { "lfo2_phase", "LFO 2 Phase",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo2_retrigger", "LFO 2 Retrigger",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo2_fade", "LFO 2 Fade In",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "lfo2_smoothing", "LFO 2 Smoothing",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo2_polarity", "LFO 2 Polarity",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo2_steps", "LFO 2 Steps",
      ParameterType::integer, ParameterUnit::none,
      2.0f, 32.0f, 8.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo3_shape", "LFO 3 Shape",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 6.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo3_rate", "LFO 3 Rate",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      0.01f, 400.0f, 1.0f,
      0.13f, 0.0f,
      true, true, true },

    { "lfo3_phase", "LFO 3 Phase",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo3_retrigger", "LFO 3 Retrigger",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo3_fade", "LFO 3 Fade In",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "lfo3_smoothing", "LFO 3 Smoothing",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo3_polarity", "LFO 3 Polarity",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo3_steps", "LFO 3 Steps",
      ParameterType::integer, ParameterUnit::none,
      2.0f, 32.0f, 8.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo4_shape", "LFO 4 Shape",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 6.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo4_rate", "LFO 4 Rate",
      ParameterType::floatingPoint, ParameterUnit::hertz,
      0.01f, 400.0f, 1.0f,
      0.13f, 0.0f,
      true, true, true },

    { "lfo4_phase", "LFO 4 Phase",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo4_retrigger", "LFO 4 Retrigger",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo4_fade", "LFO 4 Fade In",
      ParameterType::floatingPoint, ParameterUnit::milliseconds,
      0.0f, 10000.0f, 0.0f,
      0.25f, 0.0f,
      true, true, false },

    { "lfo4_smoothing", "LFO 4 Smoothing",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      0.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, true, false },

    { "lfo4_polarity", "LFO 4 Polarity",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 1.0f, 1.0f,
      1.0f, 1.0f,
      true, false, false },

    { "lfo4_steps", "LFO 4 Steps",
      ParameterType::integer, ParameterUnit::none,
      2.0f, 32.0f, 8.0f,
      1.0f, 1.0f,
      true, false, false },

    // Modulation matrix ------------------------------------------------------
    // Sixteen slots, each a source, a destination and a bipolar depth
    // (CLAUDE.md §15). Source and destination are indices into dsp::ModSource and
    // dsp::ModDestination, whose numeric order is part of the saved-state
    // contract for exactly the same reason a parameter identifier is.
    //
    // Depth is marked not modulatable. CLAUDE.md §15 says modulating a depth is
    // desirable where practical, and it is not implemented, so claiming it here
    // would put a promise in the metadata that the engine does not keep.
    { "mod01_source", "Mod 1 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod01_destination", "Mod 1 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod01_depth", "Mod 1 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod02_source", "Mod 2 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod02_destination", "Mod 2 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod02_depth", "Mod 2 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod03_source", "Mod 3 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod03_destination", "Mod 3 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod03_depth", "Mod 3 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod04_source", "Mod 4 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod04_destination", "Mod 4 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod04_depth", "Mod 4 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod05_source", "Mod 5 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod05_destination", "Mod 5 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod05_depth", "Mod 5 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod06_source", "Mod 6 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod06_destination", "Mod 6 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod06_depth", "Mod 6 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod07_source", "Mod 7 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod07_destination", "Mod 7 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod07_depth", "Mod 7 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod08_source", "Mod 8 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod08_destination", "Mod 8 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod08_depth", "Mod 8 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod09_source", "Mod 9 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod09_destination", "Mod 9 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod09_depth", "Mod 9 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod10_source", "Mod 10 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod10_destination", "Mod 10 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod10_depth", "Mod 10 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod11_source", "Mod 11 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod11_destination", "Mod 11 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod11_depth", "Mod 11 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod12_source", "Mod 12 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod12_destination", "Mod 12 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod12_depth", "Mod 12 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod13_source", "Mod 13 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod13_destination", "Mod 13 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod13_depth", "Mod 13 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod14_source", "Mod 14 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod14_destination", "Mod 14 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod14_depth", "Mod 14 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod15_source", "Mod 15 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod15_destination", "Mod 15 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod15_depth", "Mod 15 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

    { "mod16_source", "Mod 16 Source",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 15.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod16_destination", "Mod 16 Destination",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 16.0f, 0.0f,
      1.0f, 1.0f,
      true, false, false },

    { "mod16_depth", "Mod 16 Depth",
      ParameterType::floatingPoint, ParameterUnit::normalised,
      -1.0f, 1.0f, 0.0f,
      1.0f, 0.0f,
      true, false, true },

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

    // MIDI expression --------------------------------------------------------
    //
    // Setup rather than sound: these describe the controller on the desk, not
    // the patch. They are still registered parameters because they must be
    // saved with the project and shown in the interface, but none is
    // automatable — a pitch-bend range moving on an automation lane is a bug
    // being recorded, not a musical gesture.
    { "midi_bend_range", "Pitch Bend Range",
      ParameterType::integer, ParameterUnit::semitones,
      1.0f, 48.0f, 2.0f,
      1.0f, 1.0f,
      false, false, false },

    { "mpe_zone", "MPE Zone",
      ParameterType::integer, ParameterUnit::none,
      0.0f, 2.0f, 0.0f,
      1.0f, 1.0f,
      false, false, false },

    { "mpe_members", "MPE Member Channels",
      ParameterType::integer, ParameterUnit::none,
      1.0f, 15.0f, 15.0f,
      1.0f, 1.0f,
      false, false, false },

    { "mpe_bend_range", "MPE Note Bend Range",
      ParameterType::integer, ParameterUnit::semitones,
      1.0f, 96.0f, 48.0f,
      1.0f, 1.0f,
      false, false, false },

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

/** @returns the registry index of the parameter with this ID, or -1.

    An index rather than a pointer, because that is the form the subsystems
    which key arrays by parameter need: the UI bridge's dirty flags, and the MIDI
    mapping table, which is read on the audio thread and cannot afford a string
    comparison there.
*/
[[nodiscard]] constexpr int indexOfParameter (std::string_view id) noexcept
{
    for (std::size_t i = 0; i < parameterDefinitions.size(); ++i)
        if (parameterDefinitions[i].id == id)
            return static_cast<int> (i);

    return -1;
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
