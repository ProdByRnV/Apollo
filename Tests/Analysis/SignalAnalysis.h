#pragma once

/*
    Measuring audio, for tests that need a number rather than a verdict.

    WHY THIS EXISTS. Phase 10 validates the assembled instrument, and that needs
    measurements no test here had: total harmonic distortion, DC offset, a scan
    for inharmonic content, and a peak frequency resolved finely enough to talk
    about cents. A measurement that is quietly wrong is worse than no
    measurement — it is a number somebody will believe — so these live in one
    place, with their reasoning written down, rather than being reinvented per
    test.

    WHAT IT DOES NOT DO IS REPLACE THE FOUR SPECTRUM HELPERS the DSP tests
    already have. Those measure different things — an oversampler's stopband, a
    clipper's fold-back, a filter's corner — with thresholds tuned against their
    own analysis, and converting them would be churn in passing tests for no
    functional gain (CLAUDE.md §41). The duplication is real and is recorded as
    a known item rather than pretended away; this is the toolkit new measurement
    work should use.

    WHAT IT IS NOT. Not DSP that ships. Nothing here runs in the plugin; it is
    test equipment, and it is allowed to be slow, allocate freely and work in
    double precision, because it is standing in for an oscilloscope and an
    analyser rather than for anything in the signal path.

    THE WINDOW IS BLACKMAN-HARRIS, and that is a decision rather than a default.
    A tone whose frequency does not land exactly on a bin leaks into its
    neighbours; Hann's first sidelobe is about -31 dB, which is far above the
    aliasing and distortion figures being measured here, so leakage would be
    reported as the thing under test. It was, once: a pure sine measured
    -47 dBc at 110 Hz and -112 dBc at 3 kHz, purely because the second landed on
    a bin and the first did not. Blackman-Harris sidelobes are near -92 dB.
*/

#include <juce_dsp/juce_dsp.h>

#include <cstddef>
#include <vector>

namespace apollo::analysis
{

/** A magnitude spectrum in decibels, relative to its own loudest bin. */
struct Spectrum
{
    /** One entry per bin up to Nyquist. Index 0 is DC. */
    std::vector<float> decibels;

    double sampleRate = 0.0;
    int size = 0;

    /** @returns the bin nearest @p frequencyHz. */
    [[nodiscard]] std::size_t binFor (double frequencyHz) const noexcept;

    /** @returns the level at @p frequencyHz, in dB relative to the loudest bin. */
    [[nodiscard]] float levelAt (double frequencyHz) const noexcept;

    /** @returns the loudest bin's frequency, ignoring DC.

        Quadratically interpolated against its neighbours, so a tone between two
        bins is reported where it actually is rather than at whichever bin it
        leaned towards. That is the difference between resolving pitch to a few
        cents and to a few per cent.
    */
    [[nodiscard]] double peakFrequency() const noexcept;
};

/** @returns the spectrum of @p samples.

    @param samples     at least `size` samples; the first `size` are used.
    @param size        transform length, a power of two.
    @param sampleRate  what the samples were rendered at.
*/
[[nodiscard]] Spectrum analyse (const std::vector<float>& samples, int size, double sampleRate);

/** @returns the largest absolute sample. */
[[nodiscard]] float peakOf (const std::vector<float>& samples) noexcept;

/** @returns the root-mean-square level. */
[[nodiscard]] double rmsOf (const std::vector<float>& samples) noexcept;

/** @returns the mean sample value — the DC offset.

    A synthesiser has no business producing one: it costs headroom, it moves the
    zero crossing a downstream effect sees, and on a long note it is inaudible
    right up until something clips.
*/
[[nodiscard]] double dcOffsetOf (const std::vector<float>& samples) noexcept;

/** @returns true if every sample is finite. */
[[nodiscard]] bool allFinite (const std::vector<float>& samples) noexcept;

/** @returns the level in dB of @p value relative to 1.0, floored at @p floorDb. */
[[nodiscard]] double toDecibels (double value, double floorDb = -200.0) noexcept;

/** Total harmonic distortion plus noise, as a fraction of the whole signal.

    Measured the way an analyser measures it: the energy in everything that is
    not the fundamental, over the energy in all of it. A pure sine reads near
    zero; anything else reads how far from a sine it is.

    ONLY MEANINGFUL FOR A SIGNAL THAT IS SUPPOSED TO BE A SINE. A saw is all
    harmonics by design, and its THD is a number about the waveform rather than
    about the instrument's fidelity — which is why the validation suite measures
    this on the sub oscillator, the one source whose output is a sine on purpose.

    @param samples       the rendered tone.
    @param fundamentalHz what it was supposed to be.
    @param sampleRate    what it was rendered at.
    @param size          transform length, a power of two.
*/
[[nodiscard]] double totalHarmonicDistortionPlusNoise (const std::vector<float>& samples,
                                                       double fundamentalHz,
                                                       double sampleRate,
                                                       int size);

/** @returns the highest level, in dB relative to the fundamental, of any bin
    that is not the fundamental or one of its harmonics.

    The aliasing figure: everything inharmonic that the instrument produced and
    should not have. Harmonics are excluded because a bright waveform is
    supposed to have them.

    @param tolerance  how far from an exact multiple still counts as a harmonic,
                      as a fraction of the fundamental. Wide enough to cover
                      windowing spread and detuned unison, narrow enough that a
                      genuine alias between harmonics is still caught.
*/
[[nodiscard]] float worstInharmonicLevel (const Spectrum& spectrum,
                                          double fundamentalHz,
                                          double tolerance = 0.06);

} // namespace apollo::analysis
