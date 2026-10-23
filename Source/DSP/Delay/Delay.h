#pragma once

/*
    The stereo delay.

    PRD §20 asks for a stereo delay with left/right timing, ping-pong, feedback,
    filtering, millisecond and tempo-synced timing, and a mix. This is that, and
    the decisions worth knowing before reading the code are these:

    THE TIME GLIDES RATHER THAN JUMPING. Moving the delay time moves the read
    pointer, and there are only two honest things to do about that: glide the
    pointer, which pitches the repeats as they stretch or compress, or crossfade
    between an old tap and a new one, which keeps the pitch and blurs the moment.
    Apollo glides. It is what a delay has sounded like since the tape machine, it
    is the behaviour people automate a delay time *for*, and the alternative
    needs a second read head, a fade curve and a decision about how long the fade
    lasts — three things to get wrong in exchange for removing a sound users
    want.

    FEEDBACK IS BOUNDED BY CONSTRUCTION, NOT BY HOPE. The control reaches
    `maximumFeedback`, which is below unity, so every repeat is smaller than the
    one before it even with the damping filter wide open. That is what makes the
    stability claim checkable rather than aspirational: the loop gain is a number
    you can read here, and `Tests/DSP/DelayTests.cpp` runs it at the top of its
    range for a long time and watches it decay.

    THE FILTERS ARE IN THE LOOP, NOT ACROSS THE OUTPUT. A lowpass on the output
    darkens every repeat by the same amount, once. A lowpass inside the feedback
    path darkens each repeat a little more than the last, which is what an echo
    does in a room and what tape does to itself. The highpass is there for the
    same reason in the other direction: without it, any DC or subsonic content
    that enters the loop is still circulating minutes later.

    EACH SIDE HAS ITS OWN TIME, AND BY DEFAULT THEY ARE ONE (Phase 13h-1).
    Linked, the right side reads the left side's time and note value, which is
    the delay as it was before it could be split. Split, the right side has a
    time and a note value of its own, read in whichever way the timing switch
    says. Two smoothers rather than one with an offset, so a split delay glides
    each side from where it was rather than dragging one along behind the other.

    THE TIME CAN WANDER (Phase 13h-1). A sine moves the read position either
    side of the time that was asked for, by up to `maximumModulationMs`: the
    slow wobble of a tape machine at low rates, and a chorus on every repeat at
    higher ones. The right side runs a quarter of a cycle behind the left, so
    the two sides never bend in step and the repeats spread across the stereo
    field rather than sliding back and forth together. One oscillator gives both
    sides: a rotating pair of numbers is a sine and its cosine at once, for four
    multiplies a sample and no call to `sin`. At zero depth the read position is
    exactly what it was before the modulation existed, which is what keeps every
    sound made before it unchanged.

    The swing is held short of the write position, so a one-millisecond delay
    with ten milliseconds of depth wobbles less rather than flattening against
    the end of the line.

    IT ADDS NO LATENCY. A delay is not a lookahead: the dry path leaves the
    moment it arrives, and the repeats arrive late because that is the effect.
    The rack is told zero, and the tail — how long the repeats take to fall
    below hearing — is reported instead.

    REAL-TIME CONTRACT: `prepare` allocates. Everything else is audio-thread
    callable and allocates nothing.
*/

#include "DSP/Delay/DelayLine.h"
#include "DSP/Effects/AudioEffect.h"
#include "DSP/Filters/StateVariableFilter.h"
#include "DSP/Utilities/LinearSmoothedValue.h"

#include <array>

namespace apollo::dsp
{

class Delay final : public AudioEffect
{
public:
    /** Note values a synced delay can take.

        Fourteen, covering whole to thirty-second with the dotted and triplet
        forms of the useful ones. Enumerated in full from the start and never
        reordered, for the reason the rack's slots are (ADR-0054): this indexes
        a discrete parameter, and a discrete parameter's range and meaning are
        part of the permanent automation contract.
    */
    enum class Division
    {
        whole = 0,
        halfDotted,
        half,
        halfTriplet,
        quarterDotted,
        quarter,
        quarterTriplet,
        eighthDotted,
        eighth,
        eighthTriplet,
        sixteenthDotted,
        sixteenth,
        sixteenthTriplet,
        thirtySecond
    };

    /** Number of values `Division` has. */
    static constexpr int divisionCount = 14;

    struct Settings
    {
        /** True to take the time from the host's tempo rather than from
            `timeMs`. A host that reports no tempo falls back to 120 BPM rather
            than stopping, so a synced delay in the standalone still repeats
            musically (CLAUDE.md §38).
        */
        bool tempoSynced = false;

        /** The left side's time, and both sides' while they are linked. */
        float timeMs = 500.0f;

        Division division = Division::quarter;

        /** True for one time on both sides; false to give the right side its
            own `rightTimeMs` and `rightDivision`.
        */
        bool linked = true;

        float rightTimeMs = 500.0f;

        Division rightDivision = Division::quarter;

        /** How fast the time wanders, in Hz. */
        float modulationRateHz = 0.5f;

        /** How far the time wanders either side of where it was set, in
            milliseconds. Zero is no modulation at all.
        */
        float modulationDepthMs = 0.0f;

