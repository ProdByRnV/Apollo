#include "DSP/Unison/UnisonLayout.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double pi = 3.14159265358979323846;

/** Root two, as the normalisation that makes a centred pan position unity.

    See the pan law note in `update`.
*/
const float rootTwo = std::sqrt (2.0f);

[[nodiscard]] float clampUnitInterval (float value) noexcept
{
    if (! (value > 0.0f))
        return 0.0f; // Also catches NaN, which must not reach std::pow.

    return value > 1.0f ? 1.0f : value;
}

} // namespace

UnisonLayout::UnisonLayout() noexcept
{
    update (1, 0.0f, 0.0f);
}

bool UnisonLayout::matches (int voiceCount, float detuneAmount, float stereoSpread) const noexcept
{
    return voiceCount == builtVoiceCount
        && detuneAmount == builtDetuneAmount
        && stereoSpread == builtStereoSpread;
}

void UnisonLayout::update (int voiceCount, float detuneAmount, float stereoSpread) noexcept
{
    ++generation;

    builtVoiceCount = voiceCount;
    builtDetuneAmount = detuneAmount;
    builtStereoSpread = stereoSpread;

    count = voiceCount < 1 ? 1 : (voiceCount > maxVoices ? maxVoices : voiceCount);

    const auto detune = clampUnitInterval (detuneAmount) * maxDetuneCents;
    const auto spread = clampUnitInterval (stereoSpread);

    // 1/sqrt(N).
    //
    // The textbook normalisation for summing N uncorrelated sources, and the
    // honest description of what it does here is that unison voices are only
    // *approximately* uncorrelated. They are detuned, so they drift in and out
    // of phase: averaged over time the stack sits near a single voice's level,
    // but at the moments the voices align it reaches sqrt(N) times it. That is
    // inherent to unison rather than a defect of the normalisation — 1/N would
    // make the stack quieter the more voices you add, which is the opposite of
    // what the control is for.
    //
    // The consequence is recorded rather than hidden: VoiceEngine's headroom
    // guarantee is stated for the default patch, and unison is explicitly
    // outside it (ARCHITECTURE.md, VoiceEngine::outputGain).
    const auto normalisation = 1.0f / std::sqrt (static_cast<float> (count));

    for (int i = 0; i < maxVoices; ++i)
    {
        const auto index = static_cast<std::size_t> (i);

        if (i >= count)
        {
            // Kept well-defined rather than stale: an out-of-range read is a
            // silent voice at unity pitch, not a NaN or a random old gain.
            frequencyRatio[index] = 1.0;
            gainLeft[index] = 0.0f;
            gainRight[index] = 0.0f;
            startPhase[index] = 0.0;
            continue;
        }

        // Evenly spaced across [-1, +1], so the stack is symmetrical about the
        // played pitch and an odd voice count puts one voice exactly on it.
        // Even counts deliberately leave the centre empty, which is why a
        // 2-voice unison beats rather than reinforcing the fundamental.
        //
        // Written as one division of exact integers, in double, rather than as
        // `2i/(N-1) - 1` in float. The symmetry is the point: voice i and voice
        // N-1-i must be *exactly* opposite, or the stack drags the perceived
        // pitch off the note. The float form does not deliver that — 4/7 and
        // 10/7-1 are not exact negatives once rounded — and the error is small
        // enough to pass a casual listen while being trivially avoidable here.
        const auto position = count == 1
                                ? 0.0
                                : static_cast<double> (2 * i - (count - 1))
                                      / static_cast<double> (count - 1);

        // Cents to a frequency ratio. Linear in cents, which is linear in
        // perceived interval — spacing the voices evenly in hertz instead would
        // make the stack lopsided, wider above the fundamental than below it.
        const auto cents = position * static_cast<double> (detune);
        frequencyRatio[index] = std::pow (2.0, cents / 1200.0);

        // Constant-power pan, normalised so that a centred voice is unity on
        // both channels rather than -3 dB.
        //
        // The usual constant-power law puts unity at the extremes and 0.707 at
        // centre. Apollo needs the opposite anchor: the default patch is a
        // single centred unison voice, and it must come out at exactly the level
        // the engine's gain staging was measured for (VoiceEngine::outputGain),
        // not 3 dB below it. Scaling by root two moves the -3 dB point from the
        // centre to the edges, where a hard-spread voice reaches 1.414 on one
        // channel and zero on the other — but a hard-spread stack has its
        // voices distributed across the field rather than piled on one edge, so
        // the per-channel sum stays close to the centred case.
        const auto panPosition = position * static_cast<double> (spread);
        const auto angle = (panPosition + 1.0) * (pi * 0.25);

        gainLeft[index] = rootTwo * static_cast<float> (std::cos (angle)) * normalisation;
        gainRight[index] = rootTwo * static_cast<float> (std::sin (angle)) * normalisation;

        startPhase[index] = static_cast<double> (i) / static_cast<double> (count);
    }
}

} // namespace apollo::dsp
