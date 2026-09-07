#pragma once

/*
    A single synthesis voice.

    Deliberately free of JUCE and of any plugin concept. It renders into raw
    float buffers and knows nothing about MIDI messages, parameters or hosts.
    Three reasons:

      - it can be unit tested without a host, an audio device or a message loop;
      - it lives in apollo_core, which is the only library held to Apollo's
        strict warning set (ADR-0005) — and silent narrowing or an accidental
        double promotion inside a per-sample loop is exactly the class of defect
        that set exists to catch;
      - it keeps the DSP independent of the plugin wrapper (ARCHITECTURE.md §2).

    REAL-TIME CONTRACT: every function here is callable from the audio thread.
    None of them allocate, lock, log, or perform I/O. `prepare` is the only
    function that may be called from another thread, and only while audio is
    stopped.

    PLACEHOLDERS. Two parts of this voice are deliberate stand-ins:

      - the amplitude envelope is a linear attack/release. The four DAHDSR
        envelopes are Phase 5. It exists so note transitions are click-free
        without pre-empting the phase that owns envelopes.

    The oscillator is now a band-limited wavetable (Phase 4).
*/

#include "DSP/Oscillators/WavetableOscillator.h"

#include <cstdint>

namespace apollo::engine
{

/** Where a voice is in its lifecycle. */
enum class VoiceStage
{
    idle,        ///< Silent and available for allocation.
    attack,      ///< Ramping up from silence.
    sustaining,  ///< Held at full level while the key is down.
    releasing,   ///< Ramping down after note-off.
    stealing     ///< Ramping down fast, with a new note waiting to start.
};

class Voice
{
public:
    Voice() = default;

    /** Envelope ramp lengths, in seconds.

        The attack is short enough to feel immediate on a percussive part, and
        long enough that starting from silence is not a step discontinuity.
        The steal ramp is deliberately much faster than the normal release: a
        stolen voice must free itself promptly, but stopping a waveform at
        full amplitude is exactly what a click is.
    */
    static constexpr double attackSeconds = 0.005;
    static constexpr double releaseSeconds = 0.050;
    static constexpr double stealSeconds = 0.002;

    /** Prepares for a sample rate. Not real-time safe; call while stopped. */
    void prepare (double newSampleRate) noexcept;

    /** Sets the phase this voice starts every note from, in the range [0, 1).

        Voices are given distinct offsets by the engine. Starting every voice at
        phase zero makes a chord's attack sum *coherently* rather than as the
        root-N of uncorrelated signals, and a 32-note chord then peaks several
        times higher than the gain staging assumes — measured at 1.95 before
        this existed. Distinct fixed offsets decorrelate the attack while
        staying perfectly reproducible, which randomised phase would not.
    */
    void setStartPhase (double newStartPhase) noexcept;

    /** Returns the voice to silence immediately, discarding any pending note. */
    void reset() noexcept;

    /** Starts a note.

        @param midiNote     0-127.
        @param velocity     0-1.
        @param newStartOrder  monotonically increasing allocation counter, used
                              to resolve voice age deterministically.
    */
    void startNote (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept;

    /** Begins the release stage. Ignored if the voice is not sounding. */
    void releaseNote() noexcept;

    /** Takes this voice for a different note.

        The voice first ramps to silence over `stealSeconds` and only then
        starts the new note, so the transition never contains a step. This is
        why a stolen note begins a couple of milliseconds late rather than
        instantly: an audible click is the worse trade.
    */
    void steal (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept;

    /** Sets pitch bend, in semitones, applied on the next sample. */
    void setPitchBendSemitones (float semitones) noexcept;

    /** Points the voice's oscillator at a wavetable.

        The table is owned elsewhere and outlives the voice; voices only read
        from it, and it is immutable once built, so no synchronisation is needed.
    */
    void setWavetable (const dsp::Wavetable* table) noexcept;

    /** Sets the scan position across the table, normalised to [0, 1]. */
    void setWavetablePosition (float normalisedPosition) noexcept;

    /** Adds this voice's output into @p output.

        Additive so that voices can be summed straight into the destination
        without a per-voice scratch buffer — no temporary allocation, and no
        preallocated scratch to size and own.

        @param output       array of @p numChannels writable channel pointers.
        @param numChannels  1 or 2.
        @param startSample  first sample to write.
        @param numSamples   samples to write.
    */
    void renderAdding (float* const* output, int numChannels, int startSample, int numSamples) noexcept;

    [[nodiscard]] bool isActive() const noexcept { return stage != VoiceStage::idle; }
    [[nodiscard]] bool isReleasing() const noexcept { return stage == VoiceStage::releasing; }

    /** True once note-off has arrived but the key is still held by sustain. */
    [[nodiscard]] bool isSustainHeld() const noexcept { return sustainHeld; }
    void setSustainHeld (bool shouldHold) noexcept { sustainHeld = shouldHold; }

    [[nodiscard]] int getMidiNote() const noexcept { return note; }
    [[nodiscard]] VoiceStage getStage() const noexcept { return stage; }
    [[nodiscard]] std::uint64_t getStartOrder() const noexcept { return startOrder; }

    /** Current envelope level, 0-1. Exposed for tests and metering. */
    [[nodiscard]] float getEnvelopeLevel() const noexcept { return static_cast<float> (envelopeLevel); }

private:
    void beginNote (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept;
    void updatePhaseIncrement() noexcept;
    [[nodiscard]] double nextEnvelopeValue() noexcept;

    double sampleRate = 44100.0;
    double startPhase = 0.0;

    /** The oscillator owns phase, frequency and mip selection. */
    dsp::WavetableOscillator oscillator;

    double envelopeLevel = 0.0;
    double attackIncrement = 0.0;
    double releaseDecrement = 0.0;
    double stealDecrement = 0.0;

    VoiceStage stage = VoiceStage::idle;

    int note = -1;
    float noteVelocity = 0.0f;
    float pitchBendSemitones = 0.0f;
    bool sustainHeld = false;

    std::uint64_t startOrder = 0;

    // Set while stealing, consumed when the fade reaches silence.
    int pendingNote = -1;
    float pendingVelocity = 0.0f;
    std::uint64_t pendingStartOrder = 0;
};

} // namespace apollo::engine
