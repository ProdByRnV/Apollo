#include "Engine/Voice.h"

#include <cmath>

namespace apollo::engine
{

namespace
{

/** Concert-pitch reference. A4 = MIDI note 69 = 440 Hz.

    Fixed for now; a global tuning parameter is a product decision, not a
    Phase 3 one.
*/
constexpr double referenceFrequency = 440.0;
constexpr double referenceNote = 69.0;

/** @returns the frequency of a (possibly fractional) MIDI note number. */
[[nodiscard]] double midiNoteToFrequency (double midiNote) noexcept
{
    return referenceFrequency * std::pow (2.0, (midiNote - referenceNote) / 12.0);
}

/** @returns a positive ramp increment covering the full 0-1 range in @p seconds.

    Guards against a zero or negative sample rate so a mis-prepared voice
    degrades to "instant" rather than producing infinity or NaN, which would
    propagate through the whole mix (CLAUDE.md §34.2).
*/
[[nodiscard]] double rampIncrement (double seconds, double sampleRate) noexcept
{
    const double samples = seconds * sampleRate;
    return samples > 1.0 ? 1.0 / samples : 1.0;
}

} // namespace

void Voice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    oscillator.setSampleRate (sampleRate);

    attackIncrement = rampIncrement (attackSeconds, sampleRate);
    releaseDecrement = rampIncrement (releaseSeconds, sampleRate);
    stealDecrement = rampIncrement (stealSeconds, sampleRate);

    reset();
}

void Voice::setStartPhase (double newStartPhase) noexcept
{
    startPhase = newStartPhase - std::floor (newStartPhase);
}

void Voice::setWavetable (const dsp::Wavetable* table) noexcept
{
    oscillator.setTable (table);
}

void Voice::setWavetablePosition (float normalisedPosition) noexcept
{
    oscillator.setPosition (normalisedPosition);
}

void Voice::reset() noexcept
{
    stage = VoiceStage::idle;
    oscillator.resetPhase (startPhase);
    oscillator.setFrequency (0.0);
    envelopeLevel = 0.0;
    note = -1;
    noteVelocity = 0.0f;
    sustainHeld = false;
    pendingNote = -1;
    pendingVelocity = 0.0f;
    pendingStartOrder = 0;

    // Pitch bend is a channel-wide value, not a per-note one, so it is
    // deliberately NOT cleared here: a voice reused while the wheel is held
    // must adopt the current bend, not snap back to centre.
}

void Voice::beginNote (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept
{
    note = midiNote;
    noteVelocity = velocity;
    startOrder = newStartOrder;
    sustainHeld = false;

    // Every note restarts from this voice's own fixed offset. Reproducible,
    // because the offset is fixed per voice rather than random, but decorrelated
    // across voices so a chord attack does not sum coherently. Free-running and
    // user-controlled phase remain open oscillator design choices.
    oscillator.resetPhase (startPhase);
    envelopeLevel = 0.0;
    stage = VoiceStage::attack;

    updatePhaseIncrement();
}

void Voice::startNote (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept
{
    pendingNote = -1;
    beginNote (midiNote, velocity, newStartOrder);
}

void Voice::steal (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept
{
    if (stage == VoiceStage::idle)
    {
        startNote (midiNote, velocity, newStartOrder);
        return;
    }

    // Hold the new note until the fade-out completes.
    pendingNote = midiNote;
    pendingVelocity = velocity;
    pendingStartOrder = newStartOrder;
    stage = VoiceStage::stealing;

    // The stolen note no longer belongs to any held key.
    sustainHeld = false;
}

void Voice::releaseNote() noexcept
{
    if (stage == VoiceStage::attack || stage == VoiceStage::sustaining)
        stage = VoiceStage::releasing;
}

void Voice::setPitchBendSemitones (float semitones) noexcept
{
    pitchBendSemitones = semitones;

    if (stage != VoiceStage::idle)
        updatePhaseIncrement();
}

void Voice::updatePhaseIncrement() noexcept
{
    const double bentNote = static_cast<double> (note) + static_cast<double> (pitchBendSemitones);
    oscillator.setFrequency (midiNoteToFrequency (bentNote));
}

double Voice::nextEnvelopeValue() noexcept
{
    switch (stage)
    {
        case VoiceStage::attack:
            envelopeLevel += attackIncrement;

            if (envelopeLevel >= 1.0)
            {
                envelopeLevel = 1.0;
                stage = VoiceStage::sustaining;
            }

            break;

        case VoiceStage::sustaining:
            envelopeLevel = 1.0;
            break;

        case VoiceStage::releasing:
            envelopeLevel -= releaseDecrement;

            if (envelopeLevel <= 0.0)
            {
                envelopeLevel = 0.0;
                reset();
            }

            break;

        case VoiceStage::stealing:
            envelopeLevel -= stealDecrement;

            if (envelopeLevel <= 0.0)
            {
                envelopeLevel = 0.0;

                // The fade has reached silence, so the waiting note can start
                // without a discontinuity.
                if (pendingNote >= 0)
                {
                    const int nextNote = pendingNote;
                    const float nextVelocity = pendingVelocity;
                    const std::uint64_t nextOrder = pendingStartOrder;

                    pendingNote = -1;
                    beginNote (nextNote, nextVelocity, nextOrder);
                }
                else
                {
                    reset();
                }
            }

            break;

        case VoiceStage::idle:
        default:
            return 0.0;
    }

    return envelopeLevel;
}

void Voice::renderAdding (float* const* output, int numChannels, int startSample, int numSamples) noexcept
{
    if (stage == VoiceStage::idle || output == nullptr || numChannels <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const double envelope = nextEnvelopeValue();

        // reset() inside the envelope may have idled the voice mid-block; the
        // remaining samples are silence and there is nothing left to add.
        if (stage == VoiceStage::idle && envelope <= 0.0)
            break;

        const auto value = oscillator.getNextSample()
                         * static_cast<float> (envelope)
                         * noteVelocity;

        for (int channel = 0; channel < numChannels; ++channel)
            output[channel][startSample + i] += value;
    }
}

} // namespace apollo::engine
