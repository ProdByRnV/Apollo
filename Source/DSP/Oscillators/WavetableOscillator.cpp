#include "DSP/Oscillators/WavetableOscillator.h"

#include <cmath>

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
    // A non-finite frequency would make the increment non-finite, and from
    // there every downstream guard has to cope with NaN. Rejecting it once,
    // here, keeps the invariant that phase is always finite.
    frequency = std::isfinite (frequencyHz) ? frequencyHz : 0.0;
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

    // A single subtraction is not enough. It assumes the increment is below 1.0,
    // which holds for any musical frequency but not for an absurd one — and mip
    // selection clamps the *level*, not the increment. At 1 GHz the increment is
    // over 20000, phase grows without bound, and the eventual conversion to a
    // table index is undefined behaviour.
    //
    // floor() wraps correctly for any magnitude and is only reached once per
    // cycle rather than once per sample, so the cost is nothing.
    if (phase >= 1.0 || phase < 0.0)
        phase -= std::floor (phase);

    return value;
}

} // namespace apollo::dsp
