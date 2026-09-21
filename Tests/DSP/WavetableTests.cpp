/*
    Wavetable oscillator tests, including the spectral validation Phase 4
    requires.

    The central claim of the wavetable engine is that it does not alias. That is
    not something code review can establish — aliasing is a property of the
    output signal, so it has to be measured. These tests render steady tones,
    take an FFT, classify each bin as harmonic or not, and report the worst
    non-harmonic component relative to the fundamental.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "DSP/Oscillators/Wavetable.h"
#include "DSP/Oscillators/WavetableBuilder.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Oscillators/WavetableOscillator.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;

/** FFT size for the spectral tests. 16384 bins at 48 kHz gives ~2.9 Hz
    resolution, fine enough to separate a fundamental from an alias sitting
    close to it.
*/
constexpr int fftOrder = 14;
constexpr int fftSize = 1 << fftOrder;

/** ACCEPTABLE ALIASING THRESHOLD.

    Non-harmonic content must stay at least this far below the fundamental.

    -60 dBc is roughly ten bits of clean dynamic range below the note being
    played, and comfortably below the noise floor of any real playback chain. It
    is a deliberate engineering budget rather than a measured artefact of the
    current implementation: the mipmap removes aliasing from the source, so what
    remains is interpolation error, and the threshold leaves room for that to
    grow slightly without silently degrading.
*/
constexpr float maxAliasDecibels = -60.0f;

/** Renders a steady tone and returns its magnitude spectrum in decibels
    relative to the loudest bin.

    The first samples are discarded so nothing but steady state is analysed, and
    a window is applied so a fundamental that does not land exactly on a bin does
    not smear across the spectrum and masquerade as aliasing. See the note on the
    window choice below — it is not incidental.
*/
[[nodiscard]] std::vector<float> renderSpectrum (const Wavetable& table,
                                                 double frequencyHz,
                                                 float position)
{
    WavetableOscillator oscillator;
    oscillator.setSampleRate (testSampleRate);
    oscillator.setTable (&table);
    oscillator.setPosition (position);
    oscillator.setFrequency (frequencyHz);
    oscillator.resetPhase (0.0);

    // Discard a little output so any start transient is excluded.
    for (int i = 0; i < 1024; ++i)
        (void) oscillator.getNextSample();

    std::vector<float> samples (static_cast<std::size_t> (fftSize) * 2, 0.0f);

    for (int i = 0; i < fftSize; ++i)
        samples[static_cast<std::size_t> (i)] = oscillator.getNextSample();

    // Blackman-Harris, not Hann. A tone whose frequency does not land exactly on
    // an FFT bin leaks into its neighbours, and Hann's first sidelobe is only
    // about -31 dB — far above the aliasing being measured, so leakage would be
    // reported as aliasing. (It was: a pure sine measured -47 dBc at 110 Hz,
    // which lands on bin 37.55, while the same sine measured -112 dBc at 3000 Hz,
    // which lands exactly on bin 1024.) Blackman-Harris sidelobes are near
    // -92 dB, well below the threshold under test.
    juce::dsp::WindowingFunction<float> window (static_cast<std::size_t> (fftSize),
                                                juce::dsp::WindowingFunction<float>::blackmanHarris);
    window.multiplyWithWindowingTable (samples.data(), static_cast<std::size_t> (fftSize));

    juce::dsp::FFT fft (fftOrder);
    fft.performFrequencyOnlyForwardTransform (samples.data());

    // Normalise to the loudest bin, which for these signals is the fundamental.
    float loudest = 0.0f;

    for (int bin = 1; bin < fftSize / 2; ++bin)
        loudest = std::max (loudest, samples[static_cast<std::size_t> (bin)]);

    std::vector<float> decibels (static_cast<std::size_t> (fftSize / 2), -200.0f);

    if (loudest <= 0.0f)
        return decibels;

    for (int bin = 0; bin < fftSize / 2; ++bin)
        decibels[static_cast<std::size_t> (bin)] =
            juce::Decibels::gainToDecibels (samples[static_cast<std::size_t> (bin)] / loudest, -200.0f);

    return decibels;
}

/** @returns the loudest non-harmonic bin, in dB relative to the fundamental.

    Bins within `binTolerance` of a true harmonic are excluded, as is the DC
    region, so what remains is aliasing and numerical noise.
*/
[[nodiscard]] float measureWorstAlias (const std::vector<float>& spectrumDb, double fundamentalHz)
{
    const auto binsPerHz = static_cast<double> (fftSize) / testSampleRate;

    // Wide enough to contain the window's main lobe. Blackman-Harris spreads a
    // tone across roughly eight bins, so anything narrower would read the
    // window's own shape as aliasing.
    constexpr int binTolerance = 10;

    float worst = -200.0f;

    for (int bin = 8; bin < static_cast<int> (spectrumDb.size()); ++bin)
    {
        const auto frequency = static_cast<double> (bin) / binsPerHz;

        // Distance, in bins, to the closest true harmonic of the fundamental.
        const auto harmonic = std::round (frequency / fundamentalHz);

        if (harmonic >= 1.0)
        {
            const auto harmonicBin = harmonic * fundamentalHz * binsPerHz;

            if (std::abs (static_cast<double> (bin) - harmonicBin) <= binTolerance)
                continue;
        }

        worst = std::max (worst, spectrumDb[static_cast<std::size_t> (bin)]);
    }

    return worst;
}

class WavetableTests final : public juce::UnitTest
{
public:
    WavetableTests()
        : juce::UnitTest ("Wavetable oscillator", "DSP")
    {
    }

