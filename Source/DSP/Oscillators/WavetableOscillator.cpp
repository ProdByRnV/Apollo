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

    // Re-derived from the *normalised* position, not from the frame position.
    //
    // This used to read `setPosition (static_cast<float> (framePosition))`,
    // which handed an absolute frame index to a function expecting [0, 1]: any
    // position past the first frame clamped to 1.0 and jumped the oscillator to
    // the top of the new table. Nothing sounded wrong, because Voice always set
    // the position again on the next line and again every modulation block —
    // but setTable is a public interface and it was wrong on its own terms.
    refreshReader();
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

    const auto newLevel = Wavetable::selectMipLevel (frequency, sampleRate);

    if (newLevel == mipLevel)
        return;

    // The level is part of what the Reader resolved, so a level change has to
    // rebuild it. Guarded because pitch modulation calls this every modulation
    // block and the level changes on almost none of them.
    mipLevel = newLevel;
    refreshReader();
}

void WavetableOscillator::setPosition (float newNormalisedPosition) noexcept
{
    // Non-finite is clamped to zero rather than passed on: neither comparison
    // below is true for a NaN, so it would otherwise travel into the frame
    // position and from there into an undefined conversion.
    const auto clamped = ! std::isfinite (newNormalisedPosition) ? 0.0f
                       : (newNormalisedPosition < 0.0f ? 0.0f
                       : (newNormalisedPosition > 1.0f ? 1.0f : newNormalisedPosition));

    if (clamped == normalisedPosition && reader.isValid())
        return;

    normalisedPosition = clamped;
    refreshReader();
}

void WavetableOscillator::refreshReader() noexcept
{
    if (table == nullptr || table->getNumFrames() <= 1)
        framePosition = 0.0;
    else
        framePosition = static_cast<double> (normalisedPosition)
                      * static_cast<double> (table->getNumFrames() - 1);

    reader = table != nullptr ? table->makeReader (mipLevel, framePosition)
                              : Wavetable::Reader {};
}

void WavetableOscillator::resetPhase (double startPhase) noexcept
{
    phase = startPhase - static_cast<double> (static_cast<long long> (startPhase));

    if (phase < 0.0)
        phase += 1.0;
}

float WavetableOscillator::getNextSample() noexcept
{
    // No null check, no empty check, no bounds check, no frame lookup. All of
    // it was settled by refreshReader, at most once per modulation block, and
    // a Reader with nothing behind it reads as silence (ADR-0071).
    const auto value = reader.read (phase);

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
