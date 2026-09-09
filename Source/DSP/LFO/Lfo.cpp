#include "DSP/LFO/Lfo.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double pi = 3.14159265358979323846;

/** Slowest and fastest an LFO may run.

    The bottom is one cycle every hundred seconds, which is slower than any
    musical use and still finite. The top deliberately reaches into the audio
    range: an LFO taken up to a few hundred hertz stops being modulation and
    becomes a timbre, which is a legitimate and much-used thing to do.
*/
constexpr float minimumRateHz = 0.01f;
constexpr float maximumRateHz = 400.0f;

/** Longest slew the smoothing control can ask for, in seconds. */
constexpr float maximumSmoothingSeconds = 0.25f;

[[nodiscard]] float clampRange (float value, float low, float high) noexcept
{
    // Positive test first, so NaN lands on the low end rather than travelling
    // into a phase increment (CLAUDE.md §34.2).
    if (! (value >= low))
        return low;

    return value > high ? high : value;
}

[[nodiscard]] double wrapPhase (double p) noexcept
{
    if (! std::isfinite (p))
        return 0.0;

    p -= std::floor (p);

    // floor() of a negative leaves the result in range, but guard the boundary
    // so a value of exactly 1 never escapes into an index.
    return p >= 1.0 ? 0.0 : p;
}

} // namespace

float tempoSyncedRateHz (double bpm, double beatsPerCycle) noexcept
{
    // A host that reports no tempo, or a nonsensical one, must not stop the LFO
    // or send it to infinity. 120 is the conventional fallback and keeps a
    // synced LFO moving at a musically plausible speed until a real tempo
    // arrives (CLAUDE.md §38).
    if (! (bpm > 1.0) || bpm > 1000.0)
        bpm = 120.0;

    if (! (beatsPerCycle > 0.0))
        beatsPerCycle = 1.0;

    const auto hz = bpm / 60.0 / beatsPerCycle;

    return clampRange (static_cast<float> (hz), minimumRateHz, maximumRateHz);
}

void Lfo::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    setSettings (settings);
    reset();
}

void Lfo::setSettings (const LfoSettings& newSettings) noexcept
{
    settings = newSettings;

    increment = static_cast<double> (clampRange (settings.rateHz, minimumRateHz, maximumRateHz))
              / sampleRate;

    // Smoothing as a one-pole coefficient. Expressed as a time so that the
    // control means the same thing at every sample rate.
    const auto smoothingSeconds = clampRange (settings.smoothing, 0.0f, 1.0f) * maximumSmoothingSeconds;

    smoothingCoefficient = smoothingSeconds > 0.0f
                             ? static_cast<float> (std::exp (-1.0 / (static_cast<double> (smoothingSeconds)
                                                                     * sampleRate)))
                             : 0.0f;

    const auto fadeSeconds = clampRange (settings.fadeInSeconds, 0.0f, 60.0f);
    const auto fadeSamples = static_cast<double> (fadeSeconds) * sampleRate;

    fadeIncrement = fadeSamples >= 1.0 ? static_cast<float> (1.0 / fadeSamples) : 1.0f;
}

void Lfo::setSeed (std::uint32_t newSeed) noexcept
{
    // Zero would lock a xorshift at zero forever, so it is mapped away rather
    // than trusted.
    seed = (newSeed * 2654435761u) | 1u;
    randomState = seed;
}

void Lfo::noteOn (double freeRunningPhase) noexcept
{
    phase = settings.retrigger
              ? wrapPhase (static_cast<double> (settings.phaseOffset))
              : wrapPhase (freeRunningPhase + static_cast<double> (settings.phaseOffset));

    // The fade restarts with the note whichever mode the LFO is in: it is a
    // property of this note beginning, not of the shape's motion.
    fadeGain = fadeIncrement >= 1.0f ? 1.0f : 0.0f;

    // A retriggered sample-and-hold must pick a fresh value rather than carry
    // the previous note's, and it must do so from a reproducible sequence.
    if (settings.retrigger)
    {
        randomState = seed;
        hasHeldValue = false;
    }

    current = shapeAt (phase);
    smoothed = current;
}

