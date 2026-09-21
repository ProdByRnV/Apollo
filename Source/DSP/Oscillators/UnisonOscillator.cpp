#include "DSP/Oscillators/UnisonOscillator.h"

#include <cmath>

namespace apollo::dsp
{

void UnisonOscillator::setSampleRate (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    // Every voice's increment and mip level depend on it, so the layout has to
    // be re-derived rather than merely marked stale.
    appliedGeneration = 0;
    applyLayout();
}

void UnisonOscillator::setTable (const Wavetable* newTable) noexcept
{
    if (table == newTable)
        return;

    table = newTable;

    // All sixteen, not just the sounding ones: a voice that becomes active
    // later must not be left pointing into a table that has been replaced
    // (§12.4). This runs when the table pointer changes, which is rare.
    for (int i = 0; i < maxVoices; ++i)
        refreshVoice (i);
}

void UnisonOscillator::setLayout (const UnisonLayout* newLayout) noexcept
{
    if (layout == newLayout)
        return;

    layout = newLayout;

    // A different layout object says nothing about its generation counter, so
    // the cached one is invalidated rather than compared.
    appliedGeneration = 0;
    applyLayout();
}

void UnisonOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency = std::isfinite (frequencyHz) ? frequencyHz : 0.0;
    appliedGeneration = 0;
    applyLayout();
}

void UnisonOscillator::applyLayout() noexcept
{
    const auto& active = currentLayout();

    if (active.getGeneration() == appliedGeneration)
        return;

    appliedGeneration = active.getGeneration();

    const auto count = active.getCount();

    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        increment[voice] = frequency * active.getFrequencyRatio (i) / sampleRate;

        // Copied out of the layout here rather than read through it per sample.
        // On a sixteen-voice stack that was thirty-two calls a sample into an
        // object the render loop otherwise never touches (ADR-0074).
        gainLeft[voice] = active.getGainLeft (i);
        gainRight[voice] = active.getGainRight (i);

        // The count may have just grown, in which case these last few voices
        // have never had a read position derived at all. This is the only place
        // a voice can become active, so it is the only place that has to care.
        refreshVoice (i);
    }
}

void UnisonOscillator::setPosition (float newNormalisedPosition) noexcept
{
    // Non-finite is clamped to zero rather than passed on: neither comparison
    // below is true for a NaN, so it would otherwise travel into the frame
    // position and from there into an undefined conversion.
    const auto clamped = ! std::isfinite (newNormalisedPosition) ? 0.0f
                       : (newNormalisedPosition < 0.0f ? 0.0f
                       : (newNormalisedPosition > 1.0f ? 1.0f : newNormalisedPosition));

    if (clamped == normalisedPosition)
        return;

    normalisedPosition = clamped;

    // Only the sounding voices. Re-deriving a read position is not free and
    // this runs every modulation block, so walking all sixteen would make a
    // one-voice stack pay fifteen times over — on the default patch, which is
    // what Apollo loads with. The voices above the count are brought up to
    // date by applyLayout, where they become active.
    const auto count = currentLayout().getCount();

    for (int i = 0; i < count; ++i)
        refreshVoice (i);
}

void UnisonOscillator::refreshVoice (int index) noexcept
{
    const auto voice = static_cast<std::size_t> (index);

    if (table == nullptr)
    {
        readers[voice] = Wavetable::Reader {};
        return;
    }

    const auto frames = table->getNumFrames();

    const auto framePosition = frames > 1
                             ? static_cast<double> (normalisedPosition)
                               * static_cast<double> (frames - 1)
                             : 0.0;

    // Each voice's own detuned pitch, so a stack spread across a mip boundary
    // gets the right level on both sides of it.
    const auto voiceFrequency = frequency * currentLayout().getFrequencyRatio (index);
    const auto level = Wavetable::selectMipLevel (voiceFrequency, sampleRate);

    readers[voice] = table->makeReader (level, framePosition);
}

void UnisonOscillator::resetPhase (double basePhase) noexcept
{
    const auto& active = currentLayout();

    for (int i = 0; i < maxVoices; ++i)
    {
        auto wrapped = basePhase + active.getStartPhase (i);

        wrapped -= std::floor (wrapped);

        phase[static_cast<std::size_t> (i)] = wrapped;
    }
}

int UnisonOscillator::getActiveVoiceCount() const noexcept
{
    return currentLayout().getCount();
}

void UnisonOscillator::addNextStereoSample (float& left, float& right) noexcept
{
    // Picked up here rather than pushed by the engine: the layout mutates in
    // place and this is the one call guaranteed to happen before it is read.
    applyLayout();

    const auto count = currentLayout().getCount();

    if (count <= 0)
        return;

    // ONE VOICE IS NOT A BATCH, and the default patch is one voice.
    //
    // The two passes below exist so that a stack of sixteen can gather all of
    // its taps and then evaluate all of its polynomials over contiguous data.
    // At a count of one there is nothing to amortise: the split costs a second
    // loop and a trip through an array, and buys nothing at all. Apollo loads
    // with oscillator 1 alone and no unison, so this is the common case rather
    // than a corner of one (ADR-0074).
    if (count == 1)
    {
        Wavetable::Reader::Taps single;

        readers[0].gather (phase[0], single);

        phase[0] += increment[0];

        if (phase[0] >= 1.0 || phase[0] < 0.0)
            phase[0] -= std::floor (phase[0]);

        const auto sample = Wavetable::Reader::interpolate (single);

        left += static_cast<float> (sample * static_cast<double> (gainLeft[0]));
        right += static_cast<float> (sample * static_cast<double> (gainRight[0]));

        return;
    }

    // Scratch on the stack rather than in the object. Sixteen voices of taps is
    // 640 bytes, which stays in L1 for the two passes below and then stops
    // existing — where a member would have cost that much per oscillator per
    // voice, forty kilobytes across a full instrument, written and read every
    // sample.
    Wavetable::Reader::Taps taps[maxVoices];

    // PASS ONE: the gather. Loads at addresses derived from each voice's own
    // phase, which is the part no vector unit helps with (ADR-0073).
    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        readers[voice].gather (phase[voice], taps[voice]);

        phase[voice] += increment[voice];

        // A single subtraction is not enough. It assumes the increment is below
        // 1.0, which holds for any musical frequency but not for an absurd one
        // — and mip selection clamps the *level*, not the increment. floor()
        // wraps correctly for any magnitude and is only reached once per cycle.
        if (phase[voice] >= 1.0 || phase[voice] < 0.0)
            phase[voice] -= std::floor (phase[voice]);
    }

    // PASS TWO: the arithmetic, walking contiguous arrays with no
    // data-dependent addressing. One shared polynomial, evaluated a voice at a
    // time here and once per call in Reader::read — the same arithmetic on a
    // different schedule, and a test asserts the two agree (ADR-0074).
    auto sumLeft = 0.0;
    auto sumRight = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        // No branch: a reader with nothing behind it gathered zeroes, and zeroes
        // interpolate to silence (ADR-0074).
        const auto sample = Wavetable::Reader::interpolate (taps[voice]);

        sumLeft += sample * static_cast<double> (gainLeft[voice]);
        sumRight += sample * static_cast<double> (gainRight[voice]);
    }

    left += static_cast<float> (sumLeft);
    right += static_cast<float> (sumRight);
}

} // namespace apollo::dsp
