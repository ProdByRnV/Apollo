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
