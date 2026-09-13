#include "DSP/EQ/EqualiserBand.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apollo::dsp
{

namespace
{

constexpr double pi = std::numbers::pi_v<double>;

/** How long a smoothed setting takes to cover most of the distance to its
    target, in seconds.

    Twenty milliseconds: long enough that a jump from one end of the frequency
    range to the other is heard as a sweep rather than a click, short enough that
    a deliberate move still feels attached to the hand that made it. The same
    figure the master gain uses, for the same reason.
*/
constexpr double smoothingSeconds = 0.02;

/** Distances below which a smoothed setting is snapped onto its target.

    An exponential approach never actually arrives, and "never arrives" matters
    here beyond tidiness: a band is only skipped once its gain is *exactly* zero,
    so a gain creeping towards zero for ever would keep the whole band in the
    signal path for ever.
*/
constexpr double logFrequencyEpsilon = 1.0e-5;  ///< About a thousandth of a cent.
constexpr double gainEpsilon = 1.0e-4;          ///< Decibels.
constexpr double bandwidthEpsilon = 1.0e-5;     ///< Octaves.

/** Q at which a pole pair is underdamped enough to ring audibly. Below this a
    filter settles without overshoot and has no tail worth reporting.
*/
constexpr double resonantQ = 0.70710678118654752;

/** Decibels of decay the tail estimate waits for, expressed as the multiple of
    the time constant that reaches it: ln(10^(60/20)).
*/
constexpr double tailTimeConstants = 6.907755278982137;

[[nodiscard]] int clampOrder (int order) noexcept
{
    return std::clamp (order, 1, EqualiserBand::maxOrder);
}

[[nodiscard]] double clampGainDb (double gainDb) noexcept
{
    const auto limit = static_cast<double> (EqualiserBand::maximumGainDb);

    // Comparisons rather than std::clamp, so a NaN — which fails every one of
    // them — lands on zero and silences the band rather than propagating.
    if (gainDb > limit) return limit;
    if (gainDb >= -limit) return gainDb;
    if (gainDb < -limit) return -limit;

    return 0.0;
}

[[nodiscard]] double clampBandwidth (double octaves) noexcept
{
    const auto low = static_cast<double> (EqualiserBand::minimumBandwidthOctaves);
    const auto high = static_cast<double> (EqualiserBand::maximumBandwidthOctaves);

    if (octaves > high) return high;
    if (octaves > low) return octaves;

    return low;
}

} // namespace

bool EqualiserBand::hasGain (Type type) noexcept
{
    return type == Type::lowShelf || type == Type::peaking || type == Type::highShelf;
}

BiquadCoefficients EqualiserBand::design (const Settings& settings, double sampleRate) noexcept
{
    if (! (sampleRate > 0.0) || settings.type == Type::off || settings.muted)
        return {};

    const auto frequency = static_cast<double> (settings.frequencyHz);
    const auto gainDb = clampGainDb (static_cast<double> (settings.gainDb));
    const auto q = rbj::qForBandwidth (clampBandwidth (static_cast<double> (settings.bandwidthOctaves)),
                                       frequency, sampleRate);

    switch (settings.type)
    {
        case Type::lowPass:   return rbj::lowPass   (frequency, q, sampleRate);
        case Type::bandPass:  return rbj::bandPass  (frequency, q, sampleRate);
        case Type::highPass:  return rbj::highPass  (frequency, q, sampleRate);
        case Type::notch:     return rbj::notch     (frequency, q, sampleRate);
        case Type::lowShelf:  return rbj::lowShelf  (frequency, q, gainDb, sampleRate);
        case Type::peaking:   return rbj::peaking   (frequency, q, gainDb, sampleRate);
        case Type::highShelf: return rbj::highShelf (frequency, q, gainDb, sampleRate);

        case Type::off:
        default:
            return {};
    }
}

double EqualiserBand::magnitudeDbAt (const Settings& settings, double sampleRate, double hz) noexcept
{
    if (settings.type == Type::off || settings.muted)
        return 0.0;

    const auto magnitude = design (settings, sampleRate).magnitudeAt (hz, sampleRate);

    // Every section is identical, so the response is one section's raised to the
    // order — which in decibels is one section's multiplied by it.
    const auto order = static_cast<double> (clampOrder (settings.order));

    if (! (magnitude > 0.0))
        return -1000.0 * order;

    return 20.0 * std::log10 (magnitude) * order;
}

void EqualiserBand::prepare (double sampleRate)
{
    preparedSampleRate = sampleRate;

    // Settled at the target rather than smoothing towards it: a band arrives at
    // its settings, it does not glide up to them from wherever the last device
    // left it.
    smoothedLogFrequency = std::log (std::max (static_cast<double> (target.frequencyHz), 1.0e-6));
    smoothedGainDb = clampGainDb (static_cast<double> (target.gainDb));
    smoothedBandwidth = clampBandwidth (static_cast<double> (target.bandwidthOctaves));

    redesign();
    reset();
}

void EqualiserBand::reset() noexcept
{
    for (auto& channel : sections)
        for (auto& section : channel)
            section.reset();

    wasActive = false;
}

void EqualiserBand::setSettings (const Settings& newSettings) noexcept
{
    target = newSettings;
    target.order = clampOrder (target.order);
}

bool EqualiserBand::isTransparent() const noexcept
{
    if (target.type == Type::off || target.muted)
        return true;

    // A gain shape at exactly 0 dB is the identity: the cookbook's A is 1, and
    // its numerator and denominator come out term for term the same.
    return hasGain (target.type) && smoothedGainDb == 0.0;
}

bool EqualiserBand::advanceSmoothing (int numSamples) noexcept
{
    const auto targetLogFrequency = std::log (std::max (static_cast<double> (target.frequencyHz), 1.0e-6));
    const auto targetGainDb = clampGainDb (static_cast<double> (target.gainDb));
    const auto targetBandwidth = clampBandwidth (static_cast<double> (target.bandwidthOctaves));

    if (! (preparedSampleRate > 0.0) || numSamples <= 0)
    {
        smoothedLogFrequency = targetLogFrequency;
        smoothedGainDb = targetGainDb;
        smoothedBandwidth = targetBandwidth;
        return true;
    }

    // One pole, stepped once for the whole block. The coefficient is derived
    // from the block length so that the time the sweep takes is the same
    // whatever buffer size the host happens to be using — a smoother that moved
    // a fixed fraction per block would sweep four times faster at 64 samples
    // than at 256.
    const auto blockSeconds = static_cast<double> (numSamples) / preparedSampleRate;
    const auto coefficient = 1.0 - std::exp (-blockSeconds / smoothingSeconds);

    const auto step = [coefficient] (double& current, double destination, double epsilon)
    {
        const auto distance = destination - current;

        if (distance < epsilon && distance > -epsilon)
        {
            const auto arrived = current != destination;
            current = destination;
            return arrived;
        }

        current += coefficient * distance;
        return true;
    };

    auto moved = step (smoothedLogFrequency, targetLogFrequency, logFrequencyEpsilon);
    moved = step (smoothedGainDb, targetGainDb, gainEpsilon) || moved;
    moved = step (smoothedBandwidth, targetBandwidth, bandwidthEpsilon) || moved;

    return moved;
}

void EqualiserBand::redesign() noexcept
{
    Settings resolved = target;

    resolved.frequencyHz = static_cast<float> (std::exp (smoothedLogFrequency));
    resolved.gainDb = static_cast<float> (smoothedGainDb);
    resolved.bandwidthOctaves = static_cast<float> (smoothedBandwidth);

    coefficients = design (resolved, preparedSampleRate);
}

void EqualiserBand::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto moved = advanceSmoothing (numSamples);

    if (isTransparent())
    {
        // Nothing to do, and nothing that should be remembered: whatever the
        // sections hold now is audio from before the band was switched out, and
        // it must not be poured back in when the band returns.
        activeSections = 0;
        wasActive = false;
        return;
    }

    if (moved)
        redesign();

    const auto order = clampOrder (target.order);

    // A band coming back into circuit, or one that has just grown a section,
    // starts that section from silence rather than from whatever it was holding.
    if (! wasActive || order > activeSections)
    {
        const auto from = wasActive ? activeSections : 0;

        for (auto& channel : sections)
            for (auto section = from; section < order; ++section)
                channel[static_cast<std::size_t> (section)].reset();
    }

    activeSections = order;
    wasActive = true;

    const auto usableChannels = std::min (numChannels, maxEffectChannels);

    for (auto channel = 0; channel < usableChannels; ++channel)
    {
        auto* samples = channels[channel];

        if (samples == nullptr)
            continue;

        auto& state = sections[static_cast<std::size_t> (channel)];

        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto value = static_cast<double> (samples[sample]);

            for (auto section = 0; section < order; ++section)
                value = state[static_cast<std::size_t> (section)].process (value, coefficients);

            samples[sample] = static_cast<float> (value);
        }
    }
}

