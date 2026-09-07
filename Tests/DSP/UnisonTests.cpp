/*
    Unison tests.

    Two things are being pinned here, and they are different in kind.

    The UnisonLayout tests cover *design decisions* — the detune distribution,
    the pan law and the gain normalisation. Those are choices with audible
    consequences that a reader cannot verify by inspecting a render, so they are
    asserted directly on the numbers.

    The UnisonOscillator tests cover *behaviour*: that a single-voice stack is
    indistinguishable from a plain oscillator, that a spread stack is genuinely
    stereo, and that the sum stays inside the bound the engine gain staging
    documents.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
#include <vector>

#include "DSP/Oscillators/UnisonOscillator.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Oscillators/WavetableOscillator.h"
#include "DSP/Unison/UnisonLayout.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;

/** A rendered stereo block from one unison stack. */
struct StereoBlock
{
    std::vector<float> left;
    std::vector<float> right;

    [[nodiscard]] float peak() const noexcept
    {
        float highest = 0.0f;

        for (std::size_t i = 0; i < left.size(); ++i)
            highest = std::max (highest, std::max (std::abs (left[i]), std::abs (right[i])));

        return highest;
    }

    [[nodiscard]] bool isFinite() const noexcept
    {
        for (std::size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;

        return true;
    }

    /** @returns the largest absolute difference between the two channels.

        Zero means the stack is a mono signal duplicated, which is exactly what
        a centred single voice should be and what a spread stack should not.
    */
    [[nodiscard]] float largestChannelDifference() const noexcept
    {
        float largest = 0.0f;

        for (std::size_t i = 0; i < left.size(); ++i)
            largest = std::max (largest, std::abs (left[i] - right[i]));

        return largest;
    }
};

[[nodiscard]] StereoBlock render (UnisonOscillator& oscillator, int numSamples)
{
    StereoBlock block;
    block.left.assign (static_cast<std::size_t> (numSamples), 0.0f);
    block.right.assign (static_cast<std::size_t> (numSamples), 0.0f);

    for (int i = 0; i < numSamples; ++i)
        oscillator.addNextStereoSample (block.left[static_cast<std::size_t> (i)],
                                        block.right[static_cast<std::size_t> (i)]);

    return block;
}

class UnisonTests final : public juce::UnitTest
{
public:
    UnisonTests()
        : juce::UnitTest ("Unison", "DSP")
    {
    }

    void runTest() override
    {
        testSingleVoiceIsCentredAndUntuned();
        testDetuneIsSymmetricAndScaled();
        testSpreadDistributesAcrossTheField();
        testNormalisationFollowsRootN();
        testDegenerateLayoutInputsAreClamped();
        testMatchesAndGenerationTrackUpdates();
        testSingleVoiceStackMatchesAPlainOscillator();
        testSpreadStackIsGenuinelyStereo();
        testDetunedStackBeats();
        testStackStaysWithinItsDocumentedBound();
        testExtremeInputsStayFinite();
    }

private:
    WavetableLibrary library;

    //==========================================================================
    // Layout: the design decisions.

