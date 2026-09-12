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

        float timeMs = 500.0f;

        Division division = Division::quarter;

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

    /** The delay the settings currently resolve to, in seconds. Exposed so a
        test can check the tempo arithmetic without measuring an impulse.
    */
    [[nodiscard]] double getDelaySeconds() const noexcept;

    /** @returns how many beats one repeat of @p division lasts. */
    [[nodiscard]] static double beatsFor (Division division) noexcept;

private:
    void refreshTime() noexcept;
    void refreshFilters() noexcept;

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

    LinearSmoothedValue feedbackGain;
    LinearSmoothedValue mix;
};

} // namespace apollo::dsp
