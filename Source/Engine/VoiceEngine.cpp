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

    return index >= dsp::WavetableLibrary::numChoices ? dsp::WavetableLibrary::numChoices - 1 : index;
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

    // The trace runs at a fixed rate in seconds, so its interval in samples is
    // derived from the device's. Floored at one, because a capture chunk is
    // shortened to meet it and a chunk of zero samples would not advance.
    {
        const auto samples = static_cast<int> (preparedSampleRate / telemetry::modulationTraceHz);
        traceInterval = samples > 1 ? samples : 1;
        traceCountdown = traceInterval;
    }

    // Free-running phases restart with the sample rate, and their increments are
    // re-derived from it.
    for (std::size_t i = 0; i < freeRunningLfoPhase.size(); ++i)
    {
        freeRunningLfoPhase[i] = 0.0;
        freeRunningIncrement[i] = static_cast<double> (lfoSettings[i].rateHz) / preparedSampleRate;
    }

    applySourceParameters();

    follower.prepare (preparedSampleRate);

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

    follower.reset();
    followerValue = 0.0f;

    for (auto& voice : voices)
        voice.setFollower (0.0f);

    sustainPedalDown = false;
    heldCount = 0;
    lastNote = std::numeric_limits<float>::quiet_NaN();
    pitchBendSemitones = 0.0f;
    wheelNormalised = 0.0f;
    aftertouch = 0.0f;
    nextStartOrder = 1;

    channelBend.fill (0.0f);
    channelTimbre.fill (0.5f);

    for (auto& voice : voices)
    {
        voice.setPitchBendSemitones (0.0f);
        voice.setPitchBendNormalised (0.0f);
    }
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

void VoiceEngine::noteOn (int midiNote, float velocity, int midiChannel) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return;

    // A velocity of zero is a note-off by another name, and several controllers
    // and sequencers still send it that way.
    if (velocity <= 0.0f)
    {
        noteOff (midiNote, midiChannel);
        return;
    }

    // The keys held down, for the voice modes and for a glide only between
    // held notes (Phase 19e). Bookkeeping only in Poly with no glide, which is
    // how Apollo played before either existed.
    const auto keysWereHeld = heldCount > 0;
    pushHeldKey (midiNote, velocity, midiChannel);

    if (voiceMode != VoiceMode::poly)
    {
        monoNoteOn (midiNote, velocity, midiChannel, keysWereHeld);
        return;
    }

    auto* voice = findVoiceForNewNote();

    if (voice == nullptr)
        return;

    // A glide from the last note, to this one, in Poly: portamento.
    const auto from = glideSource();
    voice->setNextGlide (from, glideSeconds > 0.0f && (keysWereHeld || ! glideOnlyWhenHeld) ? glideSeconds : 0.0f);
    lastNote = static_cast<float> (midiNote);

    const auto order = nextStartOrder++;

    prepareVoiceForNote (*voice, midiChannel);

    if (voice->isActive())
        voice->steal (midiNote, velocity, order, midiChannel);
    else
        voice->startNote (midiNote, velocity, order, midiChannel);
}

void VoiceEngine::prepareVoiceForNote (Voice& target, int midiChannel) noexcept
{
    auto* voice = &target;

    voice->setPitchBendSemitones (pitchBendSemitones);
    voice->setPitchBendNormalised (wheelNormalised);

    // Pitch bend and timbre are *positions*, so a note started while a finger is
    // already displaced adopts what is in force rather than snapping to centre
    // and gliding — which on an MPE controller is every note, since the
    // controller sends the note's initial bend before the note-on. Pressure is
    // deliberately not inherited (ADR-0044); the voice zeroes it itself.
    if (mpeZone.isMemberChannel (midiChannel))
    {
        const auto index = static_cast<std::size_t> (midiChannel - midi::firstMidiChannel);

        voice->setNoteBendSemitones (channelBend[index] * memberBendRange);
        voice->setTimbre (channelTimbre[index]);
    }
    else
    {
        voice->setNoteBendSemitones (0.0f);
        voice->setTimbre (0.5f);
    }

    // Handed over before the note starts, so a free-running LFO adopts the
    // instrument's current phase rather than one block's worth behind it.
    for (int i = 0; i < Voice::numLfos; ++i)
        voice->setFreeRunningLfoPhase (i, freeRunningLfoPhase[static_cast<std::size_t> (i)]);
}

