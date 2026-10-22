/*
    Effect parameters as matrix destinations (Phase 13e-5, PRD §16).

    The effects run once, on the summed voices, so the question these answer is
    which value of a per-voice source they get:

      - while notes sound, the newest one's: velocity routed to the reverb's
        mix follows the last note played;
      - when nothing sounds, the engine-wide sources alone — a macro and the
        mod wheel still reach the effects, and a per-note source contributes
        nothing;
      - with no routing, nothing moves at all;
      - and through the processor, a constant routed to an effect control
        sounds the same as setting the control there, across a note and its
        tail, including where the sum would pass the control's range.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Modulation/ModulationTypes.h"
#include "Engine/VoiceEngine.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;

using Destination = dsp::ModDestination;
using Source = dsp::ModSource;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Renders @p blocks of an engine, and works out the effect offsets after. */
void advance (engine::VoiceEngine& voiceEngine, int blocks)
{
    std::vector<float> left (block), right (block);
    float* channels[2] = { left.data(), right.data() };

    for (int b = 0; b < blocks; ++b)
        voiceEngine.render (channels, 2, 0, block);

    voiceEngine.updateEffectModulation();
}

void prepared (engine::VoiceEngine& voiceEngine, Source source,
              Destination destination, float depth)
{
    voiceEngine.prepare (rate);

    dsp::ModulationRouting routing;
    routing.slots[0] = { source, destination, depth };
    voiceEngine.setModulationRouting (routing);
}

/** A short note through the rack, then its tail, left channel. */
[[nodiscard]] std::vector<float> render (const std::function<void (ApolloAudioProcessor&)>& configure)
{
    ApolloAudioProcessor processor;
    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "osc1_position", 1.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_release", 20.0f);
    configure (processor);
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < 60; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        if (b == 8)
            midi.addEvent (juce::MidiMessage::noteOff (1, 57), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

[[nodiscard]] float worstDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    auto worst = 0.0f;

    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));

    return worst;
}

