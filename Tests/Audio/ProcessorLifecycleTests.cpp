/*
    Processor lifecycle tests.

    Hosts drive prepare/process/reset/release in orders that are easy to get
    wrong and awkward to observe from inside a DAW: block sizes that change
    every callback, sample rates that change mid-session, bypass, and repeated
    re-initialisation. These tests exercise that contract directly, without a
    host, so a regression shows up here rather than in someone's session.
*/

#include <juce_core/juce_core.h>

#include <cmath>

#include "Audio/ApolloAudioProcessor.h"

namespace
{

/** @returns true if every sample in every channel is exactly zero. */
bool isSilent (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (data[sample] != 0.0f)
                return false;
    }

    return true;
}

/** @returns true if every sample is finite (no NaN, no infinity). */
bool isFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (data[sample]))
                return false;
    }

    return true;
}

/** Fills a buffer with a recognisable non-zero pattern, so that a failure to
    clear an output channel is visible rather than accidentally looking like
    silence.
*/
void fillWithPattern (juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample (channel, sample, 0.5f - static_cast<float> ((sample + channel) % 3));
}

class ProcessorLifecycleTests final : public juce::UnitTest
{
public:
    ProcessorLifecycleTests()
        : juce::UnitTest ("Processor lifecycle", "Audio")
    {
    }

    void runTest() override
    {
        testInitialState();
        testPrepareReleaseCycle();
        testRepeatedPreparation();
        testVariableBlockSizes();
        testSampleRateChanges();
        testOutputIsSilentAndFinite();
        testProcessingBeforePreparation();
        testResetIsAlwaysSafe();
        testBusLayoutPolicy();
        testBlocksLongerThanPrepared();
        testMetadata();
    }

private:
    void testInitialState()
    {
        beginTest ("A freshly constructed processor is not prepared");

        apollo::ApolloAudioProcessor processor;
        expect (! processor.isPrepared());
    }

    void testPrepareReleaseCycle()
    {
        beginTest ("prepareToPlay and releaseResources track state");

        apollo::ApolloAudioProcessor processor;

        expectEquals (processor.getPreparedSampleRate(), 0.0,
                      "an unprepared processor reports no sample rate");

        processor.prepareToPlay (44100.0, 512);
        expect (processor.isPrepared());
        expectEquals (processor.getPreparedSampleRate(), 44100.0);
        expectEquals (processor.getPreparedBlockSize(), 512);

        processor.releaseResources();
        expect (! processor.isPrepared());
    }

    /** Hosts re-initialise plugins on device changes and when reopening a
        session. Preparing an already-prepared processor must be safe.
    */
    void testRepeatedPreparation()
    {
        beginTest ("Repeated prepare/release cycles are safe");

        apollo::ApolloAudioProcessor processor;

        for (int i = 0; i < 5; ++i)
        {
            processor.prepareToPlay (48000.0, 256);
            expect (processor.isPrepared());

            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            fillWithPattern (buffer);
            processor.processBlock (buffer, midi);

            expect (isFinite (buffer), "output must stay finite across re-initialisation");

            processor.releaseResources();
            expect (! processor.isPrepared());
        }

        // Preparing twice without an intervening release is legal.
        processor.prepareToPlay (44100.0, 128);
        processor.prepareToPlay (96000.0, 1024);
        expect (processor.isPrepared());
        expectEquals (processor.getPreparedSampleRate(), 96000.0);
        expectEquals (processor.getPreparedBlockSize(), 1024);
    }

    /** A host may deliver any block size up to the prepared maximum, and some
        deliver a different size on every callback.
    */
    void testVariableBlockSizes()
    {
        beginTest ("Block sizes varying up to the prepared maximum are handled");

        constexpr int maximumBlockSize = 2048;

        apollo::ApolloAudioProcessor processor;
        processor.prepareToPlay (44100.0, maximumBlockSize);

        // Preallocated once at the maximum, then processed in partial slices —
        // exactly how a host reuses its own buffer.
        juce::AudioBuffer<float> buffer (2, maximumBlockSize);
        juce::MidiBuffer midi;

        for (const int blockSize : { 1, 2, 3, 16, 31, 64, 127, 256, 480, 512, 1024, 2048 })
        {
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, blockSize);
            fillWithPattern (block);

            processor.processBlock (block, midi);

            expect (isFinite (block),
                    "output must be finite at block size " + juce::String (blockSize));
            expect (isSilent (block),
                    "output must be silent at block size " + juce::String (blockSize));
        }

