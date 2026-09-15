#pragma once

/*
    Turning a spectrum into a band-limited wavetable.

    ONE PATH, TWO SOURCES. Apollo's built-in tables are described by formula and
    a loaded table is described by analysing somebody's audio, but both end up
    as the same thing — a list of harmonics per frame — and both are turned into
    a mipmap here. That matters more than it sounds: the band-limiting *is* the
    anti-aliasing (Wavetable.h), and a second implementation of it would be a
    second set of answers about which harmonics a note may keep.

    WHY HARMONICS AND NOT SAMPLES. A wavetable cannot be stored as the samples
    somebody drew, because the mipmap needs the same waveform at eleven
    bandwidths and there is no way to remove harmonics from a block of samples
    without knowing what they were. Going through the spectrum makes
    band-limiting exact: a level that keeps H harmonics is rendered by summing H
    harmonics, so the ones it is not allowed to have are never generated rather
    than filtered out afterwards.

    PHASE IS KEPT. A harmonic is a cosine and a sine coefficient rather than an
    amplitude, because two waveforms with identical harmonic *amplitudes* and
    different phases are different waveforms. They sound the same in isolation —
    the ear is famously insensitive to absolute phase — but they are not the
    same shape on a scope, they do not have the same peak level, and they behave
    differently the moment anything nonlinear is downstream, which in Apollo is
    a distortion, a filter drive and a compressor. A loaded table that came back
    with its phases flattened would not be the table the user supplied.

    NOT REAL-TIME SAFE. This allocates and takes milliseconds. It runs while a
    table is being prepared, never while one is being played (CLAUDE.md §7.3).
*/

#include <vector>

#include "DSP/Oscillators/Wavetable.h"

namespace apollo::dsp
{

/** One harmonic's contribution to a frame.

    The waveform is the sum over k of
    `cosine[k] * cos(2 pi k t) + sine[k] * sin(2 pi k t)`.

    The classic shapes are pure sine phase, so their `cosine` terms are zero and
    the tables below read the way the textbooks write them.
*/
struct Harmonic
{
    double cosine = 0.0;
    double sine = 0.0;

    [[nodiscard]] bool isSilent() const noexcept { return cosine == 0.0 && sine == 0.0; }
};

/** The spectrum of one frame. Index k - 1 holds harmonic k. */
using FrameSpectrum = std::vector<Harmonic>;

/** A whole table: one spectrum per frame, all the same length. */
using TableSpectrum = std::vector<FrameSpectrum>;

/** @returns a spectrum of @p numHarmonics silent harmonics. */
[[nodiscard]] FrameSpectrum makeSilentSpectrum (int numHarmonics);

/** @returns the spectrum of a table given one cycle of samples per frame.

    The other way into a wavetable: where the built-in tables are written as
    harmonics and rendered, a table somebody supplies arrives as waveforms and
    has to be taken apart before it can be band-limited. Both meet here, and
    from this point on the engine cannot tell them apart.

    Each frame must be a power of two samples long — the analysis is a
    transform, and a transform of the wrong length is not an approximation but
    a different question. A frame that is not is returned silent rather than
    guessed at, so a malformed table is quiet in one frame instead of wrong in
    all of them.

    @param frames        one cycle per frame.
    @param numHarmonics  how many to extract per frame; what the frame cannot
                         supply comes back silent.
*/
[[nodiscard]] TableSpectrum analyseFrames (const std::vector<std::vector<double>>& frames,
                                           int numHarmonics);

/** Renders @p spectrum into @p table as a band-limited mipmap.

    Each frame is rendered at every mip level, then the whole frame is scaled by
    one factor taken from its **loudest** level.

    That the factor comes from the loudest level rather than the most detailed
    one is not a detail. Removing harmonics does not simply lower a peak: a
    partial sum can peak *higher* than the complete one, because the harmonics
    that were cancelling the fundamental's crest are the ones that were removed.
    Normalising against the top level alone let the reduced levels reach 1.097 —
    measured — and a voice could then exceed the amplitude the engine's gain
    staging assumes.

    One factor per frame is equally essential: normalising each level
    independently would lift the duller ones and make a note change loudness as
    it crossed an octave boundary.

    A frame of silence is left silent rather than scaled by infinity.

    @param table     resized to the spectrum's frame count.
    @param spectrum  one entry per frame; an empty spectrum empties the table.
*/
void buildWavetable (Wavetable& table, const TableSpectrum& spectrum);

} // namespace apollo::dsp
