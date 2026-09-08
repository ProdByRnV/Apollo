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

#include "Engine/VoiceEngine.h"
#include "Parameters/ParameterLayout.h"
#include "State/StateSerialization.h"

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

    /** The authoritative parameter and state system.

        APVTS owns every automatable parameter and is the single source of truth
        that host automation, preset recall, MIDI and the UI all flow through
        (ARCHITECTURE.md §6, UI_BINDINGS.md §1). Nothing else may hold a
        competing copy of a parameter value.
    */
    [[nodiscard]] juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    [[nodiscard]] const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts; }

    /** Outcome of the most recent setStateInformation call.

        A host hands over whatever it has stored, which may be truncated, from a
        different product, or from a future Apollo. Rejecting is a normal
        outcome, so it is recorded for the editor to surface rather than
        silently discarded (CLAUDE.md §33).
    */
    [[nodiscard]] state::StateLoadResult getLastStateLoadResult() const noexcept
    {
        return lastStateLoadResult.load (std::memory_order_relaxed);
    }

    /** Incremented whenever state is replaced wholesale, so an attached editor
        can tell a preset/project load apart from an ordinary parameter change
        and resynchronise everything at once.
    */
    [[nodiscard]] int getStateReloadCounter() const noexcept
    {
        return stateReloadCounter.load (std::memory_order_relaxed);
    }

    /** The synthesis engine.

        Exposed so tests can inspect voice allocation directly rather than
        inferring it from the rendered audio.
    */
    [[nodiscard]] engine::VoiceEngine& getVoiceEngine() noexcept { return voiceEngine; }
    [[nodiscard]] const engine::VoiceEngine& getVoiceEngine() const noexcept { return voiceEngine; }

    /** Pitch-bend range in semitones either side of centre.

        ±2 is the near-universal default. It becomes a parameter when the
        modulation system lands in Phase 5; hard-coding a different value would
        make Apollo disagree with every other instrument on the same MIDI input.
    */
    static constexpr float pitchBendRangeSemitones = 2.0f;

private:
    /** Applies one MIDI message to the engine. Audio thread; must stay
        allocation-free and bounded.
    */
    void handleMidiMessage (const juce::MidiMessage& message) noexcept;

    /** Pushes the source-section parameters into the engine. Audio thread. */
    void applySourceParameters() noexcept;

    /** @returns the master gain as a linear multiplier. */
    [[nodiscard]] float readMasterGainLinear() const noexcept;
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

    std::atomic<state::StateLoadResult> lastStateLoadResult { state::StateLoadResult::ok };
    std::atomic<int> stateReloadCounter { 0 };

    engine::VoiceEngine voiceEngine;

    /** Cached pointer to the master gain's plain (dB) value.

        Resolved once at construction: looking a parameter up by string on every
        block would be an unbounded search in the audio callback.
    */
    std::atomic<float>* masterGainParameter = nullptr;

    /** One primary oscillator's raw parameter values.

        Grouped rather than declared as sixteen separate members so that the two
        oscillators are resolved and read by the same code, and a control added
        to one cannot quietly be forgotten on the other.
    */
    struct OscillatorParameterPointers
    {
        std::atomic<float>* wavetable = nullptr;
        std::atomic<float>* position = nullptr;
        std::atomic<float>* unison = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* spread = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;

        /** Null for oscillator 1, which tracks the played note exactly. */
        std::atomic<float>* semitones = nullptr;
        std::atomic<float>* fine = nullptr;

        /** Resolves every pointer for the oscillator with this ID prefix.

            @param prefix  "osc1_" or "osc2_".
        */
        void resolve (juce::AudioProcessorValueTreeState& state, juce::StringRef prefix);
    };

    /** Source-section parameters, resolved once for the same reason. */
    OscillatorParameterPointers osc1Parameters;
    OscillatorParameterPointers osc2Parameters;

    /** One DAHDSR envelope's raw parameter values.

        Grouped for the same reason the oscillators are: envelopes 2-4 arrive
        with the modulation matrix, and reading them all through one function
        means a stage added to one cannot be forgotten on the others.
    */
    struct EnvelopeParameterPointers
    {
        std::atomic<float>* delayMs = nullptr;
        std::atomic<float>* attackMs = nullptr;
        std::atomic<float>* holdMs = nullptr;
        std::atomic<float>* decayMs = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* releaseMs = nullptr;
        std::atomic<float>* curve = nullptr;

        /** Resolves every pointer for the envelope with this ID prefix.

             prefix  "env1_" and, from the matrix onwards, "env2_" upward.
        */
        void resolve (juce::AudioProcessorValueTreeState& state, juce::StringRef prefix);
    };

    EnvelopeParameterPointers envelope1;

    std::atomic<float>* subLevelParameter = nullptr;
    std::atomic<float>* subOctaveParameter = nullptr;
    std::atomic<float>* noiseLevelParameter = nullptr;

    /** Master gain is declared `smoothed` in the registry, and a jump in gain is
        a click. Multiplicative smoothing ramps evenly in dB, which is how gain
        is perceived; it is safe here because the parameter's floor (-60 dB) is
        still a positive linear value.
    */
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGain;

    /** Declared after the members it does not depend on, but before anything
        that observes it: APVTS must outlive every listener attached to it.
    */
    juce::AudioProcessorValueTreeState apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApolloAudioProcessor)
};

} // namespace apollo
