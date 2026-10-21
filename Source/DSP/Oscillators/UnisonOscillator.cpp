#include "DSP/Oscillators/UnisonOscillator.h"

#include <cmath>

namespace apollo::dsp
{

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

/** The DC blocker's corner. Low enough to leave the lowest note anyone plays
    untouched — 0.03 dB down at 20 Hz — and high enough to settle in a few
    hundred milliseconds when a modulated warp moves the offset. */
constexpr double dcCornerHz = 5.0;

/** polyBLEP residual for a jump of 2, at @p tau samples from it (negative
    before). Zero outside a sample either side. */
[[nodiscard]] double blepResidual (double tau) noexcept
{
    if (tau < 0.0)
        return (tau + 1.0) * (tau + 1.0);

    return -(1.0 - tau) * (1.0 - tau);
}

/** polyBLAMP residual for a change of slope of 1 per sample: the polyBLEP
    residual for a unit jump, integrated. (Esqueda, Välimäki and Bilbao, "Rounding
    Corners with BLAMP", DAFx 2016.) */
[[nodiscard]] double blampResidual (double tau) noexcept
{
    if (tau < 0.0)
        return (tau + 1.0) * (tau + 1.0) * (tau + 1.0) / 6.0;

    return (1.0 - tau) * (1.0 - tau) * (1.0 - tau) / 6.0;
}

} // namespace

void UnisonOscillator::setSampleRate (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    dcCoefficient = std::exp (-twoPi * dcCornerHz / sampleRate);

    // Every voice's increment and mip level depend on it, so the layout has to
    // be re-derived rather than merely marked stale.
    appliedGeneration = 0;
    applyLayout();
}

void UnisonOscillator::setTable (const Wavetable* newTable) noexcept
{
    if (table == newTable)
        return;

    table = newTable;

    // All sixteen, not just the sounding ones: a voice that becomes active
    // later must not be left pointing into a table that has been replaced
    // (§12.4). This runs when the table pointer changes, which is rare.
    for (int i = 0; i < maxVoices; ++i)
        refreshVoice (i);
}

void UnisonOscillator::setLayout (const UnisonLayout* newLayout) noexcept
{
    if (layout == newLayout)
        return;

    layout = newLayout;

    // A different layout object says nothing about its generation counter, so
    // the cached one is invalidated rather than compared.
    appliedGeneration = 0;
    applyLayout();
}

void UnisonOscillator::setFrequency (double frequencyHz) noexcept
{
    frequency = std::isfinite (frequencyHz) ? frequencyHz : 0.0;
    appliedGeneration = 0;
    applyLayout();
}

void UnisonOscillator::applyLayout() noexcept
{
    const auto& active = currentLayout();

    if (active.getGeneration() == appliedGeneration)
        return;

    appliedGeneration = active.getGeneration();

    const auto count = active.getCount();

    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        increment[voice] = frequency * active.getFrequencyRatio (i) / sampleRate;

        // Copied out of the layout here rather than read through it per sample.
        // On a sixteen-voice stack that was thirty-two calls a sample into an
        // object the render loop otherwise never touches (ADR-0074).
        gainLeft[voice] = active.getGainLeft (i);
        gainRight[voice] = active.getGainRight (i);

        // The count may have just grown, in which case these last few voices
        // have never had a read position derived at all. This is the only place
        // a voice can become active, so it is the only place that has to care.
        refreshVoice (i);
    }
}

void UnisonOscillator::setPosition (float newNormalisedPosition) noexcept
{
    // Non-finite is clamped to zero rather than passed on: neither comparison
    // below is true for a NaN, so it would otherwise travel into the frame
    // position and from there into an undefined conversion.
    const auto clamped = ! std::isfinite (newNormalisedPosition) ? 0.0f
                       : (newNormalisedPosition < 0.0f ? 0.0f
                       : (newNormalisedPosition > 1.0f ? 1.0f : newNormalisedPosition));

    if (clamped == normalisedPosition)
        return;

    normalisedPosition = clamped;

    // Only the sounding voices. Re-deriving a read position is not free and
    // this runs every modulation block, so walking all sixteen would make a
    // one-voice stack pay fifteen times over — on the default patch, which is
    // what Apollo loads with. The voices above the count are brought up to
    // date by applyLayout, where they become active.
    const auto count = currentLayout().getCount();

    for (int i = 0; i < count; ++i)
        refreshVoice (i);
}

