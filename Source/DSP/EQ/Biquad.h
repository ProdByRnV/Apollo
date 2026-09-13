#pragma once

/*
    A second-order section, and the RBJ cookbook that designs it.

    WHY A BIQUAD HERE WHEN THE VOICE FILTER IS NOT ONE. `StateVariableFilter.h`
    argues at length that a biquad is the wrong choice for a synthesiser's
    filter, and that argument still stands: a biquad's delay-free feedback path
    is only correct for the coefficients it was built with, so sweeping its
    cutoff quickly misbehaves, and a synthesiser sweeps its cutoff constantly.

    An equaliser is the opposite case. Its bands sit still. They are set once,
    nudged occasionally, and automated slowly if at all — and in exchange the
    biquad gives exactly what an equaliser needs and the TPT form does not: the
    named, standard, universally agreed response shapes. A peaking bell at
    +6 dB with a one-octave bandwidth means one specific curve, every equaliser
    in the world draws that curve, and the RBJ cookbook is where its coefficients
    come from. Fruity Parametric EQ 2 — the reference Apollo's equaliser is built
    against — is RBJ too, which is the other half of the reason (ADR-0059).

    THE BILINEAR WARP IS REAL AND IS NOT COMPENSATED. The transform that turns
    an analogue prototype into these coefficients compresses the frequency axis
    towards Nyquist, so a band asked for at 18 kHz at 44.1 kHz lands slightly
    below 18 kHz and its shape is slightly squeezed. Pre-warping the centre
    frequency would fix the centre and leave the shape squeezed, which trades
    one visible error for a subtler one; every equaliser that quotes RBJ has the
    same behaviour, and matching it is the point. What *is* guaranteed is
    stability: the frequency is clamped below Nyquist before any trigonometry.

    DOUBLE PRECISION, DELIBERATELY. A narrow band low in the spectrum at a high
    sample rate puts this filter's poles very close to the unit circle, where
    single-precision coefficients quantise the pole position audibly and
    single-precision state accumulates noise in the feedback path. Doubling the
    arithmetic width costs a few multiplies per sample on the master bus, once,
    and buys a filter that behaves the same at 192 kHz as it does at 44.1 kHz
    (CLAUDE.md §47: correctness and audio quality before performance).

    DIRECT FORM II TRANSPOSED. Two state words per section rather than four, and
    the form with the best-behaved rounding of the four canonical ones.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O. The designers call `sin`, `cos`, `sinh` and
    `pow`, which is arithmetic rather than a system service, and they are called
    per block at most — never per sample.
*/

namespace apollo::dsp
{

/** What a band's settings resolve to: one second-order section's coefficients,
    already normalised so that a0 is 1.

    Separate from the state for the same reason `SvfCoefficients` is: one design
    serves both channels, and a stereo band pays for its trigonometry once.
*/
struct BiquadCoefficients
{
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    [[nodiscard]] bool operator== (const BiquadCoefficients&) const = default;

    /** @returns |H(f)|, the linear magnitude this section applies at @p hz.

        Evaluated from the coefficients rather than from the settings that
        produced them, so it reports what the filter *is* — including the
        bilinear warp and any clamping that was applied on the way in.
    */
    [[nodiscard]] double magnitudeAt (double hz, double sampleRate) const noexcept;
};

/** One section's memory. One of these per channel per section. */
struct BiquadState
{
    /** Below this the section's whole memory is inaudible and is zeroed. */
    static constexpr double denormalFloor = 1.0e-18;

    void reset() noexcept
    {
        z1 = 0.0;
        z2 = 0.0;
    }

