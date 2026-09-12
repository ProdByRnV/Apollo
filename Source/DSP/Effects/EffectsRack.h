#pragma once

/*
    The effects rack: six slots, in the order the user put them in.

    WHAT A RACK IS HERE. Not a container of effects — a *permutation* of them.
    Every effect Apollo has is constructed once, prepared once and owned by this
    object for its lifetime; a slot holds which effect is in that position, and
    reordering the chain rewrites six enums. Nothing is created, destroyed,
    moved or allocated when the user changes the rack, which is what makes
    reordering safe to do while audio is running (CLAUDE.md §9.1).

    PRD §18 asks for add, remove, reorder and bypass. Here:

      - add / remove  = putting an effect in a slot, or setting the slot empty.
      - reorder       = putting it in a different slot.
      - bypass        = leaving it in the chain with its bypass set, which keeps
                        its position and its latency while taking it out of
                        circuit.

    DUPLICATES ARE RESOLVED, NOT REJECTED. Two slots can be set to the same
    effect — by automation, by a preset written by a future version, by a user
    dragging quickly. There is only one of each effect, so the second occurrence
    cannot be honoured: the first one in chain order wins and the later slot
    reads as empty. Deterministic, and the same answer every time the same chain
    is applied, which is what a preset needs.

    REAL-TIME CONTRACT. `prepare` allocates and runs while audio is stopped.
    `setChain`, `process` and `reset` are audio-thread callable and allocate
    nothing, lock nothing and perform no I/O (CLAUDE.md §7).
*/

#include "DSP/Delay/Delay.h"
#include "DSP/Distortion/Distortion.h"
#include "DSP/Effects/AudioEffect.h"
#include "DSP/Reverb/Reverb.h"

#include <array>

namespace apollo::dsp
{

class EffectsRack
{
public:
    /** One position in the chain. */
    struct Slot
    {
        EffectType effect = EffectType::none;

        /** Switched out of circuit, but still in the chain and still counted in
            the rack's latency. See `AudioEffect::processBypassed`.
        */
        bool bypassed = false;

        [[nodiscard]] bool operator== (const Slot&) const = default;
    };

    using Chain = std::array<Slot, rackSlotCount>;

    EffectsRack() = default;

    /** Prepares every effect, whether or not it is in the chain.

        All of them, deliberately: an effect that is prepared only when it is
        first used would have to allocate on the audio thread the moment someone
        added it to a slot. Preparing the whole set costs a few buffers once.
    */
    void prepare (double sampleRate, int maxBlockSize);

    /** Clears every effect's state, including effects not in the chain. */
    void reset() noexcept;

    /** Sets the chain. Audio-thread safe.

        The chain stored is the *resolved* one: duplicates and effects whose
        phase has not landed are reduced to empty slots, so what
        `getChain()` returns is what `process` will actually do.
    */
    void setChain (const Chain& newChain) noexcept;

    /** The resolved chain. */
    [[nodiscard]] const Chain& getChain() const noexcept { return chain; }

    /** Runs the chain in place over the output buffer. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Total delay the chain adds, in samples.

        Counts every effect in the chain whether bypassed or not, because a
        bypassed effect still routes the signal through its compensation delay.
        Changes only when the chain changes.
    */
    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples; }

    /** The longest tail any active effect in the chain has, in seconds.

        Computed on demand rather than cached, and that is not an oversight: an
        effect's tail depends on its *settings* — a delay's time and feedback, a
        reverb's decay — not only on whether it is in the chain. A cached figure
        was refreshed when the chain changed and then went stale the moment
        anyone turned the decay control, which meant the host was told the tail
        of a reverb nobody was using any more.

        Latency stays cached because the audio thread reads it on every block
        and it genuinely only moves with the chain. This is read by the host on
        the message thread, where six virtual calls cost nothing worth saving.
    */
    [[nodiscard]] double getTailSeconds() const noexcept;

    /** True for an effect that exists in this build.

        The slot parameter's range covers every effect Apollo will have, so that
        the automation contract is fixed from the start (ADR-0054); this is what
        tells the rack which of those values mean something yet.
    */
    [[nodiscard]] static bool isImplemented (EffectType type) noexcept;

    /** The host's tempo, for the effects that sync to it. Audio-thread safe.

        The rack takes it rather than each effect being handed it separately,
        because the tempo is a property of the session rather than of any one
        effect, and one call per block is cheaper than asking which effects care.
    */
    void setTempo (double bpm) noexcept;

    /** The effects themselves, for the caller that resolves their parameters. */
    [[nodiscard]] Distortion& distortion() noexcept { return distortionUnit; }
    [[nodiscard]] const Distortion& distortion() const noexcept { return distortionUnit; }

    [[nodiscard]] Delay& delay() noexcept { return delayUnit; }
    [[nodiscard]] const Delay& delay() const noexcept { return delayUnit; }

    [[nodiscard]] Reverb& reverb() noexcept { return reverbUnit; }
    [[nodiscard]] const Reverb& reverb() const noexcept { return reverbUnit; }

private:
    /** @returns the effect an enum names, or nullptr for `none` and for
        anything not implemented yet.
    */
    [[nodiscard]] AudioEffect* effectFor (EffectType type) noexcept;
    [[nodiscard]] const AudioEffect* effectFor (EffectType type) const noexcept;

    void refreshLatency() noexcept;

    Distortion distortionUnit;
    Delay delayUnit;
    Reverb reverbUnit;

    Chain chain {};

    int latencySamples = 0;
};

} // namespace apollo::dsp
