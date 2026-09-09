#include "Audio/ApolloAudioProcessor.h"

#include "ApolloVersion.h"

#if APOLLO_WITH_WEBVIEW
 #include "UI/ApolloWebViewEditor.h"
#else
 #include <juce_audio_utils/juce_audio_utils.h>
#endif

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
    : juce::AudioProcessor (makeBusesProperties()),
      apvts (*this, nullptr, params::stateTreeType, params::createParameterLayout())
{
    // Resolved once, here: a string lookup per block would be an unbounded
    // search in the audio callback.
    masterGainParameter = apvts.getRawParameterValue ("master_gain");

    osc1Parameters.resolve (apvts, "osc1_");
    osc2Parameters.resolve (apvts, "osc2_");

    for (std::size_t i = 0; i < envelopeParameters.size(); ++i)
        envelopeParameters[i].resolve (apvts, "env" + juce::String (i + 1) + "_");

    for (std::size_t i = 0; i < lfoParameters.size(); ++i)
        lfoParameters[i].resolve (apvts, "lfo" + juce::String (i + 1) + "_");

    for (std::size_t i = 0; i < modulationParameters.size(); ++i)
        modulationParameters[i].resolve (
            apvts, "mod" + juce::String (i + 1).paddedLeft ('0', 2) + "_");

    filter1Parameters.resolve (apvts, "filter1_");
    filter2Parameters.resolve (apvts, "filter2_");
    filterRoutingParameter = apvts.getRawParameterValue ("filter_routing");

    subLevelParameter = apvts.getRawParameterValue ("sub_level");
    subOctaveParameter = apvts.getRawParameterValue ("sub_octave");
    noiseLevelParameter = apvts.getRawParameterValue ("noise_level");
}

void ApolloAudioProcessor::OscillatorParameterPointers::resolve (
    juce::AudioProcessorValueTreeState& state, juce::StringRef prefix)
{
    const juce::String base (prefix);

    wavetable = state.getRawParameterValue (base + "wavetable");
    position = state.getRawParameterValue (base + "position");
    unison = state.getRawParameterValue (base + "unison");
    detune = state.getRawParameterValue (base + "detune");
    spread = state.getRawParameterValue (base + "spread");
    level = state.getRawParameterValue (base + "level");
    pan = state.getRawParameterValue (base + "pan");

    // Absent for oscillator 1, which has no tuning controls of its own: it
    // plays the note it was given. getRawParameterValue returns null for an
    // unregistered ID, which is exactly the wanted result, but relying on that
    // silently would also swallow a genuine typo — so the two that are expected
    // to be absent are the only two not asserted.
    semitones = state.getRawParameterValue (base + "semitones");
    fine = state.getRawParameterValue (base + "fine");

    jassert (wavetable != nullptr && position != nullptr && unison != nullptr
             && detune != nullptr && spread != nullptr && level != nullptr && pan != nullptr);
}

void ApolloAudioProcessor::EnvelopeParameterPointers::resolve (
    juce::AudioProcessorValueTreeState& state, juce::StringRef prefix)
{
    const juce::String base (prefix);

    delayMs = state.getRawParameterValue (base + "delay");
    attackMs = state.getRawParameterValue (base + "attack");
    holdMs = state.getRawParameterValue (base + "hold");
    decayMs = state.getRawParameterValue (base + "decay");
    sustain = state.getRawParameterValue (base + "sustain");
    releaseMs = state.getRawParameterValue (base + "release");
    curve = state.getRawParameterValue (base + "curve");

    jassert (delayMs != nullptr && attackMs != nullptr && holdMs != nullptr
             && decayMs != nullptr && sustain != nullptr && releaseMs != nullptr
             && curve != nullptr);
}

void ApolloAudioProcessor::FilterParameterPointers::resolve (
    juce::AudioProcessorValueTreeState& state, juce::StringRef prefix)
{
    const juce::String base (prefix);

    type = state.getRawParameterValue (base + "type");
    cutoff = state.getRawParameterValue (base + "cutoff");
    resonance = state.getRawParameterValue (base + "resonance");
    drive = state.getRawParameterValue (base + "drive");

    jassert (type != nullptr && cutoff != nullptr && resonance != nullptr && drive != nullptr);
}

void ApolloAudioProcessor::LfoParameterPointers::resolve (
    juce::AudioProcessorValueTreeState& state, juce::StringRef prefix)
{
    const juce::String base (prefix);

    shape = state.getRawParameterValue (base + "shape");
    rate = state.getRawParameterValue (base + "rate");
    phase = state.getRawParameterValue (base + "phase");
    retrigger = state.getRawParameterValue (base + "retrigger");
    fadeMs = state.getRawParameterValue (base + "fade");
    smoothing = state.getRawParameterValue (base + "smoothing");
    polarity = state.getRawParameterValue (base + "polarity");
    steps = state.getRawParameterValue (base + "steps");

    jassert (shape != nullptr && rate != nullptr && phase != nullptr && retrigger != nullptr
             && fadeMs != nullptr && smoothing != nullptr && polarity != nullptr
             && steps != nullptr);
}

