#pragma once

/*
    The reverb: a feedback delay network.

    WHY AN FDN AND NOT A BANK OF COMBS. The classic Schroeder arrangement —
    parallel comb filters into series allpasses — is simpler and is what most
    open reverbs are. It also has the problem it is famous for: each comb rings
    at its own harmonic series, so the tail has a pitch, and the usual fix is to
    tune the comb lengths by ear until the ringing is spread out enough to
    ignore. A feedback delay network mixes every line into every other line
    through an orthogonal matrix, so no line's echoes stay in their own series
    and the modes are spread by construction rather than by tuning.

    The orthogonal matrix is also where the stability argument comes from, and
    that is the second reason. An orthogonal matrix preserves energy exactly, so
    the *only* thing that changes the loop's energy is the decay gain applied to
    each line. Those gains are below one by construction, so the tail decays —
    provably, rather than because it was measured once and looked fine
    (ROADMAP's "validate decay stability").

    THE STRUCTURE, in order:

        in ─► pre-delay ─► 4 diffusion allpasses ─┐
                                                  ▼
                              ┌──────────── feedback network ────────────┐
                              │  8 delay lines, mutually prime lengths   │
                              │  one damping lowpass inside each         │
                              │  Hadamard mix, then per-line decay gain  │
                              └──────────────────┬───────────────────────┘
                                                 ▼
                            even lines ─► left, odd lines ─► right
                                                 ▼
                                     width ─► dry/wet mix ─► out

    - **Pre-delay** is the gap between the sound and the room answering it. It is
      what makes a large room sound large rather than merely long.
    - **Diffusion** smears the input so the network is fed something dense
      instead of a click. See `Allpass.h`.
    - **Damping** is a lowpass inside each line, so the tail loses its top as it
      decays, which is what air and soft surfaces do. Without it a reverb sounds
      like a metal tank, and that is not a figure of speech — a plate reverb is
      one.
    - **Size** scales the line lengths, and **decay** sets how long the tail
      takes to fall 60 dB. They are independent on purpose: a small room with a
      long decay is a tiled bathroom, a large room with a short decay is a
      treated studio, and both are sounds people want.
    - **Mode** chooses the base lengths: a room is tens of milliseconds, a hall
      is more than twice that.

    DENORMALS. A reverb tail spends most of its life below -200 dB, which on some
    processors is where every multiply costs orders of magnitude more than it
    should (CLAUDE.md §37). Every line's feedback write is flushed to zero below a
    threshold, so the tail ends rather than grinding on inaudibly.

    IT ADDS NO LATENCY. The dry path leaves when it arrives; the pre-delay is
    part of the effect, not a lookahead. The tail is reported instead.

    REAL-TIME CONTRACT: `prepare` allocates. Everything else is audio-thread
    callable and allocates nothing.
*/

#include "DSP/Delay/DelayLine.h"
#include "DSP/Effects/AudioEffect.h"
#include "DSP/Reverb/Allpass.h"
#include "DSP/Utilities/LinearSmoothedValue.h"

#include <array>

namespace apollo::dsp
{

class Reverb final : public AudioEffect
{
public:
    /** The two rooms PRD §21 asks for. They differ in the base delay lengths,
        which is what room size physically *is*.
    */
    enum class Mode
    {
        room = 0,
        hall
    };

    struct Settings
    {
        Mode mode = Mode::hall;

        /** Scales the line lengths, 0 to 1, across `sizeRange`. */
        float size = 0.5f;

        /** Time for the tail to fall 60 dB, in seconds. */
        float decaySeconds = 2.0f;

        /** Lowpass corner inside every line. At or above `dampingOpenHz` the
            damping is switched out.
        */
        float dampingHz = 6000.0f;

        /** Gap before the room answers, in milliseconds. */
        float preDelayMs = 20.0f;

        /** 0 collapses the tail to mono, 1 is the network's full spread. */
        float width = 1.0f;

        float mix = 0.0f;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    /** Delay lines in the network.

        Eight. Four is audibly sparse on transients and sixteen costs twice as
        much for a difference that needs a quiet room to hear; eight is the usual
        answer and the measurements in PROJECT-STATE.md §5b are what would
        justify changing it.
    */
    static constexpr int lineCount = 8;

    /** Diffusion allpasses per channel. */
    static constexpr int diffusionStages = 4;

    static constexpr float minimumSizeScale = 0.5f;
    static constexpr float maximumSizeScale = 1.5f;

    static constexpr float dampingOpenHz = 19000.0f;

    static constexpr double maximumPreDelaySeconds = 0.25;

    /** Longest decay the control can ask for, in seconds. */
    static constexpr float maximumDecaySeconds = 20.0f;

    Reverb() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;

    /** Passes audio through and feeds the network silence.

        The same reasoning as the delay's: this effect has no latency to
        compensate, but it does have memory, and a reverb switched out and back
        in must not resume a tail from a minute ago.
    */
    void processBypassed (float* const* channels, int numChannels, int numSamples) noexcept override;

    /** The decay plus the pre-delay, which is how long after the input stops
        the reverb is still producing sound.
    */
    [[nodiscard]] double getTailSeconds() const noexcept override;

    void setSettings (const Settings& newSettings) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    /** The length of line @p index in samples at the current size and mode.
        Exposed so a test can check the scaling without measuring a tail.
    */
    [[nodiscard]] float getLineLength (int index) const noexcept;

    /** @returns the base length of line @p index for @p mode, in samples at
        48 kHz. Mutually prime, so the network's echoes do not line up.
    */
    [[nodiscard]] static int baseLength (Mode mode, int index) noexcept;

private:
    void refreshLengths() noexcept;
    void refreshDecay() noexcept;
    void refreshDamping() noexcept;

    /** One-pole lowpass state, one per line. A one-pole rather than the state
        variable filter used elsewhere: this runs eight times per sample, and a
        first-order response is all a damping control needs — the difference
        between 6 and 12 dB per octave here is not a difference anyone tunes by.
    */
    struct Damper
    {
        float state = 0.0f;

        [[nodiscard]] float process (float input, float coefficient) noexcept
        {
            state += coefficient * (input - state);
            return state;
        }
    };

    std::array<DelayLine, lineCount> lines;
    std::array<Damper, lineCount> dampers;
    std::array<float, lineCount> lineLengths {};
    std::array<float, lineCount> decayGains {};

    std::array<std::array<Allpass, diffusionStages>, maxEffectChannels> diffusers;
    std::array<DelayLine, maxEffectChannels> preDelay;

    Settings settings;

    double preparedSampleRate = 0.0;
    double sampleRateScale = 1.0;
    int maximumLineSamples = 0;
    int maximumPreDelaySamples = 0;

    float dampingCoefficient = 1.0f;
    bool dampingActive = false;

    LinearSmoothedValue preDelaySamples;
    LinearSmoothedValue width;
    LinearSmoothedValue mix;
};

} // namespace apollo::dsp