double EqualiserBand::magnitudeDbAt (double hz) const noexcept
{
    if (isTransparent())
        return 0.0;

    const auto magnitude = coefficients.magnitudeAt (hz, preparedSampleRate);
    const auto order = static_cast<double> (clampOrder (target.order));

    if (! (magnitude > 0.0))
        return -1000.0 * order;

    return 20.0 * std::log10 (magnitude) * order;
}

double EqualiserBand::getTailSeconds() const noexcept
{
    if (isTransparent() || ! (preparedSampleRate > 0.0))
        return 0.0;

    const auto frequency = rbj::clampFrequency (static_cast<double> (target.frequencyHz),
                                                preparedSampleRate);

    const auto q = rbj::qForBandwidth (clampBandwidth (static_cast<double> (target.bandwidthOctaves)),
                                       frequency, preparedSampleRate);

    // A band only rings if its poles are underdamped, and a shape whose gain
    // cuts has nothing to ring: its poles are the ones being *added* damping.
    const auto resonates = hasGain (target.type) ? smoothedGainDb > 0.0 : q > resonantQ;

    if (! resonates)
        return 0.0;

    // The envelope of an underdamped pole pair decays with a time constant of
    // Q/(pi*f); this is how long that takes to fall 60 dB. Cascading identical
    // sections shapes the envelope without moving the pole, so the order does
    // not extend it.
    return tailTimeConstants * q / (pi * frequency);
}

} // namespace apollo::dsp
