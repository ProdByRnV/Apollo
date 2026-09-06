/*
    Processor-level rendering tests.

    These cover the part of the audio path the engine tests cannot reach: the
    translation from MIDI into engine calls, sample-accurate event placement, and
    the master gain stage.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Engine/VoiceEngine.h"

namespace
{

constexpr double testSampleRate = 48000.0;

/** One MIDI event at an absolute sample position in a render. */
struct TimedMessage
{
    int samplePosition;
    juce::MidiMessage message;
};

/** Renders a sequence through a fresh processor, in blocks of @p blockSize.

    Splitting the same sequence differently must not change the result, which is
    what makes this useful: the block size is the variable, the output is not.
*/
juce::AudioBuffer<float> renderSequence (const std::vector<TimedMessage>& sequence,
                                         int totalSamples,
                                         int blockSize)
{
    apollo::ApolloAudioProcessor processor;
    processor.setRateAndBufferSizeDetails (testSampleRate, blockSize);
    processor.prepareToPlay (testSampleRate, blockSize);

    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block (2, blockSize);

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const auto numSamples = juce::jmin (blockSize, totalSamples - start);

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, numSamples);
        view.clear();

        // Events that fall inside this block, rebased to block-relative
        // positions — exactly how a host presents them.
        juce::MidiBuffer midi;

        for (const auto& event : sequence)
            if (event.samplePosition >= start && event.samplePosition < start + numSamples)
                midi.addEvent (event.message, event.samplePosition - start);

        processor.processBlock (view, midi);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, view, channel, 0, numSamples);
    }

    processor.releaseResources();
    return output;
}

[[nodiscard]] float peakOf (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getMagnitude (0, buffer.getNumSamples());
}

[[nodiscard]] bool isFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }

    return true;
}

class MidiRenderingTests final : public juce::UnitTest
{
public:
    MidiRenderingTests()
        : juce::UnitTest ("MIDI rendering", "Audio")
    {
    }

    void runTest() override
    {
        testNoteProducesAudioThroughProcessor();
        testEventPlacementIsSampleAccurate();
        testBlockSizeDoesNotChangeOutput();
        testSustainPedalThroughMidi();
        testPitchBendThroughMidi();
        testAllNotesOffThroughMidi();
        testMasterGainAffectsOutput();
    }

private:
    static std::vector<TimedMessage> simpleSequence()
    {
        return {
            { 0,     juce::MidiMessage::noteOn (1, 60, 0.8f) },
            { 1000,  juce::MidiMessage::noteOn (1, 64, 0.8f) },
            { 2000,  juce::MidiMessage::noteOn (1, 67, 0.8f) },
            { 5000,  juce::MidiMessage::noteOff (1, 60) },
            { 6000,  juce::MidiMessage::noteOff (1, 64) },
            { 7000,  juce::MidiMessage::noteOff (1, 67) }
        };
    }

    void testNoteProducesAudioThroughProcessor()
    {
        beginTest ("A note played through the processor produces audio");

        const std::vector<TimedMessage> sequence {
            { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) }
        };

        const auto rendered = renderSequence (sequence, 8192, 512);

