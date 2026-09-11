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
      apvts (*this, nullptr, params::stateTreeType, params::createParameterLayout()),
      midiControl (apvts)
{
    telemetry = std::make_unique<telemetry::TelemetryHub>();

    // The per-source taps live inside the voice loop, so the engine is the only
    // thing that can take them. It holds the hub without owning it, and reads
    // once per block whether anything is watching.
    voiceEngine.setTelemetry (telemetry.get());

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

    bendRangeParameter = apvts.getRawParameterValue ("midi_bend_range");
    mpeZoneParameter = apvts.getRawParameterValue ("mpe_zone");
    mpeMembersParameter = apvts.getRawParameterValue ("mpe_members");
    mpeBendRangeParameter = apvts.getRawParameterValue ("mpe_bend_range");

    // Resolved once, for the same reason the pointers above are: an RPN arrives
    // on the audio thread, and looking its parameter up by string there would be
    // an unbounded search in the callback.
    bendRangeParameterIndex = params::indexOfParameter ("midi_bend_range");
    mpeZoneParameterIndex = params::indexOfParameter ("mpe_zone");
    mpeMembersParameterIndex = params::indexOfParameter ("mpe_members");
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

    // Captures go with the audio they were taken from. A transport jump or a
    // device change must not leave a scope showing a picture of sound that is no
    // longer being produced.
    telemetry->reset();
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

    // MIDI expression setup. Read every block like everything else, so a change
    // from the interface, from a restored project or from a controller's own
    // MPE Configuration Message all arrive by the same route and none of them
    // needs a notification path of its own.
    {
        midi::MpeZone zone;

        const auto zoneIndex = static_cast<int> (readParameter (mpeZoneParameter, 0.0f));

        // Clamped rather than cast blindly: a corrupt document must not be able
        // to select a zone that does not exist.
        zone.type = zoneIndex == 1   ? midi::MpeZoneType::lower
                    : zoneIndex == 2 ? midi::MpeZoneType::upper
                                     : midi::MpeZoneType::off;

        zone.memberCount = juce::jlimit (
            1, 15, static_cast<int> (readParameter (mpeMembersParameter, 15.0f)));

        voiceEngine.setMpeZone (zone);

        voiceEngine.setPitchBendRange (
            readParameter (bendRangeParameter, pitchBendRangeSemitones),
            readParameter (mpeBendRangeParameter,
                           static_cast<float> (midi::defaultMemberPitchBendRange)));
    }

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
    // Every control-change message is offered to the MIDI Learn layer first,
    // and then handled here as well. The two are not alternatives: CC 1 is the
    // mod wheel — a modulation source in its own right — *and* a controller a
    // user is entitled to map to a parameter, and a message that did one job
    // instead of the other depending on hidden state would be worse than a
    // message that does both (CLAUDE.md §16.2).
    //
    // The mapping layer refuses the controllers whose fixed meaning Apollo acts
    // on below, so a sustain pedal can never be taken over by a mapping.
    const auto channel = message.getChannel();

    if (message.isController())
    {
        const auto controller = message.getControllerNumber();
        const auto value = message.getControllerValue();

        // RPN plumbing first, and it is *not* offered to MIDI Learn: CC 6, 38
        // and 98-101 carry no control value of their own, they carry the halves
        // of a parameter number and a data entry. A mapping on one of them
        // would jerk a parameter every time a controller announced itself.
        if (midi::RpnParser::isRpnController (controller))
        {
            handleRpn (rpnParser.process (channel, controller, value));
        }
        else
        {
            midiControl.handleControllerMessage (channel, controller, value);
        }
    }

    if (message.isNoteOn())
    {
        voiceEngine.noteOn (message.getNoteNumber(), message.getFloatVelocity(), channel);
    }
    else if (message.isNoteOff())
    {
        voiceEngine.noteOff (message.getNoteNumber(), channel);
    }
    else if (message.isPitchWheel())
    {
        // JUCE reports 0-16383 with 8192 at centre. The engine is handed the
        // wheel *position* rather than a semitone count, because which range
        // applies depends on whether this channel is a member of an MPE zone —
        // a decision the engine already has to make and the processor does not.
        constexpr float centre = 8192.0f;
        const auto normalised = (static_cast<float> (message.getPitchWheelValue()) - centre) / centre;
        voiceEngine.setPitchBend (channel, normalised);
    }
    else if (message.isControllerOfType (1))
    {
        // The mod wheel, which is CC 1 on every controller that has one. It is
        // handled here rather than through MIDI Learn because it is a modulation
        // source in its own right (CLAUDE.md §15), not a mapping to a parameter.
        voiceEngine.setModWheel (static_cast<float> (message.getControllerValue()) / 127.0f);
    }
    else if (message.isControllerOfType (midi::timbreController)
             && voiceEngine.getMpeZone().isMemberChannel (channel))
    {
        // CC 74 is MPE's third expression dimension, but only on a member
        // channel of an active zone. Everywhere else it stays an ordinary
        // control change, and therefore an ordinary MIDI Learn target — which is
        // why this branch tests the zone rather than the controller number alone.
        voiceEngine.setTimbre (channel,
                               static_cast<float> (message.getControllerValue()) / 127.0f);
    }
    else if (message.isChannelPressure())
    {
        // Channel aftertouch. Under MPE this addresses the one note on its
        // member channel; everywhere else it reaches every sounding voice, which
        // is what it has always meant.
        voiceEngine.setChannelPressure (
            channel, static_cast<float> (message.getChannelPressureValue()) / 127.0f);
    }
    else if (message.isAftertouch())
    {
        // Polyphonic key pressure names its own note, so it needs no channel
        // convention to be per-note — which is why it predates MPE by decades.
        voiceEngine.setPolyPressure (channel,
                                     message.getNoteNumber(),
                                     static_cast<float> (message.getAfterTouchValue()) / 127.0f);
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

void ApolloAudioProcessor::handleRpn (const midi::RpnMessage& message) noexcept
{
    // AUDIO THREAD.
    if (! message.isValid())
        return;

    // Nothing here writes the engine. Both RPNs name a *setting* that a
    // parameter already owns, and two owners for one value is how a control
    // ends up disagreeing with the interface showing it (ADR-0045). The queued
    // change reaches APVTS on the message thread and comes back down with every
    // other parameter on the next block.
    const auto queue = [this] (int parameterIndex, float plain)
    {
        const auto index = static_cast<std::size_t> (parameterIndex);

        if (parameterIndex < 0 || index >= params::parameterCount())
            return;

        const auto& definition = params::parameterDefinitions[index];
        const auto span = definition.maximum - definition.minimum;

        if (span <= 0.0f)
            return;

        const auto clamped = juce::jlimit (definition.minimum, definition.maximum, plain);

        midiControl.requestParameterChange (parameterIndex,
                                            (clamped - definition.minimum) / span);
    };

    switch (message.type)
    {
        case midi::RpnType::pitchBendSensitivity:
        {
            // Semitones in the coarse half, cents in the fine one. Apollo's
            // range control is whole semitones, so the cents are read and
            // rounded rather than dropped: a controller that asks for 2
            // semitones and 50 cents is asking for more than two, not for two.
            const auto semitones = static_cast<float> (message.valueMsb)
                                 + static_cast<float> (message.valueLsb) / 100.0f;

            // A member channel is describing the per-note range, and the
            // manager channel — or any channel with no zone — the wheel's.
            queue (voiceEngine.getMpeZone().isMemberChannel (message.channel)
                       ? params::indexOfParameter ("mpe_bend_range")
                       : bendRangeParameterIndex,
                   std::round (semitones));
            break;
        }

        case midi::RpnType::mpeConfiguration:
        {
            // The MPE Configuration Message: the standard way a controller
            // announces its zone, so plugging one in configures Apollo instead
            // of leaving the player to find a switch (CLAUDE.md §16.3).
            //
            // The channel it arrives on says which zone: 1 is the lower zone,
            // 16 the upper. A member count of zero disables it.
            const auto members = message.valueMsb;

            if (message.channel != midi::firstMidiChannel
                && message.channel != midi::lastMidiChannel)
                break;

            const auto zone = members <= 0
                                ? midi::MpeZoneType::off
                                : (message.channel == midi::firstMidiChannel
                                       ? midi::MpeZoneType::lower
                                       : midi::MpeZoneType::upper);

            queue (mpeZoneParameterIndex, static_cast<float> (static_cast<int> (zone)));

            if (members > 0)
                queue (mpeMembersParameterIndex, static_cast<float> (members));

            break;
        }

        case midi::RpnType::none:
        default:
            break;
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

    // Picked up once per block, before any message is handled, so every MIDI
    // event in this buffer is matched against the same mapping table. A table
    // that changed halfway through a block would be a difference no one could
    // observe and everyone would have to reason about.
    midiControl.refreshMappings();

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

    // The visualisation tap, last, so the output scope shows what actually
    // leaves the plugin rather than the mix before the gain stage. One linear
    // pass over a buffer that was just written, which is why it costs what it
    // does — see PROJECT-STATE.md §5b (ADR-0047).
    //
    // Written whenever anything is watching, including when the engine produced
    // silence: a source that has stopped must be seen to stop rather than
    // holding its last picture (CLAUDE.md §26.1). Skipped entirely when nothing
    // is — an instance with no editor open should cost what it did before scopes
    // existed, and in a session holding twenty of them that is nineteen.
    if (telemetry->isCapturing())
        telemetry->scope (telemetry::ScopeSource::output)
            .writeMixedToMono (outputs, numOutputChannels, 0, numSamples);
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
        // The document replaced the whole state tree, MIDI mappings included, so
        // the live table is rebuilt from what actually arrived rather than left
        // pointing at the previous project's controller assignments.
        midiControl.restoreFromState();

        // Every parameter may have moved at once. Bumping the counter lets an
        // attached editor resynchronise wholesale instead of inferring a preset
        // load from a burst of individual changes.
        stateReloadCounter.fetch_add (1, std::memory_order_relaxed);
    }
}

} // namespace apollo
