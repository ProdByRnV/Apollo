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

    /** The pitch wheel's own position, -1 to +1, centred at rest.

        The wheel, not the semitones it produced: the bend range is a control,
        and a source that divided semitones by it would change meaning whenever
        the range was widened.
    */
    pitchBend,

    /** Aftertouch, 0 to 1, and **per note**.

        Channel pressure hands the same value to every voice the message
        reaches, which for a plain keyboard is all of them — so this behaves
        exactly as it always has. Polyphonic aftertouch and MPE address one
        voice each, and the source then differs from note to note (Phase 6b).
    */
    aftertouch,

    /** A value chosen once per note and held for its lifetime. Bipolar.

        Per note, not per sample: this is the source that makes repeated notes
        differ from one another, which is a different job from the noise
        generator and from a sample-and-hold LFO.
    */
    random,

    /** MPE's third expression dimension — CC 74, the "timbre" or "slide" axis —
        as a bipolar value centred on the controller's rest position.

        Appended rather than placed beside aftertouch, because the numeric value
        of every entry above is already in saved routings (see the note at the
        top of this file). Per note, like aftertouch.
    */
    timbre,

    /** The four macro controls, 0 to 1 after each one's range and curve
        (Macro.h). Engine-wide, like the mod wheel: every voice reads the same
        value. Appended in Phase 13e-3, so every source above keeps its number.
    */
    macro1,
    macro2,
    macro3,
    macro4,

    /** How loud the instrument is, 0 to 1 on a decibel scale, from the summed
        voices before the effects (EnvelopeFollower.h). Engine-wide, like the
        macros. Appended in Phase 18e, after release, so every source above
        keeps its number; the source range growing is recorded in ADR-0123. */
    follower,

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

    /** Each oscillator's warp amount (Phase 13b, PRD §16). Appended rather than
        placed beside the other oscillator destinations: a destination is stored
        in presets as its number, and inserting would renumber every
        destination after it (ADR-0090). */
    osc1Warp,
    osc2Warp,

    /** Each filter's drive and mix, 0 to 1, and its key tracking, -1 to 1:
        the filter controls Phase 13c added, made destinations in 13e-4 and
        appended for the same reason as the warp. */
    filter1Drive,
    filter1Mix,
    filter1KeyTrack,
    filter2Drive,
    filter2Mix,
    filter2KeyTrack,

    /** Effect parameters, appended in Phase 13e-5: the ones each effect
        smooths per sample, so an offset that arrives once a block glides.
        The effects run on the summed voices, so these are evaluated against
        the newest sounding voice, or against the engine-wide sources when
        nothing sounds (VoiceEngine::updateEffectModulation). */
    fxDistortionDrive,
    fxDistortionMix,
    fxDelayFeedback,
    fxDelayMix,
    fxReverbMix,
    fxCompressorMix,

    /** The four macros' knobs (Phase 18h): an offset moves the knob, which
        then goes through the macro's curve and range, and everything the
        macro is routed to follows (MacroModulation.h). Appended after
        release, so every destination above keeps its number; the range
        growing is recorded in ADR-0126. */
    macro1,
    macro2,
    macro3,
    macro4,

    /** The master gain, in decibels (Phase 19b). Consumed by the processor,
        after the effects, like the effect destinations: the newest voice's
        offset, or the engine-wide sources' when nothing sounds. Appended after
        release, as everything from here on is (ADR-0131). */
    masterGain,

    /** Each envelope's stage times and sustain (Phase 19d). A time moves as
        its knob does — a depth of 1 is the knob's whole travel, on the knob's
        own curve — so an offset means the same at 5 ms as at 5 s; the sustain
        is a level, 0 to 1. Per voice: velocity shortening an attack shortens
        that note's. */
    envelope1Delay,
    envelope1Attack,
    envelope1Hold,
    envelope1Decay,
    envelope1Sustain,
    envelope1Release,
    envelope2Delay,
    envelope2Attack,
    envelope2Hold,
    envelope2Decay,
    envelope2Sustain,
    envelope2Release,
    envelope3Delay,
    envelope3Attack,
    envelope3Hold,
    envelope3Decay,
    envelope3Sustain,
    envelope3Release,
    envelope4Delay,
    envelope4Attack,
    envelope4Hold,
    envelope4Decay,
    envelope4Sustain,
    envelope4Release,

    /** Each LFO's rate, as a fraction of its knob's travel on the knob's own
        curve, and its phase, in cycles (Phase 19d). Per voice. */
    lfo1Rate,
    lfo1Phase,
    lfo2Rate,
    lfo2Phase,
    lfo3Rate,
    lfo3Phase,
    lfo4Rate,
    lfo4Phase,

    count
};

