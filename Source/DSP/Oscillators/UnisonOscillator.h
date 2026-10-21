#pragma once

/*
    A stereo unison stack of wavetable oscillators.

    Holds maxVoices WavetableOscillators and sums the first `count` of them,
    detuned and panned according to a UnisonLayout it does not own. The layout
    is shared by every voice in the engine (see UnisonLayout), so this class
    holds a non-owning pointer and re-reads it when its generation changes.

    WHAT THIS DOES *NOT* DO. It applies no level and no oscillator pan. The
    stack produces a stereo image whose centre is unity; balancing that image
    left or right, and scaling it, are the Voice's job, because those two are
    the parameters that must be smoothed per sample and the Voice is where the
    smoothers live. Keeping them out of here also means the stereo image is
    built once, by the spread, rather than panned twice.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O. The only non-trivial work is re-deriving the
    per-voice frequencies, which happens on a note start, a pitch change or a
    layout change, never per sample.
*/

#include "DSP/Oscillators/WavetableOscillator.h"
#include "DSP/Unison/UnisonLayout.h"

#include <array>
#include <cstdint>

namespace apollo::dsp
{

class UnisonOscillator
{
public:
    static constexpr int maxVoices = UnisonLayout::maxVoices;

    /** How the waveform is reshaped as it is read (PRD §12, Phase 13b).

        Every mode is a change to *where* in the cycle the table is read, never
        to the samples, so an amount of zero is exactly no warp, and the table's
        own band-limiting still applies. The values are the `oscN_warp_mode`
        parameter's, and permanent.

          SYNC    reads the cycle up to eight times per period, restarting at
                  the period: hard sync.
          ASYM    squeezes the first half of the cycle and stretches the second,
                  moving the waveform's midpoint earlier.
          PWM     plays the whole cycle in a shrinking fraction of the period and
                  holds its starting value for the rest: pulse-width modulation
                  of any waveform.
          BEND    slows the middle of the cycle and speeds its ends, by a cubic
                  that keeps both ends fixed.
          MIRROR  plays the first half of the cycle forwards and then backwards,
                  crossfaded with the original by the amount.
    */
    enum class WarpMode
    {
        off = 0,
        sync = 1,
        asym = 2,
        pwm = 3,
        bend = 4,
        mirror = 5
    };

    static constexpr int numWarpModes = 6;

    UnisonOscillator() = default;

    /** Sets the warp. Cheap when nothing changed, which is most calls: the
        voice sets it every modulation block.

        @param amount  [0, 1]; clamped, and NaN is zero.
    */
    void setWarp (WarpMode mode, float amount) noexcept;

    /** @returns how much faster than the note the warp reads the table at its
        fastest point — 1 when there is no warp. Exposed for tests. */
    [[nodiscard]] double getWarpSlope() const noexcept { return warpSlope; }

    /** @returns where in the cycle the warp reads for phase @p p, in [0, 1).
        The mapping itself, exposed so tests can check it rather than infer it.
        MIRROR is not a phase mapping and returns @p p unchanged. */
    [[nodiscard]] double warpPhase (double p) const noexcept;

    /** Draws one cycle of @p source at @p normalisedPosition as this warp
        reads it, into @p points evenly spaced phases of @p destination.

        The picture of the warp (PRD §30.2), drawn by the same mapping the voices
        play rather than a copy of it: from the table's finest level, as the
        unwarped display always was, and centred when warped, because the DC
        blocker removes the warped waveform's average from what is heard.
        Without the edges' corrections, which exist only for a sampled signal.
    */
    void drawWarpedCycle (const Wavetable& source, float normalisedPosition,
                          float* destination, int points) const noexcept;

    void setSampleRate (double newSampleRate) noexcept;

    /** Points every unison voice at a table. Passing nullptr silences them. */
    void setTable (const Wavetable* newTable) noexcept;

    /** Points the stack at a layout. Passing nullptr falls back to a single
        centred voice at unity gain, so a mis-wired oscillator still sounds.
    */
    void setLayout (const UnisonLayout* newLayout) noexcept;

    /** Sets the fundamental, before unison detune, in hertz. */
    void setFrequency (double frequencyHz) noexcept;

    /** Sets the scan position across the table, normalised to [0, 1]. */
    void setPosition (float normalisedPosition) noexcept;

