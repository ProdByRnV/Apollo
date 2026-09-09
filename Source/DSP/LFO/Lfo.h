#pragma once

/*
    A low-frequency oscillator.

    Separate from the wavetable oscillators, and deliberately so. Those exist to
    be heard and are built around not aliasing: band-limited mipmaps, cubic
    interpolation, a mip level chosen per note. An LFO is never heard directly —
    it moves a control — so none of that machinery buys anything, and a square
    wave that is *exactly* square is more useful than one that has been rounded
    off to keep its harmonics below a Nyquist frequency it will never approach.

    WHAT AN LFO NEEDS THAT AN AUDIO OSCILLATOR DOES NOT. Retrigger, so a shape
    can start from the top on every note. Free-running, so several voices can
    share one motion instead of each drifting on its own. A fade-in, so vibrato
    can arrive after the note rather than with it. Smoothing, because a square
    or a sample-and-hold stepping a filter cutoff is a click. A polarity switch,
    because "0 to 1" and "-1 to +1" are different musical ideas. All of those are
    here; none of them belong in an audio oscillator.

    RATE. Set in hertz. Tempo synchronisation is a matter of computing the right
    hertz from the host's tempo, which `tempoSyncedRateHz` does, so nothing in
    this class needs to know that a transport exists — and an LFO keeps working
    unchanged when the host provides no tempo at all (CLAUDE.md §38).

    OUTPUT RANGE. Bipolar shapes run -1 to +1 and unipolar 0 to 1. The bipolar
    form is the primary one: modulation depth is bipolar by specification
    (CLAUDE.md §15), so a bipolar source composes with it directly.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O.
*/

#include <cstdint>

namespace apollo::dsp
{

/** LFO waveform.

    The set PRD §15.2 specifies. Custom drawable curves are listed there too and
    are absent here on purpose: a drawable curve is a resource the user creates,
    so it needs the editor that draws it and the preset format that stores it,
    which are Phases 7 and 9.
*/
enum class LfoShape
{
    sine = 0,
    triangle,
    saw,          ///< Rises across the cycle.
    reverseSaw,   ///< Falls across the cycle.
    square,
    sampleAndHold, ///< A new random value each cycle.
    step           ///< A staircase: the ramp quantised into equal steps.
};

/** @returns the LFO rate in hertz that matches @p beatsPerCycle at @p bpm.

    Free of the class on purpose. Tempo synchronisation is arithmetic on the
    host's tempo, and keeping it here means the LFO has no opinion about
    transports, and that this can be tested without one.

    A non-positive or absurd tempo returns a rate the caller can still use rather
    than zero or infinity: a host that reports nothing should leave an LFO
    running at a sensible speed, not stop it (CLAUDE.md §38).
*/
[[nodiscard]] float tempoSyncedRateHz (double bpm, double beatsPerCycle) noexcept;

/** One LFO's configuration, as plain data. */
struct LfoSettings
{
    LfoShape shape = LfoShape::sine;

    /** Cycles per second. */
    float rateHz = 1.0f;

    /** Where in the cycle a retriggered LFO starts, 0 to 1. */
    float phaseOffset = 0.0f;

    /** True restarts the shape on every note; false lets it run continuously,
        shared across voices, so notes played at different times stay in step
        with each other rather than each carrying its own phase.
    */
    bool retrigger = true;

    /** Seconds over which the LFO fades up to full depth after a note starts.

        The shape collapses toward the centre of its range rather than toward
        its bottom, in either polarity, so a vibrato arrives by widening rather
        than by sliding the pitch up to meet it.
    */
    float fadeInSeconds = 0.0f;

    /** Slew applied to the output, 0 to 1.

        The reason a square-wave LFO on a filter cutoff does not click. Zero is
        no smoothing at all, which keeps a square exactly square for the cases
        that want that.
    */
    float smoothing = 0.0f;

    /** True gives -1 to +1; false gives 0 to 1. */
    bool bipolar = true;

    /** Steps in a cycle of the `step` shape. Clamped to at least two. */
    int stepCount = 8;

    [[nodiscard]] bool operator== (const LfoSettings&) const = default;
};

class Lfo
{
public:
    Lfo() = default;

    void prepare (double newSampleRate) noexcept;

    /** Replaces the configuration. Identical settings are discarded.

        Changing the shape or the rate does not reset the phase: an LFO that
        jumped back to the start every time a control moved would be unusable
        while being adjusted.
    */
    void setSettings (const LfoSettings& newSettings) noexcept;

    [[nodiscard]] const LfoSettings& getSettings() const noexcept { return settings; }

    /** Seeds the random source used by the sample-and-hold shape.

        Given a distinct value per voice by the engine, for the same reason the
        noise generator is: several voices holding the same random value is one
        modulation applied N times, not N modulations.
    */
    void setSeed (std::uint32_t newSeed) noexcept;

    /** Starts a note.

        @param freeRunningPhase  the phase a free-running LFO adopts, normally
                                 the engine's shared phase for this LFO. Ignored
                                 when `retrigger` is set.
    */
    void noteOn (double freeRunningPhase) noexcept;

    /** Returns to the start of the cycle and clears the fade and the slew. */
    void reset() noexcept;

    /** Advances one sample and @returns the new value. */
    [[nodiscard]] float getNextValue() noexcept;

    [[nodiscard]] float getCurrentValue() const noexcept { return current; }

    /** Phase within the cycle, 0 to 1. Exposed for tests and for the UI, which
        draws a playhead across the shape.
    */
    [[nodiscard]] double getPhase() const noexcept { return phase; }

private:
    /** @returns the raw shape at @p p, bipolar, before fade, polarity or slew. */
    [[nodiscard]] float shapeAt (double p) noexcept;

    [[nodiscard]] float nextRandom() noexcept;

    double sampleRate = 44100.0;

    LfoSettings settings;

    double phase = 0.0;
    double increment = 0.0;

    float current = 0.0f;

    /** Slew state, kept apart from `current` so that reading the current value
        never advances anything.
    */
    float smoothed = 0.0f;
    float smoothingCoefficient = 0.0f;

    /** Fade-in progress, 0 to 1. Reaches 1 and stays there. */
    float fadeGain = 1.0f;
    float fadeIncrement = 1.0f;

    /** Held value for the sample-and-hold shape, and the phase at which it was
        last replaced.
    */
    float heldValue = 0.0f;
    bool hasHeldValue = false;

    std::uint32_t randomState = 0x9E3779B9u;
    std::uint32_t seed = 1u;
};

} // namespace apollo::dsp
