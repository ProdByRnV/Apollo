/*
    The master gain, and the envelopes' and LFOs' own controls, as matrix
    destinations (Phase 19b, 19d; ADR-0131, ADR-0132).

    What these hold:

      - the destinations are appended, every released number where it was, and
        each names the knob it moves, on that knob's own curve;
      - an offset moves a time along its knob, so a quarter of the travel is a
        quarter of the travel whatever the time is set to, and none moves
        nothing at all;
      - a stage whose time or end moves while it runs carries on along its
        curve rather than starting it again — the property that makes an LFO
        on a decay sound like a decay;
      - in a voice, velocity shortens a note's attack and lengthens its delay,
        from the note's first sample;
      - an LFO's rate and phase move with the matrix and go back when it stops;
      - through the processor, a constant routed to the master sounds the same
        as setting the master there, and a routing to it ducks the output.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Envelopes/Envelope.h"
#include "DSP/LFO/Lfo.h"
#include "DSP/Modulation/GeneratorModulation.h"
#include "DSP/Modulation/ModulationTypes.h"
#include "Engine/VoiceEngine.h"
#include "Parameters/ModulationTargets.h"
#include "Parameters/ParameterDefinitions.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;

using Destination = dsp::ModDestination;
using Source = dsp::ModSource;

[[nodiscard]] const params::ParameterDefinition& definition (std::string_view id)
{
    const auto index = params::indexOfParameter (id);
    jassert (index >= 0);
    return params::parameterDefinitions[static_cast<std::size_t> (index < 0 ? 0 : index)];
}

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Samples until @p envelope leaves @p stage, up to @p limit. */
[[nodiscard]] int samplesIn (dsp::Envelope& envelope, dsp::EnvelopeStage stage, int limit)
{
    int count = 0;

    while (envelope.getStage() == stage && count < limit)
    {
        static_cast<void> (envelope.getNextValue());
        ++count;
    }

    return count;
}

/** The largest step between neighbouring samples of @p envelope over @p count. */
[[nodiscard]] float largestStep (dsp::Envelope& envelope, int count,
                                 const std::function<void (int)>& eachSample)
{
    auto previous = envelope.getCurrentValue();
    auto worst = 0.0f;

    for (int i = 0; i < count; ++i)
    {
        eachSample (i);
        const auto value = envelope.getNextValue();
        worst = std::max (worst, std::abs (value - previous));
        previous = value;
    }

    return worst;
}

/** One voice of an engine, rendered for @p samples; returns the output's left. */
[[nodiscard]] std::vector<float> renderEngine (engine::VoiceEngine& voiceEngine, int samples)
{
    std::vector<float> left (static_cast<std::size_t> (samples)), right (static_cast<std::size_t> (samples));
    float* channels[2] = { left.data(), right.data() };

    for (int start = 0; start < samples; start += block)
        voiceEngine.render (channels, 2, start, std::min (block, samples - start));

    return left;
}

[[nodiscard]] int firstSound (const std::vector<float>& signal)
{
    for (std::size_t i = 0; i < signal.size(); ++i)
        if (std::abs (signal[i]) > 1.0e-6f)
            return static_cast<int> (i);

    return -1;
}

/** Samples until voice 0's amplitude envelope first reaches full level. */
[[nodiscard]] int samplesToPeak (engine::VoiceEngine& voiceEngine, int limit)
{
    std::vector<float> left (16), right (16);
    float* channels[2] = { left.data(), right.data() };

    for (int done = 0; done < limit; done += 16)
    {
        if (voiceEngine.getVoice (0).getEnvelopeValue (0) >= 0.999f)
            return done;

        voiceEngine.render (channels, 2, 0, 16);
    }

    return limit;
}

/** A note through the processor and its tail, left channel. */
[[nodiscard]] std::vector<float> renderProcessor (const std::function<void (ApolloAudioProcessor&)>& configure)
{
    ApolloAudioProcessor processor;
    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_release", 20.0f);
    configure (processor);
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < 20; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        if (b == 12)
            midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

[[nodiscard]] double rms (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;

    for (auto i = from; i < to && i < signal.size(); ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

    return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, to - from)));
}