    /** Restarts every unison voice from @p basePhase plus its layout offset. */
    void resetPhase (double basePhase) noexcept;

    /** Restarts every unison voice, adding a random offset to each (PRD §11).

        @param randomAmount  how much of a cycle the offset may span, [0, 1].
                             Zero is exactly `resetPhase (basePhase)`.
        @param seed          chosen per note by the caller. The offsets are a
                             hash of it and the voice's index, not draws from a
                             shared generator, so the same seed restarts the
                             same stack the same way: a note renders
                             identically every time it is rendered, which is
                             what the regression renders and ADR-0017 rely on.
    */
    void resetPhase (double basePhase, float randomAmount, std::uint32_t seed) noexcept;

    /** Adds the next sample of the stack into @p left and @p right, advancing
        every active unison voice by one sample.

        Additive so the Voice can accumulate its sources into one stereo pair
        without a scratch buffer.
    */
    void addNextStereoSample (float& left, float& right) noexcept;

    [[nodiscard]] int getActiveVoiceCount() const noexcept;

private:
    void applyLayout() noexcept;

    /** @returns the layout in force, never null. */
    [[nodiscard]] const UnisonLayout& currentLayout() const noexcept
    {
        return layout != nullptr ? *layout : fallbackLayout;
    }

    /** Re-derives one voice's read position from the table, its own pitch and
        the shared scan position.
    */
    void refreshVoice (int index) noexcept;

    /** Renders the stack with the current warp. A separate path from the
        unwarped one, so that a patch with no warp runs exactly the code it ran
        before warp existed, at exactly its cost. */
    void addNextWarpedStereoSample (float& left, float& right) noexcept;

    /** The warped stack, specialised per mode so that nothing inside the voice
        loop asks which mode it is. The first version did ask, once per voice
        per sample, among other costs — and the worst patch the controls allow
        missed its deadline with warp on (ADR-0090). */
    template <WarpMode mode>
    void renderWarpedStack (int count, double& sumLeft, double& sumRight) noexcept;

    /** The read position for master phase @p p under @p mode — the one mapping
        both the render and warpPhase() use. */
    template <WarpMode mode>
    [[nodiscard]] double mapWarpPhase (double p) const noexcept;

    // Reciprocals of the warp's constants, so the per-sample mapping multiplies.
    double asymRiseScale = 1.0;   // 0.5 / knee
    double asymFallScale = 1.0;   // 0.5 / (1 - knee)
    double pwmScale = 1.0;        // 1 / width

    // THE WARP. The mode and amount the user asked for, and the constants each
    // mode needs, derived once in setWarp rather than per sample.
    WarpMode warpMode = WarpMode::off;
    float warpAmount = 0.0f;

    double syncRatio = 1.0;
    double asymKnee = 0.5;
    double pwmWidth = 1.0;
    double bendCurve = 0.0;
    double mirrorMix = 0.0;

    /** The fastest the warp reads the table, as a multiple of the note. The mip
        level is chosen for the note times this, so the table carries no
        harmonic the warp could push past Nyquist (§12.2). */
    double warpSlope = 1.0;

    // THE WARP'S EDGES, AND WHY THEY NEED THEIR OWN ANTI-ALIASING (ADR-0090).
    //
    // Choosing the mip level for the fastest read is necessary and is not
    // enough. A warp is a nonlinear map of the read position, so — like FM —
    // it spreads a band-limited waveform's spectrum without limit, and most of
    // what it spreads comes from a few points per cycle: a JUMP in the output
    // (sync restarting the cycle) or a CORNER, where the read speed changes
    // abruptly (asym's knee, PWM's edge, mirror's turn). A jump's harmonics
    // fall at 6 dB an octave and a corner's at 12, which no oversampling factor
    // anyone would pay for brings under the budget at high notes. Measured
    // before this existed: -25 to -47 dBc at 1 kHz.
    //
    // So each edge is corrected where it happens: a polyBLEP residual for a
    // jump and a polyBLAMP residual for a corner, over the two samples either
    // side, with the size of each taken from the table. The edges' positions
    // are fixed by the mode; their sizes depend on the table and the read
    // position, so they are recomputed per voice whenever those change.
    static constexpr int maxWarpEdges = 2;

    int warpEdgeCount = 0;
    std::array<double, maxWarpEdges> warpEdgePhase {};

