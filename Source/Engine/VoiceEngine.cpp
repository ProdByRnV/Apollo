#include "Engine/VoiceEngine.h"

#include <cmath>

namespace apollo::engine
{

namespace
{
[[nodiscard]] int clampPolyphony (int requested) noexcept
{
    if (requested < 1)
        return 1;

    if (requested > VoiceEngine::maxPolyphony)
        return VoiceEngine::maxPolyphony;

    return requested;
}

[[nodiscard]] int clampWavetableIndex (int index) noexcept
{
    if (index < 0)
        return 0;

    return index >= dsp::WavetableLibrary::numTables ? dsp::WavetableLibrary::numTables - 1 : index;
}
} // namespace

VoiceEngine::VoiceEngine()
{
    // The voices start out holding default settings, which point at no table
    // and would render silence. Resolving the default parameters here means an
    // engine is playable the moment it is constructed, without waiting for the
    // processor's first block.
    applySourceParameters();
    applyFilterParameters();
}

void VoiceEngine::prepare (double sampleRate) noexcept
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        voices[i].prepare (sampleRate);

        // Spread the voices evenly around the cycle. Fixed per voice, so
        // rendering stays reproducible, but distinct across voices so a chord
        // attack does not sum coherently — see Voice::setStartPhase.
        voices[i].setStartPhase (static_cast<double> (i) / static_cast<double> (voices.size()));

        // A distinct, non-zero seed per voice, so simultaneous voices produce
        // independent noise rather than N copies of one stream.
        voices[i].setNoiseSeed (static_cast<std::uint32_t> (i) + 1u);

        // And a separate one for the modulation random sources, for the same
        // reason: a chord whose notes all drew the same "random" value is one
        // modulation applied N times.
        voices[i].setModulationSeed (static_cast<std::uint32_t> (i) * 2654435761u + 17u);
    }

    // Free-running phases restart with the sample rate, and their increments are
    // re-derived from it.
    for (std::size_t i = 0; i < freeRunningLfoPhase.size(); ++i)
    {
        freeRunningLfoPhase[i] = 0.0;
        freeRunningIncrement[i] = static_cast<double> (lfoSettings[i].rateHz) / preparedSampleRate;
    }

    applySourceParameters();

    for (auto& voice : voices)
        voice.setAmplitudeEnvelope (amplitudeEnvelope);

    // Coefficients depend on the sample rate, so they are rebuilt here rather
    // than carried over from whatever rate the engine last ran at.
    applyFilterParameters();

    reset();
}

void VoiceEngine::reset() noexcept
{
    for (auto& voice : voices)
        voice.reset();

    sustainPedalDown = false;
    pitchBendSemitones = 0.0f;
    nextStartOrder = 1;

    for (auto& voice : voices)
        voice.setPitchBendSemitones (0.0f);
}

void VoiceEngine::setPolyphony (int numVoices) noexcept
{
    const int newPolyphony = clampPolyphony (numVoices);

    // Voices that fall outside the new limit are released rather than cut, so
    // lowering polyphony while notes are sounding fades them out instead of
    // producing a step.
    for (int i = newPolyphony; i < maxPolyphony; ++i)
        voices[static_cast<std::size_t> (i)].releaseNote();

    polyphony = newPolyphony;
}

Voice* VoiceEngine::findVoiceForNewNote() noexcept
{
    // 1. A free voice, lowest index first.
    for (int i = 0; i < polyphony; ++i)
    {
        auto& voice = voices[static_cast<std::size_t> (i)];

        if (! voice.isActive())
            return &voice;
    }

    // 2. Otherwise steal. A voice already releasing is the least destructive
    //    choice: its note has ended and only its tail is still sounding.
    //    Oldest first, so the tail nearest to finishing goes.
    Voice* oldestReleasing = nullptr;
    Voice* oldest = nullptr;

    for (int i = 0; i < polyphony; ++i)
    {
        auto& voice = voices[static_cast<std::size_t> (i)];

        // Strictly-less comparisons, scanning ascending, make the choice
        // deterministic: equal ages resolve to the lowest index, so the same
        // input always steals the same voice.
        if (voice.isReleasing()
            && (oldestReleasing == nullptr || voice.getStartOrder() < oldestReleasing->getStartOrder()))
            oldestReleasing = &voice;

        if (oldest == nullptr || voice.getStartOrder() < oldest->getStartOrder())
            oldest = &voice;
    }

    // 3. Failing that, the oldest voice overall.
    return oldestReleasing != nullptr ? oldestReleasing : oldest;
}

