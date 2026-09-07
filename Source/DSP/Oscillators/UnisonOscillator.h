#pragma once

/*
    A stereo unison stack of wavetable oscillators.

    Holds maxVoices WavetableOscillators and sums the first `count` of them,
    detuned and panned according to a UnisonLayout it does not own. The layout
    is shared by every voice in the engine (see UnisonLayout), so this class
    holds a non-owning pointer and re-reads it when its generation changes.

    WHAT THIS DOES *NOT* DO. It applies no level and no oscillator pan. The
    stack produces a stereo image whose centre is unity; balancing that image
    left or right, and scaling it, are the Voice's job, because those two are
    the parameters that must be smoothed per sample and the Voice is where the
    smoothers live. Keeping them out of here also means the stereo image is
    built once, by the spread, rather than panned twice.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O. The only non-trivial work is re-deriving the
    per-voice frequencies, which happens on a note start, a pitch change or a
    layout change, never per sample.
*/

#include "DSP/Oscillators/WavetableOscillator.h"
#include "DSP/Unison/UnisonLayout.h"

#include <array>

namespace apollo::dsp
{

class UnisonOscillator
{
public:
    static constexpr int maxVoices = UnisonLayout::maxVoices;

    UnisonOscillator() = default;

    void setSampleRate (double newSampleRate) noexcept;

    /** Points every unison voice at a table. Passing nullptr silences them. */
    void setTable (const Wavetable* newTable) noexcept;

    /** Points the stack at a layout. Passing nullptr falls back to a single
        centred voice at unity gain, so a mis-wired oscillator still sounds.
    */
    void setLayout (const UnisonLayout* newLayout) noexcept;

    /** Sets the fundamental, before unison detune, in hertz. */
    void setFrequency (double frequencyHz) noexcept;

    /** Sets the scan position across the table, normalised to [0, 1]. */
    void setPosition (float normalisedPosition) noexcept;

    /** Restarts every unison voice from @p basePhase plus its layout offset. */
    void resetPhase (double basePhase) noexcept;

    /** Adds the next sample of the stack into @p left and @p right, advancing
        every active unison voice by one sample.

        Additive so the Voice can accumulate its sources into one stereo pair
        without a scratch buffer.
    */
    void addNextStereoSample (float& left, float& right) noexcept;

    [[nodiscard]] int getActiveVoiceCount() const noexcept;

private:
    void applyLayout() noexcept;

    /** @returns the layout in force, never null. */
    [[nodiscard]] const UnisonLayout& currentLayout() const noexcept
    {
        return layout != nullptr ? *layout : fallbackLayout;
    }

    std::array<WavetableOscillator, static_cast<std::size_t> (maxVoices)> oscillators {};

    const UnisonLayout* layout = nullptr;

    /** Used when no layout has been supplied. Default-constructed to a single
        centred voice, so the fallback is a plain mono oscillator rather than
        silence.
    */
    UnisonLayout fallbackLayout;

    double frequency = 0.0;

    /** The layout generation the per-voice frequencies were derived from, so a
        layout that changes underneath this object is noticed rather than
        assumed away.
    */
    unsigned int appliedGeneration = 0;
};

} // namespace apollo::dsp
