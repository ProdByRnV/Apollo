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

    /** Re-derives one voice's read position from the table, its own pitch and
        the shared scan position.
    */
    void refreshVoice (int index) noexcept;

    //==========================================================================
    // THE STACK IS FLAT, NOT SIXTEEN OSCILLATOR OBJECTS (ADR-0074).
    //
    // This held a `std::array<WavetableOscillator, 16>` until Phase 10d-5. Each
    // of those is about a hundred bytes — table pointer, Reader, sample rate,
    // frequency, phase, increment, positions, mip level — so reading sixteen
    // phases meant sixteen loads a hundred bytes apart, and the gains came
    // through a call into the layout object per voice per sample.
    //
    // Phase 10d-4 measured what removing that is worth: **1.39x** on a
    // sixteen-voice stack, with no change to the arithmetic and no vector code.
    // It also measured what float and SIMD would add on top — a further 1.27x
    // — and that was declined, so what follows is deliberately ordinary scalar
    // double-precision code whose only trick is where it keeps its data.
    //
    // Parallel arrays rather than an array of structures, because the second
    // pass of `addNextStereoSample` walks each of these in order and that is
    // the shape a compiler can vectorise.

    /** Where each voice is in its cycle, and how far it advances per sample.
        The two hottest arrays in the instrument.
    */
    std::array<double, static_cast<std::size_t> (maxVoices)> phase {};
    std::array<double, static_cast<std::size_t> (maxVoices)> increment {};

    /** Each voice's resolved read position.

        Per voice rather than shared because detuning can put two voices on
        different mip levels — the levels change at particular frequencies and a
        stack spread over fifty cents can straddle one. Usually they are all
        identical, and nothing here depends on that being so.
    */
    std::array<Wavetable::Reader, static_cast<std::size_t> (maxVoices)> readers {};

    /** Copied out of the layout when it changes, rather than fetched through it
        per voice per sample.
    */
    std::array<float, static_cast<std::size_t> (maxVoices)> gainLeft {};
    std::array<float, static_cast<std::size_t> (maxVoices)> gainRight {};

    const Wavetable* table = nullptr;

    double sampleRate = 44100.0;

    const UnisonLayout* layout = nullptr;

    /** Used when no layout has been supplied. Default-constructed to a single
        centred voice, so the fallback is a plain mono oscillator rather than
        silence.
    */
    UnisonLayout fallbackLayout;

    double frequency = 0.0;

    /** The scan position, held here so that a voice which becomes active later
        can be given it.

        Since ADR-0071 setting a position rebuilds a Reader, so `setPosition`
        touches only the voices that are actually sounding rather than all
        sixteen — a one-voice stack must not pay for fifteen it does not have.
        The voices above the count are brought up to date by `applyLayout`, the
        one place a voice can become active.
    */
    float normalisedPosition = 0.0f;

    /** The layout generation the per-voice frequencies were derived from, so a
        layout that changes underneath this object is noticed rather than
        assumed away.
    */
    unsigned int appliedGeneration = 0;
};

} // namespace apollo::dsp