void Lfo::reset() noexcept
{
    phase = wrapPhase (static_cast<double> (settings.phaseOffset));

    current = 0.0f;
    smoothed = 0.0f;

    fadeGain = fadeIncrement >= 1.0f ? 1.0f : 0.0f;

    heldValue = 0.0f;
    hasHeldValue = false;
    randomState = seed;
}

float Lfo::nextRandom() noexcept
{
    // Marsaglia's 32-bit xorshift, as used by the noise generator: reproducible,
    // and cheap enough that a per-cycle call costs nothing.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;

    constexpr float scale = 1.0f / 2147483648.0f;

    return static_cast<float> (static_cast<std::int32_t> (randomState)) * scale;
}

float Lfo::shapeAt (double p) noexcept
{
    switch (settings.shape)
    {
        case LfoShape::sine:
            return static_cast<float> (std::sin (2.0 * pi * p));

        case LfoShape::triangle:
            // Rises from -1 to +1 over the first half and falls back over the
            // second, so it starts at the bottom like the saw does.
            return static_cast<float> (p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p));

        case LfoShape::saw:
            return static_cast<float> (2.0 * p - 1.0);

        case LfoShape::reverseSaw:
            return static_cast<float> (1.0 - 2.0 * p);

        case LfoShape::square:
            // Exactly square. No band-limiting: an LFO is never heard, so the
            // harmonics an audio oscillator would have to suppress are simply
            // not a problem here, and a rounded edge would be a worse control.
            return p < 0.5 ? 1.0f : -1.0f;

        case LfoShape::sampleAndHold:
        {
            if (! hasHeldValue)
            {
                heldValue = nextRandom();
                hasHeldValue = true;
            }

            return heldValue;
        }

        case LfoShape::step:
        {
            // The ramp quantised into equal steps: a staircase saw. A
            // user-drawn step sequence is a different thing and needs the editor
            // that draws it, which is Phase 7.
            const auto steps = settings.stepCount < 2 ? 2 : settings.stepCount;
            const auto index = static_cast<double> (static_cast<int> (p * static_cast<double> (steps)));

            return static_cast<float> (2.0 * index / static_cast<double> (steps - 1) - 1.0);
        }

        default:
            return 0.0f;
    }
}

float Lfo::getNextValue() noexcept
{
    // The value for *this* sample is taken at the current phase, and the phase
    // advances afterwards. The other order would mean the first sample after a
    // note started was already one increment into the shape, so a saw set to
    // retrigger would never quite begin at its bottom.
    auto value = shapeAt (phase);

    // Slew. Applied before polarity so the smoothing time means the same thing
    // whichever range the output is in.
    if (smoothingCoefficient > 0.0f)
    {
        smoothed = value + (smoothed - value) * smoothingCoefficient;
        value = smoothed;
    }
    else
    {
        smoothed = value;
    }

    // Fade collapses the shape toward the centre of its range rather than toward
    // its bottom, in both polarities: a vibrato should arrive by widening, not
    // by sliding the pitch up to meet it.
    if (fadeGain < 1.0f)
    {
        fadeGain += fadeIncrement;

        if (fadeGain > 1.0f)
            fadeGain = 1.0f;
    }

    value *= fadeGain;

    if (! settings.bipolar)
        value = 0.5f * (value + 1.0f);

    current = value;

    // Advance for the next call.
    const auto previousPhase = phase;
    phase += increment;

    if (phase >= 1.0 || phase < previousPhase)
    {
        phase = wrapPhase (phase);

        // A cycle boundary is where sample-and-hold takes a new value. Detected
        // from the wrap rather than from a counter, so it stays correct when the
        // rate changes part-way through a cycle.
        if (settings.shape == LfoShape::sampleAndHold)
            hasHeldValue = false;
    }

    return current;
}

} // namespace apollo::dsp
