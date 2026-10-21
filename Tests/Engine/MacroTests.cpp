/*
    Macro controls (Phase 13e-3, PRD §17).

    PRD §17 lists what a macro must support, and each has a test here:

      - MIDI Learn: a controller assigned to a macro moves it;
      - multiple destinations, with bipolar depth: one macro turning one
        oscillator down while it turns another up, through the matrix;
      - minimum and maximum ranges, and curve shaping: the knob through its
        curve and range, exactly, and through the processor, a range that
        changes what the matrix receives;
      - preset storage: a macro and its shape come back from saved state;
      - and no particular controller: nothing is assigned by default.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <limits>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Modulation/Macro.h"
#include "MIDI/MidiControlManager.h"
#include "Parameters/ParameterDefinitions.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

[[nodiscard]] float plainValue (ApolloAudioProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : -1.0f;
}

/** The matrix source and destination numbers this file routes. */
constexpr float macro1Source = static_cast<float> (dsp::ModSource::macro1);
constexpr float osc1Level = static_cast<float> (dsp::ModDestination::osc1Level);
constexpr float osc2Level = static_cast<float> (dsp::ModDestination::osc2Level);

/** A held note's left channel, from when its level has settled. */
[[nodiscard]] std::vector<float> heldSignal (ApolloAudioProcessor& processor)
{
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> signal;

    for (int b = 0; b < 60; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        processor.processBlock (buffer, midi);

        if (b >= 13)
            signal.insert (signal.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return signal;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal)
{
    double sum = 0.0;

    for (const auto sample : signal)
        sum += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (sum / static_cast<double> (signal.size()));
}

/** RMS of a minus b. */
[[nodiscard]] double differenceOf (const std::vector<float>& a, const std::vector<float>& b)
{
    std::vector<float> difference (a.size());

    for (std::size_t i = 0; i < a.size(); ++i)
        difference[i] = a[i] - b[i];

    return rmsOf (difference);
}

/** RMS of the left channel over a held note's last half second. */
[[nodiscard]] double heldLevel (ApolloAudioProcessor& processor)
{
    return rmsOf (heldSignal (processor));
}

class MacroTests final : public juce::UnitTest
{
public:
    MacroTests()
        : juce::UnitTest ("Macros", "Engine")
    {
    }

    void runTest() override
    {
        testTheShape();
        testOneMacroManyDestinations();
        testTheRangeReachesTheMatrix();
        testNothingIsAssignedByDefault();
        testMidiLearnMovesAMacro();
        testAMacroIsSaved();
    }

private:
    void testTheShape()
    {
        beginTest ("A macro's knob goes through its curve and then its range");

        const dsp::MacroSettings plain;

        for (const auto knob : { 0.0f, 0.13f, 0.5f, 0.91f, 1.0f })
            expectEquals (dsp::shapeMacro (knob, plain), knob, "the defaults are not the knob itself");

        const dsp::MacroSettings range { 0.2f, 0.6f, 0.0f };
        expectWithinAbsoluteError (dsp::shapeMacro (0.0f, range), 0.2f, 1.0e-6f, "the bottom of a range");
        expectWithinAbsoluteError (dsp::shapeMacro (1.0f, range), 0.6f, 1.0e-6f, "the top of a range");
        expectWithinAbsoluteError (dsp::shapeMacro (0.5f, range), 0.4f, 1.0e-6f, "the middle of a range");

        const dsp::MacroSettings inverted { 1.0f, 0.0f, 0.0f };
        expectWithinAbsoluteError (dsp::shapeMacro (0.25f, inverted), 0.75f, 1.0e-6f, "an inverted range");

        // A curve of 1 is a fourth root and -1 a fourth power, so the ends stay put.
        expectWithinAbsoluteError (dsp::shapeMacro (0.0625f, { 0.0f, 1.0f, 1.0f }), 0.5f, 1.0e-6f, "a curve of 1");
        expectWithinAbsoluteError (dsp::shapeMacro (0.5f, { 0.0f, 1.0f, -1.0f }), 0.0625f, 1.0e-6f, "a curve of -1");
        expectWithinAbsoluteError (dsp::shapeMacro (1.0f, { 0.0f, 1.0f, -1.0f }), 1.0f, 1.0e-6f, "a curve moved an end");

        // Out of range and not a number: clamped, NaN to the bottom.
        expectEquals (dsp::shapeMacro (std::numeric_limits<float>::quiet_NaN(), range), 0.2f, "NaN");
        expectEquals (dsp::shapeMacro (7.0f, plain), 1.0f, "past the top");
        expectEquals (dsp::shapeMacro (-3.0f, plain), 0.0f, "below the bottom");
    }

    void testOneMacroManyDestinations()
    {
        beginTest ("One macro reaches two destinations, in opposite directions");

        // Macro 1 turns oscillator 1 down and oscillator 2, an octave above it,
        // up. At the knob's bottom the sound is oscillator 1 alone; at its top,
        // oscillator 2 alone; and the two are different signals, so each end can
        // be told from the other and from the macro doing nothing.
        const auto render = [] (float knob, bool routed, float osc1, float osc2)
        {
            ApolloAudioProcessor processor;
            setPlain (processor, "env1_sustain", 1.0f);
            setPlain (processor, "osc1_level", osc1);
            setPlain (processor, "osc2_level", osc2);
            setPlain (processor, "osc2_semitones", 12.0f);
            setPlain (processor, "macro1_value", knob);

            if (routed)
            {
                setPlain (processor, "mod01_source", macro1Source);
                setPlain (processor, "mod01_destination", osc1Level);
                setPlain (processor, "mod01_depth", -1.0f);

                setPlain (processor, "mod02_source", macro1Source);
                setPlain (processor, "mod02_destination", osc2Level);
                setPlain (processor, "mod02_depth", 1.0f);
            }

            return heldSignal (processor);
        };

        const auto osc1Alone = render (0.0f, false, 1.0f, 0.0f);
        const auto osc2Alone = render (0.0f, false, 0.0f, 1.0f);
        const auto bottom = render (0.0f, true, 1.0f, 0.0f);
        const auto top = render (1.0f, true, 1.0f, 0.0f);

        const auto bottomOff = differenceOf (bottom, osc1Alone);
        const auto topOff = differenceOf (top, osc2Alone);
        const auto alonesApart = differenceOf (osc1Alone, osc2Alone);

        logMessage ("  RMS of the difference: bottom from oscillator 1 alone " + juce::String (bottomOff, 5)
                    + ", top from oscillator 2 alone " + juce::String (topOff, 5)
                    + ", the two alone from each other " + juce::String (alonesApart, 4));

        expect (alonesApart > 0.02, "the two oscillators are not distinguishable");
        expect (bottomOff < alonesApart * 0.01, "the bottom is not oscillator 1 alone");
        expect (topOff < alonesApart * 0.01, "the top is not oscillator 2 alone");
    }

    void testTheRangeReachesTheMatrix()
    {
        beginTest ("A macro's range sets what the matrix receives");

        // Macro 1 turns oscillator 1 down, by the macro's value. With the
        // maximum at a half, the knob at its top takes the level only half way.
        const auto levelWith = [] (float maximum)
        {
            ApolloAudioProcessor processor;
            setPlain (processor, "env1_sustain", 1.0f);
            setPlain (processor, "macro1_value", 1.0f);
            setPlain (processor, "macro1_max", maximum);
            setPlain (processor, "mod01_source", macro1Source);
            setPlain (processor, "mod01_destination", osc1Level);
            setPlain (processor, "mod01_depth", -1.0f);
            return heldLevel (processor);
        };

        const auto full = levelWith (1.0f);
        const auto half = levelWith (0.5f);

        ApolloAudioProcessor unrouted;
        setPlain (unrouted, "env1_sustain", 1.0f);
        const auto untouched = heldLevel (unrouted);

        logMessage ("  oscillator 1 untouched " + juce::String (untouched, 4) + "; macro at its top with a maximum of 1 "
                    + juce::String (full, 4) + ", of a half " + juce::String (half, 4));

        expect (full < untouched * 0.01, "a full macro did not take the level to nothing");
        expectWithinAbsoluteError (half, untouched * 0.5, untouched * 0.05, "a maximum of a half did not halve it");
    }

    void testNothingIsAssignedByDefault()
    {
        beginTest ("No controller is assigned to a macro by default");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (rate, block);

        expectEquals (static_cast<int> (processor.getMidiControl().getMappings().size()), 0,
                      "a fresh instance has a MIDI mapping");

        for (int n = 1; n <= dsp::numMacros; ++n)
            expectEquals (plainValue (processor, "macro" + juce::String (n) + "_value"), 0.0f,
                          "macro " + juce::String (n) + " does not start at its bottom");
    }

    void testMidiLearnMovesAMacro()
    {
        beginTest ("A controller assigned to a macro moves it");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (rate, block);

        midi::Mapping mapping;
        mapping.address.controller = 20;
        mapping.parameterIndex = params::indexOfParameter ("macro2_value");
        expect (midi::succeeded (processor.getMidiControl().assign (mapping)));

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midiIn;
        midiIn.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 0);
        processor.processBlock (buffer, midiIn);
        processor.getMidiControl().flushPendingChanges();

        expectWithinAbsoluteError (plainValue (processor, "macro2_value"), 1.0f, 1.0e-5f, "CC 20 did not move macro 2");
    }

    void testAMacroIsSaved()
    {
        beginTest ("A macro and its shape come back from saved state");

        juce::MemoryBlock saved;

        {
            ApolloAudioProcessor processor;
            setPlain (processor, "macro3_value", 0.37f);
            setPlain (processor, "macro3_min", 0.1f);
            setPlain (processor, "macro3_max", 0.8f);
            setPlain (processor, "macro3_curve", -0.4f);
            processor.getStateInformation (saved);
        }

        ApolloAudioProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expectWithinAbsoluteError (plainValue (restored, "macro3_value"), 0.37f, 1.0e-4f, "the knob");
        expectWithinAbsoluteError (plainValue (restored, "macro3_min"), 0.1f, 1.0e-4f, "the minimum");
        expectWithinAbsoluteError (plainValue (restored, "macro3_max"), 0.8f, 1.0e-4f, "the maximum");
        expectWithinAbsoluteError (plainValue (restored, "macro3_curve"), -0.4f, 1.0e-4f, "the curve");
    }
};

MacroTests macroTests;

} // namespace
