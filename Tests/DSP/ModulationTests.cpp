/*
    Modulation matrix tests.

    Two layers, tested separately because they fail differently.

    The routing arithmetic is pure: sources in, offsets out, no engine involved.
    That part is checked exhaustively — combination, inversion, inactive slots,
    and indices a corrupt preset could supply.

    The rest is checked through the engine, by rendering audio and measuring what
    changed, because the interesting failures are not arithmetic. A routing that
    evaluates perfectly and never reaches a voice, or reaches only the first
    voice, or leaves its offset applied after the note ends, produces correct
    numbers and wrong sound. Those are what the engine-level tests are for.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "DSP/Modulation/ModulationTypes.h"
#include "Engine/VoiceEngine.h"

using namespace apollo;
using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 512;

/** Renders @p numBlocks of the engine and returns the left channel. */
[[nodiscard]] std::vector<float> render (engine::VoiceEngine& voiceEngine, int numBlocks)
{
    std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);

    float* channels[2] = { left.data(), right.data() };

    std::vector<float> output;
    output.reserve (static_cast<std::size_t> (numBlocks * blockSize));

    for (int block = 0; block < numBlocks; ++block)
    {
        voiceEngine.render (channels, 2, 0, blockSize);
        output.insert (output.end(), left.begin(), left.end());
    }

    return output;
}

[[nodiscard]] float peakOf (const std::vector<float>& values, std::size_t from, std::size_t to)
{
    auto peak = 0.0f;

    for (auto i = from; i < std::min (to, values.size()); ++i)
        peak = std::max (peak, std::abs (values[i]));

    return peak;
}

/** Builds a routing with one slot filled in. */
[[nodiscard]] ModulationRouting oneRouting (ModSource source, ModDestination destination, float depth)
{
    ModulationRouting routing;

    routing.slots[0].source = source;
    routing.slots[0].destination = destination;
    routing.slots[0].depth = depth;

    return routing;
}

//==============================================================================

class ModulationTests final : public juce::UnitTest
{
public:
    ModulationTests()
        : juce::UnitTest ("Modulation matrix", "DSP")
    {
    }