void UnisonOscillator::refreshVoice (int index) noexcept
{
    const auto voice = static_cast<std::size_t> (index);

    if (table == nullptr)
    {
        readers[voice] = Wavetable::Reader {};
        return;
    }

    const auto frames = table->getNumFrames();

    const auto framePosition = frames > 1
                             ? static_cast<double> (normalisedPosition)
                               * static_cast<double> (frames - 1)
                             : 0.0;

    // Each voice's own detuned pitch, so a stack spread across a mip boundary
    // gets the right level on both sides of it — and, under a warp, multiplied
    // by two things:
    //
    //   - the warp's fastest read, because a warp that reads the cycle three
    //     times faster at its steepest point plays every harmonic of the table
    //     three times higher there;
    //   - a margin of 1 + amount, because a warp is a nonlinear map of the read
    //     position and spreads the table's spectrum past that fastest read the
    //     way FM does, more the harder it bends the cycle. Measured: a fixed
    //     margin of 2 bought 7 to 15 dB of aliasing, and also took everything
    //     above 12 kHz off a middle C the moment any warp at all was applied.
    //     Scaling it with the amount costs nothing at a gentle warp and the full
    //     octave at a hard one (ADR-0090).
    //
    // Both are 1 with no warp, so an unwarped voice chooses the level it always
    // did.
    const auto warped = warpMode != WarpMode::off && warpAmount > 0.0f;
    const auto warpReach = warped ? warpSlope * (1.0 + static_cast<double> (warpAmount)) : 1.0;

    const auto voiceFrequency = frequency * currentLayout().getFrequencyRatio (index) * warpReach;
    const auto level = Wavetable::selectMipLevel (voiceFrequency, sampleRate);

    readers[voice] = table->makeReader (level, framePosition);

    // The edges are sized from what this reader reads, so they follow it.
    if (warpMode != WarpMode::off)
        computeWarpEdges (index);
}

double UnisonOscillator::warpedValue (int index, double p) const noexcept
{
    return warpedRead (readers[static_cast<std::size_t> (index)], p);
}

double UnisonOscillator::warpedRead (const Wavetable::Reader& reader, double p) const noexcept
{
    Wavetable::Reader::Taps taps;
    reader.gather (warpPhase (p), taps);

    auto value = Wavetable::Reader::interpolate (taps);

    if (warpMode == WarpMode::mirror)
    {
        Wavetable::Reader::Taps mirrorTaps;
        reader.gather (p < 0.5 ? p : 1.0 - p, mirrorTaps);
        value += mirrorMix * (Wavetable::Reader::interpolate (mirrorTaps) - value);
    }

    return value;
}

void UnisonOscillator::drawWarpedCycle (const Wavetable& source, float position,
                                        float* destination, int points) const noexcept
{
    if (destination == nullptr || points <= 0)
        return;

    const auto frames = source.getNumFrames();
    const auto clamped = ! std::isfinite (position) ? 0.0f
                       : (position < 0.0f ? 0.0f : (position > 1.0f ? 1.0f : position));
    const auto framePosition = frames > 1
                             ? static_cast<double> (clamped) * static_cast<double> (frames - 1)
                             : 0.0;

    const auto reader = source.makeReader (0, framePosition);
    const auto warped = warpMode != WarpMode::off && warpAmount > 0.0f;

    double sum = 0.0;

    for (int i = 0; i < points; ++i)
    {
        const auto p = static_cast<double> (i) / static_cast<double> (points);
        const auto value = warped ? warpedRead (reader, p) : static_cast<double> (reader.read (p));

        destination[i] = static_cast<float> (value);
        sum += value;
    }

    // Only a warped cycle is centred: the unwarped path has no blocker, and its
    // picture is the table's own frame, offset and all.
    if (warped)
    {
        const auto mean = static_cast<float> (sum / static_cast<double> (points));

        for (int i = 0; i < points; ++i)
            destination[i] -= mean;
    }
}

