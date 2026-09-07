#pragma once

/*
    A wavetable oscillator.

    Owns phase and reads from a Wavetable it does not own — tables are immutable
    once built and shared by every voice, so this holds a plain non-owning
    pointer and copying an oscillator is trivial.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O. The only per-note work is choosing a mip
    level, which is a short loop over a compile-time constant.
*/

#include "DSP/Oscillators/Wavetable.h"

namespace apollo::dsp
{

class WavetableOscillator
{
public:
    WavetableOscillator() = default;

    /** Sets the sample rate. Safe to call from the audio thread; it only
        recomputes the phase increment.
    */
    void setSampleRate (double newSampleRate) noexcept;

    /** Points the oscillator at a table. Passing nullptr silences it. */
    void setTable (const Wavetable* newTable) noexcept;

    /** Sets the fundamental frequency in hertz, and reselects the mip level.

        The level is chosen here rather than per sample: it depends only on
        pitch, and re-deciding it every sample would be both wasted work and a
        source of discontinuity when a modulated pitch crossed a boundary.
    */
    void setFrequency (double frequencyHz) noexcept;

    /** Sets the scan position across the table, normalised to [0, 1]. */
    void setPosition (float normalisedPosition) noexcept;

    /** Resets phase to @p startPhase, in [0, 1). */
    void resetPhase (double startPhase) noexcept;

    /** @returns the next sample and advances the phase. */
    [[nodiscard]] float getNextSample() noexcept;

    [[nodiscard]] double getPhase() const noexcept { return phase; }
    [[nodiscard]] int getMipLevel() const noexcept { return mipLevel; }

private:
    void updateIncrement() noexcept;

    const Wavetable* table = nullptr;

    double sampleRate = 44100.0;
    double frequency = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    double framePosition = 0.0;
    int mipLevel = 0;
};

} // namespace apollo::dsp
