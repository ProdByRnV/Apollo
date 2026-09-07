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

    SOURCE SECTION (Phase 4b). A voice sums four sources into a stereo pair:

        osc 1   unison stack of band-limited wavetable oscillators
        osc 2   the same, transposable relative to the played note
        sub     a sine one or two octaves down
        noise   decorrelated stereo white noise

    Each source has its own level, and the two primary oscillators have a
    stereo balance on top of the image their unison spread creates. A source
    whose level is exactly zero, and which is not still ramping down to it, is
    skipped rather than rendered and multiplied by nothing — which is what makes
    the default patch (oscillator 1 alone) cost what it did before the other
    three existed.

    REAL-TIME CONTRACT: every function here is callable from the audio thread.
    None of them allocate, lock, log, or perform I/O. `prepare` is the only
    function that may be called from another thread, and only while audio is
    stopped.

    PLACEHOLDER. The amplitude envelope is a linear attack/release. The four
    DAHDSR envelopes are Phase 5. It exists so note transitions are click-free
    without pre-empting the phase that owns envelopes.
*/

#include "DSP/Noise/NoiseGenerator.h"
#include "DSP/Oscillators/UnisonOscillator.h"
#include "DSP/Oscillators/WavetableOscillator.h"
#include "DSP/Utilities/LinearSmoothedValue.h"
#include "Engine/SourceSettings.h"

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

    /** Ramp length for a source level or balance change, in seconds.

        Level and balance are the two source parameters where a per-block step
        is an audible click rather than a small pitch or timbre jump, so they
        are the two that are smoothed per sample (CLAUDE.md §36). 20 ms matches
        the master gain ramp: long enough to remove the step from automation,
        short enough that a deliberate move still feels immediate.
    */
    static constexpr double gainRampSeconds = 0.02;

    /** Prepares for a sample rate. Not real-time safe; call while stopped. */
    void prepare (double newSampleRate) noexcept;

    /** Sets the phase this voice starts every note from, in the range [0, 1).

        Voices are given distinct offsets by the engine. Starting every voice at
        phase zero makes a chord attack sum *coherently* rather than as the
        root-N of uncorrelated signals, and a 32-note chord then peaks several
        times higher than the gain staging assumes — measured at 1.95 before
        this existed. Distinct fixed offsets decorrelate the attack while
        staying perfectly reproducible, which randomised phase would not.
    */
    void setStartPhase (double newStartPhase) noexcept;

    /** Sets the noise generator seed.

        Given a distinct value per voice by the engine, so that several voices
        sounding at once produce independent noise. Identical noise across
        voices would sum coherently and be N times louder than the level asks
        for, as well as sounding like one source rather than many.
    */
    void setNoiseSeed (std::uint32_t seed) noexcept;

    /** Returns the voice to silence immediately, discarding any pending note. */
    void reset() noexcept;

    /** Replaces the source configuration.

        Cheap to call every block: identical settings are recognised and
        discarded before any work is done.
    */
    void setSources (const VoiceSourceSettings& newSources) noexcept;

    [[nodiscard]] const VoiceSourceSettings& getSources() const noexcept { return sources; }

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

    /** Adds this voice output into @p output.

        Additive so that voices can be summed straight into the destination
        without a per-voice scratch buffer — no temporary allocation, and no
        preallocated scratch to size and own.

        Channel convention: channel 0 is left and channel 1 is right. A mono
        destination receives the *average* of the pair rather than their sum, so
        the mono peak can never exceed the stereo peak and the headroom
        guarantee holds for both layouts. Any channel beyond the second
        receives the same average, which keeps an unexpected layout audible
        rather than silent.

        @param output       array of @p numChannels writable channel pointers.
        @param numChannels  1 or more.
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
    void updateGainTargets() noexcept;
    void snapGainsToTargets() noexcept;
    [[nodiscard]] double nextEnvelopeValue() noexcept;

    /** A smoothed stereo gain pair for one source, and whether it is worth
        rendering.

        The two are kept together because they are the same question: a source
        is skippable exactly when its level is zero *and* neither of its gains
        is still ramping down to zero. Testing only the level would cut a
        fading-out source off mid-ramp, which is the click the smoothing exists
        to prevent.
    */
    struct SourceGain
    {
        dsp::LinearSmoothedValue left;
        dsp::LinearSmoothedValue right;

        /** Prepares both ramps for a sample rate.

            The parameter is `newSampleRate` rather than `sampleRate` because
            this struct is nested inside Voice, which has a `sampleRate` field:
            Clang's -Wshadow treats a nested class's parameter as shadowing the
            enclosing class's member, and Apollo builds with warnings as errors.
            GCC and MSVC accept the shorter name, so the mistake compiles
            everywhere except the two Clang CI jobs.
        */
        void reset (double newSampleRate) noexcept;
        void setTargets (float level, float pan) noexcept;
        void snap() noexcept;

        [[nodiscard]] bool isAudible() const noexcept
        {
            return left.getTargetValue() > 0.0f || right.getTargetValue() > 0.0f
                || left.isSmoothing() || right.isSmoothing();
        }
    };

    double sampleRate = 44100.0;
    double startPhase = 0.0;

    dsp::UnisonOscillator oscillator1;
    dsp::UnisonOscillator oscillator2;
    dsp::WavetableOscillator subOscillator;
    dsp::NoiseGenerator noise;

    /** Kept so that reset can restore the noise stream to a known point rather
        than leaving it wherever the last render finished.
    */
    std::uint32_t noiseSeed = 1u;

    VoiceSourceSettings sources;

    SourceGain gain1;
    SourceGain gain2;
    SourceGain subGain;
    SourceGain noiseGain;

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