        // A zero-length block is legal and must not be treated as an error.
        juce::AudioBuffer<float> emptyBlock (buffer.getArrayOfWritePointers(), 2, 0);
        processor.processBlock (emptyBlock, midi);

        processor.releaseResources();
    }

    void testSampleRateChanges()
    {
        beginTest ("Sample-rate changes are handled");

        apollo::ApolloAudioProcessor processor;

        for (const double sampleRate : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            processor.prepareToPlay (sampleRate, 512);
            expectEquals (processor.getPreparedSampleRate(), sampleRate);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            fillWithPattern (buffer);

            processor.processBlock (buffer, midi);

            expect (isFinite (buffer),
                    "output must be finite at " + juce::String (sampleRate) + " Hz");
            expect (isSilent (buffer),
                    "output must be silent at " + juce::String (sampleRate) + " Hz");

            processor.releaseResources();
        }
    }

    /** With no input bus enabled, every output channel must be cleared. A
        processor that leaves the host's buffer untouched would emit whatever
        the previous plugin left behind — in the worst case, full-scale noise.
    */
    void testOutputIsSilentAndFinite()
    {
        beginTest ("Unwritten output channels are cleared");

        apollo::ApolloAudioProcessor processor;
        processor.prepareToPlay (44100.0, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        // Deliberately hostile: a buffer full of full-scale values.
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, channel == 0 ? 1.0f : -1.0f);

        processor.processBlock (buffer, midi);

        expect (isSilent (buffer), "output must be silent when nothing is synthesised");
        expect (isFinite (buffer), "output must contain no NaN or infinity");

        processor.releaseResources();
    }

    /** Some hosts, and some plugin scanners, call processBlock before
        prepareToPlay or after releaseResources. It must not crash.
    */
    void testProcessingBeforePreparation()
    {
        beginTest ("Processing outside the prepared window does not crash");

        apollo::ApolloAudioProcessor processor;

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        fillWithPattern (buffer);
        processor.processBlock (buffer, midi);
        expect (isFinite (buffer), "output must be finite before prepareToPlay");

        processor.prepareToPlay (44100.0, 256);
        processor.releaseResources();

        fillWithPattern (buffer);
        processor.processBlock (buffer, midi);
        expect (isFinite (buffer), "output must be finite after releaseResources");
    }

    void testResetIsAlwaysSafe()
    {
        beginTest ("reset is safe at any point in the lifecycle");

        apollo::ApolloAudioProcessor processor;

        processor.reset();

        processor.prepareToPlay (44100.0, 512);
        processor.reset();
        expect (processor.isPrepared(), "reset must not undo preparation");

        processor.releaseResources();
        processor.reset();
        expect (! processor.isPrepared());
    }


    void testBlocksLongerThanPrepared()
    {
        beginTest ("A block longer than the processor was prepared for is processed, not skipped");

        // No host should send one: the maximum is what prepareToPlay was told.
        // What happened when one arrived was not a crash — the oversampler
        // refuses a block larger than it was prepared for, and returns without
        // touching it — but the effects that use it were silently skipped, so
        // the audio came out unprocessed rather than wrong-sounding in any way
        // a listener could attribute.
        //
        // An over-long block is now processed in pieces of the prepared size,
        // which produces the same audio the same span would in legal blocks.
        // Found in Phase 12a by a reliability test that made the mistake
        // itself (ADR-0080).
        //
        // This lives here rather than in the host harness because a VST3 host
        // wrapper has its own buffers sized to the same promise, and an
        // over-long block corrupts those long before Apollo sees it. The
        // processor is where the defence can be tested at all.
        const auto renderWith = [] (int blockSize, int totalSamples)
        {
            apollo::ApolloAudioProcessor processor;
            processor.setPlayConfigDetails (0, 2, 48000.0, 256);
            processor.prepareToPlay (48000.0, 256);

            // The distortion, which is the oversampled effect that was being
            // skipped, with enough drive that skipping it is obvious.
            if (auto* slot = processor.getValueTreeState().getParameter ("fx_slot1"))
                slot->setValueNotifyingHost (slot->convertTo0to1 (1.0f));

            if (auto* drive = processor.getValueTreeState().getParameter ("fx_distortion_drive"))
                drive->setValueNotifyingHost (drive->convertTo0to1 (30.0f));

            if (auto* mix = processor.getValueTreeState().getParameter ("fx_distortion_mix"))
                mix->setValueNotifyingHost (mix->convertTo0to1 (1.0f));

            juce::AudioBuffer<float> output (2, totalSamples);
            output.clear();

            for (int start = 0; start < totalSamples; start += blockSize)
            {
                const auto length = juce::jmin (blockSize, totalSamples - start);

                juce::AudioBuffer<float> block (2, length);
                block.clear();

                juce::MidiBuffer midi;

                if (start == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

                processor.processBlock (block, midi);

                for (int channel = 0; channel < 2; ++channel)
                    output.copyFrom (channel, start, block, channel, 0, length);
            }

            processor.releaseResources();
            return output;
        };

        const auto legal = renderWith (256, 4096);
        const auto overLong = renderWith (4096, 4096);

        auto largestDifference = 0.0f;
        auto finite = true;

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 4096; ++i)
            {
                const auto sample = overLong.getSample (channel, i);
                finite = finite && std::isfinite (sample);
                largestDifference = juce::jmax (largestDifference,
                                                std::abs (sample - legal.getSample (channel, i)));
            }

        expect (finite, "Every sample of the over-long block is finite");
        expect (legal.getMagnitude (0, 0, 4096) > 0.001f, "The reference render made sound");

        // Identical, because the pieces are exactly the blocks the legal
        // render used: same size, same order, same parameter reads.
        expectEquals (largestDifference, 0.0f,
                      "The over-long block produced the same audio as the same span in legal blocks");
    }
    void testBusLayoutPolicy()
    {
        beginTest ("Bus layouts are accepted and rejected as specified");

        apollo::ApolloAudioProcessor processor;

        const auto supports = [&processor] (const juce::AudioChannelSet& in,
                                            const juce::AudioChannelSet& out)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (in);
            layout.outputBuses.add (out);
            return processor.checkBusesLayoutSupported (layout);
        };

        const auto disabled = juce::AudioChannelSet::disabled();
        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();

        expect (supports (disabled, stereo), "stereo out, no input is the primary instrument layout");
        expect (supports (disabled, mono), "mono out must be supported");
        expect (supports (stereo, stereo), "stereo in/out must be supported");
        expect (supports (mono, mono), "mono in/out must be supported");
        expect (supports (mono, stereo), "a narrower input than output must be supported");

        // Required, not merely tolerated: a VST3 host asking for mono out
        // describes the inactive input as the stereo it last was, so refusing
        // this refused mono output in every VST3 host (ADR-0075).
        expect (supports (stereo, mono),
                "an input wider than the output must be supported");

        expect (! supports (disabled, juce::AudioChannelSet::create5point1()),
                "surround output is not supported yet and must be rejected");
        expect (! supports (disabled, disabled),
                "a disabled output bus must be rejected");
    }

    void testMetadata()
    {
        beginTest ("Host-facing metadata is correct for an instrument");

        apollo::ApolloAudioProcessor processor;

        expectEquals (processor.getName(), juce::String ("Apollo"));
        expect (processor.acceptsMidi(), "an instrument must accept MIDI");
        expect (! processor.producesMidi());
        expect (! processor.isMidiEffect());
        // The tail is the amplitude envelope's release: a host rendering offline
        // must keep pulling until the note has finished ringing, or it truncates
        // the ending. Since Phase 5a the release is a user control, so the tail
        // has to track the parameter rather than a constant.
        expectWithinAbsoluteError (processor.getTailLengthSeconds(), 0.050, 1.0e-6,
                                   "the reported tail must match the default release time");

        if (auto* release = processor.getValueTreeState().getParameter ("env1_release"))
        {
            release->setValueNotifyingHost (release->convertTo0to1 (4000.0f));

            expect (processor.getTailLengthSeconds() > 3.0,
                    "a long release must lengthen the reported tail, or an offline"
                    " render truncates the note");

            release->setValueNotifyingHost (release->convertTo0to1 (50.0f));
        }
        expect (processor.getNumPrograms() >= 1, "hosts misbehave when a plugin reports zero programs");
    }
};

ProcessorLifecycleTests processorLifecycleTests;

} // namespace
