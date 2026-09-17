/*
    Regression renders.

    WHAT THIS ASKS THAT NOTHING ELSE DOES. Every other test in the suite states
    a property: the filter's corner is where it was asked for, the tuning is
    within a cent, the chain falls to exact silence. All of those can pass while
    Apollo quietly stops sounding like itself — a wavetable rendered with one
    harmonic fewer, an envelope curve reshaped, a reverb's diffusion altered, a
    default nudged. Each of those is a change somebody made on purpose or a
    change nobody noticed, and there is no property that separates the two.

    So this asks the only question that can catch the second kind: **is it still
    the same sound?** Fifteen renders, described in enough detail to notice a
    real change and loosely enough to survive four compilers, compared against
    references checked in beside them.

    A FAILURE HERE IS NOT NECESSARILY A BUG. It means a render moved. If the
    change was intended, the goldens are regenerated in the same commit and the
    diff records what it did to the sound; if it was not, something is wrong
    that no other test was watching for. The suite's job is to make the
    difference between those two impossible to miss, not to decide which it was.

    THE MACHINERY IS THE POINT, AND IT IS WHAT PHASES 10C AND 10D WILL LEAN ON.
    Optimisation is a change that is *supposed* to leave the sound alone, and
    without a way to check that, "it still sounds fine to me" is the only
    available evidence.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string_view>
#include <vector>

#include "Parameters/ParameterLayout.h"
#include "Regression/Fingerprint.h"
#include "Regression/GoldenRenders.h"
#include "Regression/RenderCase.h"

using namespace apollo;
using namespace apollo::regression;

namespace
{

/** @returns the golden called @p name, or nullptr. */
[[nodiscard]] const Golden* goldenNamed (std::string_view name)
{
    for (const auto& golden : goldens())
        if (golden.name == name)
            return &golden;

    return nullptr;
}

//==============================================================================
// The patch the sensitivity test perturbs, and the perturbations.
//
// A deliberately ordinary sound — one oscillator, five unison voices spread
// wide, a low pass with some resonance, a release long enough to outlast the
// note — chosen so that each of the four things a fingerprint measures has
// something to measure.

constexpr Setting baseSettings[] {
    { "osc1_level", 0.80f },
    { "osc1_unison", 5.0f },
    { "osc1_detune", 0.25f },
    { "osc1_spread", 1.00f },

    { "osc1_position", 0.35f },

    { "filter1_type", 1.0f },   // low pass
    { "filter1_cutoff", 2000.0f },
    { "filter1_resonance", 1.50f },

    { "env1_attack", 5.0f },
    { "env1_sustain", 0.80f },
    { "env1_release", 200.0f },
};

constexpr NoteEvent baseNote[] {
    { 0.0, 57, 0.85f, true },
    { 0.8, 57, 0.0f, false },
};

/** One realistic regression, and the part of the fingerprint that should see it. */
struct Perturbation
{
    const char* what;
    const char* seenBy;
    std::string_view id;
    float value;
};

constexpr Perturbation perturbations[] {
    // A fifth on a cutoff — about three semitones of filter movement, plainly
    // audible on a sustained sound.
    //
    // THE SIZES BELOW ARE COMFORTABLE RATHER THAN MARGINAL, on purpose: a
    // sensitivity test that sat exactly on its threshold would be a test that
    // went red for reasons unrelated to what it is watching. The thresholds
    // themselves were measured against this same patch and are recorded in
    // PROJECT-STATE §5d — the cutoff band moves 0.223 dB per one per cent, so
    // the suite notices a corner that moves by about a two-hundredth, and this
    // perturbation is forty times that.
    { "the filter moved by a fifth", "the bands", "filter1_cutoff", 2400.0f },

    // A third of a decibel of level.
    { "the level moved by a third of a decibel", "peak and RMS", "osc1_level", 0.77f },

    // Fifteen per cent on a release. The note ends at four fifths of the
    // render, so this can only show in the last three segments.
    { "the release grew by fifteen per cent", "the later segments", "env1_release", 230.0f },

    // The stereo image narrowed, with everything else identical. Only the side
    // channel can see this, which is why it is measured.
    { "the stereo spread narrowed", "the side channel", "osc1_spread", 0.80f },

    // Five per cent along the wavetable. The parameter most specific to this
    // instrument, and the one a change to the table renderer would move without
    // touching anything a level or an envelope could see.
    { "the wavetable position moved by a twentieth", "the bands", "osc1_position", 0.40f },
};

class RegressionTests final : public juce::UnitTest
{
public:
    RegressionTests()
        : juce::UnitTest ("Regression renders", "Regression")
    {
    }