void UnisonOscillator::computeWarpEdges (int index) noexcept
{
    const auto voice = static_cast<std::size_t> (index);
    const auto& reader = readers[voice];

    // The table's own value and slope at a read position, the slope per unit of
    // read position by a central difference small against any table's spacing.
    const auto value = [&reader] (double phi)
    {
        Wavetable::Reader::Taps taps;
        reader.gather (phi, taps);
        return Wavetable::Reader::interpolate (taps);
    };

    const auto slope = [&value] (double phi)
    {
        constexpr double h = 1.0e-5;
        return (value (phi + h) - value (phi - h)) / (2.0 * h);
    };

    auto& jump = edgeJump[voice];
    auto& bend = edgeSlope[voice];

    jump.fill (0.0);
    bend.fill (0.0);

    // Every edge is described in terms of the master phase p, at the positions
    // setWarp chose: the output's jump there, and the change in its slope per
    // unit of p, after minus before.
    switch (warpMode)
    {
        case WarpMode::sync:
        {
            // Edge 0, the restart at p = 0: the cycle read `syncRatio` times
            // ends wherever the fraction leaves it and starts again from 0.
            const auto end = syncRatio - std::floor (syncRatio);
            jump[0] = value (0.0) - value (end);
            bend[0] = syncRatio * (slope (0.0) - slope (end));
            break;
        }

        case WarpMode::asym:
        {
            // Edge 0 at the knee, read position 0.5: from 0.5/knee to
            // 0.5/(1 - knee). Edge 1 at the wrap, read position 0: back again.
            const auto before = 0.5 / asymKnee;
            const auto after = 0.5 / (1.0 - asymKnee);
            bend[0] = slope (0.5) * (after - before);
            bend[1] = slope (0.0) * (before - after);
            break;
        }

        case WarpMode::pwm:
        {
            // Edge 0 at the width, where the cycle ends and its start is held:
            // slope 1/width to 0. Edge 1 at the wrap: 0 back to 1/width. Read
            // position 1 is 0, so both are the table's slope at its start.
            const auto speed = 1.0 / pwmWidth;
            bend[0] = -slope (0.0) * speed;
            bend[1] = slope (0.0) * speed;
            break;
        }

        case WarpMode::mirror:
        {
            // The mirrored read turns at the middle (+1 to -1) and at the wrap
            // (-1 to +1), weighted by how much of it is heard.
            bend[0] = -2.0 * mirrorMix * slope (0.5);
            bend[1] = 2.0 * mirrorMix * slope (0.0);
            break;
        }

        case WarpMode::bend:
        case WarpMode::off:
        default:
            // Bend's cubic has no corner: its slope is 1 + 2k at both ends of
            // the cycle, so the wrap is smooth.
            break;
    }
}

void UnisonOscillator::resetPhase (double basePhase) noexcept
{
    resetPhase (basePhase, 0.0f, 0u);
}

