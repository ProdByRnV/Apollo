#pragma once

/*
    The parametric equaliser: seven bands in series, and a level to leave by.

    SEVEN, NOT FOUR. CLAUDE.md §22 asked for a four-band parametric EQ. The
    developer subsequently asked for parity with Fruity Parametric EQ 2, which
    has seven, and the later requirement governs (CLAUDE.md §42; ADR-0059). The
    difference is not cosmetic: four bands force a choice between a high pass,
    a low shelf, a midrange cut and an air shelf, and a mix engineer wants all
    four *and* somewhere to notch a resonance.

    THE BANDS ARE IN SERIES AND THE ORDER IS FIXED. Band 1 runs first and band 7
    last, always. That is not a limitation to be lifted later — a cascade of
    minimum-phase filters commutes, so the audible result of bands 3 and 5 is
    identical whichever runs first, and an equaliser that let them be reordered
    would be offering a control that changes nothing. What the numbering does
    give is a stable identity for automation and for the display: band 4 is band
    4 in every preset ever written.

    IT IS TRANSPARENT UNTIL IT IS USED. Every band defaults to a bell at 0 dB,
    which is the exact identity, so a freshly placed equaliser passes its input
    through unchanged — and, because a transparent band is skipped rather than
    multiplied through, at no cost. The seven default frequencies are spaced
    evenly along the logarithmic axis the display is drawn on, so the bands
    start out spread across the spectrum rather than piled up in the middle,
    which is where the reference starts them too.

    THE LEVEL IS THE LAST THING. One output trim, smoothed, applied after every
    band — the reference's main fader. It exists because an equaliser that has
    been used to boost needs somewhere to give the gain back, and reaching for
    the master fader to do it would change the level of everything downstream of
    the rack rather than the level of the equaliser.

    NO LATENCY. These are IIR filters working on the samples in front of them,
    so there is nothing to compensate and nothing to declare. A linear-phase
    mode — which the reference also offers, and which is an FFT and a latency
    budget rather than a variation on this — is deliberately not here.

    REAL-TIME CONTRACT: `prepare` runs while audio is stopped. Everything else is
    audio-thread callable and allocates nothing (CLAUDE.md §7).
*/

#include "DSP/EQ/EqualiserBand.h"
#include "DSP/Effects/AudioEffect.h"
#include "DSP/Utilities/LinearSmoothedValue.h"

#include <array>

namespace apollo::dsp
{

class Equaliser final : public AudioEffect
{
public:
    /** Bands, matching Fruity Parametric EQ 2. */
    static constexpr int bandCount = 7;

    /** Range of the output trim, in decibels. Symmetrical, and the same 18 the
        bands have, so that the whole interface is drawn to one scale.
    */
    static constexpr float maximumLevelDb = 18.0f;

    /** Where the seven bands sit before anyone moves them.

        Spaced evenly in the logarithm between 20 Hz and 20 kHz, which puts them
        at equal distances across the display and equal musical intervals apart
        — roughly one and a quarter octaves between neighbours.
    */
    static constexpr std::array<float, bandCount> defaultFrequencies {
        47.0f, 112.0f, 267.0f, 632.0f, 1500.0f, 3550.0f, 8430.0f
    };

    struct Settings
    {
        std::array<EqualiserBand::Settings, bandCount> bands {};

        /** Output trim in decibels, applied after every band. */
        float levelDb = 0.0f;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    /** @returns the settings a fresh equaliser has: seven transparent bells
        spread across the spectrum.
    */
    [[nodiscard]] static Settings defaultSettings() noexcept;

    Equaliser();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;

    /** No latency to compensate, so bypass is genuinely nothing — the base
        class's do-nothing implementation is correct and is not overridden.

        The bands' own state is left alone while bypassed rather than cleared,
        and each band clears itself on the way back in, which is where that
        belongs (see `EqualiserBand`).
    */
    [[nodiscard]] int getLatencySamples() const noexcept override { return 0; }

    [[nodiscard]] double getTailSeconds() const noexcept override;

    void setSettings (const Settings& newSettings) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    /** One band, for the caller that sets or inspects a single one. */
    [[nodiscard]] const EqualiserBand& getBand (int index) const noexcept;

    /** @returns the whole equaliser's gain at @p hz in decibels: every band's
        contribution summed, plus the output trim.

        This is the curve the display draws. Summing in decibels is exact rather
        than an approximation — the bands are in series, so their linear
        magnitudes multiply, and a product of magnitudes is a sum of decibels.
    */
    [[nodiscard]] double magnitudeDbAt (double hz) const noexcept;

    /** The same answer from settings alone, without a prepared equaliser. */
    [[nodiscard]] static double magnitudeDbAt (const Settings& settings,
                                               double sampleRate,
                                               double hz) noexcept;

private:
    Settings settings;

    std::array<EqualiserBand, static_cast<std::size_t> (bandCount)> bands;

    /** Smoothed in the linear domain, and linearly rather than multiplicatively:
        the trim's range reaches 0 dB from both sides and a linear ramp through
        unity is what a trim is expected to do.
    */
    LinearSmoothedValue level;

    double preparedSampleRate = 0.0;
};

} // namespace apollo::dsp
