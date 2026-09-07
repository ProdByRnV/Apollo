#pragma once

/*
    Apollo's polyphonic voice engine.

    Owns the voice pool, decides which voice plays which note, and sums the
    result. Like Voice, it is free of JUCE and of any plugin concept: the
    processor translates MIDI into the calls below, and this class never sees a
    juce::MidiBuffer, a parameter, or a host.

    OWNERSHIP MODEL. The processor owns exactly one VoiceEngine. The engine owns
    every Voice by value, in a fixed-size array sized at compile time. Nothing
    in the audio path allocates, and no voice is ever created or destroyed while
    audio is running — `setPolyphony` only changes how many of the existing
    voices are eligible for allocation (CLAUDE.md §9.1, ARCHITECTURE.md §3.1).

    REAL-TIME CONTRACT: every function except `prepare` is callable from the
    audio thread and none of them allocate, lock, log or perform I/O.
*/

#include "DSP/Oscillators/WavetableLibrary.h"
#include "Engine/Voice.h"

#include <array>
#include <cstdint>

namespace apollo::engine
{

class VoiceEngine
{
public:
    /** Hard ceiling on simultaneous voices.

        The voice pool is this size always; polyphony selects how much of it is
        used. Fixed so the engine can be preallocated once and never resized.
    */
    static constexpr int maxPolyphony = 32;

    /** Default simultaneous voices. */
    static constexpr int defaultPolyphony = 16;

    /** Output gain applied to the summed voices.

        This is the engine's whole gain-staging rule: voices carry velocity only,
        the sum is scaled once here, and master gain is applied downstream by the
        processor. No stage silently makes up gain for another, and nothing
        limits or clips to hide a mistake (ARCHITECTURE.md §3.4).

        The value is measured, not derived. The textbook 1/sqrt(N) assumes
        uncorrelated voices, and Apollo's voices are not: 32 notes an octave
        apart are harmonically locked and reinforce, summing to roughly 11.4
        voice-equivalents rather than sqrt(32) = 5.7. Measured worst cases at
        full polyphony and full velocity, relative to a single voice:

            octaves 11.4 · unison 10.2 · whole tones 8.7 · fifths 8.4

        1/12.5 covers the worst of those with ~9% margin, and the test suite
        renders all of them and asserts the result stays inside full scale.

        The consequence is a deliberately conservative level: one note peaks
        around -22 dBFS. That is the right trade while the instrument has no
        output stage — headroom is recoverable with master gain, clipping is
        not. Revisit when the oscillator (Phase 4) and effects (Phase 8) make
        the real signal chain measurable.
    */
    static constexpr float outputGain = 0.08f;

    VoiceEngine() = default;

    /** Prepares every voice for a sample rate.

        Not real-time safe. Call from prepareToPlay, never from the audio
        callback.
    */
    void prepare (double sampleRate) noexcept;

    /** Silences every voice and clears controller state. */
    void reset() noexcept;

    /** Sets how many voices may sound at once, clamped to [1, maxPolyphony].

        Voices above the new limit are released rather than cut, so lowering
        polyphony mid-performance does not click.
    */
    void setPolyphony (int numVoices) noexcept;

    [[nodiscard]] int getPolyphony() const noexcept { return polyphony; }

    //==============================================================================
    // Control input. The processor calls these at MIDI event boundaries.

    void noteOn (int midiNote, float velocity) noexcept;
    void noteOff (int midiNote) noexcept;

    /** Sustain pedal. Releasing the pedal releases every note held by it. */
    void setSustainPedal (bool isDown) noexcept;

    /** Pitch bend for every sounding and future voice, in semitones. */
    void setPitchBendSemitones (float semitones) noexcept;

    /** Selects the wavetable, by index into the built-in library.

        Applied to sounding voices as well as future ones, so changing the table
        while notes are held takes effect immediately rather than on the next
        note. An out-of-range index is clamped, never silencing the instrument.
    */
    void setWavetableIndex (int index) noexcept;

    /** Sets the scan position across the table, normalised to [0, 1]. */
    void setWavetablePosition (float normalisedPosition) noexcept;

    [[nodiscard]] int getWavetableIndex() const noexcept { return wavetableIndex; }
    [[nodiscard]] float getWavetablePosition() const noexcept { return wavetablePosition; }

    /** The built-in tables. Exposed for tests and for the future resource layer. */
    [[nodiscard]] const dsp::WavetableLibrary& getWavetableLibrary() const noexcept { return library; }

    /** Releases all notes, as an all-notes-off controller would. */
    void allNotesOff() noexcept;

    //==============================================================================

    /** Renders @p numSamples of the summed voices into @p output.

        The buffer region is overwritten, not added to: the engine is the origin
        of the signal, and clearing here means the processor never has to
        remember to.

        Mono/stereo convention: every voice is centred, so both channels receive
        identical signal. Panning and stereo spread arrive with the oscillator
        and unison work in Phase 4; until then a stereo output is two copies of
        one mono source rather than a fake width.
    */
    void render (float* const* output, int numChannels, int startSample, int numSamples) noexcept;

    //==============================================================================
    // Introspection, for tests and metering.

    [[nodiscard]] int getActiveVoiceCount() const noexcept;
    [[nodiscard]] bool isSustainPedalDown() const noexcept { return sustainPedalDown; }
    [[nodiscard]] const Voice& getVoice (int index) const noexcept { return voices[static_cast<std::size_t> (index)]; }

private:
    [[nodiscard]] Voice* findVoiceForNewNote() noexcept;

    std::array<Voice, static_cast<std::size_t> (maxPolyphony)> voices {};

    /** Built once at construction, off the audio thread, and immutable
        thereafter — so every voice can read it concurrently without
        synchronisation.
    */
    dsp::WavetableLibrary library;

    int wavetableIndex = 0;
    float wavetablePosition = 0.0f;

    int polyphony = defaultPolyphony;
    bool sustainPedalDown = false;
    float pitchBendSemitones = 0.0f;

    /** Monotonic allocation counter, giving every note start a unique, ordered
        identity so "oldest" is exact rather than approximate.
    */
    std::uint64_t nextStartOrder = 1;
};

} // namespace apollo::engine