    /** Direct form II transposed. Five multiplies, four adds, two stores. */
    [[nodiscard]] double process (double input, const BiquadCoefficients& c) noexcept
    {
        const auto output = c.b0 * input + z1;

        const auto next1 = c.b1 * input - c.a1 * output + z2;
        const auto next2 = c.b2 * input - c.a2 * output;

        // An equaliser sitting in silence has a decaying tail in every section,
        // and a denormal in a feedback path costs orders of magnitude more than
        // the sample it represents (CLAUDE.md §37).
        //
        // FLUSHED AS A PAIR, NEVER ONE WORD AT A TIME. These two words are the
        // two quadratures of a resonator: while the section rings, one of them
        // passes through zero every half cycle with all of the energy sitting in
        // the other. Zeroing that one on its own does not end the decay — it
        // perturbs the oscillator and pushes it back up, and the result is a
        // limit cycle parked exactly at the flush threshold rather than the
        // silence the flush was for. Measured, not theorised: the independent
        // version left a residual sitting at 1e-16 indefinitely.
        //
        // `bounded` is written as two comparisons rather than around `std::abs`
        // so that a NaN — which fails both — reads as unbounded-in-neither-
        // direction and is flushed away rather than latched into the filter's
        // memory for ever.
        const auto bounded = [] (double value) noexcept
        {
            return value > denormalFloor || value < -denormalFloor;
        };

        if (bounded (next1) || bounded (next2))
        {
            z1 = next1;
            z2 = next2;
        }
        else
        {
            z1 = 0.0;
            z2 = 0.0;
        }

        return output;
    }

    double z1 = 0.0;
    double z2 = 0.0;
};

/** The RBJ Audio EQ Cookbook, which is the standard every equaliser quotes.

    Each designer takes a centre frequency, a Q and — where the shape has one — a
    gain in decibels, and returns the normalised coefficients. The caller is
    responsible for nothing: the frequency is clamped into the open interval
    between DC and Nyquist and the Q away from zero, so no combination of
    settings a parameter can express produces an unstable or infinite result.
*/
namespace rbj
{

/** Widest and narrowest Q the designers will honour.

    The lower bound is well below any useful bandwidth and exists only to keep
    the divisions finite; the upper bound is far narrower than a musician would
    ever dial and exists to keep the poles off the unit circle in double
    precision.
*/
inline constexpr double minimumQ = 0.025;
inline constexpr double maximumQ = 80.0;

/** Fraction of the sample rate above which a centre frequency is clamped.

    Not 0.5: the cookbook's cosines degenerate as w0 approaches pi, and a band
    asked for *at* Nyquist has no meaning. 0.49 leaves a band at 20 kHz
    untouched at every sample rate Apollo supports.
*/
inline constexpr double maximumFrequencyFraction = 0.49;

/** Lowest centre frequency the designers will honour, in hertz. Below this a
    band is indistinguishable from DC and its poles crowd the unit circle.
*/
inline constexpr double minimumFrequency = 1.0;

/** @returns @p hz clamped into the range the designers can honour. */
[[nodiscard]] double clampFrequency (double hz, double sampleRate) noexcept;

/** @returns the Q that gives a band a @p bandwidthOctaves wide response at
    @p hz.

    The cookbook's own relation, which is where "bandwidth in octaves" and "Q"
    are reconciled: they describe the same width, and octaves are the half of
    the pair a musician can hear. It depends on the centre frequency, because
    the same number of octaves spans a different fraction of the spectrum at
    50 Hz than at 5 kHz — which is exactly why octaves are the musical unit.
*/
[[nodiscard]] double qForBandwidth (double bandwidthOctaves, double hz, double sampleRate) noexcept;

[[nodiscard]] BiquadCoefficients lowPass  (double hz, double q, double sampleRate) noexcept;
[[nodiscard]] BiquadCoefficients highPass (double hz, double q, double sampleRate) noexcept;
[[nodiscard]] BiquadCoefficients bandPass (double hz, double q, double sampleRate) noexcept;
[[nodiscard]] BiquadCoefficients notch    (double hz, double q, double sampleRate) noexcept;

[[nodiscard]] BiquadCoefficients peaking (double hz, double q, double gainDb,
                                          double sampleRate) noexcept;

[[nodiscard]] BiquadCoefficients lowShelf (double hz, double q, double gainDb,
                                           double sampleRate) noexcept;

[[nodiscard]] BiquadCoefficients highShelf (double hz, double q, double gainDb,
                                            double sampleRate) noexcept;

} // namespace rbj

} // namespace apollo::dsp
