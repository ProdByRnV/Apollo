#pragma once

/*
    Which control each modulation destination moves, and what dropping a source
    on one does to the matrix (Phase 13h-4, ADR-0108).

    A destination is an engine concept and deliberately not a parameter id
    (DSP/Modulation/ModulationTypes.h): "All Pitch" and the voice's amplitude
    have no control of their own. But most destinations are the quantity one
    knob already sets, and dragging a source onto that knob is how a user says
    "modulate this". So the correspondence lives here, once, beside the
    registry it names — and the page is told it in the parameter metadata
    rather than keeping a copy that could drift.

    The table is the authority for two things: which knobs accept a dropped
    source, and which knob an assigned routing lights. Both used to be a table
    in the page.
*/

#include "DSP/Modulation/ModulationTypes.h"
#include "Parameters/ParameterDefinitions.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace apollo::params
{

/** A destination and the parameter whose value it offsets. */
struct ModulationTarget
{
    dsp::ModDestination destination = dsp::ModDestination::none;
    std::string_view parameterId;
};

/** Every destination with a control of its own. All Pitch and Amplitude are
    absent rather than guessed at: neither is one knob. */
inline constexpr std::array<ModulationTarget, 28> modulationTargets { {
    { dsp::ModDestination::osc1Pitch,         "osc1_semitones" },
    { dsp::ModDestination::osc2Pitch,         "osc2_semitones" },
    { dsp::ModDestination::osc1Position,      "osc1_position" },
    { dsp::ModDestination::osc2Position,      "osc2_position" },
    { dsp::ModDestination::osc1Level,         "osc1_level" },
    { dsp::ModDestination::osc2Level,         "osc2_level" },
    { dsp::ModDestination::osc1Pan,           "osc1_pan" },
    { dsp::ModDestination::osc2Pan,           "osc2_pan" },
    { dsp::ModDestination::subLevel,          "sub_level" },
    { dsp::ModDestination::noiseLevel,        "noise_level" },
    { dsp::ModDestination::filter1Cutoff,     "filter1_cutoff" },
    { dsp::ModDestination::filter1Resonance,  "filter1_resonance" },
    { dsp::ModDestination::filter2Cutoff,     "filter2_cutoff" },
    { dsp::ModDestination::filter2Resonance,  "filter2_resonance" },
    { dsp::ModDestination::osc1Warp,          "osc1_warp_amount" },
    { dsp::ModDestination::osc2Warp,          "osc2_warp_amount" },
    { dsp::ModDestination::filter1Drive,      "filter1_drive" },
    { dsp::ModDestination::filter1Mix,        "filter1_mix" },
    { dsp::ModDestination::filter1KeyTrack,   "filter1_keytrack" },
    { dsp::ModDestination::filter2Drive,      "filter2_drive" },
    { dsp::ModDestination::filter2Mix,        "filter2_mix" },
    { dsp::ModDestination::filter2KeyTrack,   "filter2_keytrack" },
    { dsp::ModDestination::fxDistortionDrive, "fx_distortion_drive" },
    { dsp::ModDestination::fxDistortionMix,   "fx_distortion_mix" },
    { dsp::ModDestination::fxDelayFeedback,   "fx_delay_feedback" },
    { dsp::ModDestination::fxDelayMix,        "fx_delay_mix" },
    { dsp::ModDestination::fxReverbMix,       "fx_reverb_mix" },
    { dsp::ModDestination::fxCompressorMix,   "fx_compressor_mix" },
} };

/** @returns the destination that offsets parameter @p id, or none. */
[[nodiscard]] constexpr dsp::ModDestination destinationForParameter (std::string_view id) noexcept
{
    for (const auto& target : modulationTargets)
        if (target.parameterId == id)
            return target.destination;

    return dsp::ModDestination::none;
}

/** @returns the parameter destination @p destination offsets, or empty. */
[[nodiscard]] constexpr std::string_view parameterForDestination (dsp::ModDestination destination) noexcept
{
    for (const auto& target : modulationTargets)
        if (target.destination == destination)
            return target.parameterId;

    return {};
}

/** The depth a dropped source starts at: a quarter of the destination's full
    range, which is two octaves of cutoff either way from an LFO, or six
    semitones of pitch. Enough to be heard and seen at once, so a drop is never
    met with silence, and small enough that it is rarely the wrong order of
    magnitude. Changed from there on the control or in the matrix. */
inline constexpr float dropDepth = 0.25f;

/** What a drop does. */
enum class DropOutcome
{
    /** An empty slot now routes the source to the destination at dropDepth. */
    assigned,

    /** A slot already pairing them had a depth of zero, so moved nothing; its
        depth is now dropDepth. Reported as an assignment, because to the user
        it is one. */
    revived,

    /** A slot already routes the source to the destination, and nothing
        changes: a second slot doing the same thing would double the depth in
        a way the matrix does not show. */
    alreadyRouted,

    /** Every slot is in use. */
    matrixFull
};

struct DropPlan
{
    DropOutcome outcome = DropOutcome::matrixFull;

    /** The slot it concerns, from 0, or -1 when the matrix is full. */
    int slot = -1;
};

/** Decides what dropping @p source on @p destination does to @p routing.

    A slot is free only when it has neither a source nor a destination. One with
    either is somebody's unfinished routing, and a drop has no business
    finishing it differently. */
[[nodiscard]] constexpr DropPlan planDrop (const dsp::ModulationRouting& routing,
                                           dsp::ModSource source,
                                           dsp::ModDestination destination) noexcept
{
    for (std::size_t i = 0; i < routing.slots.size(); ++i)
    {
        const auto& slot = routing.slots[i];

        if (slot.source == source && slot.destination == destination)
            return { slot.depth == 0.0f ? DropOutcome::revived : DropOutcome::alreadyRouted,
                     static_cast<int> (i) };
    }

    for (std::size_t i = 0; i < routing.slots.size(); ++i)
    {
        const auto& slot = routing.slots[i];

        if (slot.source == dsp::ModSource::none && slot.destination == dsp::ModDestination::none)
            return { DropOutcome::assigned, static_cast<int> (i) };
    }

    return {};
}

/** @returns the name the interface gives a source, as the matrix lists it. */
[[nodiscard]] constexpr std::string_view sourceName (dsp::ModSource source) noexcept
{
    constexpr std::array<std::string_view, static_cast<std::size_t> (dsp::ModSource::count)> names { {
        "—", "ENV 1", "ENV 2", "ENV 3", "ENV 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
        "Velocity", "Key Track", "Mod Wheel", "Pitch Bend", "Aftertouch", "Random",
        "Timbre", "Macro 1", "Macro 2", "Macro 3", "Macro 4",
    } };

    const auto index = static_cast<std::size_t> (source);
    return index < names.size() ? names[index] : std::string_view {};
}

} // namespace apollo::params
