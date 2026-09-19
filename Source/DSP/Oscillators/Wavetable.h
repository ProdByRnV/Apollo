#pragma once

/*
    A band-limited wavetable.

    ANTI-ALIASING STRATEGY (roadmap Phase 4, PRD §8.3, ARCHITECTURE.md §2.2).

    A wavetable is a periodic waveform, so playing it back at frequency f
    reproduces every harmonic it contains at multiples of f. A saw wave holding
    1024 harmonics played at 1 kHz demands content up to 1 MHz; everything above
    Nyquist folds back into the audible band as inharmonic noise, and no amount
    of interpolation removes it, because the aliasing happened in the source
    material, not in the reading of it.

    Apollo therefore stores each waveform as a **mipmap**: a series of versions
    of the same shape, each band-limited to half as many harmonics as the last.
    At playback the oscillator picks the highest-detail level whose harmonics all
    stay below Nyquist for the note being played. That is a table lookup per
    note, not per sample, and it removes aliasing at the source.

    Each level also stores only as many samples as its bandwidth needs — a level
    limited to H harmonics needs 2H samples — so the whole mipmap costs about
    twice the top level rather than N times it.

    A wavetable owns heap storage and is built outside the audio thread
    (CLAUDE.md §12.3). Once built it is immutable, so any number of voices can
    read it concurrently without synchronisation.
*/

#include <cstddef>
#include <vector>

namespace apollo::dsp
{

class Wavetable
{
public:
    /** Samples in the most detailed mip level. Also twice its harmonic limit. */
    static constexpr int topLevelSamples = 2048;

    /** Harmonics retained in the most detailed level.

        1024 harmonics is enough for a 20 Hz fundamental to stay fully bright up
        to about 20 kHz, which is the lowest note anyone will play.
    */
    static constexpr int topLevelHarmonics = topLevelSamples / 2;

    /** Number of mip levels. Level L keeps topLevelHarmonics >> L harmonics, so
        eleven levels reach a single harmonic — a pure sine, correct at any
        pitch up to Nyquist.
    */
    static constexpr int numMipLevels = 11;

    /** Harmonics retained at a mip level. */
    [[nodiscard]] static constexpr int harmonicsAtLevel (int level) noexcept
    {
        const auto harmonics = topLevelHarmonics >> level;
        return harmonics > 1 ? harmonics : 1;
    }

    /** Smallest frame size, regardless of bandwidth.

        Twice the harmonic count is the information-theoretic minimum for
        *storage*, but it is a poor basis for *interpolation*: at 2H samples the
        highest harmonic gets only two samples per cycle, and reading it at
        arbitrary phase produces large interpolation error.

        At the detailed levels that does not matter, because the harmonics near
        the limit are very quiet — a saw's 1024th harmonic is already 60 dB
        down, so its interpolation error is 60 dB down with it. At the coarse
        levels used for high notes it matters a great deal: level 8 holds only
        four harmonics, the highest of which is barely 12 dB down.

        Measured with 16 samples here, a 3520 Hz saw aliased at -35 dBc, against
        -101 dBc an octave below. Raising the floor to 512 gives every coarse
        level well over a hundred samples per cycle of its top harmonic. It
        costs under a megabyte across all four built-in tables, because the
        levels it affects are exactly the ones with few harmonics.
    */
    static constexpr int minSamplesPerFrame = 512;

    /** Samples stored per frame at a mip level: twice its harmonic limit, or the
        floor above, whichever is larger.

        **Every level's frame size is a power of two, and the read path depends
        on it.** Both candidates are: a harmonic limit is `topLevelHarmonics >>
        level` and doubling it keeps it a power of two, and the floor is 512.
        Interpolation has to wrap its four taps around the end of a periodic
        frame, and a power-of-two size turns that wrap from an integer division
        into a mask — eight divisions a sample, at 1088 oscillators, being the
        single largest cost the Phase 10c profile found (ADR-0070).

        This is an invariant rather than a coincidence, so it is asserted at
        compile time below rather than left to be rediscovered by whoever next
        changes minSamplesPerFrame.
    */
    [[nodiscard]] static constexpr int samplesAtLevel (int level) noexcept
    {
        const auto samples = harmonicsAtLevel (level) * 2;
        return samples > minSamplesPerFrame ? samples : minSamplesPerFrame;
    }

    /** @returns the mask that wraps an index into a frame at this level.

        Valid only because samplesAtLevel is always a power of two; see above.
    */
    [[nodiscard]] static constexpr int indexMaskAtLevel (int level) noexcept
    {
        return samplesAtLevel (level) - 1;
    }


    /** @returns the mip level whose harmonics all stay below Nyquist.

        @param frequencyHz    the fundamental being played.
        @param sampleRate     the current sample rate.

        Picks the most detailed level that is still safe, and clamps at both
        ends so an absurd frequency degrades to a sine rather than to aliasing
        or to an out-of-range index.
    */
    [[nodiscard]] static int selectMipLevel (double frequencyHz, double sampleRate) noexcept;

    Wavetable() = default;

    /** Allocates storage for @p numFrames frames. Not real-time safe. */
    void setSize (int numFrames);