void VoiceEngine::noteOn (int midiNote, float velocity) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return;

    // A velocity of zero is a note-off by another name, and several controllers
    // and sequencers still send it that way.
    if (velocity <= 0.0f)
    {
        noteOff (midiNote);
        return;
    }

    auto* voice = findVoiceForNewNote();

    if (voice == nullptr)
        return;

    const auto order = nextStartOrder++;

    voice->setPitchBendSemitones (pitchBendSemitones);

    // Handed over before the note starts, so a free-running LFO adopts the
    // instrument's current phase rather than one block's worth behind it.
    for (int i = 0; i < Voice::numLfos; ++i)
        voice->setFreeRunningLfoPhase (i, freeRunningLfoPhase[static_cast<std::size_t> (i)]);

    if (voice->isActive())
        voice->steal (midiNote, velocity, order);
    else
        voice->startNote (midiNote, velocity, order);
}

void VoiceEngine::noteOff (int midiNote) noexcept
{
    for (int i = 0; i < maxPolyphony; ++i)
    {
        auto& voice = voices[static_cast<std::size_t> (i)];

        if (! voice.isActive() || voice.getMidiNote() != midiNote)
            continue;

        // Already fading, or already let go: nothing further to do.
        if (voice.isReleasing() || voice.isSustainHeld())
            continue;

        if (sustainPedalDown)
            voice.setSustainHeld (true);
        else
            voice.releaseNote();
    }
}

void VoiceEngine::setSustainPedal (bool isDown) noexcept
{
    if (sustainPedalDown == isDown)
        return;

    sustainPedalDown = isDown;

    if (isDown)
        return;

    // Pedal up: everything the pedal was holding is released now.
    for (auto& voice : voices)
    {
        if (voice.isSustainHeld())
        {
            voice.setSustainHeld (false);
            voice.releaseNote();
        }
    }
}

void VoiceEngine::setPitchBendSemitones (float semitones) noexcept
{
    pitchBendSemitones = semitones;

    for (auto& voice : voices)
        voice.setPitchBendSemitones (semitones);
}

//==============================================================================
// Source configuration

void VoiceEngine::setSourceParameters (const SourceParameters& newParameters) noexcept
{
    if (newParameters == parameters)
        return;

    parameters = newParameters;
    parameters.osc1.wavetableIndex = clampWavetableIndex (parameters.osc1.wavetableIndex);
    parameters.osc2.wavetableIndex = clampWavetableIndex (parameters.osc2.wavetableIndex);

    applySourceParameters();
}

void VoiceEngine::setWavetableIndex (int index) noexcept
{
    auto updated = parameters;
    updated.osc1.wavetableIndex = clampWavetableIndex (index);

    setSourceParameters (updated);
}

void VoiceEngine::setWavetablePosition (float normalisedPosition) noexcept
{
    auto updated = parameters;
    updated.osc1.position = normalisedPosition;

    setSourceParameters (updated);
}

void VoiceEngine::setAmplitudeEnvelope (const dsp::EnvelopeSettings& newSettings) noexcept
{
    if (newSettings == amplitudeEnvelope)
        return;

    amplitudeEnvelope = newSettings;

    for (auto& voice : voices)
        voice.setAmplitudeEnvelope (amplitudeEnvelope);
}

void VoiceEngine::setFilterParameters (const FilterParameters& newParameters) noexcept
{
    if (newParameters == filterParameters)
        return;

    filterParameters = newParameters;
    applyFilterParameters();
}

void VoiceEngine::setEnvelopeSettings (int index, const dsp::EnvelopeSettings& newSettings) noexcept
{
    if (index < 0 || index >= Voice::numEnvelopes)
        return;

    if (index == 0)
    {
        // Envelope 0 is the amplitude envelope, which already has a setter that
        // keeps its own copy for comparison. Routing through it keeps one path.
        setAmplitudeEnvelope (newSettings);
        return;
    }

    for (auto& voice : voices)
        voice.setEnvelopeSettings (index, newSettings);
}

void VoiceEngine::setLfoSettings (int index, const dsp::LfoSettings& newSettings) noexcept
{
    if (index < 0 || index >= Voice::numLfos)
        return;

    const auto slot = static_cast<std::size_t> (index);

    if (newSettings == lfoSettings[slot])
        return;

    lfoSettings[slot] = newSettings;

    // The engine's own free-running phase has to advance at the same rate as the
    // voices' LFOs, or a note started later would adopt a phase that no longer
    // matches what the sounding voices are doing.
    freeRunningIncrement[slot] = static_cast<double> (newSettings.rateHz) / preparedSampleRate;

    for (auto& voice : voices)
        voice.setLfoSettings (index, newSettings);
}

