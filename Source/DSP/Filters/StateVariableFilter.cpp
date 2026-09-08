#include "DSP/Filters/StateVariableFilter.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr float pi = 3.14159265358979323846f;

/** Lowest cutoff the filter will accept, in hertz.

    Below the bottom of hearing there is nothing left to remove, and a cutoff
    approaching zero makes `g` approach zero, which pushes the filter's time
    constant toward infinity and leaves any DC offset in the state sitting there
    audibly for seconds.
*/
constexpr float minimumCutoffHz = 10.0f;

/** How close to Nyquist the cutoff may go, as a fraction of the sample rate.

    `tan(pi * f / fs)` diverges at f = fs/2. Stopping at 0.49 keeps the
    coefficient finite and well-conditioned while leaving the whole audible band
    reachable at every supported sample rate — 0.49 of 44.1 kHz is 21.6 kHz.
*/
constexpr float maximumCutoffFraction = 0.49f;

/** Damping at full resonance.

    Not zero. Zero damping is exactly the self-oscillation point, where the
    filter sustains a tone with no input and, with any numerical drift, grows
    without bound. A small floor keeps maximum resonance dramatic and stable,
    which is the trade every musical filter makes.
*/
constexpr float minimumDamping = 0.05f;

[[nodiscard]] float clampRange (float value, float low, float high) noexcept
{
    // Positive test first, so NaN lands on the low end instead of propagating
    // into a coefficient (CLAUDE.md §34.2).
    if (! (value >= low))
        return low;

    return value > high ? high : value;
}

} // namespace

void SvfCoefficients::set (float cutoffHz, float q, double sampleRate) noexcept
{
    const auto rate = sampleRate > 0.0 ? static_cast<float> (sampleRate) : 44100.0f;
    const auto highest = rate * maximumCutoffFraction;

    const auto cutoff = clampRange (cutoffHz, minimumCutoffHz, highest);

    g = std::tan (pi * cutoff / rate);

    // Damping is the reciprocal of Q. The clamp at the bottom is what keeps a
    // high resonance dramatic instead of divergent: zero damping is exactly the
    // self-oscillation point, where the filter sustains with no input and any
    // numerical drift grows without bound.
    const auto quality = clampRange (q, 0.025f, 40.0f);
    k = clampRange (1.0f / quality, minimumDamping, 10.0f);

    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void StateVariableFilter::reset() noexcept
{
    ic1eq = 0.0f;
    ic2eq = 0.0f;
}

float StateVariableFilter::processSample (float input) noexcept
{
    if (mode == Mode::off)
        return input;

    // Zavalishin's zero-delay-feedback solution. v1 is the bandpass output and
    // v2 the lowpass; the rest are formed from them.
    const auto v3 = input - ic2eq;
    const auto v1 = coefficients.a1 * ic1eq + coefficients.a2 * v3;
    const auto v2 = ic2eq + coefficients.a2 * ic1eq + coefficients.a3 * v3;

    ic1eq = 2.0f * v1 - ic1eq;
    ic2eq = 2.0f * v2 - ic2eq;

    switch (mode)
    {
        case Mode::lowpass:  return v2;
        case Mode::bandpass: return v1;
        case Mode::highpass: return input - coefficients.k * v1 - v2;

        // Highpass plus lowpass, which cancels at the cutoff and passes
        // everything else.
        case Mode::notch:    return input - coefficients.k * v1;

        case Mode::off:
        default:
            return input;
    }
}

} // namespace apollo::dsp
