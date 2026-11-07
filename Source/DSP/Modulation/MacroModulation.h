#pragma once

/*
    The macros as destinations as well as sources (Phase 18h, ADR-0126).

    A routing to a macro MOVES ITS KNOB. The offset is added to where the knob
    is, and the result goes through the macro's curve and range as the knob
    does, so a macro set to 0.2 to 0.6 stays between 0.2 and 0.6 however hard
    an LFO pushes it, and everything the macro is routed to follows. That is
    what makes one routing onto Macro 1 drive five destinations at once.

    ONE LEVEL, SO NO LOOPS. The matrix is evaluated with every macro where its
    knob is. If that put an offset on any macro, the macros so moved are
    shaped again and the matrix is evaluated once more; the offsets on the
    macros themselves are kept from the first pass. So a macro routed to
    another macro reads the other's knob as set, not as modulated, and Macro 1
    routed to Macro 2 routed to Macro 1 is two routings, not a feedback loop.

    A patch with no routing to a macro is evaluated once, exactly as before.

    REAL-TIME CONTRACT: no allocation, bounded by two passes over the slots.
*/

#include "DSP/Modulation/Macro.h"
#include "DSP/Modulation/ModulationTypes.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace apollo::dsp
{

/** One macro as the matrix needs it: where the knob is, and its shape. */
struct MacroInput
{
    float knob = 0.0f;
    MacroSettings settings {};

    [[nodiscard]] bool operator== (const MacroInput&) const = default;
};

using MacroInputs = std::array<MacroInput, static_cast<std::size_t> (numMacros)>;

/** The source index of macro @p index (from 0). */
[[nodiscard]] constexpr std::size_t macroSourceIndex (int index) noexcept
{
    return static_cast<std::size_t> (static_cast<int> (ModSource::macro1) + index);
}

/** The destination index of macro @p index (from 0). */
[[nodiscard]] constexpr std::size_t macroDestinationIndex (int index) noexcept
{
    return static_cast<std::size_t> (static_cast<int> (firstMacroDestination) + index);
}

/** Fills the macro sources of @p sources from @p macros, evaluates @p routing,
    and, where it moved a macro, evaluates it again with that macro moved.

    @p sources must hold every other source already. On return its macro
    entries are what the matrix read, modulated where they were, and
    @p destinations holds every offset, the macros' own as the first pass
    found them.
*/
inline void evaluateModulationWithMacros (const ModulationRouting& routing,
                                          ModSourceValues& sources,
                                          const MacroInputs& macros,
                                          ModDestinationValues& destinations) noexcept
{
    for (int i = 0; i < numMacros; ++i)
    {
        const auto& macro = macros[static_cast<std::size_t> (i)];
        sources[macroSourceIndex (i)] = shapeMacro (macro.knob, macro.settings);
    }

    evaluateModulation (routing, sources, destinations);

    std::array<float, static_cast<std::size_t> (numMacros)> offsets {};
    auto moved = false;

    for (int i = 0; i < numMacros; ++i)
    {
        const auto offset = destinations[macroDestinationIndex (i)];

        // An offset that is not a number moves nothing, rather than pinning
        // the macro to one end.
        offsets[static_cast<std::size_t> (i)] = std::isfinite (offset) ? offset : 0.0f;
        destinations[macroDestinationIndex (i)] = offsets[static_cast<std::size_t> (i)];
        moved = moved || offsets[static_cast<std::size_t> (i)] != 0.0f;
    }

    if (! moved)
        return;

    for (int i = 0; i < numMacros; ++i)
    {
        const auto offset = offsets[static_cast<std::size_t> (i)];

        if (offset != 0.0f)
        {
            const auto& macro = macros[static_cast<std::size_t> (i)];
            sources[macroSourceIndex (i)] = shapeMacro (macro.knob + offset, macro.settings);
        }
    }

    evaluateModulation (routing, sources, destinations);

    for (int i = 0; i < numMacros; ++i)
        destinations[macroDestinationIndex (i)] = offsets[static_cast<std::size_t> (i)];
}

} // namespace apollo::dsp