    void runTest() override
    {
        testMipLevelSelection();
        testTablesAreBuiltAndBounded();
        testPitchAccuracy();
        testAntiAliasingAcrossTheRange();
        testAliasingWhileScanning();
        testFrameScanningIsSmooth();
        testTableSelectionChangesTimbre();
        testInterpolationIsAccurate();
        testTheFastReadPathIsAnIdentity();
        testTheTwoSchedulesAgree();
        testTheResolvedReaderMatchesTheDirectRead();
        testTheScanPositionSurvivesATableChange();
        testDegenerateInputIsSafe();
        testTheLibraryIsBuiltOncePerProcess();
    }

private:
    /** The Phase 10d read path must be the same function, faster — not a
        cheaper approximation of it.

        Three things changed in Wavetable's per-sample read (ADR-0070): the
        interpolator's tap wrap became a mask instead of an integer modulo, the
        phase tap is resolved once instead of once per frame, and a two-frame
        blend crossfades the four sample points and runs one cubic rather than
        running two cubics and crossfading the results.

        The first is exact by construction, the second is a common subexpression,
        and the third is exact because cubic Hermite is *linear* in its four
        samples. All three are therefore identities on paper, and this test is
        what says they are identities in the built binary: it reimplements the
        old form — modulo wrap, two cubics, a float crossfade — and compares the
        two across a grid of levels, frame positions and phases, including the
        phases either side of the wrap point where the mask and the modulo would
        disagree if the size were not a power of two.

        Without this, the only evidence would be the regression renders, which
        would catch a mistake at the scale of a whole patch but would not say
        that the arithmetic is the same arithmetic.
    */
    void testTheFastReadPathIsAnIdentity()
    {
        beginTest ("The fast read path computes what the slow one did");

        // The invariant everything here rests on. Asserted at compile time in
        // Wavetable.h too; stated again as a test so that a failure names the
        // reason rather than only the line.
        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            const auto size = Wavetable::samplesAtLevel (level);

            expect (size > 0 && (size & (size - 1)) == 0,
                    "level " + juce::String (level) + " holds " + juce::String (size)
                        + " samples, which is not a power of two — the mask wrap is invalid");

            expectEquals (Wavetable::indexMaskAtLevel (level), size - 1);
        }

        // The mask must wrap exactly as the modulo it replaced, over every index
        // the interpolator can ask for: index - 1 at the bottom of a frame and
        // index + 2 at the top are the only cases that wrap at all, and they are
        // the ones a mistake would live in.
        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            const auto size = Wavetable::samplesAtLevel (level);
            const auto mask = Wavetable::indexMaskAtLevel (level);

            const auto modulo = [size] (int i) noexcept
            {
                i %= size;
                return i < 0 ? i + size : i;
            };

