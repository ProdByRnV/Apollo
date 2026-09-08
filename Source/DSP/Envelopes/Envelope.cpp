#include "DSP/Envelopes/Envelope.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

/** Curve tension below which a segment is treated as a straight line.

    The exponential form divides by (1 - e^-k), which approaches zero with k, so
    a tiny curve value would produce a very large scale factor multiplied by a
    very small difference. That is arithmetically fine in exact maths and
    numerically poor in floats, and the audible difference between k = 0.001 and
    a straight line is nothing at all.
*/
constexpr float linearThreshold = 1.0e-3f;

/** Maps the user-facing curve control onto the exponent the shaping uses.

    Six is chosen so that the extremes are strongly curved but still usable —
    at k = 6 a segment covers half its travel in the first eighth of its time —
    rather than so extreme that the last part of the range is indistinguishable.
*/
constexpr float curveScale = 6.0f;

[[nodiscard]] float clampUnit (float value, float low, float high) noexcept
{
    // Written as a positive test so NaN, which compares false against
    // everything, lands on the low end rather than passing through into a
    // gain (CLAUDE.md §34.2).
    if (! (value >= low))
        return low;

    return value > high ? high : value;
}

[[nodiscard]] float clampSeconds (float value) noexcept
{
    if (! (value >= 0.0f))
        return 0.0f;

    // An hour is far past any musical use and keeps the sample count inside an
    // int at every supported sample rate.
    return value > 3600.0f ? 3600.0f : value;
}

} // namespace

void Envelope::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void Envelope::setSettings (const EnvelopeSettings& newSettings) noexcept
{
    if (newSettings == settings)
        return;

    const auto previousSustain = settings.sustainLevel;

    settings = newSettings;

    if (samplesRemaining > 0 && segmentLengthSamples > 0)
    {
        // A change while a timed segment is running retimes that segment rather
        // than waiting for the next one, so a decay knob moved during a held
        // note is audible immediately. The progress already made is preserved
        // as a fraction: a segment a third of the way through stays a third of
        // the way through at its new length.
        const auto remainingFraction = static_cast<float> (samplesRemaining)
                                     / static_cast<float> (segmentLengthSamples);

        beginSegment (targetForStage (stage), secondsForStage (stage) * remainingFraction);
    }
    else if (stage == EnvelopeStage::sustain && previousSustain != settings.sustainLevel)
    {
        // Sustain is a level rather than a time, so it applies at once. A held
        // note follows the knob.
        level = clampUnit (settings.sustainLevel, 0.0f, 1.0f);
    }
}

void Envelope::noteOn() noexcept
{
    enterStage (EnvelopeStage::delay);
}

void Envelope::noteOff() noexcept
{
    if (stage == EnvelopeStage::idle)
        return;

    enterStage (EnvelopeStage::release);
}

void Envelope::reset() noexcept
{
    stage = EnvelopeStage::idle;
    level = 0.0f;

    segmentStart = 0.0f;
    segmentTarget = 0.0f;
    segmentIsLinear = true;
    linearIncrement = 0.0f;
    expTerm = 1.0f;
    expRatio = 1.0f;
    expScale = 1.0f;
    samplesRemaining = 0;
    segmentLengthSamples = 0;
}

float Envelope::secondsForStage (EnvelopeStage query) const noexcept
{
    switch (query)
    {
        case EnvelopeStage::delay:   return clampSeconds (settings.delaySeconds);
        case EnvelopeStage::attack:  return clampSeconds (settings.attackSeconds);
        case EnvelopeStage::hold:    return clampSeconds (settings.holdSeconds);
        case EnvelopeStage::decay:   return clampSeconds (settings.decaySeconds);
        case EnvelopeStage::release: return clampSeconds (settings.releaseSeconds);

        case EnvelopeStage::idle:
        case EnvelopeStage::sustain:
        default:
            return 0.0f;
    }
}

float Envelope::targetForStage (EnvelopeStage query) const noexcept
{
    switch (query)
    {
        // Delay holds wherever the envelope already is. For a fresh note that
        // is silence; for a retrigger it is whatever the previous note left,
        // which is what keeps the restart click-free.
        case EnvelopeStage::delay:   return level;
        case EnvelopeStage::attack:  return 1.0f;
        case EnvelopeStage::hold:    return 1.0f;
        case EnvelopeStage::decay:   return clampUnit (settings.sustainLevel, 0.0f, 1.0f);
        case EnvelopeStage::sustain: return clampUnit (settings.sustainLevel, 0.0f, 1.0f);
        case EnvelopeStage::release: return 0.0f;

        case EnvelopeStage::idle:
        default:
            return 0.0f;
    }
}