    /** Writable access to one frame at one level, for the generator.

        Returns nullptr for an out-of-range request rather than misbehaving.
    */
    [[nodiscard]] float* getWritePointer (int level, int frameIndex) noexcept;

    [[nodiscard]] const float* getReadPointer (int level, int frameIndex) const noexcept;

    [[nodiscard]] int getNumFrames() const noexcept { return numFrames; }
    [[nodiscard]] bool isEmpty() const noexcept { return numFrames <= 0; }

    /** Reads one interpolated sample.

        @param level       mip level, from selectMipLevel.
        @param frameIndex  frame to read.
        @param phase       normalised position in the cycle, [0, 1).

        Interpolation is 4-point cubic Hermite rather than linear. Linear
        interpolation of a sampled waveform is a lowpass filter whose response
        depends on where between samples you land, so it both dulls the top end
        and modulates that dulling as the phase drifts — audible as a faint
        buzz on sustained notes. Hermite's error is far below the level the
        mipmap already guarantees.
    */
    [[nodiscard]] float getSample (int level, int frameIndex, double phase) const noexcept;

    /** Reads one sample interpolated across both position and frame.

        @param framePosition  continuous frame position, [0, numFrames - 1].

        Frames are blended linearly. Scanning a wavetable is a slow, deliberate
        gesture, so linear blending is inaudible here and costs one crossfade
        rather than a second cubic evaluation.

        Convenience over Reader, and the slower way to do it: reading a run of
        samples at one level and position should make one Reader and keep it.
    */
    [[nodiscard]] float getSampleAtPosition (int level, double framePosition, double phase) const noexcept;

    /** Everything about a read that does not change from sample to sample.

        A voice reads one table, at one mip level, at one frame position, for a
        whole modulation block at a time — sixteen samples (`Voice.h`). Before
        this existed, every one of those samples re-derived the same frame
        pointers: two bounds-checked lookups, a `vector<vector<float>>` double
        indirection, three separate recomputations of the level's frame size,
        and the frame-position clamp and blend weight. None of it can change
        within a block, and it was the largest remaining per-sample cost after
        Phase 10d-1 (ADR-0071).

        **Lifetime.** A Reader holds raw pointers into the table's storage, so
        it is exactly as valid as the `const Wavetable*` an oscillator already
        held, and no more: a table is immutable once built, and a slot replaced
        while playing hands the voice a new pointer, at which point the
        oscillator makes a new Reader (§12.4). A Reader must not outlive the
        table it came from.

        Default-constructed, it is valid to use and produces silence.
    */
    class Reader
    {
    public:
        Reader() = default;

        /** @returns false for a Reader that will only ever produce silence —
                    no table, an empty one, or an out-of-range level.
        */
        [[nodiscard]] bool isValid() const noexcept { return lowerFrame != nullptr; }

        /** Reads one interpolated sample at @p phase, wrapped into [0, 1).

            This is the innermost function in the instrument. It is the only
            implementation of the interpolation arithmetic — `getSample` and
            `getSampleAtPosition` both route through it — which is what keeps
            the one-frame and two-frame paths from drifting apart.
        */
        [[nodiscard]] float read (double phase) const noexcept;

    private:
        friend class Wavetable;

        const float* lowerFrame = nullptr;

        /** Equal to lowerFrame when there is nothing to blend towards: a
            one-frame table, the last frame, or a blend weight of exactly zero.
            Comparing the two pointers is how `read` picks its path, so a
            degenerate blend costs one cubic rather than one cubic and four
            multiplications by nothing.
        */
        const float* upperFrame = nullptr;

        double blend = 0.0;

        int size = 0;
        int mask = 0;
    };

    /** Resolves a level and frame position into a Reader.

        @param framePosition  continuous frame position, clamped into
                              [0, numFrames - 1]. A non-finite position is
                              treated as the first frame rather than refused:
                              a broken scan control should leave the instrument
                              sounding (§33), and unlike a broken phase there is
                              an obvious answer to fall back on.
    */
    [[nodiscard]] Reader makeReader (int level, double framePosition) const noexcept;

private:
    [[nodiscard]] std::size_t offsetOf (int level, int frameIndex) const noexcept;

    int numFrames = 0;

    /** One contiguous buffer per mip level, holding numFrames frames back to
        back. Contiguous so a frame read touches one cache line run rather than
        chasing pointers.
    */
    std::vector<std::vector<float>> levels;
};

/** The invariant the read path masks on, checked where the class is complete.

    Asserted rather than commented because the failure mode is silent: a
    non-power-of-two frame size would leave `indexMaskAtLevel` wrapping to the
    wrong sample instead of to the right one, which is a quiet change in the
    waveform rather than a crash.
*/
static_assert (
    []
    {
        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            const auto size = Wavetable::samplesAtLevel (level);

            if (size <= 0 || (size & (size - 1)) != 0)
                return false;
        }

        return true;
    }(),
    "Wavetable frame sizes must be powers of two: the interpolator wraps its taps with a mask "
    "rather than a division. A non-power-of-two minSamplesPerFrame breaks that.");

} // namespace apollo::dsp