class GeneratorModulationTests final : public juce::UnitTest
{
public:
    GeneratorModulationTests()
        : juce::UnitTest ("The master, envelopes and LFOs as destinations", "Engine")
    {
    }

    void runTest() override
    {
        testTheList();
        testTheCurvesAreTheKnobs();
        testAlongTheTravel();
        testModulatedEnvelope();
        testAStageCarriesOn();
        testAStageMovedEverySixteenSamples();
        testVelocityOnAnAttack();
        testVelocityOnADelay();
        testLfoRateAndPhase();
        testAVoiceLetsGo();
        testTheMaster();
    }

private:
    void testTheList()
    {
        beginTest ("The master, the envelopes and the LFOs are appended, and nothing released moved");

        expectEquals (static_cast<int> (Destination::macro4), 34, "a released number moved");
        expectEquals (static_cast<int> (Destination::masterGain), 35);
        expectEquals (static_cast<int> (Destination::envelope1Delay), 36);
        expectEquals (static_cast<int> (Destination::envelope4Release), 59);
        expectEquals (static_cast<int> (Destination::lfo1Rate), 60);
        expectEquals (static_cast<int> (Destination::lfo4Phase), 67);
        expectEquals (static_cast<int> (Destination::count), 68);

        expect (dsp::envelopeDestination (2, dsp::EnvelopeTarget::sustain) == Destination::envelope3Sustain);
        expect (dsp::lfoRateDestination (3) == Destination::lfo4Rate);
        expect (dsp::lfoPhaseDestination (1) == Destination::lfo2Phase);

        expectEquals (dsp::fullDepthRange (Destination::masterGain), 66.0f, "the master's whole travel");
        expectEquals (dsp::fullDepthRange (Destination::envelope2Attack), 1.0f);
        expectEquals (dsp::fullDepthRange (Destination::lfo3Phase), 1.0f);

        // Every slot can name the last of them, and no further.
        const auto& slot = definition ("mod16_destination");
        expectEquals (slot.maximum, static_cast<float> (static_cast<int> (Destination::count) - 1));

        // Each names its knob, which says it can be modulated.
        const auto check = [this] (Destination destination, std::string_view id)
        {
            expect (params::parameterForDestination (destination) == id,
                    "destination " + juce::String (static_cast<int> (destination)) + " is not "
                        + juce::String (std::string (id)));
            expect (definition (id).modulatable, juce::String (std::string (id)) + " is not modulatable");
        };

        check (Destination::masterGain, "master_gain");
        check (Destination::envelope1Attack, "env1_attack");
        check (Destination::envelope4Sustain, "env4_sustain");
        check (Destination::envelope3Hold, "env3_hold");
        check (Destination::lfo2Rate, "lfo2_rate");
        check (Destination::lfo4Phase, "lfo4_phase");
    }

    void testTheCurvesAreTheKnobs()
    {
        beginTest ("The curves the DSP moves along are the registry's");

        for (int e = 1; e <= 4; ++e)
        {
            const auto prefix = "env" + std::to_string (e) + "_";

            for (const auto& [stage, maximum] : { std::pair { "delay", dsp::envelopeShortStageSeconds },
                                                  std::pair { "hold", dsp::envelopeShortStageSeconds },
                                                  std::pair { "attack", dsp::envelopeLongStageSeconds },
                                                  std::pair { "decay", dsp::envelopeLongStageSeconds },
                                                  std::pair { "release", dsp::envelopeLongStageSeconds } })
            {
                const auto& knob = definition (prefix + stage);
                expectEquals (knob.minimum, 0.0f);
                expectEquals (knob.maximum, maximum * 1000.0f, juce::String (prefix + stage));
                expectEquals (knob.skew, dsp::envelopeTimeSkew, juce::String (prefix + stage));
            }

            const auto& sustain = definition (prefix + "sustain");
            expectEquals (sustain.minimum, 0.0f);
            expectEquals (sustain.maximum, 1.0f);
            expectEquals (sustain.skew, 1.0f);
        }

        for (int l = 1; l <= 4; ++l)
        {
            const auto& knob = definition ("lfo" + std::to_string (l) + "_rate");
            expectEquals (knob.minimum, dsp::lfoRateMinimumHz);
            expectEquals (knob.maximum, dsp::lfoRateMaximumHz);
            expectEquals (knob.skew, dsp::lfoRateSkew);
        }
    }

