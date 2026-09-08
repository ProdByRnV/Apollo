#pragma once

/*
    Translation from Apollo's parameter registry into JUCE's parameter system.

    The registry (ParameterDefinitions.h) is authoritative. This is the only
    place that turns it into APVTS objects, so the host-facing parameters, the
    bridge metadata and the preset system are all generated from one source and
    cannot drift apart (UI_BINDINGS.md §3).
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters/ParameterDefinitions.h"

namespace apollo::params
{

/** ValueTree type of the APVTS state root.

    Part of the serialized-state contract: changing it invalidates every saved
    project and preset, so it is fixed here and versioned separately
    (Docs/VERSIONING.md §3).
*/
inline constexpr const char* stateTreeType = "ApolloState";

/** ValueTree type of one saved parameter inside the state root.

    JUCE writes an APVTS state as a flat list of these, and a migration step
    that renames a parameter has to find them by tag. Named here rather than
    spelled inline at the point of use, because it is part of the same
    serialized-state contract as the root type above.
*/
inline constexpr const char* parameterTreeType = "PARAM";

/** Property holding a saved parameter's identifier. */
inline constexpr const char* parameterIdProperty = "id";

/** VST3 parameter version hint.

    VST3 derives a parameter's identity from a hash that includes this hint. It
    changes only for a deliberate, migrated break in parameter identity; bumping
    it casually would silently detach every existing automation lane.
*/
inline constexpr int parameterVersionHint = 1;

/** @returns the normalisable range implied by a definition. */
[[nodiscard]] juce::NormalisableRange<float> makeRange (const ParameterDefinition& definition);

/** @returns the APVTS layout for the whole registry, in registry order. */
[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** @returns a juce::String copy of a registry string_view. */
[[nodiscard]] juce::String toJuceString (std::string_view text);

} // namespace apollo::params
