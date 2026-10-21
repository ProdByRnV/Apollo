/*
    The noise's type and key tracking, through a whole processor (Phase 13d-1).

    The noise source's own behaviour is measured in Tests/DSP/NoiseSourceTests;
    this is the voice's part — that the parameters reach it, and that key
    tracking follows the played pitch:

      - at middle C, or at zero, key tracking changes nothing at all;
      - above middle C it changes nothing either, because noise drawn every
        sample is already as bright as it can be;
      - below middle C it darkens the noise, by more the lower the note;
      - it follows the pitch wheel;
      - and pink through the processor is darker than white, at the same level.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;
constexpr int blocks = 48;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

struct Render
{
    float type = 0.0f;
    float keyTrack = 0.0f;
    int note = 60;

    /** The pitch wheel's 14-bit position, sent before the note; 8192 is centre. */
    int wheel = 8192;
};

/** The noise alone, held, filter open. Left channel. */
[[nodiscard]] std::vector<float> render (const Render& spec)
{
    ApolloAudioProcessor processor;
    setPlain (processor, "osc1_level", 0.0f);
    setPlain (processor, "noise_level", 1.0f);
    setPlain (processor, "noise_type", spec.type);
    setPlain (processor, "noise_keytrack", spec.keyTrack);
    setPlain (processor, "filter1_type", 0.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
        {
            midi.addEvent (juce::MidiMessage::pitchWheel (1, spec.wheel), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, spec.note, 0.9f), 0);
        }

        processor.processBlock (buffer, midi);

        // The first blocks hold the attack; what is measured is the held noise.
        if (b >= 4)
            rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal)
{
    double sum = 0.0;

    for (const auto sample : signal)
        sum += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (sum / static_cast<double> (signal.size()));
}

/** How bright, as the RMS of the first difference over the RMS: about 1.41
    for white noise, and smaller the darker the signal. */
[[nodiscard]] double brightnessOf (const std::vector<float>& signal)
{
    double sum = 0.0;

    for (std::size_t i = 1; i < signal.size(); ++i)
    {
        const auto step = static_cast<double> (signal[i]) - static_cast<double> (signal[i - 1]);
        sum += step * step;
    }

    return std::sqrt (sum / static_cast<double> (signal.size() - 1)) / rmsOf (signal);
}

class NoiseTypeTests final : public juce::UnitTest
{
public:
    NoiseTypeTests()
        : juce::UnitTest ("Noise type and key tracking", "Engine")
    {
    }

    void runTest() override
    {
        testMiddleCAndZeroChangeNothing();
        testAboveMiddleCChangesNothing();
        testBelowMiddleCDarkens();
        testFollowsTheWheel();
        testPinkThroughTheProcessor();
    }

private:
    void testMiddleCAndZeroChangeNothing()
    {
        beginTest ("At middle C, or at zero, key tracking changes nothing");

        const auto untracked = render ({});

        expect (rmsOf (untracked) > 1.0e-3, "the noise made no sound");
        expect (render ({ .keyTrack = 1.0f }) == untracked, "full tracking at middle C changed the noise");
        expect (render ({ .keyTrack = 0.0f, .note = 30 }) == render ({ .keyTrack = 0.0f, .note = 90 }),
                "untracked noise depended on the note");
    }

    void testAboveMiddleCChangesNothing()
    {
        beginTest ("Above middle C, tracked noise is the untracked noise");

        expect (render ({ .keyTrack = 1.0f, .note = 84 }) == render ({ .note = 84 }),
                "tracked noise above middle C differs from untracked");
    }

    void testBelowMiddleCDarkens()
    {
        beginTest ("Below middle C, tracked noise is darker, and darker the lower");

        const auto open = brightnessOf (render ({}));
        const auto octave = brightnessOf (render ({ .keyTrack = 1.0f, .note = 48 }));
        const auto twoOctaves = brightnessOf (render ({ .keyTrack = 1.0f, .note = 36 }));
        const auto halfTracked = brightnessOf (render ({ .keyTrack = 0.5f, .note = 36 }));

        logMessage ("  brightness: middle C " + juce::String (open, 3) + ", an octave down "
                    + juce::String (octave, 3) + ", two down " + juce::String (twoOctaves, 3)
                    + ", two down at half tracking " + juce::String (halfTracked, 3));

        expect (octave < open * 0.8, "an octave down is not darker");
        expect (twoOctaves < octave * 0.8, "two octaves down is not darker than one");

        // Half tracking two octaves down is one octave's worth.
        expectWithinAbsoluteError (halfTracked, octave, 0.05 * octave,
                                   "half tracking is not half the octaves");
    }

    void testFollowsTheWheel()
    {
        beginTest ("Key tracking follows the pitch wheel");

        const auto unbent = brightnessOf (render ({ .keyTrack = 1.0f, .note = 36 }));
        const auto bentDown = brightnessOf (render ({ .keyTrack = 1.0f, .note = 36, .wheel = 0 }));
        const auto bentUp = brightnessOf (render ({ .keyTrack = 1.0f, .note = 36, .wheel = 16383 }));

        logMessage ("  brightness two octaves down: bent down " + juce::String (bentDown, 4)
                    + ", unbent " + juce::String (unbent, 4) + ", bent up " + juce::String (bentUp, 4));

        expect (bentDown < unbent && unbent < bentUp, "the wheel did not move the noise's rate");
    }

    void testPinkThroughTheProcessor()
    {
        beginTest ("Pink through the processor is darker than white and as loud");

        const auto white = render ({});
        const auto pink = render ({ .type = 1.0f });

        const auto level = 20.0 * std::log10 (rmsOf (pink) / rmsOf (white));

        logMessage ("  pink against white: level " + juce::String (level, 2) + " dB, brightness "
                    + juce::String (brightnessOf (pink), 3) + " against " + juce::String (brightnessOf (white), 3));

        expect (brightnessOf (pink) < brightnessOf (white) * 0.8, "pink is not darker than white");
        expect (std::abs (level) < 0.5, "pink is " + juce::String (level, 2) + " dB from white");
    }
};

NoiseTypeTests noiseTypeTests;

} // namespace