        /** 0 to 1, scaled to `maximumFeedback`. 1 is as close to self-oscillation
            as the effect will go, which is deliberately not all the way there.
        */
        float feedback = 0.35f;

        /** Lowpass corner inside the feedback path. At or above `dampingOpenHz`
            the filter is switched out rather than run wide open.
        */
        float dampingHz = 20000.0f;

        /** Highpass corner inside the feedback path. At or below `lowCutFlatHz`
            it is switched out.
        */
        float lowCutHz = 20.0f;

        /** Cross the feedback paths, so a repeat alternates channels. */
        bool pingPong = false;

        /** Dry/wet, 0 to 1. Exactly zero leaves the signal untouched and the
            lines are still written, so turning it up does not reveal a buffer
            full of silence that should have been filling all along.
        */
        float mix = 0.0f;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    /** The longest delay the buffer can hold.

        Four seconds covers 2000 ms of free time, a whole note at any tempo down
        to 60 BPM, and the dotted whole note nobody asked for. A synced time
        longer than this is clamped rather than wrapping, because a delay that
        silently became a different note value would be worse than one that
        stopped getting longer.
    */
    static constexpr double maximumDelaySeconds = 4.0;

    /** The furthest the modulation moves the time either way. The line holds
        this much more than `maximumDelaySeconds`, so the longest delay can
        still wander.
    */
    static constexpr float maximumModulationMs = 10.0f;

    static constexpr float minimumModulationRateHz = 0.05f;
    static constexpr float maximumModulationRateHz = 10.0f;

    /** The most feedback the control can ask for. Below unity by enough that
        the loop decays even with every filter switched out.
    */
    static constexpr float maximumFeedback = 0.95f;

    static constexpr float dampingOpenHz = 19000.0f;
    static constexpr float lowCutFlatHz = 25.0f;

    /** Tempo used when the host offers none. */
    static constexpr double fallbackBpm = 120.0;

    Delay() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;

    /** Passes the audio through and feeds the lines silence.

        Not the default no-op: this effect has no latency to compensate, but it
        does have memory, and a delay switched out and back in must not replay
        what was playing when it left.
    */
    void processBypassed (float* const* channels, int numChannels, int numSamples) noexcept override;

    /** @returns how long the repeats take to fall below hearing.

        Derived from the feedback and the delay time rather than measured: the
        loop gain says exactly how many repeats it takes to reach -60 dB.
    */
    [[nodiscard]] double getTailSeconds() const noexcept override;

    void setSettings (const Settings& newSettings) noexcept;

    /** The host's tempo, in beats per minute. Audio-thread safe.

        Anything absurd or absent is replaced with `fallbackBpm`, so a synced
        delay keeps working in a standalone that has no transport at all.
    */
    void setTempo (double bpm) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    /** The left side's delay the settings currently resolve to, in seconds,
        before modulation. Exposed so a test can check the tempo arithmetic
        without measuring an impulse.
    */
    [[nodiscard]] double getDelaySeconds() const noexcept;

    /** The right side's, which is the left side's while they are linked. */
    [[nodiscard]] double getRightDelaySeconds() const noexcept;

    /** @returns how many beats one repeat of @p division lasts. */
    [[nodiscard]] static double beatsFor (Division division) noexcept;

private:
    [[nodiscard]] double secondsFor (float timeMs, Division division) const noexcept;

    void refreshTime() noexcept;
    void refreshFilters() noexcept;
    void refreshModulationRate() noexcept;
    void restartModulation() noexcept;

    struct ChannelState
    {
        DelayLine line;
        StateVariableFilter damping;
        StateVariableFilter lowCut;
    };

    std::array<ChannelState, maxEffectChannels> channelState;

    Settings settings;

    double preparedSampleRate = 0.0;
    double tempoBpm = fallbackBpm;
    int maximumDelaySamples = 0;

    SvfCoefficients dampingCoefficients;
    SvfCoefficients lowCutCoefficients;

    bool dampingActive = false;
    bool lowCutActive = false;

    /** The delay in samples, ramped rather than set.

        Long by the standards of the other smoothers here — a tenth of a second
        — because this one is not removing a step, it is *being* the glide. A
        20 ms ramp on a delay time is a click's worth of pitch bend crammed into
        a twentieth of a second.
    */
    LinearSmoothedValue delaySamples;

    /** The right side's, ramped the same way. Fed the same targets while the
        sides are linked, so it is then the left side's value exactly.
    */
    LinearSmoothedValue rightDelaySamples;

    /** The modulation depth in samples, ramped so turning it does not click. */
    LinearSmoothedValue modulationDepth;

    /** The modulation oscillator: a point turning round the unit circle, whose
        two coordinates are a sine for the left side and, a quarter of a cycle
        on, a cosine for the right. Double precision and pulled back onto the
        circle every block, because the rotation's rounding would otherwise
        spiral it in or out over a long session.
    */
    double modulationSine = 0.0;
    double modulationCosine = 1.0;
    double rotationSine = 0.0;
    double rotationCosine = 1.0;

    LinearSmoothedValue feedbackGain;
    LinearSmoothedValue mix;
};

} // namespace apollo::dsp