    void runTest() override
    {
        testDepthScalesBySourceAndRange();
        testContributionsAdd();
        testInactiveSlotsContributeNothing();
        testCorruptIndicesAreIgnored();
        testRoutingReachesEveryVoice();
        testBaseParametersAreNotCorrupted();
        testModulationStopsWithTheNote();
        testEnvelopeIsPredictableAcrossPolyphony();
        testControllersReachTheEngine();
        testModulatedCutoffHasNoZipper();
        testExtremeDepthsStayBounded();
    }

private:
    void testDepthScalesBySourceAndRange()
    {
        beginTest ("An offset is the source, the depth and the destination's range");

        ModSourceValues sources {};
        ModDestinationValues destinations {};

        sources[static_cast<std::size_t> (ModSource::lfo1)] = 1.0f;

        // Pitch's full-depth range is two octaves, so a full-depth source at
        // full deflection is 24 semitones.
        evaluateModulation (oneRouting (ModSource::lfo1, ModDestination::allPitch, 1.0f),
                            sources, destinations);

        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::allPitch)],
                                   24.0f, 1.0e-4f);

        // Half the depth is half the offset.
        evaluateModulation (oneRouting (ModSource::lfo1, ModDestination::allPitch, 0.5f),
                            sources, destinations);

        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::allPitch)],
                                   12.0f, 1.0e-4f);

        // A negative depth inverts the source, which is what bipolar depth is
        // for (CLAUDE.md §15).
        evaluateModulation (oneRouting (ModSource::lfo1, ModDestination::allPitch, -1.0f),
                            sources, destinations);

        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::allPitch)],
                                   -24.0f, 1.0e-4f);

        // A destination measured differently gets its own range: level runs 0
        // to 1, so full depth is 1 rather than 24.
        evaluateModulation (oneRouting (ModSource::lfo1, ModDestination::osc1Level, 1.0f),
                            sources, destinations);

        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::osc1Level)],
                                   1.0f, 1.0e-4f);
    }

    void testContributionsAdd()
    {
        beginTest ("Several sources can target one destination safely");

        ModSourceValues sources {};
        ModDestinationValues destinations {};

        sources[static_cast<std::size_t> (ModSource::lfo1)] = 1.0f;
        sources[static_cast<std::size_t> (ModSource::envelope2)] = 0.5f;
        sources[static_cast<std::size_t> (ModSource::velocity)] = 0.25f;

        ModulationRouting routing;

        routing.slots[0] = { ModSource::lfo1, ModDestination::filter1Cutoff, 0.5f };
        routing.slots[1] = { ModSource::envelope2, ModDestination::filter1Cutoff, 0.5f };
        routing.slots[2] = { ModSource::velocity, ModDestination::filter1Cutoff, 1.0f };

        evaluateModulation (routing, sources, destinations);

        // Cutoff's range is eight octaves: 1*0.5*8 + 0.5*0.5*8 + 0.25*1*8.
        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::filter1Cutoff)],
                                   4.0f + 2.0f + 2.0f, 1.0e-4f,
                                   "contributions to one destination must add");

        // Opposite depths cancel, which is the same rule seen from the other
        // side and is what makes a matrix predictable.
        routing.slots[1].depth = -0.5f;
        evaluateModulation (routing, sources, destinations);

        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::filter1Cutoff)],
                                   4.0f - 2.0f + 2.0f, 1.0e-4f);
    }

    void testInactiveSlotsContributeNothing()
    {
        beginTest ("A slot with no source, no destination or no depth does nothing");

        ModSourceValues sources {};
        ModDestinationValues destinations {};

        sources.fill (1.0f);

        ModulationRouting routing;

        routing.slots[0] = { ModSource::none, ModDestination::filter1Cutoff, 1.0f };
        routing.slots[1] = { ModSource::lfo1, ModDestination::none, 1.0f };
        routing.slots[2] = { ModSource::lfo2, ModDestination::filter1Cutoff, 0.0f };

        evaluateModulation (routing, sources, destinations);

        for (const auto value : destinations)
            expectWithinAbsoluteError (value, 0.0f, 0.0f,
                                       "an inactive slot must contribute exactly nothing");
    }

    void testCorruptIndicesAreIgnored()
    {
        beginTest ("Out-of-range indices are refused rather than reinterpreted");

        ModSourceValues sources {};
        ModDestinationValues destinations {};

        sources.fill (1.0f);

        ModulationRouting routing;

        // What a hand-edited or newer-version preset could contain. These must
        // do nothing rather than land on whatever entry happens to sit there.
        routing.slots[0].source = static_cast<ModSource> (9999);
        routing.slots[0].destination = ModDestination::filter1Cutoff;
        routing.slots[0].depth = 1.0f;

        routing.slots[1].source = ModSource::lfo1;
        routing.slots[1].destination = static_cast<ModDestination> (9999);
        routing.slots[1].depth = 1.0f;

        evaluateModulation (routing, sources, destinations);

        for (const auto value : destinations)
            expect (std::isfinite (value), "a corrupt index produced a non-finite offset");

        expectWithinAbsoluteError (destinations[static_cast<std::size_t> (ModDestination::filter1Cutoff)],
                                   0.0f, 0.0f,
                                   "an unknown source must not modulate anything");
    }

    void testRoutingReachesEveryVoice()
    {
        beginTest ("A routing reaches every voice, not just the first");

        // An LFO on amplitude is a tremolo, which is measurable as a difference
        // between the loud and quiet parts of the same render.
        engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (testSampleRate);

        dsp::LfoSettings lfo;
        lfo.shape = LfoShape::sine;
        lfo.rateHz = 4.0f;
        lfo.retrigger = true;
        lfo.bipolar = true;

        voiceEngine.setLfoSettings (0, lfo);
        voiceEngine.setModulationRouting (oneRouting (ModSource::lfo1, ModDestination::amplitude, -1.0f));

        // Several voices at once: a routing that only reached voice zero would
        // still produce tremolo, just a much shallower one relative to the total.
        for (const auto note : { 48, 55, 60, 64, 67, 72 })
            voiceEngine.noteOn (note, 0.8f);

        const auto output = render (voiceEngine, 24);

        // The routing is a negative depth on a bipolar sine, so the amplitude
        // scale is 1 - lfo: silent where the LFO peaks and full where it
        // troughs. At 4 Hz a cycle is 12000 samples, so the peak is at 3000 and
        // the trough at 9000 — which is the opposite way round from how this
        // test first had it.
        const auto loud = peakOf (output, 8500, 9500);
        const auto quiet = peakOf (output, 2600, 3400);

        logMessage ("  tremolo across six voices: loud " + juce::String (loud, 4)
                    + ", quiet " + juce::String (quiet, 4));

        expect (loud > 0.0f, "the engine produced no sound at all");
        expect (quiet < loud * 0.5f,
                "the tremolo only reached part of the signal: loud " + juce::String (loud, 4)
                    + " against quiet " + juce::String (quiet, 4));
    }

    void testBaseParametersAreNotCorrupted()
    {
        beginTest ("Modulation offsets a value without writing to it");

        engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (testSampleRate);

        engine::FilterParameters filters;
        filters.filter1.mode = StateVariableFilter::Mode::lowpass;
        filters.filter1.cutoffHz = 800.0f;
        filters.filter1.q = 0.707f;
        voiceEngine.setFilterParameters (filters);

        engine::SourceParameters sourceParameters;
        sourceParameters.osc1.level = 0.75f;
        voiceEngine.setSourceParameters (sourceParameters);

        dsp::LfoSettings lfo;
        lfo.rateHz = 3.0f;
        voiceEngine.setLfoSettings (0, lfo);

        ModulationRouting routing;
        routing.slots[0] = { ModSource::lfo1, ModDestination::filter1Cutoff, 0.8f };
        routing.slots[1] = { ModSource::lfo1, ModDestination::osc1Level, 0.5f };
        voiceEngine.setModulationRouting (routing);

        voiceEngine.noteOn (60, 0.9f);
        static_cast<void> (render (voiceEngine, 16));

        // The parameters the user set must read back exactly as they were set,
        // however hard they have been modulated in the meantime. This is the
        // difference between a modulation matrix and a parameter editor.
        expectWithinAbsoluteError (voiceEngine.getFilterParameters().filter1.cutoffHz, 800.0f, 0.0f,
                                   "the base cutoff was written to");
        expectWithinAbsoluteError (voiceEngine.getFilterParameters().filter1.q, 0.707f, 0.0f,
                                   "the base resonance was written to");
        expectWithinAbsoluteError (voiceEngine.getSourceParameters().osc1.level, 0.75f, 0.0f,
                                   "the base level was written to");

        // And removing the routing must restore exactly the unmodulated sound,
        // rather than leaving the last offset frozen in place.
        voiceEngine.allNotesOff();
        static_cast<void> (render (voiceEngine, 40));

        voiceEngine.setModulationRouting ({});
        voiceEngine.noteOn (60, 0.9f);
        const auto unrouted = render (voiceEngine, 8);

        engine::VoiceEngine reference;
        reference.prepare (testSampleRate);
        reference.setFilterParameters (filters);
        reference.setSourceParameters (sourceParameters);
        reference.noteOn (60, 0.9f);
        const auto expected = render (reference, 8);

        auto worst = 0.0f;

        for (std::size_t i = 0; i < expected.size(); ++i)
            worst = std::max (worst, std::abs (unrouted[i] - expected[i]));

        expect (worst < 1.0e-6f,
                "removing the routing left the engine " + juce::String (worst, 8)
                    + " away from an engine that never had one");
    }

    void testModulationStopsWithTheNote()
    {
        beginTest ("A finished note leaves no modulation behind");

        engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (testSampleRate);

        dsp::EnvelopeSettings envelope;
        envelope.attackSeconds = 0.001f;
        envelope.decaySeconds = 0.001f;
        envelope.sustainLevel = 1.0f;
        envelope.releaseSeconds = 0.005f;
        voiceEngine.setAmplitudeEnvelope (envelope);

        voiceEngine.setModulationRouting (
            oneRouting (ModSource::envelope1, ModDestination::allPitch, 1.0f));

        voiceEngine.noteOn (60, 1.0f);
        static_cast<void> (render (voiceEngine, 8));

        voiceEngine.allNotesOff();
        const auto tail = render (voiceEngine, 40);

        // Once every voice has finished, the engine must be exactly silent. A
        // voice that kept its modulation applied would still be running an
        // oscillator two octaves up rather than having freed itself.
        expectWithinAbsoluteError (peakOf (tail, tail.size() - 2000, tail.size()), 0.0f, 1.0e-7f,
                                   "the engine did not return to silence");

        expectEquals (voiceEngine.getActiveVoiceCount(), 0,
                      "a modulated note must still free its voice");
    }

    void testEnvelopeIsPredictableAcrossPolyphony()
    {
        beginTest ("An envelope behaves the same however many voices are sounding");

        // Deliberately *not* tested by comparing a chord against the sum of its
        // notes played alone. Voices start from distinct phases on purpose so a
        // chord attack does not sum coherently (ADR-0017), so those two differ by
        // design and an equality test there would be asserting a bug.
        //
        // What must hold is that one voice's envelope follows the same
        // trajectory whatever else is sounding. That is measured directly, by
        // watching the voice.
        const auto trajectoryOf = [] (int numVoices)
        {
            engine::VoiceEngine voiceEngine;
            voiceEngine.prepare (testSampleRate);

            dsp::EnvelopeSettings envelope;
            envelope.attackSeconds = 0.050f;
            envelope.decaySeconds = 0.200f;
            envelope.sustainLevel = 0.4f;
            voiceEngine.setAmplitudeEnvelope (envelope);

            dsp::LfoSettings lfo;
            lfo.rateHz = 5.0f;
            voiceEngine.setLfoSettings (0, lfo);

            voiceEngine.setModulationRouting (
                oneRouting (ModSource::lfo1, ModDestination::osc1Level, 0.5f));

            // The voice under observation is started first, so it is voice zero
            // in every case and the comparison is like for like.
            voiceEngine.noteOn (60, 0.7f);

            for (int i = 1; i < numVoices; ++i)
                voiceEngine.noteOn (48 + i, 0.7f);

            std::vector<float> trajectory;

            for (int block = 0; block < 24; ++block)
            {
                static_cast<void> (render (voiceEngine, 1));
                trajectory.push_back (voiceEngine.getVoice (0).getEnvelopeLevel());
            }

            return trajectory;
        };

        const auto alone = trajectoryOf (1);

        // Capped at the default polyphony: asking for more notes than that steals
        // voices, and a stolen voice zero legitimately stops following its own
        // envelope. That is voice allocation working, not modulation failing.
        for (const auto polyphony : { 2, 8, 16 })
        {
            const auto crowded = trajectoryOf (polyphony);

            auto worst = 0.0f;

            for (std::size_t i = 0; i < alone.size(); ++i)
                worst = std::max (worst, std::abs (crowded[i] - alone[i]));

            logMessage ("  " + juce::String (polyphony) + " voices: envelope differs from"
                        " the solo case by " + juce::String (worst, 8));

            expect (worst < 1.0e-6f,
                    "with " + juce::String (polyphony) + " voices sounding, voice zero's envelope"
                    " differed from the solo case by " + juce::String (worst, 8));
        }
    }

    void testControllersReachTheEngine()
    {
        beginTest ("Mod wheel and aftertouch reach a sounding voice");

        for (const auto source : { ModSource::modWheel, ModSource::aftertouch })
        {
            engine::VoiceEngine voiceEngine;
            voiceEngine.prepare (testSampleRate);

            voiceEngine.setModulationRouting (oneRouting (source, ModDestination::amplitude, -1.0f));

            voiceEngine.noteOn (60, 0.9f);

            const auto before = peakOf (render (voiceEngine, 8), 1024, 4096);

            // Both are unipolar, so pushing them to full with a negative depth
            // should pull the amplitude down.
            if (source == ModSource::modWheel)
                voiceEngine.setModWheel (1.0f);
            else
                voiceEngine.setAftertouch (1.0f);

            const auto after = peakOf (render (voiceEngine, 8), 1024, 4096);

            logMessage ("  " + juce::String (source == ModSource::modWheel ? "mod wheel" : "aftertouch")
                        + ": " + juce::String (before, 4) + " -> " + juce::String (after, 4));

            expect (after < before * 0.5f,
                    "the controller did not reach the voice: " + juce::String (before, 4)
                        + " became " + juce::String (after, 4));
        }
    }

    void testModulatedCutoffHasNoZipper()
    {
        beginTest ("A cutoff swept by an LFO produces no stepping");

        // The Phase 5 exit criterion. Modulation is evaluated every sixteen
        // samples, so the question this answers is whether that leaves an
        // audible staircase. Measured as the largest sample-to-sample jump,
        // compared against the same patch with the modulation switched off.
        const auto worstStep = [] (bool modulated)
        {
            engine::VoiceEngine voiceEngine;
            voiceEngine.prepare (testSampleRate);

            engine::FilterParameters filters;
            filters.filter1.mode = StateVariableFilter::Mode::lowpass;
            filters.filter1.cutoffHz = 600.0f;
            filters.filter1.q = 4.0f;
            voiceEngine.setFilterParameters (filters);

            dsp::LfoSettings lfo;
            lfo.shape = LfoShape::triangle;
            lfo.rateHz = 6.0f;
            voiceEngine.setLfoSettings (0, lfo);

            if (modulated)
                voiceEngine.setModulationRouting (
                    oneRouting (ModSource::lfo1, ModDestination::filter1Cutoff, 0.6f));

            voiceEngine.noteOn (48, 0.9f);

            const auto output = render (voiceEngine, 16);

            auto largest = 0.0f;

            // Skipping the attack, which is a legitimate fast change.
            for (std::size_t i = 4001; i < output.size(); ++i)
                largest = std::max (largest, std::abs (output[i] - output[i - 1]));

            return largest;
        };

        const auto plain = worstStep (false);
        const auto swept = worstStep (true);

        logMessage ("  largest sample step: unmodulated " + juce::String (plain, 6)
                    + ", modulated " + juce::String (swept, 6));

        // A staircase would show up as steps far larger than the signal's own
        // slew. Allowing a factor of two covers the genuine extra movement a
        // resonant sweep produces without permitting a discontinuity.
        expect (swept < plain * 2.0f + 1.0e-4f,
                "the modulated sweep stepped by " + juce::String (swept, 6)
                    + " against an unmodulated " + juce::String (plain, 6)
                    + ", which is a zipper artefact");
    }

    void testExtremeDepthsStayBounded()
    {
        beginTest ("Hostile routings stay finite and bounded");

        engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (testSampleRate);

        engine::FilterParameters filters;
        filters.filter1.mode = StateVariableFilter::Mode::lowpass;
        filters.filter1.cutoffHz = 1000.0f;
        filters.filter1.q = 8.0f;
        voiceEngine.setFilterParameters (filters);

        engine::SourceParameters sourceParameters;
        sourceParameters.osc2.level = 1.0f;
        sourceParameters.subLevel = 1.0f;
        sourceParameters.noiseLevel = 1.0f;
        voiceEngine.setSourceParameters (sourceParameters);

        dsp::LfoSettings lfo;
        lfo.rateHz = 200.0f;
        voiceEngine.setLfoSettings (0, lfo);
        voiceEngine.setLfoSettings (1, lfo);

        // Every destination driven at once, at full depth, from fast LFOs.
        ModulationRouting routing;

        for (std::size_t i = 0; i < routing.slots.size(); ++i)
        {
            routing.slots[i].source = (i % 2 == 0) ? ModSource::lfo1 : ModSource::lfo2;
            routing.slots[i].destination =
                static_cast<ModDestination> (1 + (i % (static_cast<std::size_t> (ModDestination::count) - 1)));
            routing.slots[i].depth = (i % 2 == 0) ? 1.0f : -1.0f;
        }

        voiceEngine.setModulationRouting (routing);

        for (int note = 36; note < 36 + 24; ++note)
            voiceEngine.noteOn (note, 1.0f);

        const auto output = render (voiceEngine, 24);

        for (const auto value : output)
            expect (std::isfinite (value), "an extreme routing produced a non-finite sample");

        const auto peak = peakOf (output, 0, output.size());

        logMessage ("  peak with every destination at full depth: " + juce::String (peak, 3));

        expect (peak < 8.0f,
                "an extreme routing peaked at " + juce::String (peak, 3)
                    + ", which is running away rather than modulating");
    }
};

ModulationTests modulationTests;

} // namespace
