#include "Engine/VoiceEngine.h"

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
} // namespace

void VoiceEngine::prepare (double sampleRate) noexcept
{
    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        voices[i].prepare (sampleRate);

        // Spread the voices evenly around the cycle. Fixed per voice, so
        // rendering stays reproducible, but distinct across voices so a chord
        // attack does not sum coherently — see Voice::setStartPhase.
        voices[i].setStartPhase (static_cast<double> (i) / static_cast<double> (voices.size()));
    }

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
