#include "Audio/ApolloAudioProcessor.h"

#include "ApolloVersion.h"

namespace apollo
{

/*
    Bus layout policy.

    Apollo is an instrument, so the output bus is the one that matters. An input
    bus is declared but disabled by default: it gives the standalone application
    something to monitor, gives Phase 1 a verifiable audio path before any
    synthesis exists, and reserves the route that the compressor's external
    sidechain will need (PRD §23). Hosts that do not offer it simply leave it
    disabled.
*/
juce::AudioProcessor::BusesProperties ApolloAudioProcessor::makeBusesProperties()
{
    return BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        .withInput ("Input", juce::AudioChannelSet::stereo(), false);
}

//==============================================================================

ApolloAudioProcessor::ApolloAudioProcessor()
    : juce::AudioProcessor (makeBusesProperties())
{
}

ApolloAudioProcessor::~ApolloAudioProcessor() = default;

//==============================================================================
// Processing lifecycle

void ApolloAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Every allocation and every DSP resource belongs here, never in
    // processBlock (CLAUDE.md §9.2). There is nothing to allocate yet.
    preparedSampleRate.store (sampleRate, std::memory_order_relaxed);
    preparedBlockSize.store (maximumExpectedSamplesPerBlock, std::memory_order_relaxed);

    reset();

    prepared.store (true, std::memory_order_relaxed);
}

void ApolloAudioProcessor::releaseResources()
{
    prepared.store (false, std::memory_order_relaxed);
}

void ApolloAudioProcessor::reset()
{
    // Clears transient DSP state without releasing resources. Hosts call this
    // on transport jumps and when re-enabling a bypassed plugin, so it must be
    // safe to call at any time, including before prepareToPlay.
}

bool ApolloAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    const auto& mainInput = layouts.getMainInputChannelSet();

    // Mono and stereo output only. Wider layouts are rejected rather than
    // silently mishandled; supporting them is a decision with real DSP
    // consequences (panning, unison spread) and belongs with the engine.
    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    // The input bus is optional. When present it must be mono or stereo and
    // must not be wider than the output, because pass-through writes input
    // channels directly into the shared buffer.
    if (! mainInput.isDisabled())
    {
        if (mainInput != juce::AudioChannelSet::mono()
            && mainInput != juce::AudioChannelSet::stereo())
            return false;

        if (mainInput.size() > mainOutput.size())
            return false;
    }

    return true;
}

void ApolloAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    // Flushes denormals to zero for the duration of the callback. Denormal
    // arithmetic can cost orders of magnitude more than normal arithmetic and
    // is a classic source of audio dropouts in feedback paths (CLAUDE.md §37).
    const juce::ScopedNoDenormals noDenormals;

    // MIDI is consumed by the voice engine in Phase 3.
    juce::ignoreUnused (midiMessages);

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    // JUCE hands the same buffer in for input and output, so any output channel
    // that is not written must be cleared explicitly — otherwise it carries
    // whatever the previous callback or another plugin left there.
    //
    // With the input bus disabled (the normal instrument case) this clears
    // every channel, and Apollo outputs silence until the synthesis engine
    // exists. With the input bus enabled, channels [0, numInputChannels) hold
    // the host's input and pass through untouched.
    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);
}

//==============================================================================
// Editor

juce::AudioProcessorEditor* ApolloAudioProcessor::createEditor()
{
    // The WebView editor arrives with the UI bridge in Phase 2. Until then the
    // host supplies a generic editor built from the parameter list.
    return nullptr;
}

bool ApolloAudioProcessor::hasEditor() const
{
    return false;
}

//==============================================================================
// Host-facing metadata

const juce::String ApolloAudioProcessor::getName() const
{
    return juce::String (productName.data(), productName.size());
}

bool ApolloAudioProcessor::acceptsMidi() const
{
    return true;
}

bool ApolloAudioProcessor::producesMidi() const
{
    return false;
}

bool ApolloAudioProcessor::isMidiEffect() const
{
    return false;
}

double ApolloAudioProcessor::getTailLengthSeconds() const
{
    // No effect tail yet. Effects that ring out (delay, reverb) report their own
    // tail in Phase 8; reporting a tail now would only make hosts render silence.
    return 0.0;
}

//==============================================================================
// Programs
//
// Apollo exposes a single program. Preset management is Apollo's own, delivered
// through the preset system in Phase 9 rather than through the host's flat
// program list, which cannot represent preset metadata or wavetable references.

int ApolloAudioProcessor::getNumPrograms()
{
    // At least 1: some hosts misbehave when a plugin reports zero programs.
    return 1;
}

int ApolloAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ApolloAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String ApolloAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return "Default";
}

void ApolloAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
// State

void ApolloAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Implemented against APVTS in Phase 2. Apollo has no automatable
    // parameters yet, so there is nothing to serialize.
    juce::ignoreUnused (destData);
}

void ApolloAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Implemented against APVTS in Phase 2. Until then, incoming state is
    // ignored rather than partially applied.
    juce::ignoreUnused (data, sizeInBytes);
}

} // namespace apollo