void VoiceEngine::noteOff (int midiNote, int midiChannel) noexcept
{
    removeHeldKey (midiNote, midiChannel);

    if (voiceMode != VoiceMode::poly)
    {
        monoNoteOff (midiNote);

        // Voices left sounding from before the mode changed end as they would
        // have.
        releaseVoicesPlaying (midiNote, midiChannel, 1);
        return;
    }

    releaseVoicesPlaying (midiNote, midiChannel, 0);
}

void VoiceEngine::releaseVoicesPlaying (int midiNote, int midiChannel, int firstVoice) noexcept
{
    for (int i = firstVoice; i < maxPolyphony; ++i)
    {
        auto& voice = voices[static_cast<std::size_t> (i)];

        // A voice fading out its old note to start this one. It still reports
        // the old note as its own, so without this the release found nothing,
        // and the note started afterwards and was never released: a stuck note,
        // whenever a key went up within the few milliseconds of a steal
        // (Phase 15, ADR-0111).
        if (voice.getPendingNote() == midiNote
            && (midiChannel == 0 || voice.getPendingChannel() == 0 || voice.getPendingChannel() == midiChannel))
        {
            voice.releasePendingNote (sustainPedalDown);
            continue;
        }

        // The old note of a voice being stolen is already being cut, and its
        // key no longer owns the voice: a pedal hold recorded now would pass to
        // the note about to start.
        if (voice.isStealing())
            continue;

        if (! voice.isActive() || voice.getMidiNote() != midiNote)
            continue;

        // Under MPE the same note number can sound on several channels at once,
        // so a note-off that ignored the channel would end somebody else's
        // finger. A caller that does not track channels passes 0 and gets the
        // old behaviour, which is every voice playing that note.
        if (midiChannel != 0 && voice.getChannel() != 0
            && voice.getChannel() != midiChannel)
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

void VoiceEngine::setVoicing (VoiceMode mode, float newGlideSeconds, bool onlyWhenHeld) noexcept
{
    voiceMode = mode;
    glideSeconds = std::isfinite (newGlideSeconds) && newGlideSeconds > 0.0f ? newGlideSeconds : 0.0f;
    glideOnlyWhenHeld = onlyWhenHeld;
}

void VoiceEngine::pushHeldKey (int midiNote, float velocity, int midiChannel) noexcept
{
    // The same key again — a controller that sends two note-ons — is one key,
    // and now the newest.
    removeHeldKey (midiNote, midiChannel);

    if (heldCount == static_cast<int> (heldKeys.size()))
    {
        for (std::size_t i = 1; i < heldKeys.size(); ++i)
            heldKeys[i - 1] = heldKeys[i];

        --heldCount;
    }

    heldKeys[static_cast<std::size_t> (heldCount++)] = { midiNote, velocity, midiChannel };
}

void VoiceEngine::removeHeldKey (int midiNote, int midiChannel) noexcept
{
    for (int i = 0; i < heldCount; ++i)
    {
        const auto& key = heldKeys[static_cast<std::size_t> (i)];

        if (key.note != midiNote || (midiChannel != 0 && key.channel != 0 && key.channel != midiChannel))
            continue;

        for (int j = i + 1; j < heldCount; ++j)
            heldKeys[static_cast<std::size_t> (j - 1)] = heldKeys[static_cast<std::size_t> (j)];

        --heldCount;
        return;
    }
}

float VoiceEngine::glideSource() const noexcept
{
    const Voice* newest = nullptr;

    for (const auto& voice : voices)
        if (voice.isActive() && ! voice.isStealing()
            && (newest == nullptr || voice.getStartOrder() > newest->getStartOrder()))
            newest = &voice;

    // Where the newest note is, mid-glide or not; with nothing sounding, the
    // last note played, so a phrase picked up after a rest still slides.
    return newest != nullptr ? newest->getGlidingNote() : lastNote;
}

void VoiceEngine::monoNoteOn (int midiNote, float velocity, int midiChannel, bool keysWereHeld) noexcept
{
    // One voice: the first. Any other still sounding is a tail from before
    // the mode changed, and finishes on its own.
    auto& voice = voices[0];

    const auto glide = glideSeconds > 0.0f && (keysWereHeld || ! glideOnlyWhenHeld) ? glideSeconds : 0.0f;
    const auto from = glideSource();
    lastNote = static_cast<float> (midiNote);

    // Legato: a note played while another is held moves the voice there, its
    // envelopes and LFOs carrying on.
    if (voiceMode == VoiceMode::legato && keysWereHeld && voice.isActive()
        && ! voice.isStealing() && ! voice.isReleasing())
    {
        voice.legatoTo (midiNote, glide);
        return;
    }

    // Otherwise the note takes the voice and starts it again: through the
    // steal's fade if it is sounding, so the change never clicks.
    voice.setNextGlide (from, glide);
    prepareVoiceForNote (voice, midiChannel);

    const auto order = nextStartOrder++;

    if (voice.isActive())
        voice.steal (midiNote, velocity, order, midiChannel);
    else
        voice.startNote (midiNote, velocity, order, midiChannel);
}

void VoiceEngine::monoNoteOff (int midiNote) noexcept
{
    auto& voice = voices[0];

    const auto pending = voice.getPendingNote() == midiNote;
    const auto playing = voice.isActive() && ! voice.isStealing() && voice.getMidiNote() == midiNote;

    // A key that is not the one sounding was only held: letting it go changes
    // nothing that can be heard.
    if (! pending && ! playing)
        return;

    // Another key is still down: back to the newest of them, as a held note
    // does — gliding, since a key is held.
    if (heldCount > 0)
    {
        const auto& top = heldKeys[static_cast<std::size_t> (heldCount - 1)];
        lastNote = static_cast<float> (top.note);

        if (voice.isStealing())
        {
            voice.setNextGlide (voice.getGlidingNote(), glideSeconds);
            voice.steal (top.note, top.velocity, nextStartOrder++, top.channel);
        }
        else
        {
            voice.legatoTo (top.note, glideSeconds);
        }

        return;
    }

    // Nothing held: the note ends, or the pedal holds it.
    if (pending)
    {
        voice.releasePendingNote (sustainPedalDown);
        return;
    }

    if (voice.isReleasing() || voice.isSustainHeld())
        return;

    if (sustainPedalDown)
        voice.setSustainHeld (true);
    else
        voice.releaseNote();
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
    wheelNormalised = wheelBendRange > 0.0f ? semitones / wheelBendRange : 0.0f;

    for (auto& voice : voices)
    {
        voice.setPitchBendSemitones (semitones);
        voice.setPitchBendNormalised (wheelNormalised);
    }
}

//==============================================================================
// Per-note expression

void VoiceEngine::setMpeZone (const midi::MpeZone& newZone) noexcept
{
    if (newZone == mpeZone)
        return;

    mpeZone = newZone;

    // Notes already sounding keep the channel they were started on, so nothing
    // is released or retuned here. Switching a zone mid-performance is a setup
    // change; cutting the sound to acknowledge it would be worse than letting
    // the notes in flight finish under the rule they began under.
}

void VoiceEngine::setPitchBendRange (float wheelSemitones, float memberSemitones) noexcept
{
    if (wheelSemitones == wheelBendRange && memberSemitones == memberBendRange)
        return;

    const auto heldWheel = wheelNormalised;

    wheelBendRange = wheelSemitones;
    memberBendRange = memberSemitones;

    // Re-applied from the wheel *position*, so widening the range while the
    // wheel is pushed moves the pitch immediately rather than at the next wheel
    // message — and moves it to where the wheel actually is.
    setPitchBendSemitones (heldWheel * wheelBendRange);

    for (auto& voice : voices)
    {
        const auto channel = voice.getChannel();

        if (mpeZone.isMemberChannel (channel))
        {
            const auto index = static_cast<std::size_t> (channel - midi::firstMidiChannel);
            voice.setNoteBendSemitones (channelBend[index] * memberBendRange);
        }
    }
}

void VoiceEngine::setPitchBend (int midiChannel, float normalised) noexcept
{
    if (mpeZone.isMemberChannel (midiChannel))
    {
        // A member channel's bend belongs to the one note on it, and is measured
        // in the member range — which is enormous by design, because it is a
        // finger sliding across a keybed rather than a wheel.
        channelBend[static_cast<std::size_t> (midiChannel - midi::firstMidiChannel)] = normalised;

        const auto semitones = normalised * memberBendRange;

        for (auto& voice : voices)
            if (voice.getChannel() == midiChannel)
                voice.setNoteBendSemitones (semitones);

        return;
    }

    if (! mpeZone.appliesTo (midiChannel, 0))
        return;

    // The wheel: the manager channel under MPE, and every channel without it.
    setPitchBendSemitones (normalised * wheelBendRange);
}

void VoiceEngine::setChannelPressure (int midiChannel, float value) noexcept
{
    for (auto& voice : voices)
        if (mpeZone.appliesTo (midiChannel, voice.getChannel()))
            voice.setPressure (value);

    // Kept so getAftertouch still reports something meaningful for a plain
    // controller, where "the channel pressure" is a single well-defined value.
    if (! mpeZone.isActive())
        aftertouch = value;
}

void VoiceEngine::setPolyPressure (int midiChannel, int midiNote, float value) noexcept
{
    // Polyphonic key pressure names its note, so it needs no channel convention
    // to be per-note — which is precisely why it predates MPE by thirty years.
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.getMidiNote() != midiNote)
            continue;

        if (midiChannel != 0 && voice.getChannel() != 0
            && voice.getChannel() != midiChannel)
            continue;

        voice.setPressure (value);
    }
}

