#pragma once

/*
    A DAHDSR envelope generator.

        Delay -> Attack -> Hold -> Decay -> Sustain -> Release

    Delay holds at silence before the note begins, and Hold holds at full level
    between the attack and the decay. Both are what separate DAHDSR from the
    familiar ADSR, and both are what a percussive or a swelling patch needs
    without a second envelope to fake them.

    SEGMENT SHAPE. Each timed segment travels from where the envelope currently
    is to where that segment ends, over exactly the time the user dialled, along
    a curve set by one `curve` control (PRD §15.1 asks for adjustable curve
    tension; per-stage curves are listed there as a future option, so there is
    one control, not six).

    The shape is

        level = start + (end - start) * (1 - e^(-k*p)) / (1 - e^(-k))

    for a segment progress p running 0 to 1, which is linear as k approaches
    zero and increasingly analog-like as k grows. It is evaluated *incrementally*
    — e^(-k*p) is one multiply per sample by a ratio computed once when the
    segment starts — so an exponential envelope costs a multiply and a subtract
    per sample rather than a call to exp().

    Both endpoints are exact. A segment that has run its course sits precisely on
    its target rather than approaching it asymptotically, which is what lets the
    sustain level be the level the user asked for and lets a release reach true
    silence instead of a denormal tail.

    WHAT THIS IS NOT. It does not know about voices, notes, MIDI, velocity or
    stealing. A voice being stolen is a separate, much faster fade that the Voice
    applies on top (ADR-0019); folding it in here would make the musical release
    time and the anti-click ramp the same control, which they are not.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O. `prepare` is the only one that should be
    called while audio is stopped, and only because it changes the sample rate.
*/

namespace apollo::dsp
{

/** Where an envelope is in its run. */
enum class EnvelopeStage
{
    idle,    ///< Finished or never started. Output is exactly zero.
    delay,   ///< Waiting at silence before the attack.
    attack,  ///< Rising to full level.
    hold,    ///< Holding at full level.
    decay,   ///< Falling to the sustain level.
    sustain, ///< Holding at the sustain level while the key is down.
    release  ///< Falling to silence after note-off.
};

/** One envelope's shape, as plain data.

    Compared wholesale by the voice for the same reason the source section is
    (ADR-0026): a parameter that fails to reach the engine is a bug that no
    amount of care around individual setters reliably prevents.
*/
struct EnvelopeSettings
{
    /** Stage times in seconds. Zero is legal everywhere and means "skip". */
    float delaySeconds = 0.0f;
    float attackSeconds = 0.005f;
    float holdSeconds = 0.0f;
    float decaySeconds = 0.100f;
    float releaseSeconds = 0.050f;

    /** Level held while the key is down, 0 to 1. */
    float sustainLevel = 1.0f;

    /** Curve tension, -1 to +1.

        Zero is linear. Positive is the analog shape — quick off the mark and
        easing into the target, which is how a capacitor charges and what a
        release has to do to sound natural rather than like a fade-out.
        Negative inverts it.
    */
    float curve = 0.5f;

    [[nodiscard]] bool operator== (const EnvelopeSettings&) const = default;
};

class Envelope
{
public:
    Envelope() = default;

    /** Sets the sample rate and returns the envelope to idle. */
    void prepare (double newSampleRate) noexcept;

    /** Replaces the shape.

        Cheap to call every block: identical settings are discarded. A change
        that arrives mid-segment retimes that segment from wherever the envelope
        currently is, so turning up a decay while a note is held lengthens the
        decay in progress rather than waiting for the next note. The alternative
        — applying only at the next segment — makes a knob feel dead while the
        thing it controls is audibly happening.
    */
    void setSettings (const EnvelopeSettings& newSettings) noexcept;

    [[nodiscard]] const EnvelopeSettings& getSettings() const noexcept { return settings; }

    /** Starts the envelope from the beginning of its delay stage.

        The attack begins from the envelope's *current* level rather than from
        zero. For a fresh voice that is zero anyway; for a voice retriggered
        while still sounding it is what stops the note restarting with a click.
    */
    void noteOn() noexcept;

    /** Begins the release, from wherever the envelope currently is.

        Ignored when already idle, so a stray note-off cannot restart anything.
    */
    void noteOff() noexcept;

    /** Returns to idle and silence immediately, with no ramp. */
    void reset() noexcept;

    /** Advances one sample and @returns the new level. */
    [[nodiscard]] float getNextValue() noexcept;

    [[nodiscard]] float getCurrentValue() const noexcept { return level; }
    [[nodiscard]] EnvelopeStage getStage() const noexcept { return stage; }

    [[nodiscard]] bool isActive() const noexcept { return stage != EnvelopeStage::idle; }
    [[nodiscard]] bool isReleasing() const noexcept { return stage == EnvelopeStage::release; }

private:
    /** Moves to @p next, skipping any stage whose time is zero. */
    void enterStage (EnvelopeStage next) noexcept;

    /** Starts a timed ramp from the current level to @p target over @p seconds. */
    void beginSegment (float target, float seconds) noexcept;

    /** @returns the time this stage is configured to take. */
    [[nodiscard]] float secondsForStage (EnvelopeStage query) const noexcept;

    /** @returns the level this stage ends on. */
    [[nodiscard]] float targetForStage (EnvelopeStage query) const noexcept;

    double sampleRate = 44100.0;

    EnvelopeSettings settings;
    EnvelopeStage stage = EnvelopeStage::idle;

    float level = 0.0f;

    // Current segment.
    float segmentStart = 0.0f;
    float segmentTarget = 0.0f;

    /** Set when the segment is a straight line, which avoids the exponential
        machinery entirely for the common case of a linear curve setting.
    */
    bool segmentIsLinear = true;
    float linearIncrement = 0.0f;

    /** e^(-k*p), stepped by one multiply per sample. */
    float expTerm = 1.0f;
    float expRatio = 1.0f;

    /** 1 / (1 - e^(-k)), so the per-sample maths is a multiply, not a divide. */
    float expScale = 1.0f;

    int samplesRemaining = 0;
    int segmentLengthSamples = 0;
};

} // namespace apollo::dsp