void ApolloAudioProcessor::ModSlotParameterPointers::resolve (
    juce::AudioProcessorValueTreeState& state, juce::StringRef prefix)
{
    const juce::String base (prefix);

    source = state.getRawParameterValue (base + "source");
    destination = state.getRawParameterValue (base + "destination");
    depth = state.getRawParameterValue (base + "depth");

    jassert (source != nullptr && destination != nullptr && depth != nullptr);
}

ApolloAudioProcessor::~ApolloAudioProcessor() = default;

//==============================================================================
// Processing lifecycle

void ApolloAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Every allocation and every DSP resource belongs here, never in
    // processBlock (CLAUDE.md §9.2).
    preparedSampleRate.store (sampleRate, std::memory_order_relaxed);
    preparedBlockSize.store (maximumExpectedSamplesPerBlock, std::memory_order_relaxed);

    voiceEngine.prepare (sampleRate);

    // 20 ms is long enough to remove the step from an automated gain change and
    // short enough that a deliberate move still feels immediate.
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (readMasterGainLinear());

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
    voiceEngine.reset();
    masterGain.setCurrentAndTargetValue (readMasterGainLinear());
}

float ApolloAudioProcessor::readMasterGainLinear() const noexcept
{
    if (masterGainParameter == nullptr)
        return 1.0f;

    return juce::Decibels::decibelsToGain (masterGainParameter->load (std::memory_order_relaxed));
}

namespace
{

/** @returns a raw parameter value, or @p fallback if the pointer is null.

    A null pointer means the ID was not registered. That is a programming error
    rather than a runtime condition, and it is asserted at construction — but
    the audio thread still reads defensively, because a dereference here would
    be a crash in the host's callback rather than a wrong sound.
*/
[[nodiscard]] float readParameter (const std::atomic<float>* parameter, float fallback) noexcept
{
    return parameter != nullptr ? parameter->load (std::memory_order_relaxed) : fallback;
}

} // namespace