void UnisonOscillator::resetPhase (double basePhase, float randomAmount, std::uint32_t seed) noexcept
{
    const auto& active = currentLayout();

    // NaN and negatives mean none; past 1 means a whole cycle, which is the
    // most a phase offset can mean.
    const auto amount = randomAmount > 0.0f ? (randomAmount < 1.0f ? randomAmount : 1.0f) : 0.0f;

    for (int i = 0; i < maxVoices; ++i)
    {
        auto wrapped = basePhase + active.getStartPhase (i);

        // Skipped entirely at zero rather than multiplied by it, so a stack with
        // no randomisation is computed exactly as it was before the control
        // existed.
        if (amount > 0.0f)
        {
            // A 32-bit integer hash of the seed and the voice index — a step of
            // the PCG output function — so each voice gets its own offset from
            // one per-note seed with nothing stored between notes.
            auto x = seed + static_cast<std::uint32_t> (i + 1) * 0x9E3779B9u;
            x = (x ^ (x >> 16)) * 0x7FEB352Du;
            x = (x ^ (x >> 15)) * 0x846CA68Bu;
            x ^= x >> 16;

            const auto unit = static_cast<double> (x) / 4294967296.0;

            wrapped += static_cast<double> (amount) * unit;
        }

        wrapped -= std::floor (wrapped);

        phase[static_cast<std::size_t> (i)] = wrapped;
    }

    // The DC blocker starts where a warped note's mean already is, so the note
    // does not begin with a step that takes the blocker a few hundred
    // milliseconds to drain. With no warp it is cleared, so a warp switched on
    // later starts from rest.
    dcOutLeft = 0.0;
    dcOutRight = 0.0;
    dcInLeft = 0.0;
    dcInRight = 0.0;
    dcPrimed = false;

    if (warpMode != WarpMode::off && warpAmount > 0.0f)
    {
        dcPrimed = true;

        const auto count = active.getCount();

        constexpr int points = 128;
        auto mean = 0.0;

        for (int n = 0; n < points; ++n)
            mean += warpedValue (0, (static_cast<double> (n) + 0.5) / static_cast<double> (points));

        mean /= static_cast<double> (points);

        auto sumLeft = 0.0;
        auto sumRight = 0.0;

        for (int i = 0; i < count; ++i)
        {
            sumLeft += static_cast<double> (active.getGainLeft (i));
            sumRight += static_cast<double> (active.getGainRight (i));
        }

        dcInLeft = mean * sumLeft;
        dcInRight = mean * sumRight;
    }
}

int UnisonOscillator::getActiveVoiceCount() const noexcept
{
    return currentLayout().getCount();
}

void UnisonOscillator::setWarp (WarpMode mode, float amount) noexcept
{
    const auto clamped = amount > 0.0f ? (amount < 1.0f ? amount : 1.0f) : 0.0f;
    const auto validMode = static_cast<int> (mode) >= 0 && static_cast<int> (mode) < numWarpModes
                               ? mode : WarpMode::off;

    if (validMode == warpMode && clamped == warpAmount)
        return;

    warpMode = validMode;
    warpAmount = clamped;

    // Rendering returns to the plain path at no warp, so the blocker's state
    // stops describing anything; it is primed afresh when warp returns.
    if (warpMode == WarpMode::off || warpAmount <= 0.0f)
        dcPrimed = false;

    const auto a = static_cast<double> (clamped);

    // Each mode's constant, and how fast it reads the table at its steepest
    // point. The ranges stop short of the degenerate: a knee or a width of zero
    // would read an entire cycle in no time at all.
    syncRatio = 1.0 + 7.0 * a;          // up to eight cycles per period
    asymKnee = 0.5 - 0.45 * a;          // the midpoint moves from 0.5 to 0.05
    pwmWidth = 1.0 - 0.9 * a;           // the cycle shrinks to a tenth
    bendCurve = a;                      // the cubic's weight
    mirrorMix = a;

    asymRiseScale = 0.5 / asymKnee;
    asymFallScale = 0.5 / (1.0 - asymKnee);
    pwmScale = 1.0 / pwmWidth;

    switch (warpMode)
    {
        case WarpMode::sync:   warpSlope = syncRatio; break;
        case WarpMode::asym:   warpSlope = 0.5 / asymKnee; break;
        case WarpMode::pwm:    warpSlope = 1.0 / pwmWidth; break;
        case WarpMode::bend:   warpSlope = 1.0 + 2.0 * bendCurve; break;
        case WarpMode::mirror:
        case WarpMode::off:
        default:               warpSlope = 1.0; break;
    }

    // Where each mode's edges are, in master phase (computeWarpEdges sizes them).
    switch (warpMode)
    {
        case WarpMode::sync:
            warpEdgeCount = 1;
            warpEdgePhase = { 0.0, 0.0 };
            break;

        case WarpMode::asym:
            warpEdgeCount = 2;
            warpEdgePhase = { asymKnee, 0.0 };
            break;

        case WarpMode::pwm:
            warpEdgeCount = 2;
            warpEdgePhase = { pwmWidth, 0.0 };
            break;

        case WarpMode::mirror:
            warpEdgeCount = 2;
            warpEdgePhase = { 0.5, 0.0 };
            break;

        case WarpMode::bend:
        case WarpMode::off:
        default:
            warpEdgeCount = 0;
            break;
    }

    // The read positions depend on the slope through the mip level. Only the
    // sounding voices: the rest are refreshed by applyLayout as they become
    // active, exactly as for a position change.
    const auto count = currentLayout().getCount();

    for (int i = 0; i < count; ++i)
        refreshVoice (i);
}

