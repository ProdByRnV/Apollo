#pragma once

/*
    Halfband filters for 2x sample-rate conversion.

    A halfband lowpass cuts at exactly a quarter of its own sample rate, which is
    the Nyquist frequency of the rate below it. That is the filter a 2x
    up- or down-conversion needs, and it has a property worth exploiting: every
    second coefficient is exactly zero. Half the multiplies in the obvious
    implementation are multiplies by zero, and the polyphase form below never
    performs them.

    WHY LINEAR PHASE. The alternative — a cascade of allpass sections, the
    classic cheap IIR halfband — costs less and adds less delay, and Apollo does
    not use it. A nonlinear stage is normally mixed against its own dry signal
    (`fx_distortion_mix` is already a registered parameter), and a dry path and a
    phase-rotated wet path sum to comb filtering rather than to a blend. A
    linear-phase FIR delays every frequency equally, so the dry path only has to
    be delayed by a whole number of samples to line back up. The cost is latency,
    which is reported rather than hidden (ADR-0028).

    WHY THE WINDOW IS COMPUTED, NOT TABULATED. The coefficients come from a
    Kaiser-windowed sinc evaluated at `design` time rather than from a block of
    tabulated constants. The stopband attenuation and the tap count are then
    visible as the numbers they are, and the tests can sweep them instead of
    trusting a table nobody can re-derive.

    REAL-TIME CONTRACT: `design` and `prepare` allocate and must run before
    audio starts. Every other function is callable from the audio thread and
    allocates nothing, locks nothing and performs no I/O.
*/

#include <cstddef>
#include <vector>

namespace apollo::dsp
{

/** A designed halfband filter, decomposed into the two branches the polyphase
    form needs. Immutable once designed, and shared by an upsampler and a
    downsampler, which hold their own state.
*/
class HalfbandCoefficients
{
public:
    /** Tap count for Apollo's default quality.

        79 taps puts the stopband below -100 dB and keeps the passband flat past
        20 kHz when the filter runs at twice a 48 kHz base rate, costing 40
        multiplies per converted sample. `OversamplingTests` measures what this
        design actually achieves rather than trusting the number.
    */
    static constexpr int defaultNumTaps = 79;

    /** Designs the filter. Allocates; call before audio starts.

        @param numTaps    odd, >= 7. Even values are rounded up to the next odd
                          number, because a halfband filter has a centre tap by
                          definition and an even length has no centre.
        @param stopbandDb desired stopband attenuation in dB, as a positive
                          number. Sets the Kaiser window's beta.
    */
    void design (int numTaps, double stopbandDb);

    [[nodiscard]] bool isDesigned() const noexcept { return ! branchTaps.empty(); }

    /** The full impulse response, including the zero coefficients.

        Exposed so tests can measure the frequency response of the filter that
        was actually designed, and can check that the zeros really are zero,
        rather than measuring the polyphase branches and assuming they compose.
    */
    [[nodiscard]] const std::vector<float>& getImpulseResponse() const noexcept { return impulse; }

    /** Taps of the branch that does the filtering, at the lower rate. */
    [[nodiscard]] const std::vector<float>& getBranchTaps() const noexcept { return branchTaps; }

    /** Index of the centre tap, which is also the filter's group delay in
        samples at the higher rate.
    */
    [[nodiscard]] int getCentre() const noexcept { return centre; }

    /** True when the centre tap sits at an odd index.

        This decides which output phase is a plain delay and which is the
        filtered branch, and it is the only thing that differs between the two
        legal halfband lengths. A filter with an odd centre (length 4k+3) has
        its zeros at the odd taps; one with an even centre (length 4k+1) has
        them at the even taps. Apollo uses both: see Oversampler, which needs a
        stage of each parity so that a 4x round trip lands on a whole number of
        samples.
    */
    [[nodiscard]] bool hasOddCentre() const noexcept { return (centre % 2) != 0; }

private:
    std::vector<float> impulse;
    std::vector<float> branchTaps;
    int centre = 0;
};

/** Converts one channel from a rate to twice that rate. */
class HalfbandUpsampler
{
public:
    /** Binds to coefficients and sizes the state. Allocates.

        @param coefficients  must outlive this object and stay designed.
        @param maxInputSamples  largest block that will be passed to `process`.
    */
    void prepare (const HalfbandCoefficients& coefficients, int maxInputSamples);

    void reset() noexcept;

    /** Writes 2 * numInput samples into @p output.

        @p output must have room for 2 * numInput floats, and numInput must not
        exceed the prepared maximum.
    */
    void process (const float* input, float* output, int numInput) noexcept;

private:
    const HalfbandCoefficients* coeffs = nullptr;

    std::vector<float> history;   ///< Filtered branch, at the lower rate.
    std::vector<float> delayLine; ///< Pure-delay branch, at the lower rate.

    std::size_t historyPos = 0;
    std::size_t delayPos = 0;
    int delaySamples = 0;
};

/** Converts one channel from a rate to half that rate. */
class HalfbandDownsampler
{
public:
    void prepare (const HalfbandCoefficients& coefficients, int maxOutputSamples);

    void reset() noexcept;

    /** Reads 2 * numOutput samples from @p input and writes numOutput. */
    void process (const float* input, float* output, int numOutput) noexcept;

private:
    const HalfbandCoefficients* coeffs = nullptr;

    std::vector<float> history;
    std::vector<float> delayLine;

    std::size_t historyPos = 0;
    std::size_t delayPos = 0;
    int delaySamples = 0;

    /** Extra delay applied to the filtered branch, in lower-rate samples.

        One when the centre tap sits at an even index, zero otherwise. The
        decimator reads the odd-phase branch one sample further back in that
        case — it falls straight out of the algebra, and omitting it produces a
        filter that still looks and sounds like a filter while being wrong by
        almost its full amplitude.
    */
    std::size_t filterExtraDelay = 0;
};

} // namespace apollo::dsp