/** The stages an envelope's destinations name, in the order they are listed. */
enum class EnvelopeTarget
{
    delay = 0,
    attack,
    hold,
    decay,
    sustain,
    release
};

inline constexpr int numEnvelopeTargets = 6;
inline constexpr int numLfoTargets = 2;

/** The first destination of envelope @p index (from 0): its delay. */
[[nodiscard]] constexpr ModDestination envelopeDestination (int index, EnvelopeTarget target) noexcept
{
    return static_cast<ModDestination> (static_cast<int> (ModDestination::envelope1Delay)
                                        + index * numEnvelopeTargets + static_cast<int> (target));
}

/** LFO @p index's rate (from 0), and its phase. */
[[nodiscard]] constexpr ModDestination lfoRateDestination (int index) noexcept
{
    return static_cast<ModDestination> (static_cast<int> (ModDestination::lfo1Rate) + index * numLfoTargets);
}

[[nodiscard]] constexpr ModDestination lfoPhaseDestination (int index) noexcept
{
    return static_cast<ModDestination> (static_cast<int> (ModDestination::lfo1Phase) + index * numLfoTargets);
}

/** The effect destinations, first to last; consumed by the processor rather
    than by a voice. They ended the list until 18h appended the macros. */
inline constexpr ModDestination firstEffectDestination = ModDestination::fxDistortionDrive;
inline constexpr ModDestination lastEffectDestination = ModDestination::fxCompressorMix;

inline constexpr int numEffectDestinations
    = static_cast<int> (lastEffectDestination) - static_cast<int> (firstEffectDestination) + 1;

/** The first macro destination; the four run from here to `count`. */
inline constexpr ModDestination firstMacroDestination = ModDestination::macro1;

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
        case ModDestination::osc1Warp:
        case ModDestination::osc2Warp:
        case ModDestination::filter1Drive:
        case ModDestination::filter1Mix:
        case ModDestination::filter2Drive:
        case ModDestination::filter2Mix:
            return 1.0f;

        // An octave of cutoff per octave played at full depth: half the key
        // tracking control's span, which runs -1 to 1.
        case ModDestination::filter1KeyTrack:
        case ModDestination::filter2KeyTrack:
            return 1.0f;

        // The effects' own ranges: the whole of each mix and of the delay's
        // feedback, and the distortion's whole 36 dB of drive.
        case ModDestination::fxDistortionMix:
        case ModDestination::fxDelayFeedback:
        case ModDestination::fxDelayMix:
        case ModDestination::fxReverbMix:
        case ModDestination::fxCompressorMix:
            return 1.0f;

        case ModDestination::fxDistortionDrive:
            return 36.0f;

        // The knob's whole travel, so a depth of 1 takes it from one end to
        // the other.
        case ModDestination::macro1:
        case ModDestination::macro2:
        case ModDestination::macro3:
        case ModDestination::macro4:
            return 1.0f;

        // The master knob's whole travel, -60 to +6 dB.
        case ModDestination::masterGain:
            return 66.0f;

        // A time's or a rate's whole knob travel, along the knob's curve; a
        // sustain's whole level; a whole cycle of phase.
        case ModDestination::envelope1Delay:
        case ModDestination::envelope1Attack:
        case ModDestination::envelope1Hold:
        case ModDestination::envelope1Decay:
        case ModDestination::envelope1Sustain:
        case ModDestination::envelope1Release:
        case ModDestination::envelope2Delay:
        case ModDestination::envelope2Attack:
        case ModDestination::envelope2Hold:
        case ModDestination::envelope2Decay:
        case ModDestination::envelope2Sustain:
        case ModDestination::envelope2Release:
        case ModDestination::envelope3Delay:
        case ModDestination::envelope3Attack:
        case ModDestination::envelope3Hold:
        case ModDestination::envelope3Decay:
        case ModDestination::envelope3Sustain:
        case ModDestination::envelope3Release:
        case ModDestination::envelope4Delay:
        case ModDestination::envelope4Attack:
        case ModDestination::envelope4Hold:
        case ModDestination::envelope4Decay:
        case ModDestination::envelope4Sustain:
        case ModDestination::envelope4Release:
        case ModDestination::lfo1Rate:
        case ModDestination::lfo1Phase:
        case ModDestination::lfo2Rate:
        case ModDestination::lfo2Phase:
        case ModDestination::lfo3Rate:
        case ModDestination::lfo3Phase:
        case ModDestination::lfo4Rate:
        case ModDestination::lfo4Phase:
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

    /** A second source that scales this slot's contribution — the routing's
        depth, modulated (Phase 13e-4, PRD §16). None leaves the slot as it
        was. Its value multiplies as it stands, so a unipolar source opens the
        routing from nothing to full and a bipolar one also turns it round.
    */
    ModSource via = ModSource::none;

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