    void runTest() override
    {
        testTheCasesAndTheGoldensAgree();
        testEveryRenderStillSoundsTheSame();
        testARenderIsRepeatable();
        testTheBlockSizeDoesNotChangeTheSound();
        testTheFingerprintWouldNoticeAChange();
    }

private:
    /** The bookkeeping, checked first so a missing golden reports as itself. */
    void testTheCasesAndTheGoldensAgree()
    {
        beginTest ("Every case has a golden, and every golden has a case");

        expect (! renderCases().empty(), "there are no render cases");

        expectEquals (static_cast<int> (goldens().size()),
                      static_cast<int> (renderCases().size()),
                      "the number of goldens does not match the number of cases — "
                      "regenerate with: ApolloTests --goldens > Tests/Regression/GoldenRenders.cpp");

        std::set<std::string_view> names;

        for (const auto& renderCase : renderCases())
        {
            const auto where = params::toJuceString (renderCase.name);

            expect (names.insert (renderCase.name).second, where + ": two cases share a name");
            expect (goldenNamed (renderCase.name) != nullptr, where + ": has no golden");
        }

        for (const auto& golden : goldens())
            expect (names.count (golden.name) == 1,
                    params::toJuceString (golden.name) + ": a golden with no case");
    }

    /** The test the phase exists for. */
    void testEveryRenderStillSoundsTheSame()
    {
        beginTest ("Every render matches the reference checked in beside it");

        const Tolerances tolerances;

        auto worstAnywhere = 0.0f;
        juce::String worstCase;

        for (const auto& renderCase : renderCases())
        {
            const auto where = params::toJuceString (renderCase.name);
            const auto* golden = goldenNamed (renderCase.name);

            if (golden == nullptr)
                continue; // Already reported above.

            juce::AudioBuffer<float> rendered;
            const auto failure = render (renderCase, rendered);

            expect (failure.isEmpty(), failure);

            if (failure.isNotEmpty())
                continue;

            const auto print = fingerprintOf (rendered, renderCase.sampleRate);
            const auto differences = compare (golden->print, print, tolerances);
            const auto worst = worstDifference (golden->print, print);

            if (worst > worstAnywhere)
            {
                worstAnywhere = worst;
                worstCase = where;
            }

            // Logged whether it passed or not. The figure is how much room the
            // tolerances have on this toolchain, and reading it across the four
            // CI platforms is the only way to know whether they are set
            // sensibly rather than hopefully.
            logMessage ("  " + where.paddedRight (' ', 30)
                        + " worst " + juce::String (worst, 4) + " dB"
                        + ", checksum " + juce::String::toHexString (static_cast<juce::int64> (print.checksum)));

            expect (differences.empty(),
                    where + ": " + juce::String (static_cast<int> (differences.size()))
                        + " measurements moved" + describe (differences));
        }

        logMessage ("  largest difference anywhere: " + juce::String (worstAnywhere, 4)
                    + " dB, in " + (worstCase.isEmpty() ? juce::String ("nothing") : worstCase));
    }

    /** Rendering the same case twice must produce the same samples.

        Not a statement about floating point — it is a statement about state.
        Anything that leaked between two renders in one process, a static that
        was written once, a table built lazily and reused half-initialised,
        would show here and nowhere else, because every other test in the suite
        builds a processor and throws it away.
    */
    void testARenderIsRepeatable()
    {
        beginTest ("The same case rendered twice produces the same samples");

        // Three chosen for what they carry rather than for coverage: an empty
        // patch, a full effects rack, and the noise generator — which is the one
        // source with a random element in it, and therefore the one where a
        // seed that was not reset would show.
        for (const auto* wanted : { "Init", "Rasp Bass", "Dust Sweep" })
        {
            const RenderCase* found = nullptr;

            for (const auto& renderCase : renderCases())
                if (renderCase.name == wanted)
                    found = &renderCase;

            expect (found != nullptr, juce::String (wanted) + ": no such case");

            if (found == nullptr)
                continue;

            juce::AudioBuffer<float> first;
            juce::AudioBuffer<float> second;

            expect (render (*found, first).isEmpty());
            expect (render (*found, second).isEmpty());

            const auto a = fingerprintOf (first, found->sampleRate);
            const auto b = fingerprintOf (second, found->sampleRate);

            expect (a.checksum == b.checksum,
                    juce::String (wanted) + ": two renders of the same case differed");
        }
    }

