#pragma once

/*
    The noise gate.

    PRD §22 asks for threshold, attack, hold, release and range, and for a gate
    "suitable for reducing unwanted noise while avoiding audible pumping where
    practical". Those two halves are in tension, and hold is what resolves them.

    WHY HOLD IS THE INTERESTING CONTROL. A gate with only a threshold chatters:
    a signal sitting near the threshold crosses it dozens of times a second, and
    the gate opens and shuts with it — which is far more audible than the noise
    it was opening and shutting to remove. Hold keeps the gate open for a fixed
    time after the signal last exceeded the threshold, so a signal hovering at
    the line holds the gate open instead of strobing it. It is the difference
    between a gate and a tremolo nobody asked for.

    WHY IT WATCHES THE PEAK. A gate is deciding whether there is a signal at
    all, and the transient that arrives while it is shut is exactly what it must
    not miss. RMS averages that transient away by design, which is right for a
    compressor and wrong here (`LevelDetector.h`).

    RANGE RATHER THAN OFF. A shut gate attenuates by the range amount rather
    than silencing: a channel that disappears completely draws attention to
    itself, while one pushed 40 dB down is simply gone. Range at its maximum is
    silence for anyone who wants it.

    IT ADDS NO LATENCY. There is no lookahead, so a transient arriving at a shut
    gate is attenuated for the length of the attack before the gate is open —
    which is what every gate without lookahead does, and why the attack control
    exists.

    REAL-TIME CONTRACT: `prepare` may allocate; nothing here does. Everything
    else is audio-thread callable.
*/

#include "DSP/Dynamics/LevelDetector.h"
#include "DSP/Effects/AudioEffect.h"

#include <cmath>

namespace apollo::dsp
{

class NoiseGate final : public AudioEffect
{
public:
    struct Settings
    {
        /** Level the signal must exceed to open the gate, in decibels. */
        float thresholdDb = -60.0f;

        float attackMs = 1.0f;
        float holdMs = 50.0f;
        float releaseMs = 100.0f;

        /** How far down a shut gate pushes the signal, in decibels. 0 is a gate
            that does nothing; the bottom of the range is silence.
        */
        float rangeDb = -60.0f;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    /** Below this the range is treated as total silence rather than a very
        small number, so a fully closed gate is exactly zero.
    */
    static constexpr float silentRangeDb = -79.0f;

    NoiseGate() = default;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;

    void setSettings (const Settings& newSettings) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return settings; }

    /** The gain the gate is currently applying, as a linear multiplier.
        Exposed so a test can watch it rather than inferring it from the audio.
    */
    [[nodiscard]] float getCurrentGain() const noexcept { return ramp.getCurrentGain(); }

    /** True while the hold timer is keeping the gate open after the signal has
        already fallen below the threshold.
    */
    [[nodiscard]] bool isHolding() const noexcept { return holdCountdown > 0; }

private:
    /** Re-derives the linear threshold, the closed gain, the ramp times and
        the hold length from the settings. One path, called from both
        `prepare` and `setSettings`, because two paths is how the ramp times
        once ended up swapped in only one of them.
    */
    void refreshDerived() noexcept;

    Settings settings;

    LevelDetector detector;
    GainRamp ramp;

    double preparedSampleRate = 0.0;

    float thresholdLinear = 0.001f;
    float closedGain = 0.001f;

    int holdSamples = 0;
    int holdCountdown = 0;
};

} // namespace apollo::dsp
