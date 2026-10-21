/*
    The noise's transients through a whole processor (Phase 13d-2).

    The voice's part of it: a transient starts with every note and stops on its
    own while the note is still held; untracked it is the same on every note;
    tracked, it is shorter and higher above middle C and longer and lower below;
    and at middle C tracking changes nothing.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Noise/TransientLibrary.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 256;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

struct Render
{
    float type = 2.0f;
    float keyTrack = 0.0f;
    int note = 60;

    /** Where each note-on goes, in blocks. Every note is held to the end. */
    std::vector<int> notesAt { 0 };
    int blocks = 120;
};

/** The noise alone, filter off, instant attack, held. Left channel. */
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
    int next = 0;

    for (int b = 0; b < spec.blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        for (const auto at : spec.notesAt)
            if (at == b)
                midi.addEvent (juce::MidiMessage::noteOn (1, spec.note + 12 * (next++ % 2), 0.9f), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

/** The index of the last sample that is not exactly zero, or -1. */
[[nodiscard]] long lastSound (const std::vector<float>& signal, std::size_t from = 0)
{
    long last = -1;

    for (auto n = from; n < signal.size(); ++n)
        if (signal[n] != 0.0f)
            last = static_cast<long> (n);

    return last;
}

[[nodiscard]] double energyOf (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;

    for (auto n = from; n < to && n < signal.size(); ++n)
        sum += static_cast<double> (signal[n]) * static_cast<double> (signal[n]);

    return sum;
}

class NoiseTransientTests final : public juce::UnitTest
{
public:
    NoiseTransientTests()
        : juce::UnitTest ("Noise transients through the processor", "Engine")
    {
    }

    void runTest() override
    {
        testEachPlaysAndStops();
        testEveryNoteStartsOne();
        testUntrackedIsTheSameOnEveryNote();
        testTrackingPitchesIt();
        testMiddleCIsUntouched();
    }

private:
    void testEachPlaysAndStops()
    {
        beginTest ("Each transient plays from the note's start and stops while the note is held");

        const char* names[] { "click", "pick", "thump", "chiff" };

        for (int i = 0; i < dsp::TransientLibrary::numTransients; ++i)
        {
            const auto signal = render ({ .type = static_cast<float> (2 + i) });
            const auto length = dsp::TransientLibrary::shared().get (i).length();
            const auto last = lastSound (signal);

            logMessage ("  " + juce::String (names[i]) + ": last sound at sample " + juce::String (last)
                        + " of a " + juce::String (length) + "-sample transient");

            expect (energyOf (signal, 0, static_cast<std::size_t> (length)) > 1.0e-4,
                    juce::String (names[i]) + " made no sound");

            // A sample or two over the transient's own length: the gain ramps
            // and the interpolation's reach, not a second sound.
            expect (last >= 0 && last <= length + 2,
                    juce::String (names[i]) + " went on past its end, to " + juce::String (last));
        }
    }

    void testEveryNoteStartsOne()
    {
        beginTest ("Every note starts its own transient");

        // Two notes forty blocks apart, each on a voice of its own.
        const auto signal = render ({ .type = 3.0f, .notesAt = { 0, 40 } });
        const auto second = static_cast<std::size_t> (40 * block);
        const auto length = static_cast<std::size_t> (dsp::TransientLibrary::shared().get (1).length());

        expect (energyOf (signal, second, second + length) > 1.0e-4, "the second note had no transient");
        expectEquals (energyOf (signal, length + 8, second), 0.0, "there was sound between the two");
    }

    void testUntrackedIsTheSameOnEveryNote()
    {
        beginTest ("Untracked, a transient is the same on every note");

        expect (render ({ .note = 30 }) == render ({ .note = 90 }), "the note changed an untracked transient");
    }

    void testTrackingPitchesIt()
    {
        beginTest ("Tracked, a transient is shorter an octave up and longer an octave down");

        const auto length = static_cast<double> (dsp::TransientLibrary::shared().get (2).length());
        const auto up = static_cast<double> (lastSound (render ({ .type = 4.0f, .keyTrack = 1.0f, .note = 72 })));
        const auto down = static_cast<double> (lastSound (render ({ .type = 4.0f, .keyTrack = 1.0f, .note = 48 })));

        logMessage ("  thump, " + juce::String (length) + " samples: an octave up it ends at "
                    + juce::String (up) + ", an octave down at " + juce::String (down));

        expectWithinAbsoluteError (up, length / 2.0, 4.0, "an octave up is not half as long");
        expectWithinAbsoluteError (down, length * 2.0, 4.0, "an octave down is not twice as long");
    }

    void testMiddleCIsUntouched()
    {
        beginTest ("At middle C, tracking changes nothing");

        expect (render ({ .type = 5.0f, .keyTrack = 1.0f }) == render ({ .type = 5.0f }),
                "tracking at middle C changed the transient");
    }
};

NoiseTransientTests noiseTransientTests;

} // namespace
