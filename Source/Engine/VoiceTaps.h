#pragma once

/*
    Where a voice deposits each part of itself, so it can be watched separately.

    PRD §30.1 asks for a scope on every individual source rather than only on the
    mix, and the only place those signals exist is inside the per-sample loop:
    by the time a voice has returned, oscillator 1, oscillator 2, the sub and the
    noise have already been added together and the sum is all that is left.

    THIS IS A SET OF DESTINATIONS, NOT A CAPTURE. A voice writes plain floats
    into plain buffers and knows nothing about rings, threads or interfaces —
    which keeps `Voice` exactly as testable as it was, and keeps the visualisation
    transport out of the DSP. The engine owns the buffers, sums every voice into
    them, and is the only thing that knows where they go afterwards.

    ADDITIVE, like `renderAdding` itself, because these are sums across the whole
    voice pool: what "oscillator 1" is producing is what all of its voices are
    producing together. The engine clears them before each pass, so a pass in
    which nothing sounds leaves zeroes — which is what makes a source that stops
    *seen* to stop rather than holding its last picture (CLAUDE.md §26.1).

    MONO, because a scope draws one trace. The two channels are averaged rather
    than summed, so a centred source reads at the level it plays at instead of
    twice it — the same convention the output tap uses.

    A NULL POINTER MEANS "NOT WATCHED", and is the normal case: when no editor is
    open nothing is tapped and the voice runs the code it ran before any of this
    existed.
*/

namespace apollo::engine
{

/** Optional mono destinations for one render call.

    Every pointer is either null or addresses at least as many samples as the
    call renders. INDEXED FROM ZERO — these are per-call scratch buffers, not the
    host's output, so they do not share `startSample`.
*/
struct VoiceTaps
{
    /** The four sources, taken after their own level and balance and before the
        filter and the amplifier.

        Before the amplifier deliberately: this is the scope you watch while
        choosing a wavetable position, and an envelope's shape drawn over the
        waveform would obscure the only thing you are looking at. What the
        envelope does to the sound is what `postFilter` and the output scope are
        for.
    */
    float* oscillator1 = nullptr;
    float* oscillator2 = nullptr;
    float* sub = nullptr;
    float* noise = nullptr;

    /** The voice's finished contribution: sources summed, filtered, and scaled
        by the envelope, velocity and any steal fade. Everything the voice does,
        immediately before it is added to the mix.
    */
    float* postFilter = nullptr;

    /** True if anything at all is being watched. */
    [[nodiscard]] bool any() const noexcept
    {
        return oscillator1 != nullptr || oscillator2 != nullptr || sub != nullptr
            || noise != nullptr || postFilter != nullptr;
    }
};

} // namespace apollo::engine