void VoiceEngine::setTimbre (int midiChannel, float value) noexcept
{
    if (mpeZone.isMemberChannel (midiChannel))
        channelTimbre[static_cast<std::size_t> (midiChannel - midi::firstMidiChannel)] = value;

    for (auto& voice : voices)
        if (mpeZone.appliesTo (midiChannel, voice.getChannel()))
            voice.setTimbre (value);
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

void VoiceEngine::setTransportPosition (double beat, bool playing) noexcept
{
    if (! playing || ! std::isfinite (beat))
        return;

    for (std::size_t i = 0; i < lfoSettings.size(); ++i)
    {
        const auto beats = lfoSettings[i].tempoBeats;

        if (lfoSettings[i].retrigger || ! (beats > 0.0))
            continue;

        const auto cycles = beat / beats;
        freeRunningLfoPhase[i] = cycles - std::floor (cycles);
    }
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

    masterModulated = false;

    for (const auto& slot : routing.slots)
        masterModulated = masterModulated || (slot.isActive() && slot.destination == dsp::ModDestination::masterGain);

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

void VoiceEngine::setMacro (int index, const dsp::MacroInput& input) noexcept
{
    if (index < 0 || index >= dsp::numMacros)
        return;

    auto& current = macroInputs[static_cast<std::size_t> (index)];

    if (input == current)
        return;

    current = input;

    for (auto& voice : voices)
        voice.setMacro (index, input);
}

void VoiceEngine::setFollowerSettings (float attackMs, float releaseMs, float gainDb) noexcept
{
    follower.setTimes (attackMs, releaseMs);
    follower.setGainDb (gainDb);
}

void VoiceEngine::updateEffectModulation() noexcept
{
    const auto first = static_cast<std::size_t> (dsp::firstEffectDestination);

    // The newest sounding voice, releasing ones included: a tail is still the
    // last note played until another arrives.
    const Voice* newest = nullptr;

    for (const auto& voice : voices)
        if (voice.isActive() && (newest == nullptr || voice.getStartOrder() > newest->getStartOrder()))
            newest = &voice;

    if (newest != nullptr)
    {
        for (std::size_t i = 0; i < effectOffsets.size(); ++i)
            effectOffsets[i] = newest->getModulationOffset (static_cast<dsp::ModDestination> (first + i));

        masterOffset = newest->getModulationOffset (dsp::ModDestination::masterGain);
        return;
    }

    // Nothing sounds: only the sources that exist without a note, the macros
    // among them, moved by those sources where they are destinations.
    dsp::ModSourceValues sources {};

    sources[static_cast<std::size_t> (dsp::ModSource::modWheel)] = modWheel;
    sources[static_cast<std::size_t> (dsp::ModSource::pitchBend)] = wheelNormalised;
    sources[static_cast<std::size_t> (dsp::ModSource::aftertouch)] = aftertouch;
    sources[static_cast<std::size_t> (dsp::ModSource::follower)] = followerValue;

    dsp::ModDestinationValues destinations {};
    dsp::evaluateModulationWithMacros (routing, sources, macroInputs, destinations);

    for (std::size_t i = 0; i < effectOffsets.size(); ++i)
        effectOffsets[i] = destinations[first + i];

    masterOffset = destinations[static_cast<std::size_t> (dsp::ModDestination::masterGain)];
}

float VoiceEngine::getEffectOffset (dsp::ModDestination destination) const noexcept
{
    const auto index = static_cast<int> (destination) - static_cast<int> (dsp::firstEffectDestination);

    return index >= 0 && index < dsp::numEffectDestinations
             ? effectOffsets[static_cast<std::size_t> (index)]
             : 0.0f;
}

void VoiceEngine::setAftertouch (float value) noexcept
{
    if (value == aftertouch)
        return;

    aftertouch = value;

    for (auto& voice : voices)
        voice.setPressure (aftertouch);
}

void VoiceEngine::applyFilterParameters() noexcept
{
    VoiceFilterSettings settings;

    const auto resolve = [this] (const FilterSlotParameters& slot)
    {
        FilterSlotSettings resolved;

        resolved.mode = slot.mode;
        resolved.drive = slot.drive;
        resolved.mix = slot.mix;
        resolved.keyTrack = slot.keyTrack;
        resolved.cutoffHz = slot.cutoffHz;
        resolved.q = slot.q;
        resolved.coefficients.set (slot.mode, slot.cutoffHz, slot.q, preparedSampleRate);

        return resolved;
    };

    settings.filter1 = resolve (filterParameters.filter1);
    settings.filter2 = resolve (filterParameters.filter2);
    settings.routing = filterParameters.routing;
    settings.inputs = filterParameters.inputs;

    for (auto& voice : voices)
        voice.setFilters (settings);
}

void VoiceEngine::applySourceParameters() noexcept
{
    // Rebuilt only when their inputs actually moved: a layout costs a handful
    // of transcendental calls per unison voice, and the inputs are static for
    // the overwhelming majority of blocks.
    const auto updateLayout = [] (dsp::UnisonLayout& layout, const OscillatorParameters& osc) noexcept
    {
        if (! layout.matches (osc.unisonVoices, osc.detune, osc.spread, osc.detuneCurve, osc.unisonBlend))
            layout.update (osc.unisonVoices, osc.detune, osc.spread, osc.detuneCurve, osc.unisonBlend);
    };

    updateLayout (unison1, parameters.osc1);
    updateLayout (unison2, parameters.osc2);

    VoiceSourceSettings settings;

    const auto resolveOscillator = [this] (OscillatorSourceSettings& resolved,
                                           const OscillatorParameters& osc,
                                           const dsp::UnisonLayout& layout,
                                           int oscillator) noexcept
    {
        // The USER choice is each oscillator's own slot (Phase 13g).
        resolved.table = &library.getTable (dsp::WavetableLibrary::slotForChoice (oscillator, osc.wavetableIndex));
        resolved.unison = &layout;
        resolved.position = osc.position;
        resolved.level = osc.level;
        resolved.pan = osc.pan;
        resolved.tuneSemitones = osc.tuneSemitones;
        resolved.startPhase = osc.startPhase;
        resolved.phaseRandom = osc.phaseRandom;
        resolved.warpMode = osc.warpMode;
        resolved.warpAmount = osc.warpAmount;
    };

    resolveOscillator (settings.osc1, parameters.osc1, unison1, 1);
    resolveOscillator (settings.osc2, parameters.osc2, unison2, 2);

    settings.subTable = &library.getSubTable (parameters.subWaveform);
    settings.subLevel = parameters.subLevel;
    settings.subOctave = parameters.subOctave;
    settings.subPhase = parameters.subPhase;
    settings.subTracksOsc1 = parameters.subTracksOsc1;

    settings.noiseLevel = parameters.noiseLevel;
    settings.noiseType = static_cast<dsp::NoiseType> (
        parameters.noiseType < 0 ? 0 : (parameters.noiseType >= dsp::numNoiseTypes ? dsp::numNoiseTypes - 1
                                                                                     : parameters.noiseType));
    settings.noiseKeyTrack = parameters.noiseKeyTrack;
    settings.noiseLoop = parameters.noiseLoop;

    if (settings.noiseType == dsp::NoiseType::sample)
    {
        // The user's sample. With none loaded — none chosen yet, or one that
        // failed to load — the noise plays white rather than nothing, so the
        // instrument never goes quiet because content is missing (CLAUDE.md
        // §33). The interface says which it is.
        settings.noiseTransient = noiseSample.get();

        if (settings.noiseTransient == nullptr)
            settings.noiseType = dsp::NoiseType::white;
    }
    else
    {
        settings.noiseTransient = dsp::isTransient (settings.noiseType)
                                    ? &transients->get (dsp::transientIndex (settings.noiseType))
                                    : nullptr;
    }

    for (auto& voice : voices)
        voice.setSources (settings);
}

void VoiceEngine::allNotesOff() noexcept
{
    heldCount = 0;

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

    // A wavetable may have been replaced underneath the voices since the last
    // block. They hold their table pointers until they are handed new ones, and
    // a swap is not a parameter change — so this is what makes a loaded table
    // actually play, and what bounds how long a retired one can still be in use
    // (WavetableLibrary.h).
    // Both asked every block, not one or the other: each call consumes its
    // change.
    const auto tablesChanged = library.takeGenerationChange();
    const auto sampleChanged = noiseSample.takeGenerationChange();

    if (tablesChanged || sampleChanged)
        applySourceParameters();

    library.beginBlock();
    noiseSample.beginBlock();

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
    //
    // The decision to capture is taken once here rather than inside the voice
    // loop, and when nobody is watching the call below is the one this engine
    // has always made.
    if (telemetryHub != nullptr && telemetryHub->isCapturing())
    {
        renderVoicesCapturing (output, numChannels, startSample, numSamples);
    }
    else
    {
        for (auto& voice : voices)
            voice.renderAdding (output, numChannels, startSample, numSamples);
    }

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* destination = output[channel];

        if (destination == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
            destination[startSample + i] *= outputGain;
    }

    // How loud that was, for the follower (Phase 18e): the summed voices at the
    // level the effects receive them, before the master. Measured as each
    // stretch between MIDI events is rendered, and handed to the voices at the
    // end of it, so the matrix reads it at most one stretch late — and the
    // voices are told only when it moved, which a held chord's sustain does
    // not do often.
    follower.process (output, numChannels, startSample, numSamples);

    const auto value = follower.getValue();

    if (value != followerValue)
    {
        followerValue = value;

        for (auto& voice : voices)
            voice.setFollower (followerValue);
    }
}

const Voice* VoiceEngine::findVoiceToTrace() const noexcept
{
    const Voice* newest = nullptr;

    for (const auto& voice : voices)
    {
        if (! voice.isActive())
            continue;

        if (newest == nullptr || voice.getStartOrder() > newest->getStartOrder())
            newest = &voice;
    }

    return newest;
}

void VoiceEngine::publishModulation() noexcept
{
    // AUDIO THREAD. Reached only while something is watching.
    using Modulator = telemetry::ModulatorSource;

    const auto* voice = findVoiceToTrace();

    // Recomputed every time rather than tracked, because it is eight bools
    // derived from sixteen slots and tracking it would be a cache to invalidate.
    std::array<bool, telemetry::modulatorSourceCount> isRouted {};

    for (const auto& slot : routing.slots)
    {
        if (! slot.isActive())
            continue;

        const auto source = static_cast<int> (slot.source);
        const auto firstEnvelope = static_cast<int> (dsp::ModSource::envelope1);
        const auto lastLfo = static_cast<int> (dsp::ModSource::lfo4);

        if (source >= firstEnvelope && source <= lastLfo)
            isRouted[static_cast<std::size_t> (source - firstEnvelope)] = true;

        // A modulator scaling another routing is in use too.
        const auto via = static_cast<int> (slot.via);

        if (via >= firstEnvelope && via <= lastLfo)
            isRouted[static_cast<std::size_t> (via - firstEnvelope)] = true;

        if (slot.source == dsp::ModSource::follower || slot.via == dsp::ModSource::follower)
            isRouted[static_cast<std::size_t> (Modulator::follower)] = true;
    }

    // Envelope 1 shapes the voice whether or not the matrix reads it, so it is
    // always doing something and is always drawn as such.
    isRouted[static_cast<std::size_t> (Modulator::envelope1)] = true;

    auto& instrument = telemetryHub->snapshot();

    // The follower is the engine's, not a voice's: its value whether or not
    // anything sounds, so a release is seen to fall away after the last note.
    telemetryHub->trace (Modulator::follower).write (followerValue);
    instrument.routed[static_cast<std::size_t> (Modulator::follower)]
        .store (isRouted[static_cast<std::size_t> (Modulator::follower)], std::memory_order_relaxed);
    instrument.envelopeStage[static_cast<std::size_t> (Modulator::follower)]
        .store (static_cast<int> (dsp::EnvelopeStage::idle), std::memory_order_relaxed);

    // The envelopes and LFOs, from the voice being traced.
    for (std::size_t i = 0; i < static_cast<std::size_t> (Modulator::follower); ++i)
    {
        const auto source = static_cast<Modulator> (i);
        const auto isEnvelope = i < static_cast<std::size_t> (Voice::numEnvelopes);
        const auto generator = static_cast<int> (
            isEnvelope ? i : i - static_cast<std::size_t> (Voice::numEnvelopes));

        // The generator's own output, even when nothing routes it. An LFO the
        // matrix ignores is not advanced at all, so it genuinely holds one value
        // and the flat line is literally true — which is why the routed flag is
        // published beside it rather than the trace being suppressed.
        const auto value = voice == nullptr
                             ? 0.0f
                             : (isEnvelope ? voice->getEnvelopeValue (generator)
                                           : voice->getLfoValue (generator));

        telemetryHub->trace (source).write (value);

        instrument.routed[i].store (isRouted[i], std::memory_order_relaxed);

        instrument.envelopeStage[i].store (
            isEnvelope && voice != nullptr
                ? static_cast<int> (voice->getEnvelopeStage (generator))
                : static_cast<int> (dsp::EnvelopeStage::idle),
            std::memory_order_relaxed);
    }

    instrument.activeVoices.store (getActiveVoiceCount(), std::memory_order_relaxed);
    instrument.polyphony.store (polyphony, std::memory_order_relaxed);

    for (std::size_t i = 0; i < telemetry::wavetableDisplayCount; ++i)
    {
        const auto oscillator = static_cast<int> (i) + 1;

        // The slot rather than the choice, so the display draws an
        // oscillator's own user table when that is what it plays.
        instrument.wavetableIndex[i].store (
            dsp::WavetableLibrary::slotForChoice (
                oscillator, oscillator == 2 ? parameters.osc2.wavetableIndex : parameters.osc1.wavetableIndex),
            std::memory_order_relaxed);

        // The oscillator's effective position, from the voice the traces follow.
        // With nothing sounding there is no modulation to add, so the parameter
        // is the answer — and the display then sits where the control says,
        // which is what someone moving that control expects to see.
        instrument.wavetablePosition[i].store (
            voice != nullptr ? voice->getOscillatorPosition (oscillator)
                             : (oscillator == 2 ? parameters.osc2.position
                                                : parameters.osc1.position),
            std::memory_order_relaxed);

        // The warp, the same way: the picture is of the cycle being played.
        const auto& osc = oscillator == 2 ? parameters.osc2 : parameters.osc1;

        instrument.wavetableWarpMode[i].store (static_cast<int> (osc.warpMode),
                                               std::memory_order_relaxed);
        instrument.wavetableWarpAmount[i].store (
            voice != nullptr ? voice->getOscillatorWarpAmount (oscillator) : osc.warpAmount,
            std::memory_order_relaxed);
    }
}

void VoiceEngine::renderVoicesCapturing (float* const* output, int numChannels, int startSample,
                                         int numSamples) noexcept
{
    // AUDIO THREAD. Reached only while something is watching.
    //
    // Each source's trace is the sum of every voice producing it, which is the
    // only reading of "what oscillator 1 is producing" that stays true when more
    // than one note is held. So the voices are summed into scratch first and the
    // scratch is published once, rather than each voice publishing its own
    // fragment of a signal it only partly owns.

    static constexpr std::array<telemetry::ScopeSource, numTaps> destinations {
        telemetry::ScopeSource::oscillator1,
        telemetry::ScopeSource::oscillator2,
        telemetry::ScopeSource::sub,
        telemetry::ScopeSource::noise,
        telemetry::ScopeSource::postFilter,
    };

    // Advanced by the chunk actually rendered, not by the maximum: a chunk that
    // was shortened to land on a trace boundary would otherwise leave the
    // samples between the two unrendered.
    for (int offset = 0; offset < numSamples;)
    {
        auto chunk = numSamples - offset;

        if (chunk > captureChunkSamples)
            chunk = captureChunkSamples;

        // Shortened to land exactly on the next trace boundary, so the trace
        // keeps a fixed rate without the capture having to run in chunks small
        // enough to divide it — which measured more than twice as expensive at
        // thirty-two voices. Only about one chunk in three is shortened at all.
        if (chunk > traceCountdown)
            chunk = traceCountdown;

        // Cleared rather than accumulated across chunks, so a chunk in which
        // nothing sounds publishes silence. That is what makes a source which
        // stops *seen* to stop instead of holding its last picture: the zeroes
        // are captured, not inferred from the absence of a write.
        for (auto& scratch : tapScratch)
            for (int i = 0; i < chunk; ++i)
                scratch[static_cast<std::size_t> (i)] = 0.0f;

        VoiceTaps taps;
        taps.oscillator1 = tapScratch[static_cast<std::size_t> (Tap::oscillator1)].data();
        taps.oscillator2 = tapScratch[static_cast<std::size_t> (Tap::oscillator2)].data();
        taps.sub = tapScratch[static_cast<std::size_t> (Tap::sub)].data();
        taps.noise = tapScratch[static_cast<std::size_t> (Tap::noise)].data();
        taps.postFilter = tapScratch[static_cast<std::size_t> (Tap::postFilter)].data();

        for (auto& voice : voices)
            voice.renderAdding (output, numChannels, startSample + offset, chunk, taps);

        for (std::size_t tap = 0; tap < numTaps; ++tap)
            telemetryHub->scope (destinations[tap]).write (tapScratch[tap].data(), chunk);

        // The modulation traces are sampled here rather than captured, because a
        // modulator is a slow value and not a waveform: one entry every 7.8 ms
        // *is* the picture, where a scope's window is a thousand samples thrown
        // away to draw a hundred and ninety-two.
        traceCountdown -= chunk;

        if (traceCountdown <= 0)
        {
            traceCountdown += traceInterval;
            publishModulation();
        }

        offset += chunk;
    }
}

} // namespace apollo::engine