void VoiceEngine::setModulationRouting (const dsp::ModulationRouting& newRouting) noexcept
{
    if (newRouting == routing)
        return;

    routing = newRouting;

    for (auto& voice : voices)
        voice.setModulationRouting (routing);
}

void VoiceEngine::setModWheel (float value) noexcept
{
    if (value == modWheel)
        return;

    modWheel = value;

    for (auto& voice : voices)
        voice.setModWheel (modWheel);
}

void VoiceEngine::setAftertouch (float value) noexcept
{
    if (value == aftertouch)
        return;

    aftertouch = value;

    for (auto& voice : voices)
        voice.setAftertouch (aftertouch);
}

void VoiceEngine::applyFilterParameters() noexcept
{
    VoiceFilterSettings settings;

    const auto resolve = [this] (const FilterSlotParameters& slot)
    {
        FilterSlotSettings resolved;

        resolved.mode = slot.mode;
        resolved.drive = slot.drive;
        resolved.cutoffHz = slot.cutoffHz;
        resolved.q = slot.q;
        resolved.coefficients.set (slot.cutoffHz, slot.q, preparedSampleRate);

        return resolved;
    };

    settings.filter1 = resolve (filterParameters.filter1);
    settings.filter2 = resolve (filterParameters.filter2);
    settings.routing = filterParameters.routing;

    for (auto& voice : voices)
        voice.setFilters (settings);
}

void VoiceEngine::applySourceParameters() noexcept
{
    // Rebuilt only when their inputs actually moved: a layout costs a handful
    // of transcendental calls per unison voice, and the inputs are static for
    // the overwhelming majority of blocks.
    if (! unison1.matches (parameters.osc1.unisonVoices, parameters.osc1.detune, parameters.osc1.spread))
        unison1.update (parameters.osc1.unisonVoices, parameters.osc1.detune, parameters.osc1.spread);

    if (! unison2.matches (parameters.osc2.unisonVoices, parameters.osc2.detune, parameters.osc2.spread))
        unison2.update (parameters.osc2.unisonVoices, parameters.osc2.detune, parameters.osc2.spread);

    VoiceSourceSettings settings;

    settings.osc1.table = &library.getTable (parameters.osc1.wavetableIndex);
    settings.osc1.unison = &unison1;
    settings.osc1.position = parameters.osc1.position;
    settings.osc1.level = parameters.osc1.level;
    settings.osc1.pan = parameters.osc1.pan;
    settings.osc1.tuneSemitones = parameters.osc1.tuneSemitones;

    settings.osc2.table = &library.getTable (parameters.osc2.wavetableIndex);
    settings.osc2.unison = &unison2;
    settings.osc2.position = parameters.osc2.position;
    settings.osc2.level = parameters.osc2.level;
    settings.osc2.pan = parameters.osc2.pan;
    settings.osc2.tuneSemitones = parameters.osc2.tuneSemitones;

    settings.subTable = &library.getSubTable();
    settings.subLevel = parameters.subLevel;
    settings.subOctave = parameters.subOctave;

    settings.noiseLevel = parameters.noiseLevel;

    for (auto& voice : voices)
        voice.setSources (settings);
}

void VoiceEngine::allNotesOff() noexcept
{
    for (auto& voice : voices)
    {
        voice.setSustainHeld (false);
        voice.releaseNote();
    }
}

int VoiceEngine::getActiveVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;

    return count;
}

void VoiceEngine::render (float* const* output, int numChannels, int startSample, int numSamples) noexcept
{
    if (output == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    // The engine is the origin of the signal, so it clears rather than adds.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* destination = output[channel];

        if (destination == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
            destination[startSample + i] = 0.0f;
    }

    // The shared phases advance whether or not anything is sounding, which is
    // the whole point of a free-running LFO. Advanced once per block rather than
    // per sample: nothing reads them except a note start, so no observer exists
    // between blocks that could tell the difference.
    for (std::size_t i = 0; i < freeRunningLfoPhase.size(); ++i)
    {
        auto phase = freeRunningLfoPhase[i]
                   + freeRunningIncrement[i] * static_cast<double> (numSamples);

        phase -= std::floor (phase);

        freeRunningLfoPhase[i] = std::isfinite (phase) ? phase : 0.0;
    }

    // Voices beyond the polyphony limit may still be releasing after a
    // polyphony change, so every voice is rendered, not just the first N.
    for (auto& voice : voices)
        voice.renderAdding (output, numChannels, startSample, numSamples);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* destination = output[channel];

        if (destination == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
            destination[startSample + i] *= outputGain;
    }
}

} // namespace apollo::engine