void ApolloAudioProcessor::applySourceParameters() noexcept
{
    const auto readOscillator = [] (const OscillatorParameterPointers& pointers,
                                    float defaultLevel) noexcept
    {
        engine::OscillatorParameters result;

        result.wavetableIndex = static_cast<int> (readParameter (pointers.wavetable, 0.0f));
        result.position = readParameter (pointers.position, 0.0f);
        result.unisonVoices = static_cast<int> (readParameter (pointers.unison, 1.0f));
        result.detune = readParameter (pointers.detune, 0.0f);
        result.spread = readParameter (pointers.spread, 0.0f);
        result.level = readParameter (pointers.level, defaultLevel);
        result.pan = readParameter (pointers.pan, 0.0f);

        // Coarse and fine tuning are one quantity to the engine. Combining them
        // here rather than in the voice keeps the DSP unaware that the user
        // interface splits them into two controls.
        result.tuneSemitones = readParameter (pointers.semitones, 0.0f)
                             + readParameter (pointers.fine, 0.0f) / 100.0f;

        return result;
    };

    engine::SourceParameters parameters;

    parameters.osc1 = readOscillator (osc1Parameters, 1.0f);
    parameters.osc2 = readOscillator (osc2Parameters, 0.0f);

    parameters.subLevel = readParameter (subLevelParameter, 0.0f);
    parameters.subOctave = static_cast<int> (readParameter (subOctaveParameter, -1.0f));
    parameters.noiseLevel = readParameter (noiseLevelParameter, 0.0f);

    voiceEngine.setSourceParameters (parameters);

    // Envelope times reach the engine in seconds. Milliseconds are what a user
    // reads on a control; seconds are what a sample count is derived from, and
    // converting once here keeps the division out of the DSP.
    for (std::size_t i = 0; i < envelopeParameters.size(); ++i)
    {
        const auto& pointers = envelopeParameters[i];

        // Envelope 1 shapes amplitude and sustains at full; 2 to 4 are
        // modulators, whose defaults decay away so an unrouted one does nothing.
        const auto isAmplitude = (i == 0);

        dsp::EnvelopeSettings envelope;

        envelope.delaySeconds = readParameter (pointers.delayMs, 0.0f) * 0.001f;
        envelope.attackSeconds = readParameter (pointers.attackMs, 5.0f) * 0.001f;
        envelope.holdSeconds = readParameter (pointers.holdMs, 0.0f) * 0.001f;
        envelope.decaySeconds = readParameter (pointers.decayMs, isAmplitude ? 100.0f : 300.0f) * 0.001f;
        envelope.releaseSeconds = readParameter (pointers.releaseMs, isAmplitude ? 50.0f : 300.0f) * 0.001f;
        envelope.sustainLevel = readParameter (pointers.sustain, isAmplitude ? 1.0f : 0.0f);
        envelope.curve = readParameter (pointers.curve, 0.5f);

        voiceEngine.setEnvelopeSettings (static_cast<int> (i), envelope);
    }

    for (std::size_t i = 0; i < lfoParameters.size(); ++i)
    {
        const auto& pointers = lfoParameters[i];

        dsp::LfoSettings lfo;

        // Clamped rather than cast blindly: the shape indexes a switch, and a
        // corrupt preset must not be able to select something that is not there.
        const auto shape = static_cast<int> (readParameter (pointers.shape, 0.0f));
        lfo.shape = static_cast<dsp::LfoShape> (shape < 0 ? 0 : (shape > 6 ? 6 : shape));

        lfo.rateHz = readParameter (pointers.rate, 1.0f);
        lfo.phaseOffset = readParameter (pointers.phase, 0.0f);
        lfo.retrigger = readParameter (pointers.retrigger, 1.0f) >= 0.5f;
        lfo.fadeInSeconds = readParameter (pointers.fadeMs, 0.0f) * 0.001f;
        lfo.smoothing = readParameter (pointers.smoothing, 0.0f);
        lfo.bipolar = readParameter (pointers.polarity, 1.0f) >= 0.5f;
        lfo.stepCount = static_cast<int> (readParameter (pointers.steps, 8.0f));

        voiceEngine.setLfoSettings (static_cast<int> (i), lfo);
    }

    dsp::ModulationRouting routing;

    for (std::size_t i = 0; i < modulationParameters.size(); ++i)
    {
        const auto& pointers = modulationParameters[i];

        const auto source = static_cast<int> (readParameter (pointers.source, 0.0f));
        const auto destination = static_cast<int> (readParameter (pointers.destination, 0.0f));

        constexpr auto sourceCount = static_cast<int> (dsp::ModSource::count);
        constexpr auto destinationCount = static_cast<int> (dsp::ModDestination::count);

        // Out-of-range indices become "none" rather than being clamped onto a
        // real routing: a preset that names a source this build does not have
        // should do nothing, not silently modulate something else.
        routing.slots[i].source = (source > 0 && source < sourceCount)
                                    ? static_cast<dsp::ModSource> (source)
                                    : dsp::ModSource::none;

        routing.slots[i].destination = (destination > 0 && destination < destinationCount)
                                         ? static_cast<dsp::ModDestination> (destination)
                                         : dsp::ModDestination::none;

        routing.slots[i].depth = readParameter (pointers.depth, 0.0f);
    }

    voiceEngine.setModulationRouting (routing);

    const auto readFilter = [] (const FilterParameterPointers& pointers, float defaultType)
    {
        engine::FilterSlotParameters slot;

        const auto type = static_cast<int> (readParameter (pointers.type, defaultType));

        // Clamped rather than cast blindly: the mode indexes a switch, and a
        // corrupt preset must not be able to select something that is not there.
        slot.mode = static_cast<dsp::StateVariableFilter::Mode> (
            type < 0 ? 0 : (type > 4 ? 4 : type));

        slot.cutoffHz = readParameter (pointers.cutoff, 20000.0f);
        slot.q = readParameter (pointers.resonance, 0.707f);
        slot.drive = readParameter (pointers.drive, 0.0f);

        return slot;
    };

    engine::FilterParameters filters;

    filters.filter1 = readFilter (filter1Parameters, 1.0f);
    filters.filter2 = readFilter (filter2Parameters, 0.0f);
    filters.routing = readParameter (filterRoutingParameter, 0.0f) >= 0.5f
                        ? engine::FilterRouting::parallel
                        : engine::FilterRouting::series;

    voiceEngine.setFilterParameters (filters);
}