template <UnisonOscillator::WarpMode mode>
double UnisonOscillator::mapWarpPhase (double p) const noexcept
{
    if constexpr (mode == WarpMode::sync)
    {
        // The cycle read syncRatio times per period, restarting with it. The
        // product is never negative, so truncation is floor — without a call.
        const auto x = p * syncRatio;
        return x - static_cast<double> (static_cast<long long> (x));
    }
    else if constexpr (mode == WarpMode::asym)
    {
        // Two straight lines meeting at the knee: half the cycle before it,
        // half after. Continuous, so the waveform has a corner, not a jump.
        return p < asymKnee ? p * asymRiseScale
                            : 0.5 + (p - asymKnee) * asymFallScale;
    }
    else if constexpr (mode == WarpMode::pwm)
    {
        // The whole cycle inside the width, then its starting value held.
        // Reading position 0 continues from where the cycle ended — a table is
        // periodic — so this too is a corner rather than a jump.
        return p < pwmWidth ? p * pwmScale : 0.0;
    }
    else if constexpr (mode == WarpMode::bend)
    {
        // x(1 - k + k x²) on [-1, 1]: fixed at both ends, rising everywhere for
        // k up to 1, slope 1 - k at the centre and 1 + 2k at the ends. A cubic
        // rather than a power curve, because it is three multiplies per voice
        // per sample and pow is not.
        const auto x = 2.0 * p - 1.0;
        const auto y = x * ((1.0 - bendCurve) + bendCurve * x * x);
        const auto warped = 0.5 * (y + 1.0);
        return warped < 1.0 ? warped : 0.0;
    }
    else
    {
        // Mirror is a crossfade of outputs, not a mapping; off is no mapping.
        return p;
    }
}

double UnisonOscillator::warpPhase (double p) const noexcept
{
    switch (warpMode)
    {
        case WarpMode::sync:   return mapWarpPhase<WarpMode::sync> (p);
        case WarpMode::asym:   return mapWarpPhase<WarpMode::asym> (p);
        case WarpMode::pwm:    return mapWarpPhase<WarpMode::pwm> (p);
        case WarpMode::bend:   return mapWarpPhase<WarpMode::bend> (p);
        case WarpMode::mirror:
        case WarpMode::off:
        default:               return p;
    }
}

