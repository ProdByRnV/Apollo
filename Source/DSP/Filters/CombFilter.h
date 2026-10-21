#pragma once

/*
    A comb filter tuned by a cutoff, for the voice's filter slots (PRD §13.1,
    Phase 13c).

    WHAT IT IS. A feedback comb with a feedforward tap on the same delay line:

        w[n] = x[n] + a · w[n − D]
        y[n] = s · (w[n] + w[n − D]) / 2

    D is one period of the cutoff, so the comb has a peak at the cutoff and at
    every multiple of it — a resonance with harmonics, which is the point of a
    comb in a synthesiser: tuned to the note, it rings at the note's own pitch.
    The feedforward tap puts a zero half way between each pair of peaks, so the
    comb is audibly a comb even with no feedback at all, where a feedback comb
    alone would do nothing.

    RESONANCE IS FEEDBACK. `a = Q / (Q + 1/2)`, from the same Q the other filter
    types take: no dead zone at the bottom of the control, 0.59 at the default,
    0.95 at the top, and held below 0.97 whatever the matrix adds.

    LOUDNESS. `s = sqrt(2 (1 − a))`, which makes the comb's power gain on a
    broadband signal exactly 1 at every resonance — resonance changes the colour
    of noise or a dense chord and not its level. A signal whose harmonics sit on
    the teeth is lifted, by 2 sqrt(Q + 1/2): +7 dB at the default and +16 dB at
    the top, where the other types' own resonant peak reaches +20.

    TUNING. D is fractional, and is read through a first-order allpass (a
    Thiran interpolator), whose gain is exactly 1 at every frequency. So neither
    the comb's loudness nor the height of its teeth depends on whether a period
    happens to be a whole number of samples. A four-point cubic interpolator was
    built first and measured: at full resonance it took 2.9 dB off a broadband
    signal when the period was fractional and nothing when it was whole, so the
    comb grew duller or brighter from one note to the next (ADR-0091). The
    allpass's price is a delay that is slightly frequency-dependent, which
    stretches the highest teeth a few cents. D is held at three samples or
    more — a comb at a third of the sample rate, 16 kHz at 48 kHz.

    A MOVING CUTOFF GLIDES. The engine updates coefficients once per modulation
    block; a delay that jumped once a block would click at every jump, so the
    delay travels toward its target with a 3 ms time constant, every sample.
    That glide is what a swept comb's pitch bend actually sounds like.

    REAL-TIME CONTRACT. `prepare` allocates the delay line and is not real-time
    safe. Everything else is: `reset` clears only a line that has been written
    since it was last cleared, so a voice that never used its comb never pays
    for clearing it.
*/

#include <vector>

namespace apollo::dsp
{

/** What a cutoff and a resonance resolve to, for a comb. */
struct CombCoefficients
{
    /** @param cutoffHz    the comb's lowest peak. Clamped to [20 Hz, fs / 3].
        @param q           resonance as a quality factor, as for every type.
        @param sampleRate  hertz.
    */
    void set (float cutoffHz, float q, double sampleRate) noexcept;

    [[nodiscard]] bool operator== (const CombCoefficients&) const = default;

    float delaySamples = 3.0f;
    float feedback = 0.0f;
    float gain = 1.0f;   ///< s, which includes the feedforward's halving.
};

class CombFilter
{
public:
    /** The lowest a comb is tuned, which is also the cutoff parameter's floor.
        The delay line is sized for it. */
    static constexpr float lowestFrequencyHz = 20.0f;

    /** The shortest delay, in samples (see the header comment). */
    static constexpr float shortestDelaySamples = 3.0f;

    /** Allocates a delay line long enough for the lowest comb at this rate.
        NOT real-time safe. */
    void prepare (double sampleRate);

    /** Silences the line and forgets the delay, so the next coefficients are
        taken at once rather than glided to. */
    void reset() noexcept;

    void setCoefficients (const CombCoefficients& newCoefficients) noexcept;

    /** Filters one sample. Before `prepare` there is no line, and the input
        passes through unchanged rather than being lost. */
    [[nodiscard]] float processSample (float input) noexcept;

private:
    std::vector<float> line;
    int mask = 0;
    int writeIndex = 0;

    float delay = shortestDelaySamples;
    float targetDelay = shortestDelaySamples;
    float glide = 1.0f;
    float longestDelay = shortestDelaySamples;

    float feedback = 0.0f;
    float gain = 1.0f;

    /** The allpass interpolator's previous output. */
    float allpassState = 0.0f;

    /** False until coefficients arrive after a reset, so a note starts at its
        own pitch rather than gliding from the last one. */
    bool primed = false;

    /** True once anything has been written since the line was last cleared. */
    bool dirty = false;
};

} // namespace apollo::dsp
