#pragma once

/*
    A real-input discrete Fourier transform, for turning a waveform into the
    harmonics it is made of.

    WHY APOLLO HAS ITS OWN. The only thing that needs a transform is wavetable
    analysis: a frame of samples has to become a list of harmonics before it can
    be band-limited (WavetableBuilder.h). That is one use, off the audio thread,
    on power-of-two buffers, and JUCE's own FFT lives in `juce_dsp` — a module
    the engine does not otherwise link, and which would be pulled into the
    plugin for this alone. A radix-2 transform is sixty lines and is exactly the
    case CLAUDE.md §32 has in mind: do not add a dependency for something the
    standard library and a page of code already cover.

    It also keeps `apollo_core` free of JUCE, which is what lets the DSP be
    tested without a plugin host.

    NOT REAL-TIME SAFE. It allocates. Analysis happens while a table is being
    prepared, never while one is being played.

    ACCURACY. Double precision throughout, decimation-in-time, with the twiddle
    factors computed from `std::cos`/`std::sin` per stage rather than by
    repeated complex multiplication — the recurrence is faster and accumulates
    error across the stage, which is exactly wrong for the low harmonics that
    carry most of a waveform.
*/

#include <complex>
#include <cstddef>
#include <vector>

namespace apollo::dsp
{

/** @returns true if @p size is a power of two and at least 2. */
[[nodiscard]] constexpr bool isTransformSize (std::size_t size) noexcept
{
    return size >= 2 && (size & (size - 1)) == 0;
}

/** Transforms @p samples in place.

    @param samples  a power-of-two number of complex values. Anything else is
                    left untouched, because a transform of the wrong length is
                    a caller's mistake rather than something to approximate.
*/
void forwardTransform (std::vector<std::complex<double>>& samples);

/** The same transform at one size, prepared once and run many times.

    `forwardTransform` computes every twiddle factor as it goes, which is right
    for wavetable analysis — it runs a few hundred times when a table is built
    and never again — and wrong for a spectrum display, which transforms the same
    length thirty times a second for as long as the editor is open. This holds
    the factors in a table instead.

    The table is filled from `std::cos`/`std::sin` entry by entry, not by
    recurrence, so its accuracy is `forwardTransform`'s exactly; the two produce
    the same result to rounding, and a test holds them to that.

    `prepare` allocates. `perform` does not, and is safe to call at a display's
    frame rate on the message thread. It is not meant for the audio thread, and
    nothing calls it there.
*/
class PreparedTransform
{
public:
    /** Sizes the tables. Anything that is not a transform size leaves the
        object unprepared, and `perform` then does nothing. */
    void prepare (std::size_t newSize);

    [[nodiscard]] std::size_t getSize() const noexcept { return size; }

    /** Transforms @p samples, `getSize()` complex values, in place. */
    void perform (std::complex<double>* samples) const noexcept;

private:
    std::size_t size = 0;

    /** e^(-2 pi i k / size), for k below size / 2. A stage of span s reads
        every (size / s)-th entry. */
    std::vector<std::complex<double>> twiddles;

    /** Where each index moves to before the butterflies begin. */
    std::vector<std::size_t> reversed;
};

/** @returns the first @p numHarmonics harmonics of one cycle of @p samples.

    The waveform is taken to be exactly one period long, so bin k *is* harmonic
    k — no windowing, no interpolation, no leakage. That is the whole reason
    wavetable frames are stored as single cycles.

    Each returned pair is scaled so that summing
    `cosine[k] cos(2 pi k t) + sine[k] sin(2 pi k t)` reproduces the input, which
    is the convention `WavetableBuilder` renders with. The DC term is discarded:
    a wavetable frame with an offset is a frame that pushes a voice off centre,
    and no oscillator wants one.

    @param samples       one cycle. Must be a power of two in length.
    @param numHarmonics  how many to return; the transform can supply at most
                         half the length, and the rest come back silent.

    @returns an empty spectrum if @p samples is not a usable length.
*/
[[nodiscard]] std::vector<std::complex<double>> analyseCycle (const std::vector<double>& samples,
                                                              int numHarmonics);

} // namespace apollo::dsp
