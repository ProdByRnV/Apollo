#pragma once

/*
    The shape of a unison stack: how its voices are detuned, where they sit in
    the stereo field, and how the sum is normalised.

    WHY THIS IS A SEPARATE OBJECT. The layout depends only on the unison count,
    the detune amount and the stereo spread — never on pitch, note or voice. It
    is therefore identical for every sounding voice, and computing it once per
    block in the engine costs a few dozen transcendental calls instead of a few
    thousand (32 voices x 2 oscillators x 16 unison voices). The oscillators
    hold a pointer to it and read.

    It is also the whole of the unison *design* in one testable place: the
    detune distribution, the pan law and the gain normalisation are decisions
    with audible consequences, and they should be assertable without
    constructing an oscillator.

    REAL-TIME CONTRACT: `update` is callable from the audio thread — it
    allocates nothing and its work is bounded by maxVoices — but it is expected
    to run at most once per block, guarded by `matches`.
*/

#include <array>

namespace apollo::dsp
{

struct UnisonLayout
{
    /** Builds the identity layout: one centred voice at unity gain and pitch.

        A default-constructed layout must be usable, not an all-zero table that
        would silence any oscillator pointed at it before the engine got round
        to its first update.
    */
    UnisonLayout() noexcept;

    /** Hard ceiling on unison voices, matching the `oscN_unison` parameter
        range documented in UI_BINDINGS.md §3.
    */
    static constexpr int maxVoices = 16;

    /** Detune, in cents, at full `detune` amount, measured from the centre of
        the stack to its edge.

        50 cents is a quarter tone, which is as wide as a unison stack can go
        before it stops reading as one thickened note and starts reading as a
        chord. It is the outer edge of the useful range rather than a typical
        setting: the parameter's default of 0.2 lands on +/-10 cents, which is
        the classic detuned-supersaw amount.
    */
    static constexpr float maxDetuneCents = 50.0f;

    /** Rebuilds the layout.

        @param voiceCount     unison voices, clamped to [1, maxVoices].
        @param detuneAmount   normalised [0, 1], scaled by maxDetuneCents.
        @param stereoSpread   normalised [0, 1]; 0 stacks every voice at centre.
    */
    void update (int voiceCount, float detuneAmount, float stereoSpread) noexcept;

    /** @returns true if the layout was built from exactly these values.

        Lets the caller skip `update` on the overwhelmingly common block where
        nothing moved, without keeping a separate dirty flag that could go stale.
    */
    [[nodiscard]] bool matches (int voiceCount, float detuneAmount, float stereoSpread) const noexcept;

    [[nodiscard]] int getCount() const noexcept { return count; }

    /** Increments on every `update`.

        A consumer holds a pointer to a layout that mutates underneath it, so it
        cannot detect a change by comparing pointers. Caching this counter lets
        an oscillator notice that the detune distribution moved and re-derive
        its per-voice frequencies, without the engine having to remember to tell
        every voice — a duty that would be silently forgotten exactly once.
    */
    [[nodiscard]] unsigned int getGeneration() const noexcept { return generation; }

    /** Frequency multiplier for one unison voice. Index must be < getCount(). */
    [[nodiscard]] double getFrequencyRatio (int index) const noexcept
    {
        return frequencyRatio[static_cast<std::size_t> (index)];
    }

    /** Left/right gain for one unison voice, including stack normalisation. */
    [[nodiscard]] float getGainLeft (int index) const noexcept
    {
        return gainLeft[static_cast<std::size_t> (index)];
    }

    [[nodiscard]] float getGainRight (int index) const noexcept
    {
        return gainRight[static_cast<std::size_t> (index)];
    }

    /** Phase offset a unison voice starts a note from, in [0, 1).

        Distinct per voice for the same reason the engine gives distinct start
        phases to its voices: a stack that all starts at phase zero sums
        coherently on the attack, and a detuned stack is *most* coherent exactly
        when it is loudest. Fixed rather than random, so rendering stays
        reproducible.
    */
    [[nodiscard]] double getStartPhase (int index) const noexcept
    {
        return startPhase[static_cast<std::size_t> (index)];
    }

private:
    int count = 1;
    unsigned int generation = 1;

    // The values the current layout was built from, so `matches` can compare
    // exactly rather than approximately.
    int builtVoiceCount = -1;
    float builtDetuneAmount = -1.0f;
    float builtStereoSpread = -1.0f;

    std::array<double, static_cast<std::size_t> (maxVoices)> frequencyRatio {};
    std::array<float, static_cast<std::size_t> (maxVoices)> gainLeft {};
    std::array<float, static_cast<std::size_t> (maxVoices)> gainRight {};
    std::array<double, static_cast<std::size_t> (maxVoices)> startPhase {};
};

} // namespace apollo::dsp