        expect (peakOf (rendered) > 0.01f, "a note must produce a signal");
        expect (isFinite (rendered));
    }

    /** A note-on placed mid-block must start exactly there, not at the block
        boundary. Quantising events to blocks smears timing by up to a full
        buffer, which is audible as loose timing and trivially wrong offline.
    */
    void testEventPlacementIsSampleAccurate()
    {
        beginTest ("Events take effect at their exact sample position");

        constexpr int blockSize = 512;
        constexpr int onset = 300;

        const std::vector<TimedMessage> sequence {
            { onset, juce::MidiMessage::noteOn (1, 69, 1.0f) }
        };

        const auto rendered = renderSequence (sequence, blockSize * 4, blockSize);
        const auto* data = rendered.getReadPointer (0);

        // Everything before the onset must be exactly silent.
        for (int i = 0; i < onset; ++i)
            expectEquals (data[i], 0.0f,
                          "sample " + juce::String (i) + " precedes the note-on but is not silent");

        // And the signal must actually begin shortly after it. The envelope
        // attack means the first samples are near zero, so allow it to build.
        float peakAfterOnset = 0.0f;

        for (int i = onset; i < rendered.getNumSamples(); ++i)
            peakAfterOnset = juce::jmax (peakAfterOnset, std::abs (data[i]));

        expect (peakAfterOnset > 0.01f, "no signal appeared after the note-on");
    }

    /** The strongest available check that event handling is genuinely
        sample-accurate and carries no state across block boundaries: the same
        sequence, split every possible way, must produce the same samples.
    */
    void testBlockSizeDoesNotChangeOutput()
    {
        beginTest ("Output is identical regardless of block size");

        constexpr int totalSamples = 12000;

        const auto reference = renderSequence (simpleSequence(), totalSamples, totalSamples);

        for (const int blockSize : { 1, 7, 32, 64, 111, 256, 512, 1024, 4096 })
        {
            const auto rendered = renderSequence (simpleSequence(), totalSamples, blockSize);

            expectEquals (rendered.getNumSamples(), reference.getNumSamples());

            float largestDifference = 0.0f;

            for (int channel = 0; channel < 2; ++channel)
            {
                const auto* a = reference.getReadPointer (channel);
                const auto* b = rendered.getReadPointer (channel);

                for (int i = 0; i < totalSamples; ++i)
                    largestDifference = juce::jmax (largestDifference, std::abs (a[i] - b[i]));
            }

            // Identical arithmetic in an identical order, so this should be
            // exact; a small tolerance guards only against the gain smoother
            // being stepped a different number of times.
            expect (largestDifference < 1.0e-6f,
                    "block size " + juce::String (blockSize) + " diverged by "
                        + juce::String (largestDifference, 8));
        }
    }

    void testSustainPedalThroughMidi()
    {
        beginTest ("The sustain pedal works through MIDI CC 64");

        // Pedal down, note on, note off: the note must still be sounding.
        const std::vector<TimedMessage> held {
            { 0,    juce::MidiMessage::controllerEvent (1, 64, 127) },
            { 10,   juce::MidiMessage::noteOn (1, 60, 1.0f) },
            { 2000, juce::MidiMessage::noteOff (1, 60) }
        };

        const auto sustained = renderSequence (held, 24000, 512);

        // Measure the last portion, well past where an unsustained note would
        // have released.
        const auto tail = juce::AudioBuffer<float> (
            const_cast<float* const*> (sustained.getArrayOfReadPointers()), 2, 20000, 4000);

        expect (tail.getMagnitude (0, tail.getNumSamples()) > 0.01f,
                "a note held by the sustain pedal must keep sounding");

        // Same sequence, but the pedal is released part way through.
        auto released = held;
        released.push_back ({ 4000, juce::MidiMessage::controllerEvent (1, 64, 0) });

        const auto stopped = renderSequence (released, 24000, 512);
        const auto stoppedTail = juce::AudioBuffer<float> (
            const_cast<float* const*> (stopped.getArrayOfReadPointers()), 2, 20000, 4000);

        expectEquals (stoppedTail.getMagnitude (0, stoppedTail.getNumSamples()), 0.0f,
                      "releasing the pedal must let the note release to silence");
    }

    void testPitchBendThroughMidi()
    {
        beginTest ("Pitch bend works through the MIDI pitch wheel");

        const auto renderWithWheel = [] (int wheelValue)
        {
            const std::vector<TimedMessage> sequence {
                { 0, juce::MidiMessage::pitchWheel (1, wheelValue) },
                { 1, juce::MidiMessage::noteOn (1, 69, 1.0f) }
            };

            return renderSequence (sequence, 24000, 512);
        };

        const auto countUpwardCrossings = [] (const juce::AudioBuffer<float>& buffer)
        {
            const auto* data = buffer.getReadPointer (0);
            int crossings = 0;

            for (int i = 1; i < buffer.getNumSamples(); ++i)
                if (data[i - 1] <= 0.0f && data[i] > 0.0f)
                    ++crossings;

            return crossings;
        };

        const auto centre = countUpwardCrossings (renderWithWheel (8192));
        const auto up = countUpwardCrossings (renderWithWheel (16383));
        const auto down = countUpwardCrossings (renderWithWheel (0));

        expect (up > centre, "bending up must raise the pitch");
        expect (down < centre, "bending down must lower the pitch");
    }

    void testAllNotesOffThroughMidi()
    {
        beginTest ("All-notes-off silences held notes");

        const std::vector<TimedMessage> sequence {
            { 0,    juce::MidiMessage::noteOn (1, 60, 1.0f) },
            { 10,   juce::MidiMessage::noteOn (1, 64, 1.0f) },
            { 20,   juce::MidiMessage::noteOn (1, 67, 1.0f) },
            { 2000, juce::MidiMessage::allNotesOff (1) }
        };

        const auto rendered = renderSequence (sequence, 24000, 512);
        const auto tail = juce::AudioBuffer<float> (
            const_cast<float* const*> (rendered.getArrayOfReadPointers()), 2, 20000, 4000);

        expectEquals (tail.getMagnitude (0, tail.getNumSamples()), 0.0f,
                      "all-notes-off must leave silence once the release completes");
    }

    void testMasterGainAffectsOutput()
    {
        beginTest ("Master gain scales the output");

        const auto renderAtGain = [] (float decibels)
        {
            apollo::ApolloAudioProcessor processor;
            processor.setRateAndBufferSizeDetails (testSampleRate, 512);
            processor.prepareToPlay (testSampleRate, 512);

            if (auto* parameter = processor.getValueTreeState().getParameter ("master_gain"))
            {
                const auto& range = parameter->getNormalisableRange();
                parameter->setValueNotifyingHost (range.convertTo0to1 (decibels));
            }

            // Re-prepared so the gain smoother starts at the new value rather
            // than ramping from the old one, which would blur the comparison.
            processor.prepareToPlay (testSampleRate, 512);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);

            float peak = 0.0f;

            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                processor.processBlock (buffer, midi);
                midi.clear();
                peak = juce::jmax (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
            }

            return peak;
        };

        const auto unity = renderAtGain (0.0f);
        const auto quieter = renderAtGain (-12.0f);
        const auto louder = renderAtGain (6.0f);

        expect (unity > 0.0f, "the note must be audible at unity gain");
        expect (quieter < unity, "-12 dB must be quieter than 0 dB");
        expect (louder > unity, "+6 dB must be louder than 0 dB");

        // -12 dB is a quarter of the amplitude; allow for the smoother.
        expectWithinAbsoluteError (quieter, unity * 0.25f, unity * 0.05f,
                                   "-12 dB should be about a quarter of unity amplitude");
    }
};

MidiRenderingTests midiRenderingTests;

} // namespace
