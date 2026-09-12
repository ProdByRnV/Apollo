#pragma once

/*
    The distortion unit.

    This is the effect that is meant to be heard as an effect, as distinct from
    `FilterDrive.h`, which is the colour a filter picks up when it is pushed.
    That one is a cubic with a 2x ceiling, runs per voice, and is deliberately
    not oversampled because thirty-two voices cannot each carry the latency
    (ADR-0033). This one runs once on the finished mix, so it can afford what
    that one could not: four times oversampling, a real transcendental shaper,
    and a drive range that goes somewhere.

    SIGNAL PATH, and why each stage is there:

        in ──► DC/subsonic highpass ──► upsample 4x ──► shaper ──► downsample
                                                                       │
        dry (delayed by the round trip) ◄──────────────────────────────┤
                                                                       ▼
                                              DC highpass ──► tone lowpass
                                                                       │
                                            out ◄── dry/wet mix ◄──────┘

    - The **pre highpass** keeps subsonic content out of the shaper. A slow
      rumble under a clipper is not heard as rumble; it moves the whole signal up
      and down against the clipping threshold and is heard as the distortion
      pumping in time with something inaudible.
    - **Oversampling** is the whole reason this stage can have a drive control
      worth the name. A nonlinearity generates harmonics above Nyquist that fold
      back into the audible band as inharmonic tones no later filter can remove;
      running it at 4x moves the fold-back point up and the downsampler removes
      what would have folded (Docs/OVERSAMPLING.md).
    - The **post highpass** removes the DC the asymmetric diode curve generates.
      Asymmetry is the point of that mode — it is what produces even harmonics —
      and a DC offset is its unwanted by-product, which would otherwise eat
      headroom for every stage after it.
    - The **tone lowpass** is the post filter: distortion adds high harmonics by
      definition, and a control that takes them back off is what makes the
      difference between a usable drive and one that is only ever too bright.

    GAIN COMPENSATION. Drive is a tone control, not a volume control, so turning
    it up must not simply make everything louder — otherwise every A/B is won by
    whichever side is louder. The compensation divides out the gain the shaper
    applies to a reference level, recomputed only when the settings change.

    LATENCY. The oversampler's round trip is a whole number of samples at the
    base rate, and the dry path is delayed by exactly that so the two halves of
    the mix stay aligned instead of combing. The unit reports that number, it
    does not change with drive, mix or bypass, and it is zero only if the
    oversampler is set to pass through.

    REAL-TIME CONTRACT: `prepare` allocates. Everything else is audio-thread
    callable and allocates nothing.
*/

#include "DSP/Effects/AudioEffect.h"
#include "DSP/Filters/StateVariableFilter.h"
#include "DSP/Oversampling/Oversampler.h"
#include "DSP/Utilities/LinearSmoothedValue.h"

#include <array>
#include <vector>

namespace apollo::dsp
{

class Distortion final : public AudioEffect
{
public:
    /** The transfer curves.

        Three, because they are three different sounds rather than three ways of
        spelling one: a smooth saturation, a hard ceiling, and an asymmetric
        curve. PRD §19 names exactly these.
    */
    enum class Mode
    {
        /** tanh. Bends gradually and never reaches its asymptote, so harmonics
            come in progressively as drive rises. Odd harmonics only.
        */
        soft = 0,

        /** A flat ceiling. Below the threshold the signal is untouched and
            above it there is nothing at all, which is why it sounds abrupt in a
            way `soft` never does.
        */
        hard,

        /** Exponential, and asymmetric: the negative half saturates earlier and
            lower than the positive half, as a diode conducting one way does.
            The asymmetry is what produces even harmonics.
        */
        diode
    };

    /** Everything the unit needs, resolved from parameters by the caller. */
    struct Settings
    {
        Mode mode = Mode::soft;

        /** Gain in front of the shaper, in decibels. */
        float driveDb = 12.0f;

        /** Post lowpass corner. At or above `toneOpenHz` the filter is switched
            out rather than run wide open, so a fully open tone control is
            transparent rather than merely gentle.
        */
        float toneHz = 20000.0f;

        /** Dry/wet, 0 to 1. Exactly zero routes the dry path only, and the
            shaper is not run at all.
        */
        float mix = 0.0f;

        /** Output trim in decibels, applied to the wet path after compensation.
        */
        float outputDb = 0.0f;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    /** Tone corner at or above which the post lowpass is switched out. */
    static constexpr float toneOpenHz = 19000.0f;

    /** Corner of the two highpasses, in hertz. Below the lowest note a keyboard
        has, so it removes offset and rumble without thinning anything played.
    */
    static constexpr float dcBlockHz = 20.0f;

    /** The oversampling factor, fixed rather than user-selectable.

        4x is the preferred quality mode CLAUDE.md §23 asks for, and fixing it
        is what keeps the reported latency constant. A quality control would be a
        latency change in disguise, which is a host renegotiation every time
        someone turns it (ADR-0054).
    */
    static constexpr Oversampler::Factor oversamplingFactor = Oversampler::Factor::x4;

    Distortion() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void processBypassed (float* const* channels, int numChannels, int numSamples) noexcept override;

    [[nodiscard]] int getLatencySamples() const noexcept override { return latencySamples; }

    /** Applies settings. Audio-thread safe; recomputes only what changed. */
    void setSettings (const Settings& newSettings) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    /** The transfer curve itself, exposed so tests can measure the shape
        directly rather than inferring it from rendered audio.

        @param input  the signal, already multiplied by the drive gain.
    */
    [[nodiscard]] static float shape (Mode mode, float input) noexcept;

    /** The gain the shaper applies to a reference level, which is what the
        compensation divides out. Exposed for the same reason as `shape`.
    */
    [[nodiscard]] static float compensationFor (Mode mode, float driveGain) noexcept;

private:
    /** Level the compensation is calibrated at: -6 dBFS, which is a realistic
        peak for a synthesiser's output rather than a full-scale square.
    */
    static constexpr float referenceLevel = 0.5f;

    void writeDry (const float* input, int channel, int numSamples) noexcept;

    struct ChannelState
    {
        Oversampler oversampler;
        StateVariableFilter preHighpass;
        StateVariableFilter postHighpass;
        StateVariableFilter toneLowpass;

        /** The dry path's delay line, exactly `latencySamples` long. Empty when
            the oversampler reports no latency.
        */
        std::vector<float> dryLine;
        int dryIndex = 0;

        /** Base-rate scratch: the signal on its way to the shaper, and the dry
            signal on its way to the mix.
        */
        std::vector<float> work;
        std::vector<float> dry;
    };

    std::array<ChannelState, maxEffectChannels> channelState;

    Settings settings;

    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    int latencySamples = 0;

    SvfCoefficients dcBlockCoefficients;
    SvfCoefficients toneCoefficients;
    bool toneActive = false;

    /** Smoothed because all three step audibly: drive and mix move the sound
        itself, and an output trim is a fader (CLAUDE.md §36). The shaper's
        compensation is not smoothed separately — it is folded into the drive
        ramp, because compensating for a gain the signal no longer has is worse
        than not compensating at all.
    */
    LinearSmoothedValue driveGain;
    LinearSmoothedValue compensation;
    LinearSmoothedValue mix;
    LinearSmoothedValue outputGain;
};

} // namespace apollo::dsp