void ApolloAudioProcessor::handleMidiMessage (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        voiceEngine.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        voiceEngine.noteOff (message.getNoteNumber());
    }
    else if (message.isPitchWheel())
    {
        // JUCE reports 0-16383 with 8192 at centre.
        constexpr float centre = 8192.0f;
        const auto normalised = (static_cast<float> (message.getPitchWheelValue()) - centre) / centre;
        voiceEngine.setPitchBendSemitones (normalised * pitchBendRangeSemitones);
    }
    else if (message.isControllerOfType (1))
    {
        // The mod wheel, which is CC 1 on every controller that has one. It is
        // handled here rather than through MIDI Learn because it is a modulation
        // source in its own right (CLAUDE.md §15), not a mapping to a parameter.
        voiceEngine.setModWheel (static_cast<float> (message.getControllerValue()) / 127.0f);
    }
    else if (message.isChannelPressure())
    {
        // Channel aftertouch. Polyphonic aftertouch is a per-note source and
        // needs the per-note controller routing that arrives with MPE in
        // Phase 6, so it is deliberately not folded into this one.
        voiceEngine.setAftertouch (static_cast<float> (message.getChannelPressureValue()) / 127.0f);
    }
    else if (message.isSustainPedalOn())
    {
        voiceEngine.setSustainPedal (true);
    }
    else if (message.isSustainPedalOff())
    {
        voiceEngine.setSustainPedal (false);
    }
    else if (message.isAllNotesOff())
    {
        voiceEngine.allNotesOff();
    }
    else if (message.isAllSoundOff())
    {
        // All-sound-off means silence now, not "release and let it ring".
        voiceEngine.reset();
    }
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

    const auto numOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    // Apollo is an instrument: the engine is the origin of the signal and
    // overwrites the whole output. Any input is deliberately not passed through
    // — the input bus is reserved for the compressor's external sidechain
    // (PRD §23), which reads it rather than mixing it.
    auto* const* outputs = buffer.getArrayOfWritePointers();

    // MIDI is applied at its exact sample offset by rendering the block in
    // segments between events. A host can place several events anywhere inside
    // one buffer, and quantising them to block boundaries would smear timing by
    // up to a full block — audible as loose timing at large buffer sizes, and
    // wrong in offline renders where it is trivially measurable.
    // Source parameters are applied once per block rather than per sample.
    // Selecting a table is a discrete switch, and detune, spread and scan
    // position are slow gestures whose per-block granularity is well inside what
    // a listener can resolve. The two that would step audibly — source level and
    // balance — are smoothed per sample inside the voice, so nothing here has to
    // be read in the render loop.
    applySourceParameters();

    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const auto eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (eventTime > position)
        {
            voiceEngine.render (outputs, numOutputChannels, position, eventTime - position);
            position = eventTime;
        }

        handleMidiMessage (metadata.getMessage());
    }

    if (position < numSamples)
        voiceEngine.render (outputs, numOutputChannels, position, numSamples - position);

    // Master gain last, so it scales the finished mix rather than one stage of
    // it. Smoothed per sample: an automated gain move must not step.
    masterGain.setTargetValue (readMasterGainLinear());

    if (masterGain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = masterGain.getNextValue();

            for (int channel = 0; channel < numOutputChannels; ++channel)
                outputs[channel][i] *= gain;
        }
    }
    else
    {
        const auto gain = masterGain.getCurrentValue();

        for (int channel = 0; channel < numOutputChannels; ++channel)
            juce::FloatVectorOperations::multiply (outputs[channel], gain, numSamples);
    }
}

//==============================================================================
// Editor

juce::AudioProcessorEditor* ApolloAudioProcessor::createEditor()
{
   #if APOLLO_WITH_WEBVIEW
    return new ui::ApolloWebViewEditor (*this);
   #else
    // Built without the WebView UI. A generic editor over the parameter list is
    // a working fallback rather than no editor at all, which keeps the plugin
    // usable on a configuration where the WebView backend is unavailable
    // (CLAUDE.md §33).
    return new juce::GenericAudioProcessorEditor (*this);
   #endif
}

bool ApolloAudioProcessor::hasEditor() const
{
    return true;
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
    // The amplitude release is the only tail Apollo has so far. Reporting it
    // lets an offline render capture the note ending instead of truncating it.
    //
    // It tracks the envelope's release control rather than a constant: since
    // Phase 5a that is a user parameter reaching ten seconds, and a fixed value
    // would silently truncate every patch with a long tail.
    // Effects that ring out report their own tails in Phase 8.
    return static_cast<double> (readParameter (envelopeParameters[0].releaseMs, 50.0f)) * 0.001;
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
    state::writeState (apvts, destData);
}

void ApolloAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // A rejected document leaves the current sound untouched. Hosts call this
    // with whatever they have stored, including state written by a different
    // product or a future Apollo, so refusing safely is the normal path rather
    // than an exceptional one (CLAUDE.md §33).
    const auto result = state::readState (apvts, data, sizeInBytes);

    lastStateLoadResult.store (result, std::memory_order_relaxed);

    if (result == state::StateLoadResult::ok)
    {
        // Every parameter may have moved at once. Bumping the counter lets an
        // attached editor resynchronise wholesale instead of inferring a preset
        // load from a burst of individual changes.
        stateReloadCounter.fetch_add (1, std::memory_order_relaxed);
    }
}

} // namespace apollo