    void testAlongTheTravel()
    {
        beginTest ("An offset moves a value along its knob, on the knob's curve");

        // None moves nothing, bit for bit, and nonsense moves nothing.
        expectEquals (dsp::offsetAlongTravel (0.123f, 0.0f, 10.0f, 0.25f, 0.0f), 0.123f);
        expectEquals (dsp::offsetAlongTravel (0.123f, 0.0f, 10.0f, 0.25f, std::nanf ("")), 0.123f);

        // The whole travel, from either end, and clamped beyond it.
        expectWithinAbsoluteError (dsp::offsetAlongTravel (0.0f, 0.0f, 10.0f, 0.25f, 1.0f), 10.0f, 1.0e-4f);
        expectWithinAbsoluteError (dsp::offsetAlongTravel (10.0f, 0.0f, 10.0f, 0.25f, -1.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (dsp::offsetAlongTravel (5.0f, 0.0f, 10.0f, 0.25f, 3.0f), 10.0f, 1.0e-4f);

        // Half the travel of a 10 s attack knob is 625 ms, as the knob reads:
        // 10 s × 0.5⁴.
        expectWithinAbsoluteError (dsp::offsetAlongTravel (0.0f, 0.0f, 10.0f, 0.25f, 0.5f), 0.625f, 1.0e-4f);

        // And the same fraction is the same movement wherever it starts: a
        // quarter up from a quarter is where the knob reads at half.
        const auto quarter = dsp::offsetAlongTravel (0.0f, 0.0f, 10.0f, 0.25f, 0.25f);
        expectWithinAbsoluteError (dsp::offsetAlongTravel (quarter, 0.0f, 10.0f, 0.25f, 0.25f), 0.625f, 1.0e-4f);

        // A rate knob that does not start at zero.
        const auto top = dsp::offsetAlongTravel (1.0f, dsp::lfoRateMinimumHz, dsp::lfoRateMaximumHz,
                                                 dsp::lfoRateSkew, 1.0f);
        expectWithinAbsoluteError (top, dsp::lfoRateMaximumHz, 0.01f);

        const auto bottom = dsp::offsetAlongTravel (1.0f, dsp::lfoRateMinimumHz, dsp::lfoRateMaximumHz,
                                                    dsp::lfoRateSkew, -1.0f);
        expectWithinAbsoluteError (bottom, dsp::lfoRateMinimumHz, 1.0e-6f);
    }

    void testModulatedEnvelope()
    {
        beginTest ("An envelope with no offsets is itself, and one with them moves only what was routed");

        dsp::EnvelopeSettings base;
        base.delaySeconds = 0.01f;
        base.attackSeconds = 0.2f;
        base.holdSeconds = 0.05f;
        base.decaySeconds = 0.4f;
        base.sustainLevel = 0.6f;
        base.releaseSeconds = 0.7f;
        base.curve = 0.3f;
        base.attackBend = 0.2f;

        dsp::ModDestinationValues none {};
        expect (dsp::modulatedEnvelope (base, 1, none) == base, "no offsets changed the envelope");

        dsp::ModDestinationValues offsets {};
        offsets[static_cast<std::size_t> (Destination::envelope2Attack)] = -0.2f;
        offsets[static_cast<std::size_t> (Destination::envelope2Sustain)] = 0.7f;
        offsets[static_cast<std::size_t> (Destination::envelope1Decay)] = 0.5f;   // another envelope's

        const auto moved = dsp::modulatedEnvelope (base, 1, offsets);

        expect (moved.attackSeconds < base.attackSeconds, "the attack did not shorten");
        expectEquals (moved.sustainLevel, 1.0f, "the sustain is a level, held to 1");
        expectEquals (moved.decaySeconds, base.decaySeconds, "envelope 1's offset reached envelope 2");
        expectEquals (moved.delaySeconds, base.delaySeconds);
        expectEquals (moved.curve, base.curve, "the curve is the base's");
        expectEquals (moved.attackBend, base.attackBend);

        // Nothing can make a time negative or past its knob.
        offsets[static_cast<std::size_t> (Destination::envelope2Attack)] = -5.0f;
        offsets[static_cast<std::size_t> (Destination::envelope2Release)] = 5.0f;

        const auto pinned = dsp::modulatedEnvelope (base, 1, offsets);
        expectEquals (pinned.attackSeconds, 0.0f);
        expectWithinAbsoluteError (pinned.releaseSeconds, dsp::envelopeLongStageSeconds, 1.0e-3f);
    }

    void testAStageCarriesOn()
    {
        beginTest ("A stage whose time or end moves carries on along its curve, without a jump");

        dsp::EnvelopeSettings settings;
        settings.attackSeconds = 0.0f;
        settings.decaySeconds = 1.0f;
        settings.sustainLevel = 0.0f;
        settings.curve = 0.5f;

        // A decay halved a third of the way through finishes in half of the
        // two thirds that were left.
        dsp::Envelope envelope;
        envelope.prepare (rate);
        envelope.setSettings (settings);
        envelope.noteOn();

        for (int i = 0; i < static_cast<int> (rate / 3.0); ++i)
            static_cast<void> (envelope.getNextValue());

        const auto before = envelope.getCurrentValue();
        settings.decaySeconds = 0.5f;
        envelope.setSettings (settings);

        expectEquals (envelope.getCurrentValue(), before, "a retimed stage jumped");

        const auto left = samplesIn (envelope, dsp::EnvelopeStage::decay, static_cast<int> (rate));
        expectWithinAbsoluteError (left, static_cast<int> (rate / 3.0), 3, "the rest of the decay at its new length");

        // A sustain moved while the decay runs: no step, and it lands there.
        settings.decaySeconds = 0.4f;
        settings.sustainLevel = 0.2f;

        dsp::Envelope moving;
        moving.prepare (rate);
        moving.setSettings (settings);
        moving.noteOn();

        const auto step = largestStep (moving, static_cast<int> (0.5 * rate), [&] (int i)
        {
            if (i % 16 == 0 && i < static_cast<int> (0.3 * rate))
            {
                auto changed = settings;
                changed.sustainLevel = 0.2f + 0.3f * std::sin (static_cast<float> (i) * 0.001f);
                moving.setSettings (changed);
            }
        });

        logMessage ("  largest step with the sustain moving: " + juce::String (step, 6));
        expect (step < 0.002f, "a moving sustain made the decay step by " + juce::String (step));
        expect (moving.getStage() == dsp::EnvelopeStage::sustain);
    }

    void testAStageMovedEverySixteenSamples()
    {
        beginTest ("A curved decay told its time every sixteen samples takes that time");

        // The matrix tells an envelope its time every control block. A stage
        // that started its curve again each time would always be at the steep
        // start of it, and a one-second analog decay would be over in a
        // fraction of that.
        dsp::EnvelopeSettings settings;
        settings.attackSeconds = 0.0f;
        settings.decaySeconds = 1.0f;
        settings.sustainLevel = 0.0f;
        settings.curve = 1.0f;

        dsp::Envelope still;
        still.prepare (rate);
        still.setSettings (settings);
        still.noteOn();

        dsp::Envelope told;
        told.prepare (rate);
        told.setSettings (settings);
        told.noteOn();

        auto worst = 0.0f;
        int sample = 0;

        while (still.getStage() == dsp::EnvelopeStage::decay && sample < static_cast<int> (2 * rate))
        {
            if (sample % 16 == 0)
            {
                // Equal in effect, unequal as numbers, so it is not discarded.
                auto nudged = settings;
                nudged.decaySeconds = (sample / 16) % 2 == 0 ? 1.0f : 1.0f + 1.0e-7f;
                told.setSettings (nudged);
            }

            worst = std::max (worst, std::abs (still.getNextValue() - told.getNextValue()));
            ++sample;
        }

        logMessage ("  worst difference from the untouched decay: " + juce::String (worst, 6));
        expect (worst < 1.0e-3f, "told its own time, the decay followed a different curve: "
                                     + juce::String (worst));
    }

    void testVelocityOnAnAttack()
    {
        beginTest ("Velocity on an attack shortens the hard notes' attack, from their first sample");

        const auto peakAfter = [] (float velocity)
        {
            engine::VoiceEngine voiceEngine;
            voiceEngine.prepare (rate);
            voiceEngine.setPolyphony (1);

            dsp::EnvelopeSettings amplitude;
            amplitude.attackSeconds = 1.0f;
            amplitude.curve = 0.0f;
            voiceEngine.setAmplitudeEnvelope (amplitude);

            dsp::ModulationRouting routing;
            routing.slots[0] = { Source::velocity, Destination::envelope1Attack, -0.5f };
            voiceEngine.setModulationRouting (routing);

            voiceEngine.noteOn (60, velocity);
            return samplesToPeak (voiceEngine, static_cast<int> (2 * rate));
        };

        const auto soft = peakAfter (0.1f);
        const auto hard = peakAfter (1.0f);

        // A 1 s attack sits at 0.1^(1/4) ≈ 0.562 of its 10 s knob. Half the
        // knob off at full velocity leaves 0.062 of it, 0.15 ms; a tenth of
        // the velocity takes 0.05 off, 0.512⁴ × 10 s ≈ 689 ms.
        logMessage ("  samples to full level: soft " + juce::String (soft) + ", hard " + juce::String (hard));

        expect (hard < static_cast<int> (0.01 * rate), "the hard note's attack was not short");
        expect (soft > static_cast<int> (0.5 * rate) && soft < static_cast<int> (0.9 * rate),
                "the soft note's attack was not about 689 ms");

        // The note's attack was its attack throughout: no long first block at
        // the unmodulated time.
        expect (hard <= 32, "the hard note began at the unmodulated attack");
    }

    void testVelocityOnADelay()
    {
        beginTest ("A delay set to nothing, lengthened by velocity, delays the note");

        const auto onset = [] (float depth)
        {
            engine::VoiceEngine voiceEngine;
            voiceEngine.prepare (rate);
            voiceEngine.setPolyphony (1);

            dsp::EnvelopeSettings amplitude;
            amplitude.delaySeconds = 0.0f;
            amplitude.attackSeconds = 0.0f;
            voiceEngine.setAmplitudeEnvelope (amplitude);

            dsp::ModulationRouting routing;
            routing.slots[0] = { Source::velocity, Destination::envelope1Delay, depth };
            voiceEngine.setModulationRouting (routing);

            voiceEngine.noteOn (60, 1.0f);
            return firstSound (renderEngine (voiceEngine, static_cast<int> (rate)));
        };

        const auto plain = onset (0.0f);
        const auto delayed = onset (0.5f);

        // Half a 2 s delay knob is 125 ms.
        logMessage ("  first sound: plain " + juce::String (plain) + ", delayed " + juce::String (delayed));
        expect (plain >= 0 && plain < 8, "an undelayed note was late");
        expectWithinAbsoluteError (delayed, static_cast<int> (0.125 * rate), 32, "the delay was not 125 ms");
    }

    void testLfoRateAndPhase()
    {
        beginTest ("An LFO's rate and phase move with the matrix, and back");

        dsp::Lfo lfo;
        lfo.prepare (rate);

        dsp::LfoSettings settings;
        settings.shape = dsp::LfoShape::saw;
        settings.rateHz = 1.0f;
        lfo.setSettings (settings);
        lfo.noteOn (0.0);

        const auto cyclesIn = [&lfo] (int samples)
        {
            int wraps = 0;
            auto previous = lfo.getNextValue();

            for (int i = 1; i < samples; ++i)
            {
                const auto value = lfo.getNextValue();
                if (value < previous - 1.0f)
                    ++wraps;
                previous = value;
            }

            return wraps;
        };

        // A third of the rate knob up from 1 Hz.
        const auto expected = dsp::offsetAlongTravel (1.0f, dsp::lfoRateMinimumHz, dsp::lfoRateMaximumHz,
                                                      dsp::lfoRateSkew, 0.3f);
        lfo.setModulation (0.3f, 0.0f);

        const auto faster = cyclesIn (static_cast<int> (rate));
        logMessage ("  1 Hz moved 0.3 of its knob runs at " + juce::String (expected, 2) + " Hz: "
                    + juce::String (faster) + " cycles in a second");
        expect (expected > 4.0f, "a third of the knob should be several hertz");
        expectWithinAbsoluteError (faster, static_cast<int> (expected), 1);

        // A control moved meanwhile keeps the offset on top of it.
        settings.rateHz = 0.5f;
        lfo.setSettings (settings);
        const auto stillMoved = cyclesIn (static_cast<int> (rate));
        expect (stillMoved > 1, "a moved rate knob dropped the modulation");

        lfo.setModulation (0.0f, 0.0f);
        expectWithinAbsoluteError (cyclesIn (4 * static_cast<int> (rate)), 2, 1, "back to 0.5 Hz");

        // A quarter cycle of phase reads a sine a quarter on, without moving
        // the phase that runs.
        dsp::Lfo sine;
        sine.prepare (rate);
        dsp::LfoSettings sineSettings;
        sineSettings.rateHz = 1.0f;
        sine.setSettings (sineSettings);
        sine.noteOn (0.0);

        const auto phaseBefore = sine.getPhase();
        sine.setModulation (0.0f, 0.25f);
        expectWithinAbsoluteError (sine.getNextValue(), 1.0f, 1.0e-4f, "a quarter on, a sine reads its top");
        expect (sine.getPhase() > phaseBefore, "the phase still runs");

        // A shift that is not a number shifts nothing: it reads as an LFO
        // never shifted at all.
        dsp::Lfo plain;
        plain.prepare (rate);
        plain.setSettings (sineSettings);
        plain.noteOn (0.0);

        dsp::Lfo nonsense;
        nonsense.prepare (rate);
        nonsense.setSettings (sineSettings);
        nonsense.noteOn (0.0);
        nonsense.setModulation (std::nanf (""), std::nanf (""));

        for (int i = 0; i < 100; ++i)
            expectEquals (nonsense.getNextValue(), plain.getNextValue());
    }

    void testAVoiceLetsGo()
    {
        beginTest ("A routing removed gives an envelope and an LFO back exactly as set");

        engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (rate);
        voiceEngine.setPolyphony (1);

        dsp::EnvelopeSettings amplitude;
        amplitude.attackSeconds = 0.3f;
        voiceEngine.setAmplitudeEnvelope (amplitude);

        dsp::ModulationRouting routing;
        routing.slots[0] = { Source::velocity, Destination::envelope1Attack, -0.5f };
        voiceEngine.setModulationRouting (routing);

        voiceEngine.noteOn (60, 1.0f);
        static_cast<void> (renderEngine (voiceEngine, 64));

        expect (voiceEngine.getVoice (0).getAmplitudeEnvelope().getSettings().attackSeconds < 0.3f,
                "the routing did not reach the envelope");

        voiceEngine.setModulationRouting ({});
        expect (voiceEngine.getVoice (0).getAmplitudeEnvelope().getSettings() == amplitude,
                "with the routing gone, the envelope is not the one set");
    }

    void testTheMaster()
    {
        beginTest ("A constant on the master is the master set there, and a routing ducks the output");

        // Macro 1 at full, a quarter of the master's travel down: 16.5 dB.
        const auto routed = renderProcessor ([] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "macro1_value", 1.0f);
            setPlain (processor, "mod01_source", static_cast<float> (Source::macro1));
            setPlain (processor, "mod01_destination", static_cast<float> (Destination::masterGain));
            setPlain (processor, "mod01_depth", -0.25f);
        });

        const auto set = renderProcessor ([] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "master_gain", -16.5f);
        });

        const auto plain = renderProcessor ([] (ApolloAudioProcessor&) {});

        auto worst = 0.0f;
        for (std::size_t i = 0; i < std::min (routed.size(), set.size()); ++i)
            worst = std::max (worst, std::abs (routed[i] - set[i]));

        logMessage ("  routed against set: worst difference " + juce::String (worst, 9));
        expect (worst < 1.0e-5f, "a constant on the master did not sound like the master set there");

        const auto ratio = rms (routed, 0, 12 * block) / rms (plain, 0, 12 * block);
        expectWithinAbsoluteError (static_cast<float> (juce::Decibels::gainToDecibels (ratio)), -16.5f, 0.05f);

        // Pushed past the knob's range, it stops at the knob's end.
        const auto pinned = renderProcessor ([] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "macro1_value", 1.0f);
            setPlain (processor, "mod01_source", static_cast<float> (Source::macro1));
            setPlain (processor, "mod01_destination", static_cast<float> (Destination::masterGain));
            setPlain (processor, "mod01_depth", 1.0f);
        });

        const auto loudest = rms (pinned, 0, 12 * block) / rms (plain, 0, 12 * block);
        expectWithinAbsoluteError (static_cast<float> (juce::Decibels::gainToDecibels (loudest)), 6.0f, 0.05f,
                                   "the master went past +6 dB");

        // A moving source draws a line across each block rather than a step:
        // the mod wheel thrown from nothing to full at the start of the third
        // block. Its gain is read sample by sample against the same notes
        // played with the wheel left alone.
        const auto withWheel = [] (bool thrown)
        {
            ApolloAudioProcessor processor;
            setPlain (processor, "osc1_wavetable", 0.0f);
            setPlain (processor, "env1_attack", 0.0f);
            setPlain (processor, "mod01_source", static_cast<float> (Source::modWheel));
            setPlain (processor, "mod01_destination", static_cast<float> (Destination::masterGain));
            setPlain (processor, "mod01_depth", -0.5f);
            processor.prepareToPlay (rate, block);

            juce::AudioBuffer<float> buffer (2, block);
            std::vector<float> rendered;

            for (int b = 0; b < 4; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;

                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

                if (b == 2 && thrown)
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);

                processor.processBlock (buffer, midi);
                rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
            }

            return rendered;
        };

        const auto moved = withWheel (true);
        const auto still = withWheel (false);

        auto first = 0.0f, last = 0.0f, steepest = 0.0f, previous = 0.0f;
        auto seen = false;

        for (auto i = static_cast<std::size_t> (block); i < static_cast<std::size_t> (3 * block); ++i)
        {
            if (std::abs (still[i]) < 1.0e-3f)
                continue;

            const auto db = juce::Decibels::gainToDecibels (std::abs (moved[i] / still[i]), -200.0f);

            if (! seen && i >= static_cast<std::size_t> (2 * block))
            {
                first = db;
                seen = true;
            }

            if (i > static_cast<std::size_t> (block) && previous != 0.0f)
                steepest = std::max (steepest, std::abs (db - previous));

            previous = db;
            last = db;
        }

        logMessage ("  across the block the wheel moved in: " + juce::String (first, 2) + " dB to "
                    + juce::String (last, 2) + " dB, steepest step " + juce::String (steepest, 3) + " dB");

        expectWithinAbsoluteError (first, 0.0f, 0.5f, "the block did not start where the last ended");
        expectWithinAbsoluteError (last, -33.0f, 0.5f, "the block did not end where the routing put it");
        expect (steepest < 1.0f, "the master stepped by " + juce::String (steepest) + " dB");
    }
};

static GeneratorModulationTests generatorModulationTests;

} // namespace
