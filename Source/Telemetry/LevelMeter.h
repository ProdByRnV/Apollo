#pragma once

/*
    The output meter.

    A scope and a meter answer different questions and must not be confused for
    one another. The scope's `peak` is the largest sample in one 43 ms window and
    nothing else — it has no ballistics, so it flickers, and a transient that
    lands between two frames is simply never shown. A meter's job is the opposite:
    to be *readable*, which means rising instantly, falling slowly, and never
    losing a peak that arrived while nobody was looking.

    THREE READINGS, BECAUSE ONE IS NOT ENOUGH.

      - **Peak** rises instantly and decays at a fixed rate in decibels per
        second. Instant attack because a meter that smooths its attack
        under-reads exactly the transients that matter, and a slow release
        because the eye cannot follow anything else.
      - **RMS** is the energy over a short window, which is what loudness
        actually tracks. Peak alone cannot distinguish a quiet signal with one
        spike from a loud one, and those two need different decisions from
        whoever is looking.
      - **Clip** holds. A sample at or beyond full scale that lasted one block
        must still be visible a second later, or the indicator only works for
        people who happened to be looking at the right moment. It is a hold
        rather than a latch with a reset: at a second and a half it is long
        enough to be seen and short enough that a single clip does not leave the
        instrument looking broken, and a reset would mean an inbound command on
        the parameter bridge, which is a conversation about parameters and has
        no business carrying one about a meter.

    CLIPPING IS DETECTED AT FULL SCALE, not below it. Apollo's output is float,
    so a sample above 1.0 is not itself destroyed — but it will be by whatever
    converts to integer downstream, and a meter that stayed quiet about it would
    be reporting on its own numeric range rather than on the signal's fate.

    REAL-TIME CONTRACT: `process` is called from the audio thread and does one
    linear pass with no allocation, no locks and no branches that depend on
    anything but the samples. `read` is the message thread's, and the two share
    only atomics.

    JUCE-free, so the ballistics can be tested against a hand-built signal with
    no host and no device.
*/

#include <array>
#include <atomic>
#include <cstddef>

namespace apollo::telemetry
{

/** Channels a meter reports. Stereo is the widest layout Apollo accepts. */
inline constexpr int meterChannels = 2;

/** How fast the peak reading falls, in decibels per second.

    20 dB/s is the broadcast convention and it is a convention for a good reason:
    slow enough that a peak stays readable for the better part of a second, fast
    enough that the meter is not still describing the last bar.
*/
inline constexpr double meterPeakDecayDbPerSecond = 20.0;

/** The RMS averaging window, in seconds.

    300 ms is roughly the ear's own integration time for loudness, which is the
    quantity this reading exists to approximate.
*/
inline constexpr double meterRmsSeconds = 0.3;

/** How long a clip stays visible after the last offending sample. */
inline constexpr double meterClipHoldSeconds = 1.5;

/** The level at or above which a sample counts as clipped. */
inline constexpr float meterClipThreshold = 1.0f;

//==============================================================================

/** What the interface is shown. Linear amplitude, not decibels: the conversion
    is presentation, and doing it here would mean picking a floor for silence
    that the interface would then have to know about anyway.
*/
struct LevelReading
{
    std::array<float, static_cast<std::size_t> (meterChannels)> peak {};
    std::array<float, static_cast<std::size_t> (meterChannels)> rms {};

    /** True while a clip is being held. */
    bool clipped = false;

    /** False until the meter has seen a single block, so an instance nobody has
        played yet reads as unmeasured rather than as silence.
    */
    bool active = false;
};

class LevelMeter
{
public:
    LevelMeter();

    /** Prepares the ballistics for a sample rate. Not real-time safe. */
    void prepare (double sampleRate) noexcept;

    /** AUDIO THREAD. Measures one block and publishes the result. */
    void process (const float* const* channels, int numChannels, int startSample,
                  int numSamples) noexcept;

    /** MESSAGE THREAD. */
    [[nodiscard]] LevelReading read() const noexcept;

    void reset() noexcept;

private:
    /** Ballistics state. Audio thread only, so plain floats. */
    std::array<float, static_cast<std::size_t> (meterChannels)> heldPeak {};
    std::array<float, static_cast<std::size_t> (meterChannels)> meanSquare {};

    double preparedSampleRate = 44100.0;

    /** Samples left before a held clip clears. */
    int clipCountdown = 0;

    /** Published state, read by the message thread. */
    std::array<std::atomic<float>, static_cast<std::size_t> (meterChannels)> publishedPeak;
    std::array<std::atomic<float>, static_cast<std::size_t> (meterChannels)> publishedRms;

    std::atomic<bool> clipped { false };
    std::atomic<bool> active { false };
};

} // namespace apollo::telemetry
