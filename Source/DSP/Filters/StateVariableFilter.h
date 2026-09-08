#pragma once

/*
    A topology-preserving state variable filter.

    WHY THIS TOPOLOGY. The obvious choice is a biquad with coefficients from the
    RBJ cookbook, and it is the wrong one for a synthesiser. A biquad's
    coefficients are derived through the bilinear transform, which warps the
    cutoff away from where it was asked for as the frequency rises, and its
    delay-free feedback path is only correct for the coefficients it was built
    with — modulate the cutoff quickly and it misbehaves, sometimes loudly. The
    TPT (topology-preserving transform) form solves the feedback exactly at every
    sample, so it stays stable while the cutoff is being swept, which is the
    normal condition in a synthesiser rather than an edge case.

    It also gives lowpass, bandpass, highpass and notch from the same two state
    variables at essentially no extra cost, which is what makes a mode control
    cheap enough to be worth having.

    The maths is Zavalishin's: `g` is the warped cutoff, `k` the damping (the
    reciprocal of Q), and the three `a` coefficients solve the zero-delay
    feedback loop.

    COEFFICIENTS ARE SEPARATE FROM STATE. `SvfCoefficients` holds what a cutoff
    and a resonance resolve to, and computing it costs a `tan`. State — the two
    integrators — lives in the filter. Every voice's filter can therefore share
    one coefficient set while keeping its own state, which is how the engine
    avoids computing 64 identical tangents per block (the same reasoning as the
    shared unison layout, ADR-0023). When per-voice modulation arrives with the
    matrix, a voice computes its own set instead, and nothing else changes.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O.
*/

namespace apollo::dsp
{

/** What a cutoff and a resonance resolve to. Immutable once set. */
struct SvfCoefficients
{
    /** Resolves a cutoff and resonance for a sample rate.

        @param cutoffHz    clamped to a musically useful range and, more
                           importantly, kept below Nyquist: `tan` diverges at
                           half the sample rate, and a cutoff allowed to reach
                           it produces an infinite coefficient.
        @param q           resonance as a quality factor, which is the unit the
                           registry has always used for it. Damping is its
                           reciprocal, so a higher Q is a narrower, louder peak.
        @param sampleRate  hertz.
    */
    void set (float cutoffHz, float q, double sampleRate) noexcept;

    [[nodiscard]] bool operator== (const SvfCoefficients&) const = default;

    float g = 0.0f;  ///< Warped cutoff, tan(pi * fc / fs).
    float k = 2.0f;  ///< Damping, 1/Q.
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
};

class StateVariableFilter
{
public:
    /** Filter response.

        `off` is a real mode rather than a separate enable flag: a filter that
        is not wanted should cost nothing and add nothing, and expressing that
        as a mode keeps one control where two would otherwise disagree.
    */
    enum class Mode
    {
        off = 0,
        lowpass,
        highpass,
        bandpass,
        notch
    };

    StateVariableFilter() = default;

    /** Clears the integrators. Leaves coefficients and mode alone. */
    void reset() noexcept;

    void setCoefficients (const SvfCoefficients& newCoefficients) noexcept { coefficients = newCoefficients; }
    void setMode (Mode newMode) noexcept { mode = newMode; }

    [[nodiscard]] Mode getMode() const noexcept { return mode; }

    /** Filters one sample.

        Returns the input unchanged when the mode is `off`, so a bypassed filter
        is transparent rather than merely quiet.
    */
    [[nodiscard]] float processSample (float input) noexcept;

private:
    SvfCoefficients coefficients;
    Mode mode = Mode::off;

    // The two integrator states. Named as in the derivation rather than
    // descriptively, because anyone checking this against the reference will be
    // looking for these names.
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};

} // namespace apollo::dsp
