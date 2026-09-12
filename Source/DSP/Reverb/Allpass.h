#pragma once

/*
    A Schroeder allpass, for diffusion.

    WHAT IT IS FOR. A reverb's feedback network makes a tail, but fed a sharp
    transient directly it makes a tail of distinct echoes — you hear the network
    rather than a room. An allpass in front smears the transient in time without
    changing its spectrum: every frequency comes out at the same level as it went
    in, later and spread out. Four of them in series, with mutually prime delays,
    turn a click into something dense enough for the network to make a room out
    of.

    WHY IT DOES NOT COLOUR THE SOUND. The feedforward and feedback paths are the
    same coefficient with opposite signs, which is exactly the condition for unit
    magnitude at every frequency. That is the whole reason to use this rather
    than another comb: it changes when energy arrives without changing what the
    energy is.

    STABILITY. |g| < 1 is the condition, and the caller is held to it by
    `maximumCoefficient` rather than trusted with it.

    REAL-TIME CONTRACT: `prepare` allocates. Everything else is audio-thread
    callable and allocates nothing.
*/

#include "DSP/Delay/DelayLine.h"

namespace apollo::dsp
{

class Allpass
{
public:
    /** The largest diffusion coefficient allowed.

        Below 1 by enough that the recursion decays quickly. Diffusion is
        supposed to smear a transient over a few milliseconds, not to ring.
    */
    static constexpr float maximumCoefficient = 0.85f;

    Allpass() = default;

    /** Sizes the buffer. Allocates; call while stopped. */
    void prepare (int delaySamples)
    {
        length = delaySamples > 1 ? delaySamples : 1;
        line.prepare (length);
    }

    void reset() noexcept { line.reset(); }

    void setCoefficient (float newCoefficient) noexcept
    {
        // Positive test first, so a NaN lands on zero rather than in the
        // feedback path (CLAUDE.md §34.2).
        if (! (newCoefficient > -maximumCoefficient))
        {
            coefficient = -maximumCoefficient;
            return;
        }

        coefficient = newCoefficient < maximumCoefficient ? newCoefficient : maximumCoefficient;
    }

    [[nodiscard]] float processSample (float input) noexcept
    {
        const auto delayed = line.read (static_cast<float> (length));
        const auto stored = input + coefficient * delayed;

        line.write (stored);

        return delayed - coefficient * stored;
    }

private:
    DelayLine line;

    int length = 1;
    float coefficient = 0.7f;
};

} // namespace apollo::dsp