[[nodiscard]] double energyAfter (const std::vector<float>& signal, std::size_t from)
{
    double sum = 0.0;

    for (auto i = from; i < signal.size(); ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

    return sum;
}

/** Macro 1 at full, routed to @p destination at @p depth. */
void macroTo (ApolloAudioProcessor& processor, Destination destination, float depth)
{
    setPlain (processor, "macro1_value", 1.0f);
    setPlain (processor, "mod01_source", static_cast<float> (Source::macro1));
    setPlain (processor, "mod01_destination", static_cast<float> (destination));
    setPlain (processor, "mod01_depth", depth);
}

class EffectModulationTests final : public juce::UnitTest
{
public:
    EffectModulationTests()
        : juce::UnitTest ("Effect parameters as destinations", "Engine")
    {
    }

    void runTest() override
    {
        testTheNewestNoteLeads();
        testWithNothingSounding();
        testNoRoutingNoOffset();
        testRoutedDelayMixIsTheMix();
        testRoutedDriveIsTheDrive();
    }

private:
    void testTheNewestNoteLeads()
    {
        beginTest ("While notes sound, the effects follow the newest one");

        engine::VoiceEngine voiceEngine;
        prepared (voiceEngine, Source::velocity, Destination::fxReverbMix, 1.0f);

        voiceEngine.noteOn (60, 0.25f);
        advance (voiceEngine, 2);
        const auto first = voiceEngine.getEffectOffset (Destination::fxReverbMix);

        voiceEngine.noteOn (64, 0.75f);
        advance (voiceEngine, 2);
        const auto second = voiceEngine.getEffectOffset (Destination::fxReverbMix);

        logMessage ("  reverb mix offset: after a note at 0.25, " + juce::String (first, 4)
                    + "; after one at 0.75 joins it, " + juce::String (second, 4));

        expectWithinAbsoluteError (first, 0.25f, 1.0e-6f, "the only note's velocity");
        expectWithinAbsoluteError (second, 0.75f, 1.0e-6f, "the newest note's velocity");

        // Velocity belongs to a note, so once both have finished it reaches
        // nothing.
        voiceEngine.allNotesOff();
        advance (voiceEngine, 100);
        expectEquals (voiceEngine.getEffectOffset (Destination::fxReverbMix), 0.0f,
                      "a per-note source still reached the effects with nothing sounding");
    }

    void testWithNothingSounding()
    {
        beginTest ("With nothing sounding, the engine-wide sources still reach the effects");

        engine::VoiceEngine macro;
        prepared (macro, Source::macro1, Destination::fxReverbMix, 0.5f);
        macro.setMacro (0, 0.8f);
        advance (macro, 1);
        expectWithinAbsoluteError (macro.getEffectOffset (Destination::fxReverbMix), 0.4f, 1.0e-6f, "a macro");

        engine::VoiceEngine wheel;
        prepared (wheel, Source::modWheel, Destination::fxDelayMix, 1.0f);
        wheel.setModWheel (0.5f);
        advance (wheel, 1);
        expectWithinAbsoluteError (wheel.getEffectOffset (Destination::fxDelayMix), 0.5f, 1.0e-6f, "the mod wheel");

        engine::VoiceEngine drive;
        prepared (drive, Source::macro1, Destination::fxDistortionDrive, 0.5f);
        drive.setMacro (0, 1.0f);
        advance (drive, 1);
        expectWithinAbsoluteError (drive.getEffectOffset (Destination::fxDistortionDrive), 18.0f, 1.0e-5f,
                                   "drive is not in decibels, 36 at full depth");
    }

    void testNoRoutingNoOffset()
    {
        beginTest ("With no routing, no effect control moves");

        engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (rate);
        voiceEngine.setMacro (0, 1.0f);
        voiceEngine.setModWheel (1.0f);
        voiceEngine.noteOn (60, 1.0f);
        advance (voiceEngine, 2);

        for (int d = static_cast<int> (dsp::firstEffectDestination); d < static_cast<int> (Destination::count); ++d)
            expectEquals (voiceEngine.getEffectOffset (static_cast<Destination> (d)), 0.0f,
                          "destination " + juce::String (d) + " moved");

        expectEquals (voiceEngine.getEffectOffset (Destination::osc1Level), 0.0f, "a voice destination answered");
    }

    void testRoutedDelayMixIsTheMix()
    {
        beginTest ("A constant routed to the delay's mix sounds as the mix set there");

        const auto delay = [] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "fx_slot1", 2.0f);
            setPlain (processor, "fx_delay_time", 150.0f);
            setPlain (processor, "fx_delay_feedback", 0.5f);
        };

        const auto routed = render ([&] (ApolloAudioProcessor& p) { delay (p); macroTo (p, Destination::fxDelayMix, 0.6f); });
        const auto set = render ([&] (ApolloAudioProcessor& p) { delay (p); setPlain (p, "fx_delay_mix", 0.6f); });
        const auto dry = render (delay);

        // Over a range: a mix of 0.8 and an offset of 0.6 is a mix of 1.
        const auto over = render ([&] (ApolloAudioProcessor& p) { delay (p); setPlain (p, "fx_delay_mix", 0.8f);
                                                                   macroTo (p, Destination::fxDelayMix, 0.6f); });
        const auto full = render ([&] (ApolloAudioProcessor& p) { delay (p); setPlain (p, "fx_delay_mix", 1.0f); });

        const auto tail = static_cast<std::size_t> (12 * block);

        logMessage ("  routed against set: worst " + juce::String (worstDifference (routed, set), 9)
                    + "; past the range against full: " + juce::String (worstDifference (over, full), 9)
                    + "; tail energy dry " + juce::String (energyAfter (dry, tail), 4)
                    + ", delayed " + juce::String (energyAfter (set, tail), 4));

        expect (energyAfter (set, tail) > energyAfter (dry, tail) * 10.0 + 1.0e-3, "the delay made no tail to compare");
        expect (worstDifference (routed, set) < 1.0e-6f, "a routed mix is not the mix");
        expect (worstDifference (over, full) < 1.0e-6f, "an offset past the range is not the range's end");
    }

    void testRoutedDriveIsTheDrive()
    {
        beginTest ("A constant routed to the distortion's drive sounds as the drive set there");

        const auto distortion = [] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "fx_slot1", 1.0f);
            setPlain (processor, "fx_distortion_mix", 1.0f);
            setPlain (processor, "fx_distortion_drive", 6.0f);
        };

        // 6 dB, and half of 36 on top: 24 dB.
        const auto routed = render ([&] (ApolloAudioProcessor& p) { distortion (p); macroTo (p, Destination::fxDistortionDrive, 0.5f); });
        const auto set = render ([&] (ApolloAudioProcessor& p) { distortion (p); setPlain (p, "fx_distortion_drive", 24.0f); });
        const auto unmoved = render (distortion);

        logMessage ("  routed drive against set: worst " + juce::String (worstDifference (routed, set), 9)
                    + "; 24 dB against 6 dB: " + juce::String (worstDifference (set, unmoved), 4));

        expect (worstDifference (set, unmoved) > 0.01f, "the drive did nothing to compare against");
        expect (worstDifference (routed, set) < 1.0e-6f, "a routed drive is not the drive");
    }
};

EffectModulationTests effectModulationTests;

} // namespace
