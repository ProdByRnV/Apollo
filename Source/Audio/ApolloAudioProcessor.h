#pragma once

/*
    Apollo's AudioProcessor.

    This is the host-facing shell around the synthesis engine. It owns the
    processing lifecycle and the parameter/state system, and it must remain
    independent of any one plugin format: everything here is expressed in terms
    of juce::AudioProcessor, never in terms of VST3 (ARCHITECTURE.md §5.1,
    §8).

    Real-time contract: processBlock and everything it calls must not allocate,
    lock, log, touch the filesystem or call into the WebView (CLAUDE.md §7).
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace apollo
{

class ApolloAudioProcessor final : public juce::AudioProcessor
{
public:
    ApolloAudioProcessor();
    ~ApolloAudioProcessor() override;

    //==============================================================================
    // Processing lifecycle

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==============================================================================
    // Editor

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    // Host-facing metadata

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    // State

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** True between prepareToPlay and releaseResources.

        Exposed because the prepare/release/reset cycle is a contract hosts
        exercise in ways that are easy to get wrong and hard to observe
        otherwise; the lifecycle tests assert on it directly.
    */
    [[nodiscard]] bool isPrepared() const noexcept { return prepared.load (std::memory_order_relaxed); }

    /** The sample rate this processor was last prepared with, or 0 if it has
        never been prepared.

        Apollo tracks this itself rather than relying on
        juce::AudioProcessor::getSampleRate(), which reflects what a host
        wrapper recorded via setRateAndBufferSizeDetails. That call is the
        wrapper's responsibility, not prepareToPlay's, so it is absent when the
        processor is driven directly — by the test suite, by an offline
        renderer, or by a host that skips it. The value the DSP is prepared for
        must come from preparation itself.
    */
    [[nodiscard]] double getPreparedSampleRate() const noexcept
    {
        return preparedSampleRate.load (std::memory_order_relaxed);
    }

    /** The maximum block size this processor was last prepared for, or 0. */
    [[nodiscard]] int getPreparedBlockSize() const noexcept
    {
        return preparedBlockSize.load (std::memory_order_relaxed);
    }

private:
    /** Apollo's bus layout.

        A static member rather than a free function because BusesProperties is a
        protected nested type of juce::AudioProcessor, reachable only from a
        derived class.
    */
    static BusesProperties makeBusesProperties();

    /** Set on the message thread by prepareToPlay/releaseResources and read by
        tests. Atomic because a host may prepare on a thread other than the one
        observing it; relaxed ordering is sufficient as nothing is published
        through it.
    */
    std::atomic<bool> prepared { false };

    /** Written by prepareToPlay, read by the DSP and by tests. */
    std::atomic<double> preparedSampleRate { 0.0 };
    std::atomic<int> preparedBlockSize { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApolloAudioProcessor)
};

} // namespace apollo