    /** A host may hand the plugin any block size it likes, and change it.

        The case table renders the same sound at 512 samples and at 64, which is
        what makes this checkable at all — two goldens of one patch. The claim
        asserted here is the audible one: the block size does not change the
        sound. Whether the two are *identical* to the last bit is logged rather
        than asserted, because that is a statement about how a compiler
        vectorised a loop of 64 against a loop of 512, and Apollo does not get
        to promise what four compilers will do with that.
    */
    void testTheBlockSizeDoesNotChangeTheSound()
    {
        beginTest ("The same patch renders the same at 512 samples and at 64");

        const RenderCase* large = nullptr;
        const RenderCase* small = nullptr;

        for (const auto& renderCase : renderCases())
        {
            if (renderCase.name == "Wire Pluck")
                large = &renderCase;

            if (renderCase.name == "Wire Pluck, 64-sample blocks")
                small = &renderCase;
        }

        expect (large != nullptr && small != nullptr, "the paired cases are missing");

        if (large == nullptr || small == nullptr)
            return;

        juce::AudioBuffer<float> atFiveTwelve;
        juce::AudioBuffer<float> atSixtyFour;

        expect (render (*large, atFiveTwelve).isEmpty());
        expect (render (*small, atSixtyFour).isEmpty());

        const auto a = fingerprintOf (atFiveTwelve, large->sampleRate);
        const auto b = fingerprintOf (atSixtyFour, small->sampleRate);

        const auto differences = compare (a, b);

        logMessage (juce::String ("  worst difference between block sizes: ")
                    + juce::String (worstDifference (a, b), 4) + " dB"
                    + (a.checksum == b.checksum ? ", and sample for sample identical"
                                                : ", not bit-identical"));

        expect (differences.empty(),
                juce::String (static_cast<int> (differences.size()))
                    + " measurements moved when only the block size changed"
                    + describe (differences));
    }

    /** The test that earns the rest of the file.

        A fingerprint that agreed with its golden no matter what Apollo did
        would pass for ever and protect nothing, and that failure mode is
        invisible — a green suite looks the same either way. So each part of the
        fingerprint is shown a change it is supposed to catch, and has to catch
        it.

        The changes are small on purpose. Anyone can detect a synthesiser that
        started outputting silence; the question is whether a filter that moved
        by three per cent, or a release fifteen per cent longer, survives
        unnoticed.
    */
    void testTheFingerprintWouldNoticeAChange()
    {
        beginTest ("A small change to the sound is caught, and by the part that should catch it");

        const RenderCase base { "sensitivity", "the unchanged patch", "",
                                baseSettings, baseNote, 48000.0, 512, 1.6 };

        juce::AudioBuffer<float> reference;
        expect (render (base, reference).isEmpty());

        const auto original = fingerprintOf (reference, base.sampleRate);

        for (const auto& perturbation : perturbations)
        {
            // The base settings with exactly one value replaced.
            std::vector<Setting> settings (std::begin (baseSettings), std::end (baseSettings));

            auto replaced = false;

            for (auto& setting : settings)
            {
                if (setting.id != perturbation.id)
                    continue;

                setting.value = perturbation.value;
                replaced = true;
            }

            expect (replaced, params::toJuceString (perturbation.id) + " is not in the base patch");

            const RenderCase changed { "sensitivity", perturbation.what, "",
                                       settings, baseNote, 48000.0, 512, 1.6 };

            juce::AudioBuffer<float> rendered;
            expect (render (changed, rendered).isEmpty());

            const auto print = fingerprintOf (rendered, base.sampleRate);
            const auto differences = compare (original, print);

            logMessage ("  " + juce::String (perturbation.what).paddedRight (' ', 46)
                        + juce::String (static_cast<int> (differences.size()))
                        + " measurements moved, worst "
                        + juce::String (worstDifference (original, print), 3) + " dB");

            expect (! differences.empty(),
                    juce::String ("the fingerprint did not notice that ")
                        + perturbation.what + " — " + perturbation.seenBy
                        + " should have seen it");
        }
    }

    /** The first few differences, for a failure message that can be acted on. */
    [[nodiscard]] static juce::String describe (const std::vector<Difference>& differences)
    {
        juce::String text;

        const auto shown = std::min<std::size_t> (differences.size(), 6);

        for (std::size_t index = 0; index < shown; ++index)
        {
            const auto& difference = differences[index];

            text << "\n      " << juce::String (difference.what)
                 << ": " << juce::String (difference.golden, 3)
                 << " -> " << juce::String (difference.measured, 3)
                 << " dB (" << juce::String (difference.delta, 3) << ")";
        }

        if (shown < differences.size())
            text << "\n      ... and " << juce::String (static_cast<int> (differences.size() - shown))
                 << " more";

        return text;
    }
};

RegressionTests regressionTests;

} // namespace
