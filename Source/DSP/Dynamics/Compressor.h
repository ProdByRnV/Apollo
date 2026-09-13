#pragma once

/*
    The compressor.

    PRD §23 asks for an RMS-based compressor with threshold, ratio, attack,
    release, makeup gain and mix. The decisions worth stating are where the
    detector sits, what it measures, and what the gain is smoothed in.

    FEED-FORWARD, NOT FEEDBACK. The detector reads the *input*, computes the
    gain reduction that input deserves, and applies it. The alternative — a
    feedback design, where the detector reads the output — is what most analogue
    compressors are, and it has a characteristic sound precisely because the
    control signal is always one loop late and the effective ratio is not the
    one on the dial. Feed-forward does what the dial says, which is the right
    default for a synthesiser's own dynamics stage, where the user is shaping a
    sound rather than reaching for a particular vintage circuit.

    RMS, AND A WINDOW WORTH THINKING ABOUT. The detector averages the squared
    signal over `LevelDetector`'s window, which follows how loud the signal
    *sounds* rather than what its peaks are doing. The window has a floor for a
    reason: shorter than a cycle of the lowest frequency present, an RMS
    detector tracks the waveform rather than its level, and the gain then
    modulates at the signal's own frequency — which is distortion rather than
    compression.

    HARD KNEE. Below the threshold nothing happens; above it the ratio applies
    in full. A soft knee — easing the ratio in over a few decibels either side —
    sounds gentler and is what most compressors offer, and it is deliberately
    not here: it is a fourth control on the curve, it makes the measured ratio a
    function of level, and adding it later changes nothing decided here.

    MIX IS PARALLEL COMPRESSION. Blending the compressed signal back against the
    dry one keeps the transients the compressor just removed while still raising
    what sits underneath them, which is the reason PRD lists a mix control on a
    compressor at all.

    IT ADDS NO LATENCY. No lookahead: the detector reads what has already
    arrived, so a transient shorter than the attack passes through before the
    gain has finished moving. That is what a compressor without lookahead does,
    and it is why the attack control matters.

    REAL-TIME CONTRACT: `prepare` may allocate; nothing here does. Everything
    else is audio-thread callable.
*/

#include "DSP/Dynamics/LevelDetector.h"
#include "DSP/Effects/AudioEffect.h"
#include "DSP/Utilities/LinearSmoothedValue.h"

#include <cmath>

namespace apollo::dsp
{

class Compressor final : public AudioEffect
{
public:
    struct Settings
    {
        /** Level above which the ratio starts applying, in decibels. */
        float thresholdDb = -18.0f;

        /** Decibels in per decibel out, above the threshold. 1 is no
            compression at all; the top of the range is close enough to limiting
            that the remaining difference is academic.
        */
        float ratio = 2.0f;

        float attackMs = 10.0f;
        float releaseMs = 120.0f;

        /** Applied after compression, in decibels. Not automatic: an automatic
            makeup gain guesses what the user wanted and is wrong whenever the
            programme changes.
        */
        float makeupDb = 0.0f;

        /** Dry/wet, 0 to 1. Below 1 this is parallel compression. */
        float mix = 1.0f;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    /** RMS window. Long enough not to follow a waveform at the bottom of the
        audible range, short enough to follow a phrase.
    */
    static constexpr float averagingMs = 10.0f;

    /** Level below which the detector reports silence rather than taking the
        logarithm of something indistinguishable from zero.
    */
    static constexpr float floorDb = -100.0f;

    Compressor() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;

    void setSettings (const Settings& newSettings) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    /** The gain reduction currently being applied, in decibels — negative when
        the compressor is working. Exposed so a test, and later a meter, can read
        it rather than infer it.
    */
    [[nodiscard]] float getGainReductionDb() const noexcept;

    /** The static curve, without any attack or release: what @p inputDb becomes
        once the compressor has settled. Exposed so the ratio can be measured
        directly rather than through a render.
    */
    [[nodiscard]] static float outputDbFor (const Settings& settings, float inputDb) noexcept;

private:
    Settings settings;

    LevelDetector detector;
    GainRamp ramp;

    double preparedSampleRate = 0.0;

    float thresholdLinear = 0.125f;
    float slope = 0.5f;

    LinearSmoothedValue makeupGain;
    LinearSmoothedValue mix;
};

} // namespace apollo::dsp
