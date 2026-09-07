#pragma once

/*
    A stereo white-noise source.

    White noise generated at the sample rate is band-limited by construction:
    it has no content above Nyquist to fold back, so unlike the oscillators it
    needs no mipmap and no oversampling.

    DECORRELATED CHANNELS. The two channels run independent generators rather
    than one generator sent to both. Identical noise on both channels is a
    mono signal sitting dead centre, which is not what a "stereo noise
    generator" means and collapses to a 3 dB louder mono signal when summed.
    Independent streams give a genuinely wide source.

    DETERMINISM. The generator is a plain xorshift, seeded explicitly rather
    than from a clock or a global. Two consequences that both matter here: a
    render is reproducible, so noise can be tested rather than merely
    eyeballed; and each voice can be given its own seed, so several voices
    sounding at once produce independent noise instead of N copies of the same
    sequence summing coherently to N times the level.

    Header-only because the whole generator is a shift, an xor and a multiply,
    and a per-sample call through a translation-unit boundary would cost more
    than the noise does.

    REAL-TIME CONTRACT: every function is callable from the audio thread. None
    allocate, lock or perform I/O.
*/

#include <cstdint>

namespace apollo::dsp
{

class NoiseGenerator
{
public:
    NoiseGenerator() = default;

    /** Seeds both channels from one value.

        The two channel states are derived from @p seed by different odd
        constants, so neighbouring seeds do not produce correlated channels —
        and a seed of zero, which would lock a xorshift at zero forever, is
        mapped away rather than trusted.
    */
    void setSeed (std::uint32_t seed) noexcept
    {
        stateLeft = (seed * 2654435761u) | 1u;
        stateRight = ((seed + 0x9E3779B9u) * 1597334677u) | 1u;
    }

    /** Adds the next stereo sample into @p left and @p right, scaling each
        channel by its own gain.

        Values are uniform in [-1, 1). Uniform rather than Gaussian: it is one
        multiply instead of a summation, its peak is bounded exactly rather
        than statistically, and the spectral result — flat — is identical,
        which is the property a noise source is chosen for.
    */
    void addNextStereoSample (float& left, float& right, float gainLeft, float gainRight) noexcept
    {
        left += nextValue (stateLeft) * gainLeft;
        right += nextValue (stateRight) * gainRight;
    }

private:
    /** Marsaglia's 32-bit xorshift. Full period over the non-zero states, which
        is 4.29 billion samples — over 24 hours at 48 kHz before it repeats.
    */
    [[nodiscard]] static float nextValue (std::uint32_t& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        // Maps the full 32-bit range onto [-1, 1). The reciprocal is exact in
        // binary, so the mapping introduces no bias of its own.
        constexpr float scale = 1.0f / 2147483648.0f;

        return static_cast<float> (static_cast<std::int32_t> (state)) * scale;
    }

    std::uint32_t stateLeft = 0x2545F491u;
    std::uint32_t stateRight = 0x9E3779B9u;
};

} // namespace apollo::dsp