void UnisonOscillator::addNextStereoSample (float& left, float& right) noexcept
{
    // Warped rendering is its own path, chosen once per call. The path below is
    // left exactly as it was, so a patch without warp costs and sounds exactly
    // what it did before warp existed — and so does a warp at an amount of
    // zero, which is no warp and must not pass through the DC blocker either.
    if (warpMode != WarpMode::off && warpAmount > 0.0f)
    {
        addNextWarpedStereoSample (left, right);
        return;
    }

    // Picked up here rather than pushed by the engine: the layout mutates in
    // place and this is the one call guaranteed to happen before it is read.
    applyLayout();

    const auto count = currentLayout().getCount();

    if (count <= 0)
        return;

    // ONE VOICE IS NOT A BATCH, and the default patch is one voice.
    //
    // The two passes below exist so that a stack of sixteen can gather all of
    // its taps and then evaluate all of its polynomials over contiguous data.
    // At a count of one there is nothing to amortise: the split costs a second
    // loop and a trip through an array, and buys nothing at all. Apollo loads
    // with oscillator 1 alone and no unison, so this is the common case rather
    // than a corner of one (ADR-0074).
    if (count == 1)
    {
        Wavetable::Reader::Taps single;

        readers[0].gather (phase[0], single);

        phase[0] += increment[0];

        if (phase[0] >= 1.0 || phase[0] < 0.0)
            phase[0] -= std::floor (phase[0]);

        const auto sample = Wavetable::Reader::interpolate (single);

        left += static_cast<float> (sample * static_cast<double> (gainLeft[0]));
        right += static_cast<float> (sample * static_cast<double> (gainRight[0]));

        return;
    }

    // Scratch on the stack rather than in the object. Sixteen voices of taps is
    // 640 bytes, which stays in L1 for the two passes below and then stops
    // existing — where a member would have cost that much per oscillator per
    // voice, forty kilobytes across a full instrument, written and read every
    // sample.
    Wavetable::Reader::Taps taps[maxVoices];

    // PASS ONE: the gather. Loads at addresses derived from each voice's own
    // phase, which is the part no vector unit helps with (ADR-0073).
    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        readers[voice].gather (phase[voice], taps[voice]);

        phase[voice] += increment[voice];

        // A single subtraction is not enough. It assumes the increment is below
        // 1.0, which holds for any musical frequency but not for an absurd one
        // — and mip selection clamps the *level*, not the increment. floor()
        // wraps correctly for any magnitude and is only reached once per cycle.
        if (phase[voice] >= 1.0 || phase[voice] < 0.0)
            phase[voice] -= std::floor (phase[voice]);
    }

    // PASS TWO: the arithmetic, walking contiguous arrays with no
    // data-dependent addressing. One shared polynomial, evaluated a voice at a
    // time here and once per call in Reader::read — the same arithmetic on a
    // different schedule, and a test asserts the two agree (ADR-0074).
    auto sumLeft = 0.0;
    auto sumRight = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        // No branch: a reader with nothing behind it gathered zeroes, and zeroes
        // interpolate to silence (ADR-0074).
        const auto sample = Wavetable::Reader::interpolate (taps[voice]);

        sumLeft += sample * static_cast<double> (gainLeft[voice]);
        sumRight += sample * static_cast<double> (gainRight[voice]);
    }

    left += static_cast<float> (sumLeft);
    right += static_cast<float> (sumRight);
}

