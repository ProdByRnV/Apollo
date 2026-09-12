#pragma once

/*
    The contract every effect in Apollo's rack obeys.

    ARCHITECTURE.md §4 requires one common processing contract — prepare,
    process, reset, bypass, latency, tail — so that the rack can hold any effect
    without knowing which one it is holding, and so that a new effect is a new
    class rather than a new branch in the rack.

    WHY THE INTERFACE IS VIRTUAL AND WHY THAT IS AFFORDABLE. A rack of six slots
    makes at most six indirect calls per block, not per sample: the dispatch is
    outside the sample loop, where its cost is a rounding error against the
    thousands of samples the call goes on to process. The alternative — a variant
    or a switch over a closed enum — buys nothing measurable and makes every new
    effect an edit to the rack.

    BYPASS IS NOT "DO NOTHING". An effect that reports latency must keep
    reporting the same latency while it is bypassed, or toggling bypass would
    change the plugin's delay and force the host to re-plan its graph mid
    performance. `processBypassed` exists for that: an effect with latency pushes
    the signal through its compensation delay and nothing else, so audio stays
    aligned and the number the host was told stays true (ADR-0054).

    REAL-TIME CONTRACT. `prepare` allocates and runs while audio is stopped.
    `process`, `processBypassed`, `reset` and the reporting functions are
    audio-thread callable and must not allocate, lock or perform I/O
    (CLAUDE.md §7).
*/

namespace apollo::dsp
{

/** Which effect occupies a rack slot.

    The full set is enumerated here from the start, before most of these exist,
    and deliberately. The slot parameters that select an effect are discrete
    parameters whose range is part of Apollo's permanent automation contract: a
    range that grew from two values to seven as effects landed would silently
    remap every saved automation lane and every preset written before the change
    (Docs/PARAMETER-CONVENTIONS.md §1, ADR-0054). Selecting an effect whose phase
    has not landed yet leaves the slot empty rather than doing something
    surprising.
*/
enum class EffectType
{
    none = 0,    ///< An empty slot. Costs nothing and adds nothing.
    distortion,  ///< Phase 8a.
    delay,       ///< Phase 8b.
    reverb,      ///< Phase 8c.
    gate,        ///< Phase 8d.
    compressor,  ///< Phase 8d.
    equaliser    ///< Phase 8e.
};

/** Number of values `EffectType` has, including `none`. */
inline constexpr int effectTypeCount = 7;

/** Slots in the rack.

    Six, because six is the number of effects PRD §18 lists, so every effect can
    be in the chain at once and no slot is a queue. Fixed rather than dynamic:
    the rack is allocated once and reordering is a permutation, not an
    allocation (CLAUDE.md §9.1).
*/
inline constexpr int rackSlotCount = 6;

/** The largest channel count any effect must handle.

    Apollo's output bus is mono or stereo (`isBusesLayoutSupported`), so effects
    size their per-channel state for two and are handed no more than that.
*/
inline constexpr int maxEffectChannels = 2;

class AudioEffect
{
public:
    virtual ~AudioEffect() = default;

    /** Sizes buffers and designs filters. Allocates; call while stopped.

        @param sampleRate    hertz.
        @param maxBlockSize  largest block that will be passed to `process`.
    */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Clears transient state without releasing resources. Audio-thread safe. */
    virtual void reset() noexcept = 0;

    /** Processes in place.

        @param channels    per-channel write pointers.
        @param numChannels 1 or 2, never more than `maxEffectChannels`.
        @param numSamples  never more than the prepared maximum.
    */
    virtual void process (float* const* channels, int numChannels, int numSamples) noexcept = 0;

    /** Passes audio through while the effect is switched out of circuit.

        The default does nothing, which is correct for any effect that reports
        zero latency. An effect with latency must override this to apply the
        same delay its active path applies, so that bypassing changes what is
        heard without changing when it is heard.
    */
    virtual void processBypassed (float* const* channels, int numChannels, int numSamples) noexcept
    {
        (void) channels;
        (void) numChannels;
        (void) numSamples;
    }

    /** Delay this effect adds, in samples at the base rate.

        Must not depend on bypass, and must not change except across `prepare`.
        A latency that moves while audio is running makes the host's delay
        compensation wrong for as long as it takes to renegotiate.
    */
    [[nodiscard]] virtual int getLatencySamples() const noexcept { return 0; }

    /** How long this effect keeps producing sound after its input stops.

        Reported so an offline render captures a reverb ending rather than
        truncating it.
    */
    [[nodiscard]] virtual double getTailSeconds() const noexcept { return 0.0; }
};

} // namespace apollo::dsp
