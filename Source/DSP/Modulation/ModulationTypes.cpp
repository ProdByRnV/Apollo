#include "DSP/Modulation/ModulationTypes.h"

namespace apollo::dsp
{

void evaluateModulation (const ModulationRouting& routing,
                         const ModSourceValues& sources,
                         ModDestinationValues& destinations) noexcept
{
    destinations.fill (0.0f);

    for (const auto& slot : routing.slots)
    {
        if (! slot.isActive())
            continue;

        const auto sourceIndex = static_cast<std::size_t> (slot.source);
        const auto destinationIndex = static_cast<std::size_t> (slot.destination);

        // Bounds are checked rather than assumed. These indices come from saved
        // state, which a corrupt or hand-edited preset can put out of range, and
        // the arrays they index are fixed size.
        if (sourceIndex >= sources.size() || destinationIndex >= destinations.size())
            continue;

        // Depth is scaled by the destination's own range here rather than by the
        // consumer, so every consumer receives an offset already in its own
        // units — semitones, octaves, or a fraction of a control's travel.
        //
        // A via source scales the whole contribution: the routing's depth,
        // modulated. Checked like the others, and an ordinary slot keeps the
        // expression it always had, so it adds exactly what it always added.
        const auto viaIndex = static_cast<std::size_t> (slot.via);

        if (slot.via == ModSource::none || viaIndex >= sources.size())
            destinations[destinationIndex] += sources[sourceIndex]
                                            * slot.depth
                                            * fullDepthRange (slot.destination);
        else
            destinations[destinationIndex] += sources[sourceIndex]
                                            * slot.depth
                                            * fullDepthRange (slot.destination)
                                            * sources[viaIndex];
    }
}

} // namespace apollo::dsp
