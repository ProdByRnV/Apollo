#include "DSP/Oscillators/UnisonOscillator.h"

namespace apollo::dsp
{

void UnisonOscillator::setSampleRate (double newSampleRate) noexcept
{
    for (auto& oscillator : oscillators)
        oscillator.setSampleRate (newSampleRate);
}

void UnisonOscillator::setTable (const Wavetable* newTable) noexcept
{
    for (auto& oscillator : oscillators)
        oscillator.setTable (newTable);
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
    frequency = frequencyHz;
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
        auto& oscillator = oscillators[static_cast<std::size_t> (i)];

        oscillator.setFrequency (frequency * active.getFrequencyRatio (i));

        // The count may have just grown, in which case these last few voices
        // have never been given a position. Cheap for the ones that already
        // have it — setPosition returns immediately when nothing moved — and
        // this is the only place a voice can become active, so it is the only
        // place that has to care.
        oscillator.setPosition (normalisedPosition);
    }
}

void UnisonOscillator::setPosition (float newNormalisedPosition) noexcept
{
    normalisedPosition = newNormalisedPosition;

    // Only the sounding voices. Setting a position now rebuilds a Reader
    // (ADR-0071), and this runs every modulation block, so walking all sixteen
    // would make a one-voice stack pay fifteen times over — on the default
    // patch, which is what Apollo loads with.
    const auto count = currentLayout().getCount();

    for (int i = 0; i < count; ++i)
        oscillators[static_cast<std::size_t> (i)].setPosition (newNormalisedPosition);
}

void UnisonOscillator::resetPhase (double basePhase) noexcept
{
    const auto& active = currentLayout();

    for (int i = 0; i < maxVoices; ++i)
        oscillators[static_cast<std::size_t> (i)].resetPhase (basePhase + active.getStartPhase (i));
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

    const auto& active = currentLayout();
    const auto count = active.getCount();

    for (int i = 0; i < count; ++i)
    {
        const auto value = oscillators[static_cast<std::size_t> (i)].getNextSample();

        left += value * active.getGainLeft (i);
        right += value * active.getGainRight (i);
    }
}

} // namespace apollo::dsp