template <UnisonOscillator::WarpMode mode>
void UnisonOscillator::renderWarpedStack (int count, double& sumLeft, double& sumRight) noexcept
{
    // TWO PASSES, as the unwarped path has (ADR-0074): the gathers — loads at
    // addresses derived from each voice's phase — and then the arithmetic over
    // contiguous arrays. The first version was one pass and asked the mode per
    // voice per sample; this is the same arithmetic in the order the unwarped
    // path proved cheaper, specialised so the mode is known at compile time.
    Wavetable::Reader::Taps taps[maxVoices];
    [[maybe_unused]] Wavetable::Reader::Taps mirrorTaps[mode == WarpMode::mirror ? maxVoices : 1];
    [[maybe_unused]] double startPhase[maxVoices];

    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);
        const auto p = phase[voice];

        startPhase[i] = p;
        readers[voice].gather (mapWarpPhase<mode> (p), taps[i]);

        if constexpr (mode == WarpMode::mirror)
        {
            // The first half of the cycle forwards, then backwards: continuous
            // at the middle and at the wrap. Crossfaded as *outputs* with the
            // unmirrored read rather than by blending the two read positions,
            // which would jump at the end of the cycle for any amount between
            // the ends.
            readers[voice].gather (p < 0.5 ? p : 1.0 - p, mirrorTaps[i]);
        }

        phase[voice] += increment[voice];

        if (phase[voice] >= 1.0 || phase[voice] < 0.0)
            phase[voice] -= std::floor (phase[voice]);
    }

    for (int i = 0; i < count; ++i)
    {
        const auto voice = static_cast<std::size_t> (i);

        auto sample = Wavetable::Reader::interpolate (taps[i]);

        if constexpr (mode == WarpMode::mirror)
            sample += mirrorMix * (Wavetable::Reader::interpolate (mirrorTaps[i]) - sample);

        // The edges: within a sample either side of one, add the residual that
        // turns the naive jump or corner into a band-limited one. The distance
        // is wrapped to the nearer side — it can only be off by one, so a
        // comparison does what floor() did — and the division is reached only
        // within a sample of an edge, which is two samples a cycle.
        if constexpr (mode != WarpMode::bend)
        {
            const auto step = increment[voice];

            for (int e = 0; e < warpEdgeCount; ++e)
            {
                const auto edge = static_cast<std::size_t> (e);

                auto distance = startPhase[i] - warpEdgePhase[edge];

                if (distance >= 0.5)
                    distance -= 1.0;
                else if (distance < -0.5)
                    distance += 1.0;

                if (distance > -step && distance < step && step > 0.0)
                {
                    const auto tau = distance / step;

                    sample += 0.5 * edgeJump[voice][edge] * blepResidual (tau)
                            + edgeSlope[voice][edge] * step * blampResidual (tau);
                }
            }
        }

        sumLeft += sample * static_cast<double> (gainLeft[voice]);
        sumRight += sample * static_cast<double> (gainRight[voice]);
    }
}

void UnisonOscillator::addNextWarpedStereoSample (float& left, float& right) noexcept
{
    applyLayout();

    const auto count = currentLayout().getCount();

    auto sumLeft = 0.0;
    auto sumRight = 0.0;

    // The mode chosen once per sample for the whole stack, not once per voice.
    switch (warpMode)
    {
        case WarpMode::sync:   renderWarpedStack<WarpMode::sync> (count, sumLeft, sumRight); break;
        case WarpMode::asym:   renderWarpedStack<WarpMode::asym> (count, sumLeft, sumRight); break;
        case WarpMode::pwm:    renderWarpedStack<WarpMode::pwm> (count, sumLeft, sumRight); break;
        case WarpMode::bend:   renderWarpedStack<WarpMode::bend> (count, sumLeft, sumRight); break;
        case WarpMode::mirror: renderWarpedStack<WarpMode::mirror> (count, sumLeft, sumRight); break;
        case WarpMode::off:
        default:               break;
    }

    // Entering the warped path mid-note — a modulated amount rising from zero —
    // the blocker is primed as though it had been passing this signal all
    // along, so its first output is the input rather than a step to zero.
    if (! dcPrimed)
    {
        dcInLeft = sumLeft;
        dcInRight = sumRight;
        dcOutLeft = sumLeft;
        dcOutRight = sumRight;
        dcPrimed = true;
    }

    // The DC blocker: y[n] = x[n] - x[n-1] + R y[n-1].
    const auto blockedLeft = sumLeft - dcInLeft + dcCoefficient * dcOutLeft;
    const auto blockedRight = sumRight - dcInRight + dcCoefficient * dcOutRight;

    dcInLeft = sumLeft;
    dcInRight = sumRight;
    dcOutLeft = blockedLeft;
    dcOutRight = blockedRight;

    left += static_cast<float> (blockedLeft);
    right += static_cast<float> (blockedRight);
}

} // namespace apollo::dsp
