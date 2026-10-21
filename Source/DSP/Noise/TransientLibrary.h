#pragma once

/*
    The noise source's built-in transients: short stereo samples played once
    from the start of each note (Phase 13d-2, PRD §10).

    SYNTHESISED, NOT RECORDED. Each is built from a formula and a fixed seed —
    filtered noise, a falling sine — so Apollo ships no recordings it would
    have to license, and every build of every platform holds the same samples
    to within floating-point rounding. They are built once per process and
    shared, immutable, for the reason the built-in wavetables are
    (CLAUDE.md §12.3): a host may make many instances, and each one paying for
    the build would put it into a constructor somebody is waiting on.

    STORED AT SIX BANDWIDTHS. Key tracking plays a transient faster above
    middle C, and a sample played faster than it was recorded moves its upper
    harmonics past Nyquist, where they fold back as aliasing. So each sample is
    kept as a mipmap: level 0 at 48 kHz holding everything below 20 kHz, and
    each level after it low-passed to half the bandwidth and stored at half the
    rate. A player picks the first level whose top, played at its speed, still
    lands below Nyquist — the same idea the wavetables use for the same reason
    (ADR-0066), done on a sample rather than a cycle.

    REAL-TIME CONTRACT: building allocates and is done once, off the audio
    thread, the first time an engine is made. Reading never allocates.
*/

#include <array>
#include <vector>

namespace apollo::dsp
{

/** One transient: a short stereo sample at six bandwidths. */
struct TransientSample
{
    static constexpr int numLevels = 6;

    /** Level 0's rate. Level k is stored at this over 2^k. */
    static constexpr double referenceRate = 48000.0;

    /** Level 0's top. Level k holds everything below this over 2^k. */
    static constexpr double referenceBandwidth = 20000.0;

    struct Level
    {
        std::vector<float> left;
        std::vector<float> right;
    };

    std::array<Level, numLevels> levels;

    /** Frames at level 0. */
    [[nodiscard]] int length() const noexcept { return static_cast<int> (levels[0].left.size()); }
};

/** Builds a transient's six levels from one channel each at 48 kHz, and
    normalises it so the loudest sample of level 0, on either side, is 1.

    The library builds its four with this. It is public so that a test can
    give it a signal whose spectrum is known and check what each level keeps.
*/
[[nodiscard]] TransientSample makeTransient (const std::vector<double>& left, const std::vector<double>& right);

class TransientLibrary
{
public:
    /** Click, pick, thump and chiff, in the order the noise type names them. */
    static constexpr int numTransients = 4;

    /** The process's one library, built the first time it is asked for. */
    [[nodiscard]] static const TransientLibrary& shared();

    /** @returns transient @p index, clamped into range. */
    [[nodiscard]] const TransientSample& get (int index) const noexcept;

private:
    TransientLibrary();

    std::array<TransientSample, static_cast<std::size_t> (numTransients)> samples;
};

} // namespace apollo::dsp