    void testSingleVoiceIsCentredAndUntuned()
    {
        beginTest ("A single unison voice is centred, at unity gain and unity pitch");

        UnisonLayout layout;
        layout.update (1, 1.0f, 1.0f);

        expectEquals (layout.getCount(), 1);
        expectWithinAbsoluteError (layout.getFrequencyRatio (0), 1.0, 1.0e-12,
                                   "a lone unison voice must play the note it was given");

        // Unity on both channels, not -3 dB. The default patch is exactly this
        // configuration, and the engine gain staging was measured for it: a
        // constant-power law anchored at the extremes would quietly make every
        // default patch 3 dB quieter than the measurement it is checked against.
        expectWithinAbsoluteError (layout.getGainLeft (0), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (layout.getGainRight (0), 1.0f, 1.0e-6f);

        // Detune and spread must be inert with nothing to spread.
        UnisonLayout untuned;
        untuned.update (1, 0.0f, 0.0f);

        expectWithinAbsoluteError (untuned.getGainLeft (0), layout.getGainLeft (0), 1.0e-6f);
    }

    void testDetuneIsSymmetricAndScaled()
    {
        beginTest ("Detune is symmetric about the played note and scales with the amount");

        UnisonLayout layout;
        layout.update (8, 1.0f, 0.0f);

        // Voice i and voice (N-1-i) must be the same interval either side, which
        // in frequency ratios means their product is one. An asymmetric stack
        // would drag the perceived pitch of every unison patch off the note.
        for (int i = 0; i < 4; ++i)
        {
            const auto low = layout.getFrequencyRatio (i);
            const auto high = layout.getFrequencyRatio (7 - i);

            expectWithinAbsoluteError (low * high, 1.0, 1.0e-9,
                                       "unison voice " + juce::String (i)
                                           + " is not mirrored by its partner");
        }

        // The widest voice must sit at exactly the documented maximum.
        const auto widestCents = 1200.0 * std::log2 (layout.getFrequencyRatio (7));
        expectWithinAbsoluteError (widestCents, static_cast<double> (UnisonLayout::maxDetuneCents), 1.0e-6,
                                   "full detune must reach the documented edge of the stack");

        // Half the amount is half the interval in cents, not half the ratio.
        UnisonLayout half;
        half.update (8, 0.5f, 0.0f);

        const auto halfCents = 1200.0 * std::log2 (half.getFrequencyRatio (7));
        expectWithinAbsoluteError (halfCents, widestCents * 0.5, 1.0e-6,
                                   "detune must be linear in cents, not in hertz");

        // No detune means every voice plays the note.
        UnisonLayout none;
        none.update (8, 0.0f, 0.0f);

        for (int i = 0; i < 8; ++i)
            expectWithinAbsoluteError (none.getFrequencyRatio (i), 1.0, 1.0e-12);
    }

    void testSpreadDistributesAcrossTheField()
    {
        beginTest ("Stereo spread distributes unison voices across the field");

        UnisonLayout stacked;
        stacked.update (8, 0.5f, 0.0f);

        // With no spread every voice is centred, so all gains are equal.
        for (int i = 1; i < 8; ++i)
        {
            expectWithinAbsoluteError (stacked.getGainLeft (i), stacked.getGainLeft (0), 1.0e-6f);
            expectWithinAbsoluteError (stacked.getGainRight (i), stacked.getGainRight (0), 1.0e-6f);
        }

        UnisonLayout spread;
        spread.update (8, 0.5f, 1.0f);

        // At full spread the outermost voices are hard panned: one channel
        // carries the voice, the other carries none of it.
        expectWithinAbsoluteError (spread.getGainRight (0), 0.0f, 1.0e-6f,
                                   "the leftmost voice must not reach the right channel");
        expectWithinAbsoluteError (spread.getGainLeft (7), 0.0f, 1.0e-6f,
                                   "the rightmost voice must not reach the left channel");

        expect (spread.getGainLeft (0) > spread.getGainLeft (7),
                "the leftmost voice must be louder on the left than the rightmost is");

        // And the distribution is monotonic across the stack, so no voice jumps
        // sides as the count changes.
        for (int i = 1; i < 8; ++i)
            expect (spread.getGainLeft (i) <= spread.getGainLeft (i - 1) + 1.0e-6f,
                    "spread must move monotonically from left to right");
    }

    void testNormalisationFollowsRootN()
    {
        beginTest ("The unison stack is normalised by one over root N");

        // Measured at the centre, where the pan law contributes unity, so the
        // remaining factor is the normalisation alone.
        for (const int count : { 1, 4, 9, 16 })
        {
            UnisonLayout layout;
            layout.update (count, 0.0f, 0.0f);

            const auto expected = 1.0f / std::sqrt (static_cast<float> (count));

            expectWithinAbsoluteError (layout.getGainLeft (0), expected, 1.0e-6f,
                                       juce::String (count) + "-voice unison is mis-normalised");
        }
    }

    void testDegenerateLayoutInputsAreClamped()
    {
        beginTest ("Degenerate layout inputs are clamped rather than propagated");

        constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
        constexpr auto infinity = std::numeric_limits<float>::infinity();

        struct Case { int count; float detune; float spread; const char* name; };

        const Case cases[] {
            { 0, 0.5f, 0.5f, "zero voices" },
            { -4, 0.5f, 0.5f, "negative voices" },
            { 1000, 0.5f, 0.5f, "more voices than the pool holds" },
            { 8, -1.0f, 0.5f, "negative detune" },
            { 8, 10.0f, 0.5f, "detune beyond full" },
            { 8, 0.5f, -3.0f, "negative spread" },
            { 8, 0.5f, 4.0f, "spread beyond full" },
            { 8, nan, 0.5f, "NaN detune" },
            { 8, 0.5f, nan, "NaN spread" },
            { 8, infinity, infinity, "infinite detune and spread" }
        };

        for (const auto& testCase : cases)
        {
            UnisonLayout layout;
            layout.update (testCase.count, testCase.detune, testCase.spread);

            expect (layout.getCount() >= 1 && layout.getCount() <= UnisonLayout::maxVoices,
                    juce::String (testCase.name) + ": voice count escaped its range");

            for (int i = 0; i < layout.getCount(); ++i)
            {
                expect (std::isfinite (layout.getFrequencyRatio (i)),
                        juce::String (testCase.name) + ": non-finite frequency ratio");
                expect (layout.getFrequencyRatio (i) > 0.0,
                        juce::String (testCase.name) + ": non-positive frequency ratio");
                expect (std::isfinite (layout.getGainLeft (i)) && std::isfinite (layout.getGainRight (i)),
                        juce::String (testCase.name) + ": non-finite gain");
                expect (layout.getStartPhase (i) >= 0.0 && layout.getStartPhase (i) < 1.0,
                        juce::String (testCase.name) + ": start phase escaped [0, 1)");
            }
        }
    }

    void testMatchesAndGenerationTrackUpdates()
    {
        beginTest ("A layout reports whether it is current, and announces changes");

        UnisonLayout layout;
        layout.update (4, 0.25f, 0.5f);

        expect (layout.matches (4, 0.25f, 0.5f), "a layout must recognise its own inputs");
        expect (! layout.matches (5, 0.25f, 0.5f));
        expect (! layout.matches (4, 0.30f, 0.5f));
        expect (! layout.matches (4, 0.25f, 0.6f));

        // The generation is what lets an oscillator holding a pointer notice
        // that the layout underneath it moved.
        const auto before = layout.getGeneration();
        layout.update (5, 0.25f, 0.5f);

        expect (layout.getGeneration() != before, "an update must change the generation");
    }

    //==========================================================================
    // Oscillator: the behaviour.

    void testSingleVoiceStackMatchesAPlainOscillator()
    {
        beginTest ("A one-voice unison stack is identical to a plain oscillator");

        const auto& table = library.getTable (0);

        UnisonLayout layout;
        layout.update (1, 0.5f, 1.0f);

        UnisonOscillator stack;
        stack.setSampleRate (testSampleRate);
        stack.setTable (&table);
        stack.setLayout (&layout);
        stack.setPosition (0.75f);
        stack.setFrequency (220.0);
        stack.resetPhase (0.0);

        WavetableOscillator plain;
        plain.setSampleRate (testSampleRate);
        plain.setTable (&table);
        plain.setPosition (0.75f);
        plain.setFrequency (220.0);
        plain.resetPhase (0.0);

        const auto block = render (stack, 2048);

        for (int i = 0; i < 2048; ++i)
        {
            const auto expected = plain.getNextSample();
            const auto index = static_cast<std::size_t> (i);

            // Sample-exact, not approximate: the whole point of anchoring the
            // pan law at the centre is that adding unison machinery to a patch
            // that uses none of it changes nothing at all.
            expectWithinAbsoluteError (block.left[index], expected, 1.0e-6f);
            expectWithinAbsoluteError (block.right[index], expected, 1.0e-6f);
        }

        expectEquals (block.largestChannelDifference(), 0.0f,
                      "a centred single voice must be mono, not a fabricated stereo image");
    }

    void testSpreadStackIsGenuinelyStereo()
    {
        beginTest ("A spread unison stack produces different signals on each channel");

        UnisonLayout centred;
        centred.update (8, 0.3f, 0.0f);

        UnisonLayout spread;
        spread.update (8, 0.3f, 1.0f);

        UnisonOscillator stack;
        stack.setSampleRate (testSampleRate);
        stack.setTable (&library.getTable (0));
        stack.setPosition (1.0f);
        stack.setFrequency (110.0);

        stack.setLayout (&centred);
        stack.resetPhase (0.0);
        const auto mono = render (stack, 4096);

        stack.setLayout (&spread);
        stack.resetPhase (0.0);
        const auto wide = render (stack, 4096);

        expectEquals (mono.largestChannelDifference(), 0.0f,
                      "with no spread the two channels must be identical");

        expect (wide.largestChannelDifference() > 0.05f,
                "with full spread the two channels must carry different signals");
    }

    void testDetunedStackBeats()
    {
        beginTest ("A detuned unison stack beats rather than reinforcing");

        UnisonLayout layout;
        layout.update (2, 1.0f, 0.0f);

        UnisonOscillator stack;
        stack.setSampleRate (testSampleRate);
        stack.setTable (&library.getTable (0));
        stack.setPosition (0.0f); // A sine, so the envelope is the beat itself.
        stack.setLayout (&layout);
        stack.setFrequency (220.0);
        stack.resetPhase (0.0);

        // Two voices 50 cents apart at 220 Hz differ by about 6.4 Hz, so a beat
        // cycle is roughly 156 ms. Two seconds contains many of them.
        const auto block = render (stack, static_cast<int> (testSampleRate * 2.0));

        float quietest = std::numeric_limits<float>::max();
        float loudest = 0.0f;

        // Peak per 20 ms window: the beat shows up as the spread between the
        // loudest window and the quietest.
        const int windowSamples = static_cast<int> (testSampleRate * 0.02);

        for (std::size_t start = 0; start + static_cast<std::size_t> (windowSamples) <= block.left.size();
             start += static_cast<std::size_t> (windowSamples))
        {
            float windowPeak = 0.0f;

            for (int i = 0; i < windowSamples; ++i)
                windowPeak = std::max (windowPeak, std::abs (block.left[start + static_cast<std::size_t> (i)]));

            quietest = std::min (quietest, windowPeak);
            loudest = std::max (loudest, windowPeak);
        }

        expect (loudest > quietest * 2.0f,
                "a detuned pair must move through cancellation and reinforcement, but the "
                "loudest window was only " + juce::String (loudest / quietest, 2)
                    + " times the quietest");
    }

    void testStackStaysWithinItsDocumentedBound()
    {
        beginTest ("A unison stack stays within the root-N bound its normalisation implies");

        const auto& table = library.getTable (0);

        UnisonLayout single;
        single.update (1, 0.0f, 0.0f);

        UnisonOscillator reference;
        reference.setSampleRate (testSampleRate);
        reference.setTable (&table);
        reference.setLayout (&single);
        reference.setPosition (1.0f);
        reference.setFrequency (110.0);
        reference.resetPhase (0.0);

        const auto singlePeak = render (reference, 8192).peak();

        expect (singlePeak > 0.1f, "the reference oscillator must produce a signal");

        for (const int count : { 2, 4, 8, 16 })
        {
            UnisonLayout layout;
            layout.update (count, 0.2f, 0.5f);

            UnisonOscillator stack;
            stack.setSampleRate (testSampleRate);
            stack.setTable (&table);
            stack.setLayout (&layout);
            stack.setPosition (1.0f);
            stack.setFrequency (110.0);
            stack.resetPhase (0.0);

            // Two seconds, so the detuned voices have time to drift through
            // alignment — the moment a shorter render would miss.
            const auto peak = render (stack, static_cast<int> (testSampleRate * 2.0)).peak();
            const auto bound = std::sqrt (static_cast<float> (count)) * singlePeak;

            expect (peak <= bound * 1.05f,
                    juce::String (count) + "-voice unison peaked at "
                        + juce::String (peak / singlePeak, 2)
                        + " times a single voice, beyond the root-N bound of "
                        + juce::String (std::sqrt (static_cast<float> (count)), 2));
        }
    }

    void testExtremeInputsStayFinite()
    {
        beginTest ("Extreme oscillator input stays finite");

        constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
        constexpr auto infinity = std::numeric_limits<double>::infinity();

        UnisonLayout layout;
        layout.update (16, 1.0f, 1.0f);

        for (const double frequency : { 0.0, -440.0, 1.0e9, nan, infinity })
        {
            UnisonOscillator stack;
            stack.setSampleRate (testSampleRate);
            stack.setTable (&library.getTable (0));
            stack.setLayout (&layout);
            stack.setPosition (0.5f);
            stack.setFrequency (frequency);
            stack.resetPhase (0.0);

            expect (render (stack, 1024).isFinite(),
                    "a frequency of " + juce::String (frequency) + " produced non-finite output");
        }

        // A stack with no table must be silent rather than dereference null.
        UnisonOscillator empty;
        empty.setSampleRate (testSampleRate);
        empty.setLayout (&layout);
        empty.setFrequency (440.0);

        expectEquals (render (empty, 512).peak(), 0.0f,
                      "an oscillator with no table must be silent");

        // And so must one with no layout at all, which falls back to a single
        // centred voice rather than to an all-zero gain table.
        UnisonOscillator noLayout;
        noLayout.setSampleRate (testSampleRate);
        noLayout.setTable (&library.getTable (0));
        noLayout.setPosition (1.0f);
        noLayout.setFrequency (220.0);
        noLayout.resetPhase (0.0);

        const auto fallback = render (noLayout, 2048);

        expect (fallback.isFinite());
        expect (fallback.peak() > 0.1f,
                "an oscillator with no layout must fall back to a single audible voice");
    }
};

UnisonTests unisonTests;

} // namespace