void Envelope::beginSegment (float target, float seconds) noexcept
{
    segmentStart = level;
    segmentTarget = target;

    const auto samples = static_cast<double> (seconds) * sampleRate;

    segmentLengthSamples = samples >= 1.0 ? static_cast<int> (samples) : 0;
    samplesRemaining = segmentLengthSamples;

    if (segmentLengthSamples <= 0)
        return;

    const auto k = clampUnit (settings.curve, -1.0f, 1.0f) * curveScale;

    if (std::abs (k) < linearThreshold)
    {
        segmentIsLinear = true;
        linearIncrement = (segmentTarget - segmentStart) / static_cast<float> (segmentLengthSamples);
        return;
    }

    segmentIsLinear = false;

    // e^(-k*p) at p = 0 is one, and stepping p by 1/N each sample multiplies it
    // by e^(-k/N). One exp() per segment, none per sample.
    expTerm = 1.0f;
    expRatio = std::exp (-k / static_cast<float> (segmentLengthSamples));

    const auto denominator = 1.0f - std::exp (-k);
    expScale = denominator != 0.0f ? 1.0f / denominator : 1.0f;
}

void Envelope::enterStage (EnvelopeStage next) noexcept
{
    stage = next;

    // Zero-length stages are skipped outright rather than costing a sample
    // each. The loop terminates because sustain and idle are untimed.
    while (stage != EnvelopeStage::idle && stage != EnvelopeStage::sustain)
    {
        beginSegment (targetForStage (stage), secondsForStage (stage));

        if (samplesRemaining > 0)
            return;

        // The stage takes no time, so its endpoint is reached immediately.
        level = segmentTarget;

        switch (stage)
        {
            case EnvelopeStage::delay:   stage = EnvelopeStage::attack;  break;
            case EnvelopeStage::attack:  stage = EnvelopeStage::hold;    break;
            case EnvelopeStage::hold:    stage = EnvelopeStage::decay;   break;
            case EnvelopeStage::decay:   stage = EnvelopeStage::sustain; break;
            case EnvelopeStage::release: stage = EnvelopeStage::idle;    break;

            case EnvelopeStage::idle:
            case EnvelopeStage::sustain:
            default:
                return;
        }
    }

    if (stage == EnvelopeStage::sustain)
        level = clampUnit (settings.sustainLevel, 0.0f, 1.0f);
    else if (stage == EnvelopeStage::idle)
        level = 0.0f;
}

float Envelope::getNextValue() noexcept
{
    switch (stage)
    {
        case EnvelopeStage::idle:
            return 0.0f;

        case EnvelopeStage::sustain:
            // Untimed: the level only moves when the sustain setting does, or
            // when note-off arrives.
            return level;

        default:
            break;
    }

    if (samplesRemaining <= 0)
    {
        // Defensive: a segment with no samples should have been skipped by
        // enterStage, so reaching here means the stage is over.
        level = segmentTarget;
    }
    else
    {
        --samplesRemaining;

        if (samplesRemaining == 0)
        {
            // Assigned rather than accumulated, so a segment lands exactly on
            // its target however long it ran.
            level = segmentTarget;
        }
        else if (segmentIsLinear)
        {
            level += linearIncrement;
        }
        else
        {
            expTerm *= expRatio;
            level = segmentStart + (segmentTarget - segmentStart) * (1.0f - expTerm) * expScale;
        }
    }

    if (samplesRemaining <= 0)
    {
        switch (stage)
        {
            case EnvelopeStage::delay:   enterStage (EnvelopeStage::attack);  break;
            case EnvelopeStage::attack:  enterStage (EnvelopeStage::hold);    break;
            case EnvelopeStage::hold:    enterStage (EnvelopeStage::decay);   break;
            case EnvelopeStage::decay:   enterStage (EnvelopeStage::sustain); break;
            case EnvelopeStage::release: enterStage (EnvelopeStage::idle);    break;

            case EnvelopeStage::idle:
            case EnvelopeStage::sustain:
            default:
                break;
        }
    }

    return level;
}

} // namespace apollo::dsp
