#pragma once

/*
    What can modulate what, and by how much.

    The matrix is generic by design (CLAUDE.md §15): a routing is a source, a
    destination and a bipolar depth, and nothing in the engine knows which
    particular pairings are musically sensible. That is deliberate — a matrix
    that only permits the combinations someone thought of in advance is a list of
    features, not a matrix.

    SOURCES AND DESTINATIONS ARE ENGINE CONCEPTS, NOT PARAMETER IDS. A
    destination like "oscillator 1 pitch" has no parameter behind it at all —
    oscillator 1 plays the note it is given, and only oscillator 2 has tuning
    controls. Tying destinations to parameter identifiers would therefore make
    the obvious modulation target unreachable, and would also mean a parameter
    rename changed the meaning of a saved routing. They are their own enumeration
    for both reasons.

    ORDER IS PART OF THE CONTRACT. A saved routing stores these as numbers, so
    the numeric value of every entry is permanent once released, exactly like a
    parameter identifier (Docs/PARAMETER-CONVENTIONS.md §1). New entries are
    appended; nothing is inserted or reordered.
*/

#include <array>
#include <cstddef>

namespace apollo::dsp
{

/** Something that produces a modulating value.

    Values are bipolar, -1 to +1, unless noted. Bipolar is the primary form
    because depth is bipolar, so the two compose without a conversion.
*/
enum class ModSource
{
    none = 0,   ///< An unused slot. Contributes nothing.

    envelope1,  ///< Also the amplitude envelope, and usable as a modulator too.
    envelope2,
    envelope3,
    envelope4,

    lfo1,
    lfo2,
    lfo3,
    lfo4,

    /** Note velocity, 0 to 1. Unipolar: there is no such thing as negative
        velocity, and making it bipolar would put an untouched key at -1.
    */
    velocity,

    /** Key tracking: the played note relative to middle C, scaled so the
        playable range spans roughly -1 to +1. Bipolar, because tracking should
        do the opposite thing below the centre from what it does above it.
    */
    keyTrack,

    modWheel,     ///< CC 1, 0 to 1.
    pitchBend,    ///< -1 to +1, centred at rest.
    aftertouch,   ///< Channel pressure, 0 to 1.

    /** A value chosen once per note and held for its lifetime. Bipolar.

        Per note, not per sample: this is the source that makes repeated notes
        differ from one another, which is a different job from the noise
        generator and from a sample-and-hold LFO.
    */
    random,

    count
};

/** Something a modulation can be routed to.

    Every entry is a quantity the engine already has; none of them are new
    controls. What modulation does is offset the value a parameter supplies, at
    the point it is used, without writing to the parameter itself.
*/
enum class ModDestination
{
    none = 0,

    /** Pitch, in semitones. Reaches both oscillators and the sub when routed to
        `allPitch`, which is what a vibrato normally wants.
    */
    allPitch,
    osc1Pitch,
    osc2Pitch,

    osc1Position,
    osc2Position,

    osc1Level,
    osc2Level,
    osc1Pan,
    osc2Pan,

    subLevel,
    noiseLevel,

    filter1Cutoff,
    filter1Resonance,
    filter2Cutoff,
    filter2Resonance,

    /** The voice's output amplitude, multiplying the amplitude envelope. */
    amplitude,

    count
};

/** How far a destination may be pushed by modulation at full depth.

    A depth of 1 on a destination moves it by this much. The values are what make
    a depth control mean the same thing across destinations that are measured in
    different units: half a depth on a cutoff and half a depth on a pan should
    both feel like half.
*/
[[nodiscard]] constexpr float fullDepthRange (ModDestination destination) noexcept
{
    switch (destination)
    {
        // Two octaves either way, which covers vibrato at the small end and
        // dramatic sweeps at the large one.
        case ModDestination::allPitch:
        case ModDestination::osc1Pitch:
        case ModDestination::osc2Pitch:
            return 24.0f;

        // The whole scan range, the whole level range, the whole stereo field.
        case ModDestination::osc1Position:
        case ModDestination::osc2Position:
        case ModDestination::osc1Level:
        case ModDestination::osc2Level:
        case ModDestination::osc1Pan:
        case ModDestination::osc2Pan:
        case ModDestination::subLevel:
        case ModDestination::noiseLevel:
        case ModDestination::amplitude:
            return 1.0f;

        // Cutoff is modulated in octaves rather than hertz, because that is how
        // it is heard: a fixed number of hertz is a huge move at the bottom of
        // the range and imperceptible at the top.
        case ModDestination::filter1Cutoff:
        case ModDestination::filter2Cutoff:
            return 8.0f;

        case ModDestination::filter1Resonance:
        case ModDestination::filter2Resonance:
            return 10.0f;

        case ModDestination::none:
        case ModDestination::count:
        default:
            return 0.0f;
    }
}

/** One routing: a source, a destination, and how much.

    Depth is bipolar, so a routing can invert its source as well as scale it
    (CLAUDE.md §15).
*/
struct ModulationSlot
{
    ModSource source = ModSource::none;
    ModDestination destination = ModDestination::none;
    float depth = 0.0f;

    /** True when this slot would do nothing, so it can be skipped entirely
        rather than multiplied by zero.
    */
    [[nodiscard]] constexpr bool isActive() const noexcept
    {
        return source != ModSource::none
            && destination != ModDestination::none
            && depth != 0.0f;
    }

    [[nodiscard]] bool operator== (const ModulationSlot&) const = default;
};

/** How many routings a patch may have.

    Sixteen is a judgement rather than a limit of the design: it is more than
    any patch built so far has wanted, and each slot costs three parameters, so
    doubling it doubles that cost for something nobody has asked for. Raising it
    later is additive and safe; lowering it would break saved patches.
*/
inline constexpr int maxModulationSlots = 16;

/** The whole routing table, as plain data. */
struct ModulationRouting
{
    std::array<ModulationSlot, static_cast<std::size_t> (maxModulationSlots)> slots {};

    [[nodiscard]] bool operator== (const ModulationRouting&) const = default;
};

/** The current value of every source, indexed by ModSource.

    Filled once per control block by the voice, then read by the matrix. Kept as
    a flat array rather than gathered per slot so that a source feeding six
    destinations is still only evaluated once.
*/
using ModSourceValues = std::array<float, static_cast<std::size_t> (ModSource::count)>;

/** The accumulated offset for every destination, indexed by ModDestination. */
using ModDestinationValues = std::array<float, static_cast<std::size_t> (ModDestination::count)>;

/** Evaluates @p routing against @p sources into @p destinations.

    Contributions to the same destination **add**, and the sum is not clamped
    here: what a sensible limit is depends on the destination, and the consumer
    is the only thing that knows. A level cannot go below zero; a pitch can go
    anywhere; a cutoff is clamped in octaves and then again in hertz. Clamping
    centrally would have to pick one rule and would be wrong for most of them.

    Real-time safe: no allocation, no locks, bounded by the slot count.
*/
void evaluateModulation (const ModulationRouting& routing,
                         const ModSourceValues& sources,
                         ModDestinationValues& destinations) noexcept;

} // namespace apollo::dsp
