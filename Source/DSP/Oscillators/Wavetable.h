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
    */
    [[nodiscard]] static constexpr int samplesAtLevel (int level) noexcept
    {
        const auto samples = harmonicsAtLevel (level) * 2;
        return samples > minSamplesPerFrame ? samples : minSamplesPerFrame;
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
    */
    [[nodiscard]] float getSampleAtPosition (int level, double framePosition, double phase) const noexcept;

private:
    [[nodiscard]] std::size_t offsetOf (int level, int frameIndex) const noexcept;

    int numFrames = 0;

    /** One contiguous buffer per mip level, holding numFrames frames back to
        back. Contiguous so a frame read touches one cache line run rather than
        chasing pointers.
    */
    std::vector<std::vector<float>> levels;
};

} // namespace apollo::dsp