    /** Per voice, per edge: how far the output jumps there, and how far its
        slope (per unit of phase) changes. */
    std::array<std::array<double, maxWarpEdges>, static_cast<std::size_t> (maxVoices)> edgeJump {};
    std::array<std::array<double, maxWarpEdges>, static_cast<std::size_t> (maxVoices)> edgeSlope {};

    void computeWarpEdges (int index) noexcept;

    /** The warped output of voice @p index at phase @p p, before any edge
        correction. Used to size the edges and to find the waveform's mean. */
    [[nodiscard]] double warpedValue (int index, double p) const noexcept;

    /** The same through any reader: what warpedValue and drawWarpedCycle share. */
    [[nodiscard]] double warpedRead (const Wavetable::Reader& reader, double p) const noexcept;

    // A WARPED WAVEFORM IS NOT CENTRED. Warping a zero-mean cycle in time
    // changes its average — mirroring a saw reads only its rising half — and
    // the offset was measured at up to a quarter of full scale. A one-pole DC
    // blocker on the warped stack removes it; it is started from the warped
    // waveform's own mean when a note begins, so the note does not open with a
    // decaying step.
    double dcCoefficient = 0.0;
    double dcInLeft = 0.0;
    double dcOutLeft = 0.0;
    double dcInRight = 0.0;
    double dcOutRight = 0.0;

    /** False until the blocker's state describes the signal it is filtering —
        at a note's start, or when warp rises from zero mid-note. */
    bool dcPrimed = false;

    //==========================================================================
    // THE STACK IS FLAT, NOT SIXTEEN OSCILLATOR OBJECTS (ADR-0074).
    //
    // This held a `std::array<WavetableOscillator, 16>` until Phase 10d-5. Each
    // of those is about a hundred bytes — table pointer, Reader, sample rate,
    // frequency, phase, increment, positions, mip level — so reading sixteen
    // phases meant sixteen loads a hundred bytes apart, and the gains came
    // through a call into the layout object per voice per sample.
    //
    // Phase 10d-4 measured what removing that is worth: **1.39x** on a
    // sixteen-voice stack, with no change to the arithmetic and no vector code.
    // It also measured what float and SIMD would add on top — a further 1.27x
    // — and that was declined, so what follows is deliberately ordinary scalar
    // double-precision code whose only trick is where it keeps its data.
    //
    // Parallel arrays rather than an array of structures, because the second
    // pass of `addNextStereoSample` walks each of these in order and that is
    // the shape a compiler can vectorise.

    /** Where each voice is in its cycle, and how far it advances per sample.
        The two hottest arrays in the instrument.
    */
    std::array<double, static_cast<std::size_t> (maxVoices)> phase {};
    std::array<double, static_cast<std::size_t> (maxVoices)> increment {};

    /** Each voice's resolved read position.

        Per voice rather than shared because detuning can put two voices on
        different mip levels — the levels change at particular frequencies and a
        stack spread over fifty cents can straddle one. Usually they are all
        identical, and nothing here depends on that being so.
    */
    std::array<Wavetable::Reader, static_cast<std::size_t> (maxVoices)> readers {};

    /** Copied out of the layout when it changes, rather than fetched through it
        per voice per sample.
    */
    std::array<float, static_cast<std::size_t> (maxVoices)> gainLeft {};
    std::array<float, static_cast<std::size_t> (maxVoices)> gainRight {};

    const Wavetable* table = nullptr;

    double sampleRate = 44100.0;

    const UnisonLayout* layout = nullptr;

    /** Used when no layout has been supplied. Default-constructed to a single
        centred voice, so the fallback is a plain mono oscillator rather than
        silence.
    */
    UnisonLayout fallbackLayout;

    double frequency = 0.0;

    /** The scan position, held here so that a voice which becomes active later
        can be given it.

        Since ADR-0071 setting a position rebuilds a Reader, so `setPosition`
        touches only the voices that are actually sounding rather than all
        sixteen — a one-voice stack must not pay for fifteen it does not have.
        The voices above the count are brought up to date by `applyLayout`, the
        one place a voice can become active.
    */
    float normalisedPosition = 0.0f;

    /** The layout generation the per-voice frequencies were derived from, so a
        layout that changes underneath this object is noticed rather than
        assumed away.
    */
    unsigned int appliedGeneration = 0;
};

} // namespace apollo::dsp
