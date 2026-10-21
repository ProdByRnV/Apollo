#include "DSP/Filters/CombFilter.h"

#include <algorithm>
#include <cmath>

namespace apollo::dsp
{

namespace
{

/** The most feedback any setting or modulation can reach. */
constexpr float maximumFeedback = 0.97f;

/** How long a moving delay takes to get 63 % of the way to its target. */
constexpr double glideSeconds = 0.003;

/** The most the delay may move in one sample, in samples. */
constexpr float maximumSlew = 1.0f;

[[nodiscard]] float clampRange (float value, float low, float high) noexcept
{
    // Positive test first, so NaN lands on the low end (CLAUDE.md §34.2).
    if (! (value >= low))
        return low;

    return value > high ? high : value;
}

} // namespace

void CombCoefficients::set (float cutoffHz, float q, double sampleRate) noexcept
{
    const auto rate = sampleRate > 0.0 ? static_cast<float> (sampleRate) : 44100.0f;
    const auto cutoff = clampRange (cutoffHz, CombFilter::lowestFrequencyHz,
                                    rate / CombFilter::shortestDelaySamples);

    delaySamples = clampRange (rate / cutoff, CombFilter::shortestDelaySamples,
                               rate / CombFilter::lowestFrequencyHz);

    // The same Q range the state-variable filter accepts, so one resonance
    // value means the same extremes whichever type reads it.
    const auto quality = clampRange (q, 0.025f, 40.0f);
    feedback = std::min (quality / (quality + 0.5f), maximumFeedback);

    gain = std::sqrt (2.0f * (1.0f - feedback)) * 0.5f;
}

void CombFilter::prepare (double sampleRate)
{
    const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // The longest delay, plus the interpolator's reach beyond it.
    const auto needed = static_cast<int> (std::ceil (rate / static_cast<double> (lowestFrequencyHz))) + 4;

    auto size = 1;

    while (size < needed)
        size <<= 1;

    line.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    longestDelay = static_cast<float> (size - 3);

    glide = static_cast<float> (1.0 - std::exp (-1.0 / (glideSeconds * rate)));

    writeIndex = 0;
    dirty = false;
    primed = false;
}

void CombFilter::reset() noexcept
{
    if (dirty)
        std::fill (line.begin(), line.end(), 0.0f);

    dirty = false;
    primed = false;
    writeIndex = 0;
    allpassState = 0.0f;
}

void CombFilter::setCoefficients (const CombCoefficients& newCoefficients) noexcept
{
    // Clamped against this line rather than trusted: coefficients resolved for
    // a higher rate than the line was prepared for must not read past it.
    targetDelay = clampRange (newCoefficients.delaySamples, shortestDelaySamples, longestDelay);
    feedback = clampRange (newCoefficients.feedback, 0.0f, maximumFeedback);
    gain = newCoefficients.gain;

    if (! primed)
    {
        delay = targetDelay;
        primed = true;
    }
}

float CombFilter::processSample (float input) noexcept
{
    if (line.empty())
        return input;

    // The glide, slew-limited to one sample of delay per sample. A
    // time-varying allpass in a feedback loop can gain energy when the samples
    // it reads jump about; measured, a comb swept end to end every 64 samples
    // without this limit reached 3.8 million. The limit only engages on a jump
    // of more than about 145 samples — across most of the comb's range in one
    // block — and ordinary modulation never meets it.
    const auto step = (targetDelay - delay) * glide;
    delay += step > maximumSlew ? maximumSlew : (step < -maximumSlew ? -maximumSlew : step);

    // w[n − D] through a first-order allpass: a whole number of samples N,
    // then the allpass for the remaining d, kept in [0.5, 1.5) where its delay
    // is flattest across frequency and its pole furthest from the unit circle.
    const auto whole = static_cast<int> (delay - 0.5f);
    const auto d = delay - static_cast<float> (whole);
    const auto eta = (1.0f - d) / (1.0f + d);

    const auto newer = line[static_cast<std::size_t> ((writeIndex - whole) & mask)];
    const auto older = line[static_cast<std::size_t> ((writeIndex - whole - 1) & mask)];

    auto delayed = eta * newer + older - eta * allpassState;

    if (std::abs (delayed) < 1.0e-15f)
        delayed = 0.0f;

    allpassState = delayed;

    auto w = input + feedback * delayed;

    // A ringing comb decays geometrically and would otherwise spend its last
    // seconds in denormals (CLAUDE.md §37).
    if (std::abs (w) < 1.0e-15f)
        w = 0.0f;

    line[static_cast<std::size_t> (writeIndex)] = w;
    writeIndex = (writeIndex + 1) & mask;
    dirty = true;

    return gain * (w + delayed);
}

} // namespace apollo::dsp