            for (int index = 0; index < size; ++index)
            {
                expectEquals ((index + mask) & mask, modulo (index - 1));
                expectEquals ((index + 1) & mask, modulo (index + 1));
                expectEquals ((index + 2) & mask, modulo (index + 2));
            }
        }

        // Now the arithmetic, against a table with real harmonic content in
        // every frame — a flat or near-silent table would agree trivially.
        const auto& table = library().getTable (0);

        double worst = 0.0;

        const auto maxFrame = static_cast<double> (table.getNumFrames() - 1);

        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            const auto size = Wavetable::samplesAtLevel (level);
            const auto sampleWidth = 1.0 / static_cast<double> (size);

            // Frame positions: both ends, an exact integer in the middle (where
            // the blend is degenerate and the two forms must agree *exactly*),
            // and two positions between frames.
            for (const double framePosition : { 0.0, 0.5, 1.0, 3.0, 7.25, maxFrame - 0.5, maxFrame })
            {
                // Phases either side of both wrap points, plus a spread across
                // the cycle that does not land on sample boundaries.
                for (const double phase : { 0.0,
                                            sampleWidth * 0.5,
                                            sampleWidth * 0.9999,
                                            0.25,
                                            1.0 / 3.0,
                                            0.5,
                                            0.7071,
                                            1.0 - sampleWidth * 1.5,
                                            1.0 - sampleWidth * 0.0001 })
                {
                    const auto fast = table.getSampleAtPosition (level, framePosition, phase);
                    const auto slow = slowReadAtPosition (table, level, framePosition, phase);

                    worst = std::max (worst, std::abs (static_cast<double> (fast - slow)));
                }
            }
        }

        logMessage ("  largest disagreement with the pre-10d form: "
                    + juce::String (worst, 12));

        // The two differ only by the order of floating-point operations: one
        // cubic instead of two, and a crossfade in double rather than in float.
        // A signal bounded by about 1.0 leaves this many bits of room, which is
        // far tighter than anything audible and tight enough to catch a genuine
        // change in the function.
        expect (worst < 1.0e-6,
                "the fast read path disagrees with the slow one by " + juce::String (worst, 12)
                    + ", which is too much to be operation ordering");

        // At an integer frame position the blend is degenerate, and the fast
        // path routes to the same single-frame read getSample uses. That is
        // meant to be bit-identical, not merely close — it is what keeps a
        // one-frame table and a scanned table agreeing at their shared points.
        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            for (const int frameIndex : { 0, 1, 7, table.getNumFrames() - 1 })
            {
                for (const double phase : { 0.0, 0.125, 1.0 / 3.0, 0.75, 0.99 })
                {
                    const auto viaPosition = table.getSampleAtPosition (
                        level, static_cast<double> (frameIndex), phase);
                    const auto viaFrame = table.getSample (level, frameIndex, phase);

                    expect (viaPosition == viaFrame,
                            "level " + juce::String (level) + ", frame " + juce::String (frameIndex)
                                + ": a whole-numbered frame position read "
                                + juce::String (viaPosition, 12) + " where the frame itself read "
                                + juce::String (viaFrame, 12));
                }
            }
        }
    }

    /** Gathering sixteen voices and then interpolating sixteen times must give
        exactly what interpolating each voice as it is gathered gives.

        Phase 10d-5 split `Reader::read` into a `gather` and an `interpolate` so
        that `UnisonOscillator` could do all of one and then all of the other —
        a scalar gather pass, then arithmetic over contiguous arrays that a
        compiler can vectorise (ADR-0074). The two are now the same arithmetic
        on **different schedules**.

        ADR-0071 made `read` the single implementation of the interpolation for
        a reason: it is what stops the one-frame and two-frame paths drifting
        apart. Splitting it put that at risk, and the mitigation is that neither
        half was duplicated — `read` *is* gather-then-interpolate, and the stack
        calls the same two functions in a different order. This test is what
        says so, and it demands **bit-identical** results, because there is no
        arithmetic difference between the schedules for floating point to round
        differently.

        Without it, the guarantee would rest on reading the code and noticing
        that neither function appears twice.
    */
    void testTheTwoSchedulesAgree()
    {
        beginTest ("Gathering a batch then interpolating equals doing each in turn");

        const auto& table = library().getTable (0);

        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            for (const double framePosition : { 0.0, 3.0, 7.35,
                                                static_cast<double> (table.getNumFrames() - 1) })
            {
                const auto reader = table.makeReader (level, framePosition);

                // Sixteen phases spread across the cycle, the way a detuned
                // unison stack spreads them.
                std::vector<double> phases;

                for (int voice = 0; voice < 16; ++voice)
                    phases.push_back (static_cast<double> (voice) * 0.0613 + 0.00417);

                // Schedule one: interpolate each as it is gathered.
                std::vector<float> oneAtATime;

                for (const auto phase : phases)
                    oneAtATime.push_back (reader.read (phase));

                // Schedule two: gather the batch, then interpolate the batch.
                std::vector<Wavetable::Reader::Taps> taps (phases.size());

                for (std::size_t i = 0; i < phases.size(); ++i)
                    reader.gather (phases[i], taps[i]);

                for (std::size_t i = 0; i < phases.size(); ++i)
                {
                    const auto batched =
                        static_cast<float> (Wavetable::Reader::interpolate (taps[i]));

                    expect (batched == oneAtATime[i],
                            "level " + juce::String (level) + ", frame position "
                                + juce::String (framePosition, 2) + ", voice "
                                + juce::String (static_cast<int> (i)) + ": batched read "
                                + juce::String (batched, 12) + " where one at a time read "
                                + juce::String (oneAtATime[i], 12));
                }
            }
        }

        // And the degenerate case, which is the one that used to need a branch:
        // a reader with nothing behind it must gather zeroes, and zeroes must
        // interpolate to silence. That is what lets both schedules drop the
        // check rather than move it.
        const Wavetable::Reader empty;

        for (const double phase : { 0.0, 0.5, -3.25, 1.0e12,
                                    std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity() })
        {
            Wavetable::Reader::Taps taps;
            taps.y0 = taps.y1 = taps.y2 = taps.y3 = 999.0;
            taps.fraction = 0.5;

            empty.gather (phase, taps);

            expectEquals (taps.y0, 0.0);
            expectEquals (taps.y1, 0.0);
            expectEquals (taps.y2, 0.0);
            expectEquals (taps.y3, 0.0);
            expectEquals (Wavetable::Reader::interpolate (taps), 0.0);
            expectEquals (empty.read (phase), 0.0f);
        }

        // A valid reader handed a hostile phase must do the same, so that the
        // stack never has to distinguish the two failures.
        const auto valid = table.makeReader (0, 0.0);

        for (const double phase : { std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity(),
                                    -std::numeric_limits<double>::infinity() })
        {
            Wavetable::Reader::Taps taps;
            taps.y1 = 999.0;

            valid.gather (phase, taps);

            expectEquals (taps.y1, 0.0, "a hostile phase must gather silence, not stale taps");
            expectEquals (valid.read (phase), 0.0f);
        }
    }

    /** A Reader held across a run of samples must read what the direct call
        reads for each of them.

        Phase 10d-2 moved everything about a read that cannot change within a
        modulation block — the frame pointers, the frame size, the blend weight
        — out of the per-sample path and into a Reader resolved once
        (ADR-0071). `getSampleAtPosition` now builds a throwaway Reader per
        call, which is how the two are guaranteed to agree; this test is what
        says the *held* Reader, the one the oscillator actually uses, does not
        drift from it over a run of samples.

        Bit-identical is the bar, not merely close. There is no arithmetic here
        that differs between the two paths — only when it is performed.
    */
    void testTheResolvedReaderMatchesTheDirectRead()
    {
        beginTest ("A Reader held across a block reads what the direct call reads");

        const auto& table = library().getTable (0);

        for (int level = 0; level < Wavetable::numMipLevels; ++level)
        {
            for (const double framePosition : { 0.0, 2.5, 7.25,
                                                static_cast<double> (table.getNumFrames() - 1) })
            {
                const auto reader = table.makeReader (level, framePosition);

                expect (reader.isValid(), "a reader over a real table should be valid");

                // A run of phases of the kind an oscillator actually produces:
                // advancing by an irrational increment so that no sample lands
                // on a stored one twice.
                double phase = 0.0;

                for (int i = 0; i < 512; ++i)
                {
                    const auto held = reader.read (phase);
                    const auto direct = table.getSampleAtPosition (level, framePosition, phase);

                    expect (held == direct,
                            "level " + juce::String (level) + ", frame position "
                                + juce::String (framePosition, 2) + ", phase "
                                + juce::String (phase, 9) + ": held reader read "
                                + juce::String (held, 12) + " where the direct call read "
                                + juce::String (direct, 12));

                    phase += 0.00723418;

                    if (phase >= 1.0)
                        phase -= 1.0;
                }
            }
        }

        // A default-constructed Reader is usable and silent, which is what lets
        // the oscillator drop its null check.
        const Wavetable::Reader empty;

        expect (! empty.isValid());

        for (const double phase : { 0.0, 0.5, -1.0, 1.0e12,
                                    std::numeric_limits<double>::quiet_NaN() })
            expectEquals (empty.read (phase), 0.0f, "an empty reader must be silent");

        // A non-finite frame position is treated as the first frame rather than
        // refused. Unlike a broken phase, a broken scan position has an obvious
        // answer to fall back on, and a mis-set control should leave the
        // instrument sounding (§33).
        for (const double bad : { std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity() })
        {
            const auto reader = table.makeReader (0, bad);

            expect (reader.isValid(), "a non-finite frame position should not silence the table");

            for (const double phase : { 0.0, 0.25, 0.5, 0.75 })
                expectEquals (reader.read (phase), table.getSample (0, 0, phase),
                              "a non-finite frame position should read the first frame");
        }
    }

    /** Changing the table must keep the scan position where the user put it.

        This was wrong until Phase 10d-2 and is worth a test rather than only a
        commit message. `WavetableOscillator::setTable` re-applied the position
        by passing the *frame* position to a function that expects a normalised
        one, so any position past the first frame clamped to 1.0 and jumped the
        oscillator to the top of the new table.

        Nothing sounded wrong, because Voice sets the position again on the very
        next line and again every modulation block. That is exactly why it
        wanted pinning: the defect was invisible through the only caller Apollo
        had, and setTable is a public interface.

        Normalised rather than absolute is also the right thing to preserve on
        its own terms — tables may have different frame counts, and "half way
        along" means the same thing in all of them while "frame 9.6" does not.
    */
    void testTheScanPositionSurvivesATableChange()
    {
        beginTest ("Changing the table keeps the scan position, not the frame index");

        const auto& first = library().getTable (0);
        const auto& second = library().getTable (1);

        WavetableOscillator oscillator;
        oscillator.setSampleRate (testSampleRate);
        oscillator.setTable (&first);
        oscillator.setFrequency (220.0);

        for (const float position : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            oscillator.setPosition (position);
            oscillator.resetPhase (0.0);

            // What the oscillator produces on the first table at this position.
            std::vector<float> before (64, 0.0f);

            for (auto& sample : before)
                sample = oscillator.getNextSample();

            // Move to the other table and back. The position must be exactly
            // where it was, so the samples must be exactly what they were.
            oscillator.setTable (&second);
            oscillator.setTable (&first);
            oscillator.resetPhase (0.0);

            for (std::size_t i = 0; i < before.size(); ++i)
                expectEquals (oscillator.getNextSample(), before[i],
                              "position " + juce::String (position, 2) + ", sample "
                                  + juce::String (static_cast<int> (i))
                                  + ": a round trip through another table moved the scan position");
        }

        // And directly: at the top of the table the oscillator must not read
        // the same thing it reads in the middle, or the test above would pass
        // on a table whose frames are all alike.
        oscillator.setTable (&first);
        oscillator.setPosition (0.0f);
        oscillator.resetPhase (0.25);
        const auto atBottom = oscillator.getNextSample();

        oscillator.setPosition (1.0f);
        oscillator.resetPhase (0.25);
        const auto atTop = oscillator.getNextSample();

        expect (std::abs (atBottom - atTop) > 1.0e-4f,
                "the two ends of the table should not read alike, or this test proves nothing");
    }

    /** The read path as it stood before Phase 10d, kept here as the reference
        the fast form is checked against.

        Deliberately a transcription rather than a tidied version: a modulo
        wrap, one cubic evaluated per frame, and a crossfade of the two results
        in float. If this ever needs changing to keep the comparison passing,
        that is the signal that the read path's *behaviour* has moved and not
        only its cost.
    */
    [[nodiscard]] static float slowReadAtPosition (const Wavetable& table, int level,
                                                   double framePosition, double phase)
    {
        const auto readOneFrame = [&table, level, phase] (int frameIndex) -> float
        {
            const auto* frame = table.getReadPointer (level, frameIndex);

            if (frame == nullptr)
                return 0.0f;

            const auto size = Wavetable::samplesAtLevel (level);

            if (! std::isfinite (phase))
                return 0.0f;

            const auto wrappedPhase = phase - std::floor (phase);
            const auto position = wrappedPhase * static_cast<double> (size);

            auto index = static_cast<int> (position);
            const auto fraction = position - static_cast<double> (index);

            if (index >= size)
                index = size - 1;

            if (index < 0)
                index = 0;

            const auto wrap = [size] (int i) noexcept
            {
                i %= size;
                return i < 0 ? i + size : i;
            };

            const auto y0 = static_cast<double> (frame[wrap (index - 1)]);
            const auto y1 = static_cast<double> (frame[index]);
            const auto y2 = static_cast<double> (frame[wrap (index + 1)]);
            const auto y3 = static_cast<double> (frame[wrap (index + 2)]);

            const auto c0 = y1;
            const auto c1 = 0.5 * (y2 - y0);
            const auto c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
            const auto c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);

            return static_cast<float> (((c3 * fraction + c2) * fraction + c1) * fraction + c0);
        };

        const auto numFrames = table.getNumFrames();

        if (numFrames <= 0)
            return 0.0f;

        if (numFrames == 1)
            return readOneFrame (0);

        const auto maxFrame = static_cast<double> (numFrames - 1);
        const auto clamped = framePosition < 0.0 ? 0.0
                                                 : (framePosition > maxFrame ? maxFrame : framePosition);

        const auto lowerFrame = static_cast<int> (clamped);
        const auto upperFrame = lowerFrame + 1 < numFrames ? lowerFrame + 1 : lowerFrame;
        const auto blend = static_cast<float> (clamped - static_cast<double> (lowerFrame));

        const auto lower = readOneFrame (lowerFrame);
        const auto upper = readOneFrame (upperFrame);

        return lower + (upper - lower) * blend;
    }

    /** The tables are built once per process, not once per instrument.

        Rendering the four spectral tables is a couple of hundred milliseconds
        of real work — measured, after the first version took half a second and
        the standalone visibly waited for its own interface. A host that
        instantiates a plugin forty times while scanning its menu must not pay
        that forty times, so the built-ins are shared: immutable from the moment
        they exist, and therefore safe for every instance to read.

        This measures the *second* instrument, which is the one a host makes
        thirty-nine more of. The bound is loose because it is about a
        catastrophe — a return to per-instance building — rather than about the
        speed of this machine.

        The tables themselves are checked everywhere else in this file; what is
        checked here is that sharing them did not hand out something empty.
    */
    void testTheLibraryIsBuiltOncePerProcess()
    {
        beginTest ("A second instrument costs nothing to give tables to");

        // The first one here may or may not be the process's first — the suite
        // no longer builds one before main(), so whichever test runs first pays
        // for it. Either way this one is not, which is all this test needs.
        const WavetableLibrary first;

        const auto start = juce::Time::getHighResolutionTicks();

        const WavetableLibrary second;

        const auto seconds = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - start);

        logMessage ("  a further instrument got its tables in "
                    + juce::String (seconds * 1000.0, 3) + " ms");

        expect (seconds < 0.05,
                "a second instrument took " + juce::String (seconds * 1000.0, 1)
                    + " ms, which means the tables are being built again per instance");

        // Shared, and the same bytes: two instruments must be reading one table
        // rather than two copies of it.
        for (int index = 0; index < WavetableLibrary::numTables; ++index)
        {
            expect (&first.getTable (index) == &second.getTable (index),
                    "table " + juce::String (index) + " is not shared");

            expect (! second.getTable (index).isEmpty(),
                    "table " + juce::String (index) + " came out empty");
        }

        expect (&first.getSubTable() == &second.getSubTable(), "the sub table is not shared");

        // And sharing must not have made them mutable through the back door: a
        // table published into one instrument is that instrument's business.
        auto replacement = std::make_unique<Wavetable>();
        {
            TableSpectrum spectrum { makeSilentSpectrum (4) };
            spectrum[0][0].sine = 1.0;

            buildWavetable (*replacement, spectrum);
        }

        WavetableLibrary third;

        expect (third.publish (0, std::move (replacement)));
        expect (third.isReplaced (0));
        expect (! first.isReplaced (0), "publishing into one instrument moved another");
        expect (&first.getTable (0) != &third.getTable (0));
    }

    /** The table library, built on first use rather than held as a member.

        A juce::UnitTest subclass is constructed at **static-initialisation
        time**, because that is how it registers itself. An expensive member is
        therefore expensive before main() runs, in every invocation of the
        binary — including `--help`, `--list`, and a ctest run of one unrelated
        category.

        This was not theoretical: building the four band-limited tables takes
        **165 ms**, and holding one here meant every run of the suite paid it up
        front. It also made the cost impossible to measure from inside the
        process, which is what PROJECT-STATE issue 18 was about — Phase 10c
        timed the first library construction it could see and got 0.01 ms,
        because this member had already done the work before the benchmark
        existed.
    */
    [[nodiscard]] WavetableLibrary& library()
    {
        if (lazyLibrary == nullptr)
            lazyLibrary = std::make_unique<WavetableLibrary>();

        return *lazyLibrary;
    }

    std::unique_ptr<WavetableLibrary> lazyLibrary;

    /** The mipmap is the anti-aliasing mechanism, so its selection rule is
        worth pinning directly rather than only through its audible effect.
    */
    void testMipLevelSelection()
    {
        beginTest ("Mip level selection keeps every harmonic below Nyquist");

        for (const double frequency : { 20.0, 55.0, 110.0, 440.0, 1000.0, 4186.0, 10000.0 })
        {
            const auto level = Wavetable::selectMipLevel (frequency, testSampleRate);

            expect (level >= 0 && level < Wavetable::numMipLevels,
                    "level out of range at " + juce::String (frequency) + " Hz");

            const auto harmonics = Wavetable::harmonicsAtLevel (level);
            const auto highest = frequency * static_cast<double> (harmonics);

            expect (highest <= testSampleRate * 0.5 + 1.0,
                    "at " + juce::String (frequency) + " Hz the chosen level keeps "
                        + juce::String (harmonics) + " harmonics, reaching "
                        + juce::String (highest, 0) + " Hz, above Nyquist");
        }

        // Higher notes must never select a more detailed level than lower ones.
        int previous = -1;

        for (int note = 12; note <= 120; note += 6)
        {
            const auto frequency = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const auto level = Wavetable::selectMipLevel (frequency, testSampleRate);

            expect (level >= previous, "mip level went down as pitch went up");
            previous = level;
        }
    }

    void testTablesAreBuiltAndBounded()
    {
        beginTest ("Every built-in table is populated and normalised");

        for (int index = 0; index < WavetableLibrary::numTables; ++index)
        {
            const auto& table = library().getTable (index);

            expect (! table.isEmpty(), "table " + juce::String (index) + " is empty");
            expectEquals (table.getNumFrames(), WavetableLibrary::framesPerTable);

            for (int frame = 0; frame < table.getNumFrames(); ++frame)
            {
                for (int level = 0; level < Wavetable::numMipLevels; ++level)
                {
                    const auto* data = table.getReadPointer (level, frame);
                    expect (data != nullptr);

                    if (data == nullptr)
                        continue;

                    float peak = 0.0f;
                    bool allFinite = true;

                    for (int i = 0; i < Wavetable::samplesAtLevel (level); ++i)
                    {
                        peak = std::max (peak, std::abs (data[i]));
                        allFinite = allFinite && std::isfinite (data[i]);
                    }

                    expect (allFinite, "table " + juce::String (index) + " level "
                                           + juce::String (level) + " contains non-finite data");

                    // Normalisation targets unity at the top level; lower levels
                    // hold fewer harmonics and so peak at or below it.
                    expect (peak <= 1.001f,
                            "table " + juce::String (index) + " level " + juce::String (level)
                                + " peaks at " + juce::String (peak, 4));

                    expect (peak > 0.0f,
                            "table " + juce::String (index) + " level " + juce::String (level)
                                + " frame " + juce::String (frame) + " is silent");
                }
            }
        }
    }

    void testPitchAccuracy()
    {
        beginTest ("The oscillator reproduces the requested frequency");

        for (const double frequency : { 55.0, 220.0, 440.0, 1760.0 })
        {
            const auto spectrum = renderSpectrum (library().getTable (0), frequency, 1.0f);

            // The loudest bin should sit at the fundamental.
            int loudestBin = 0;
            float loudest = -200.0f;

            for (int bin = 1; bin < static_cast<int> (spectrum.size()); ++bin)
            {
                if (spectrum[static_cast<std::size_t> (bin)] > loudest)
                {
                    loudest = spectrum[static_cast<std::size_t> (bin)];
                    loudestBin = bin;
                }
            }

            const auto binWidth = testSampleRate / static_cast<double> (fftSize);
            const auto measured = static_cast<double> (loudestBin) * binWidth;

            // Tolerance is bounded by the FFT's own resolution, not by a
            // percentage: at 55 Hz one bin is 2.9 Hz, so a 1% tolerance would be
            // demanding precision the measurement cannot deliver. Pitch accuracy
            // to a fraction of a semitone is asserted separately, and far more
            // sharply, by the zero-crossing test in the engine suite.
            expect (std::abs (measured - frequency) <= binWidth * 1.5,
                    "requested " + juce::String (frequency, 1) + " Hz, measured "
                        + juce::String (measured, 1) + " Hz (bin width "
                        + juce::String (binWidth, 2) + " Hz)");
        }
    }

    /** The headline Phase 4 requirement. A naive wavetable read would alias
        badly at high pitches, so the range is swept from the bottom of the
        keyboard to well above the top.
    */
    void testAntiAliasingAcrossTheRange()
    {
        beginTest ("Aliasing stays below the threshold across the pitch range");

        // The saw end of table 0 — the brightest content available, and so the
        // hardest case for aliasing.
        const auto& table = library().getTable (0);

        struct Note { const char* name; double frequency; };

        static constexpr Note notes[] {
            { "A0 (lowest)",   27.5 },
            { "A1",            55.0 },
            { "A2",           110.0 },
            { "A3",           220.0 },
            { "A4",           440.0 },
            { "A5",           880.0 },
            { "A6",          1760.0 },
            { "A7",          3520.0 },
            { "C8",          4186.0 },
            { "above range", 8000.0 }
        };

        for (const auto& note : notes)
        {
            const auto spectrum = renderSpectrum (table, note.frequency, 1.0f);
            const auto worstAlias = measureWorstAlias (spectrum, note.frequency);

            logMessage ("    " + juce::String (note.name).paddedRight (' ', 14)
                        + juce::String (note.frequency, 1).paddedLeft (' ', 8) + " Hz   worst alias "
                        + juce::String (worstAlias, 1) + " dBc");

            expect (worstAlias < maxAliasDecibels,
                    juce::String (note.name) + ": worst alias " + juce::String (worstAlias, 1)
                        + " dBc exceeds the " + juce::String (maxAliasDecibels, 0) + " dBc budget");
        }
    }

    /** Scanning changes the harmonic content, so aliasing has to hold across
        positions, not just at the extremes.
    */
    void testAliasingWhileScanning()
    {
        beginTest ("Aliasing stays below the threshold at every scan position");

        for (int tableIndex = 0; tableIndex < WavetableLibrary::numTables; ++tableIndex)
        {
            const auto& table = library().getTable (tableIndex);

            for (const float position : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto spectrum = renderSpectrum (table, 1000.0, position);
                const auto worstAlias = measureWorstAlias (spectrum, 1000.0);

                expect (worstAlias < maxAliasDecibels,
                        "table " + juce::String (tableIndex) + " position "
                            + juce::String (position, 2) + ": worst alias "
                            + juce::String (worstAlias, 1) + " dBc");
            }
        }
    }

    /** Scanning must not produce steps. A discontinuity as frames cross would
        be audible as a click on an automated or modulated position.
    */
    void testFrameScanningIsSmooth()
    {
        beginTest ("Sweeping the scan position produces no discontinuity");

        const auto& table = library().getTable (0);

        WavetableOscillator oscillator;
        oscillator.setSampleRate (testSampleRate);
        oscillator.setTable (&table);
        oscillator.setFrequency (220.0);
        oscillator.resetPhase (0.0);

        constexpr int numSamples = 48000;
        std::vector<float> rendered (static_cast<std::size_t> (numSamples), 0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            // A full sweep across the table during the render.
            oscillator.setPosition (static_cast<float> (i) / static_cast<float> (numSamples - 1));
            rendered[static_cast<std::size_t> (i)] = oscillator.getNextSample();
        }

        const auto largestStepOf = [] (const std::vector<float>& data)
        {
            float largest = 0.0f;

            for (std::size_t i = 1; i < data.size(); ++i)
                largest = std::max (largest, std::abs (data[i] - data[i - 1]));

            return largest;
        };

        // Measured against the waveform's own slope, not an absolute number. A
        // band-limited saw is *supposed* to move fast — its transition spans a
        // few samples — so an absolute threshold would only measure how bright
        // the table is. What matters is whether sweeping adds a discontinuity
        // beyond what holding a fixed position already produces.
        float largestStaticStep = 0.0f;

        for (const float position : { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
        {
            WavetableOscillator fixedOscillator;
            fixedOscillator.setSampleRate (testSampleRate);
            fixedOscillator.setTable (&table);
            fixedOscillator.setFrequency (220.0);
            fixedOscillator.setPosition (position);
            fixedOscillator.resetPhase (0.0);

            std::vector<float> staticRender (static_cast<std::size_t> (numSamples), 0.0f);

            for (int i = 0; i < numSamples; ++i)
                staticRender[static_cast<std::size_t> (i)] = fixedOscillator.getNextSample();

            largestStaticStep = std::max (largestStaticStep, largestStepOf (staticRender));
        }

        const auto sweptStep = largestStepOf (rendered);

        expect (sweptStep <= largestStaticStep * 1.10f,
                "sweeping the scan position added a discontinuity: swept step "
                    + juce::String (sweptStep, 4) + " against a static maximum of "
                    + juce::String (largestStaticStep, 4));

        for (const auto sample : rendered)
            expect (std::isfinite (sample), "scanning produced non-finite output");
    }

    void testTableSelectionChangesTimbre()
    {
        beginTest ("Each table scans through the shapes it claims to");

        const auto binOfHarmonic = [] (int harmonic)
        {
            return static_cast<std::size_t> (
                std::round (440.0 * harmonic * static_cast<double> (fftSize) / testSampleRate));
        };

        const auto harmonicOf = [&binOfHarmonic] (const std::vector<float>& spectrum, int harmonic)
        {
            return spectrum[binOfHarmonic (harmonic)];
        };

        // SWEEP opens its bandwidth across the table. At the bottom it is
        // essentially the fundamental alone; at the top it is a full saw.
        const auto sweepClosed = renderSpectrum (library().getTable (0), 440.0, 0.0f);
        const auto sweepOpen = renderSpectrum (library().getTable (0), 440.0, 1.0f);

        expect (harmonicOf (sweepClosed, 4) < -60.0f,
                "the closed end of Sweep should be near enough a sine: "
                    + juce::String (harmonicOf (sweepClosed, 4), 1) + " dBc at the fourth");

        expect (harmonicOf (sweepOpen, 4) > -30.0f,
                "the open end should be a saw: "
                    + juce::String (harmonicOf (sweepOpen, 4), 1) + " dBc at the fourth");

        // PULSE is the one that cannot be written as a blend of two shapes, and
        // this is why: at the bottom it is a square, which has *no even
        // harmonics at all*, and at the top it is a narrow pulse, which has
        // every harmonic. The second harmonic alone tells the two apart.
        const auto square = renderSpectrum (library().getTable (1), 440.0, 0.0f);
        const auto narrow = renderSpectrum (library().getTable (1), 440.0, 1.0f);

        expect (harmonicOf (square, 2) < -50.0f,
                "a square has no second harmonic: "
                    + juce::String (harmonicOf (square, 2), 1) + " dBc");

        expect (harmonicOf (narrow, 2) > harmonicOf (square, 2) + 40.0f,
                "a narrow pulse has one: " + juce::String (harmonicOf (narrow, 2), 1)
                    + " dBc against " + juce::String (harmonicOf (square, 2), 1));

        // FORMANT carries a resonant peak that climbs as the table is scanned.
        // Low in its travel the peak sits over the low harmonics, high in its
        // travel it has moved above them — so the *ratio* between a high and a
        // low harmonic has to rise, even though both are present throughout.
        const auto formantLow = renderSpectrum (library().getTable (2), 440.0, 0.0f);
        const auto formantHigh = renderSpectrum (library().getTable (2), 440.0, 1.0f);

        const auto tiltLow = harmonicOf (formantLow, 16) - harmonicOf (formantLow, 2);
        const auto tiltHigh = harmonicOf (formantHigh, 16) - harmonicOf (formantHigh, 2);

        expect (tiltHigh > tiltLow + 12.0f,
                "the formant peak should climb: tilt went from "
                    + juce::String (tiltLow, 1) + " dB to " + juce::String (tiltHigh, 1) + " dB");

        // FOLD is a sine at the bottom and a folded one at the top. It is the
        // table built by drawing samples and analysing them, so this also
        // exercises the route a loaded wavetable takes into the engine.
        const auto unfolded = renderSpectrum (library().getTable (3), 440.0, 0.0f);
        const auto folded = renderSpectrum (library().getTable (3), 440.0, 1.0f);

        expect (harmonicOf (unfolded, 3) < -60.0f,
                "an unfolded sine has no third harmonic: "
                    + juce::String (harmonicOf (unfolded, 3), 1) + " dBc");

        expect (harmonicOf (folded, 3) > -20.0f,
                "a folded one is full of them: "
                    + juce::String (harmonicOf (folded, 3), 1) + " dBc");

        // And no two tables are the same sound at the top of their travel,
        // which is the claim the parameter's four positions make.
        const std::vector<std::vector<float>> ends {
            sweepOpen, narrow, formantHigh, folded
        };

        for (std::size_t a = 0; a < ends.size(); ++a)
        {
            for (auto b = a + 1; b < ends.size(); ++b)
            {
                // Across the whole band the mip level keeps at this pitch, not
                // just the first few harmonics. Sweep and Formant are both a
                // saw down at the bottom of the spectrum — the formant peak has
                // climbed to the fortieth harmonic by the top of its travel,
                // which is exactly where they differ and nowhere near harmonic
                // twelve. A comparison that stopped early called them identical
                // and was measuring the wrong part of the sound.
                auto difference = 0.0f;

                for (int harmonic = 2; harmonic <= 48; ++harmonic)
                    difference += std::abs (harmonicOf (ends[a], harmonic)
                                            - harmonicOf (ends[b], harmonic));

                expect (difference > 20.0f,
                        "tables " + juce::String (static_cast<int> (a)) + " and "
                            + juce::String (static_cast<int> (b))
                            + " are too alike: " + juce::String (difference, 1) + " dB apart");
            }
        }
    }

    /** Interpolation error shows up as a noise floor under a pure tone, so the
        sine end of a table is the cleanest way to measure it.
    */
    void testInterpolationIsAccurate()
    {
        beginTest ("Interpolation error stays below the aliasing budget");

        // Position 0 of table 0 is a pure sine, so anything else in the spectrum
        // is interpolation error.
        for (const double frequency : { 110.0, 440.0, 1000.0, 3000.0 })
        {
            const auto spectrum = renderSpectrum (library().getTable (0), frequency, 0.0f);
            const auto worst = measureWorstAlias (spectrum, frequency);

            logMessage ("    sine at " + juce::String (frequency, 0).paddedLeft (' ', 6)
                        + " Hz   interpolation floor " + juce::String (worst, 1) + " dBc");

            expect (worst < maxAliasDecibels,
                    "interpolation floor at " + juce::String (frequency, 0) + " Hz was "
                        + juce::String (worst, 1) + " dBc");
        }
    }

    void testDegenerateInputIsSafe()
    {
        beginTest ("Degenerate input produces silence rather than misbehaviour");

        WavetableOscillator oscillator;
        oscillator.setSampleRate (testSampleRate);

        // No table assigned.
        oscillator.setFrequency (440.0);
        expectEquals (oscillator.getNextSample(), 0.0f, "an oscillator with no table must be silent");

        oscillator.setTable (&library().getTable (0));

        // Zero, negative and absurd frequencies must all stay finite.
        for (const double frequency : { 0.0, -440.0, 1.0e9, testSampleRate })
        {
            oscillator.setFrequency (frequency);

            for (int i = 0; i < 256; ++i)
                expect (std::isfinite (oscillator.getNextSample()),
                        "non-finite output at " + juce::String (frequency) + " Hz");
        }

        // Regression: an absurd frequency makes the phase increment enormous, so
        // a wrap that subtracts 1.0 once never catches up and the phase grows
        // without bound. Converting that to a table index is undefined
        // behaviour — UBSan caught it as "2.15e+09 is outside the range of
        // representable values of type 'int'" while MSVC silently produced
        // usable-looking garbage. Phase must stay in [0, 1) no matter what.
        for (const double frequency : { 1.0e6, 1.0e9, 1.0e15 })
        {
            oscillator.setFrequency (frequency);
            oscillator.resetPhase (0.0);

            for (int i = 0; i < 4096; ++i)
                (void) oscillator.getNextSample();

            expect (oscillator.getPhase() >= 0.0 && oscillator.getPhase() < 1.0,
                    "phase escaped [0, 1) at " + juce::String (frequency)
                        + " Hz: " + juce::String (oscillator.getPhase()));
        }

        // A non-finite frequency must be rejected rather than poisoning phase.
        for (const double frequency : { std::numeric_limits<double>::infinity(),
                                        -std::numeric_limits<double>::infinity(),
                                        std::numeric_limits<double>::quiet_NaN() })
        {
            oscillator.setFrequency (frequency);
            oscillator.resetPhase (0.0);

            for (int i = 0; i < 256; ++i)
                expect (std::isfinite (oscillator.getNextSample()),
                        "a non-finite frequency produced non-finite output");

            expect (std::isfinite (oscillator.getPhase()), "phase became non-finite");
        }

        // Reading the table directly with a hostile phase must also be safe:
        // Wavetable is a public interface, not only the oscillator's private one.
        const auto& table = library().getTable (0);

        for (const double phase : { 0.0, 1.0, -1.0, 1.0e12, -1.0e12,
                                    std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN() })
        {
            const auto sample = table.getSample (0, 0, phase);
            expect (std::isfinite (sample),
                    "table read returned non-finite output at phase " + juce::String (phase));
        }

        // Out-of-range positions clamp rather than read out of bounds.
        oscillator.setFrequency (440.0);

        for (const float position : { -5.0f, -0.001f, 1.001f, 5.0f })
        {
            oscillator.setPosition (position);

            for (int i = 0; i < 256; ++i)
                expect (std::isfinite (oscillator.getNextSample()),
                        "non-finite output at position " + juce::String (position));
        }
    }
};

WavetableTests wavetableTests;

} // namespace
