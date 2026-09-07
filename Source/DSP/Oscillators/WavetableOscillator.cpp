#include "DSP/Oscillators/WavetableOscillator.h"

namespace apollo::dsp
{

void WavetableOscillator::setSampleRate (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    updateIncrement();
}

void WavetableOscillator::setTable (const Wavetable* newTable) noexcept
{
    table = newTable;
    setPosition (static_cast<float> (framePosition));
}

void WavetableOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency = frequencyHz;
    updateIncrement();
}

void WavetableOscillator::updateIncrement() noexcept
{
    phaseIncrement = frequency / sampleRate;
    mipLevel = Wavetable::selectMipLevel (frequency, sampleRate);
}

void WavetableOscillator::setPosition (float normalisedPosition) noexcept
{
    if (table == nullptr || table->getNumFrames() <= 1)
    {
        framePosition = 0.0;
        return;
    }

    const auto clamped = normalisedPosition < 0.0f ? 0.0f
                                                   : (normalisedPosition > 1.0f ? 1.0f : normalisedPosition);

    framePosition = static_cast<double> (clamped) * static_cast<double> (table->getNumFrames() - 1);
}

void WavetableOscillator::resetPhase (double startPhase) noexcept
{
    phase = startPhase - static_cast<double> (static_cast<long long> (startPhase));

    if (phase < 0.0)
        phase += 1.0;
}

float WavetableOscillator::getNextSample() noexcept
{
    if (table == nullptr || table->isEmpty())
        return 0.0f;

    const auto value = table->getSampleAtPosition (mipLevel, framePosition, phase);

    phase += phaseIncrement;

    // Subtraction rather than fmod: the increment stays well below 1.0 for any
    // frequency the mip selection permits, so one branch always suffices.
    if (phase >= 1.0)
        phase -= 1.0;
    else if (phase < 0.0)
        phase += 1.0;

    return value;
}

} // namespace apollo::dsp
