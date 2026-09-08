#pragma once

/*
    Apollo's polyphonic voice engine.

    Owns the voice pool, decides which voice plays which note, and sums the
    result. Like Voice, it is free of JUCE and of any plugin concept: the
    processor translates MIDI and parameters into the calls below, and this
    class never sees a juce::MidiBuffer, an APVTS, or a host.

    OWNERSHIP MODEL. The processor owns exactly one VoiceEngine. The engine owns
    every Voice by value, in a fixed-size array sized at compile time. Nothing
    in the audio path allocates, and no voice is ever created or destroyed while
    audio is running — `setPolyphony` only changes how many of the existing
    voices are eligible for allocation (CLAUDE.md §9.1, ARCHITECTURE.md §3.1).

    The engine is also where parameters become resources. It holds the wavetable
    library and the unison layouts, resolves a table index into a pointer and a
    unison configuration into a layout exactly once per block, and hands voices
    only the resolved form (see SourceSettings.h). Deriving a unison layout is
    the same work for every voice, so doing it here rather than per voice is the
    difference between a few dozen transcendental calls per block and a few
    thousand.

    REAL-TIME CONTRACT: every function except `prepare` is callable from the
    audio thread and none of them allocate, lock, log or perform I/O.
*/

#include "DSP/Envelopes/Envelope.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Unison/UnisonLayout.h"
#include "Engine/FilterSettings.h"
#include "Engine/SourceSettings.h"
#include "Engine/Voice.h"

#include <array>
#include <cstdint>

namespace apollo::engine
{

/** One primary oscillator's parameters, as the host and UI express them.

    Distinct from OscillatorSourceSettings, which is the *resolved* form the
    voices consume: this holds a table index rather than a pointer and a unison
    configuration rather than a layout. Keeping the two apart is what stops a
    voice from ever needing to know that a wavetable library exists.
*/
struct OscillatorParameters
{
    int wavetableIndex = 0;

    /** Scan position across the table, normalised to [0, 1]. */
    float position = 0.0f;

    /** Unison voices, clamped to [1, UnisonLayout::maxVoices]. */
    int unisonVoices = 1;

    /** Unison detune, normalised to [0, 1]. */
    float detune = 0.0f;

    /** Unison stereo spread, normalised to [0, 1]. */
    float spread = 0.0f;

    /** Output level, [0, 1]. Exactly zero skips the oscillator. */
    float level = 0.0f;

    /** Stereo balance: -1 hard left, 0 centre, +1 hard right. */
    float pan = 0.0f;

    /** Transposition from the played note, in semitones. Fractional values
        express a fine-tune control.
    */
    float tuneSemitones = 0.0f;

    [[nodiscard]] bool operator== (const OscillatorParameters&) const = default;
};

/** The complete source section, as parameters.

    The defaults are Apollo's default patch: oscillator 1 alone, centred, no
    unison, with the other three sources silent. That is deliberate and load
    bearing — it is the configuration the engine's headroom guarantee is stated
    for (see `outputGain`).
*/
struct SourceParameters
{
    OscillatorParameters osc1 { .unisonVoices = 1, .detune = 0.2f, .spread = 0.5f, .level = 1.0f };
    OscillatorParameters osc2 { .unisonVoices = 1, .detune = 0.2f, .spread = 0.5f, .level = 0.0f };

    float subLevel = 0.0f;
    int subOctave = -1;

    float noiseLevel = 0.0f;

    [[nodiscard]] bool operator== (const SourceParameters&) const = default;
};

/** One filter slot, as the host and UI express it. */
struct FilterSlotParameters
{
    dsp::StateVariableFilter::Mode mode = dsp::StateVariableFilter::Mode::off;

    float cutoffHz = 20000.0f;

    /** Resonance as a quality factor. 0.707 is Butterworth. */
    float q = 0.707f;

    /** Saturation in front of the filter, 0 to 1. */
    float drive = 0.0f;

    [[nodiscard]] bool operator== (const FilterSlotParameters&) const = default;
};

/** The filter section, as parameters.

    The defaults are transparent: filter 1 is a lowpass wide open at 20 kHz and
    filter 2 is off, so adding the section changed no existing patch and left
    the Phase 3 gain staging intact.
*/
struct FilterParameters
{
    FilterSlotParameters filter1 { dsp::StateVariableFilter::Mode::lowpass, 20000.0f, 0.707f, 0.0f };
    FilterSlotParameters filter2;

    FilterRouting routing = FilterRouting::series;

    [[nodiscard]] bool operator== (const FilterParameters&) const = default;
};

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

        This is the engine's whole gain-staging rule: voices carry velocity and
        their own source levels, the sum is scaled once here, and master gain is
        applied downstream by the processor. No stage silently makes up gain for
        another, and nothing limits or clips to hide a mistake
        (ARCHITECTURE.md §3.4).

        The value is measured, not derived. The textbook 1/sqrt(N) assumes
        uncorrelated voices, and Apollo's voices are not: 32 notes an octave
        apart are harmonically locked and reinforce, summing to roughly 11.4
        voice-equivalents rather than sqrt(32) = 5.7. Measured worst cases at
        full polyphony and full velocity, relative to a single voice:

            octaves 11.4 · unison 10.2 · whole tones 8.7 · fifths 8.4

        1/12.5 covers the worst of those with ~9% margin, and the test suite
        renders all of them and asserts the result stays inside full scale.

        WHAT THE GUARANTEE COVERS. It is stated for the *default patch*:
        oscillator 1 alone, one unison voice, centred. Under that patch no
        voicing, velocity or polyphony can drive the output past full scale, and
        that is asserted. A single default note peaks at 0.080, measured.

        It is deliberately not claimed for every reachable parameter
        combination. Four sources at full level with 16-voice unison on both
        oscillators peaks at 7.63 across the same voicings — about 18 dB over
        full scale — and pricing that in would put a single default note near
        -40 dBFS: an instrument nobody could use, defending against a patch
        nobody would build by accident. Stacked sources are instead guaranteed
        only to stay *finite* and bounded, which is also asserted; the master
        gain is the user's control, and metering arrives with the output stage
        in Phase 8.

        The consequence is a deliberately conservative level: one note peaks
        around -22 dBFS. That is the right trade while the instrument has no
        output stage — headroom is recoverable with master gain, clipping is
        not.
    */
    static constexpr float outputGain = 0.08f;

    VoiceEngine();

    /** Not copyable or movable.

        Every voice holds a pointer to a unison layout that lives inside this
        object. A copy or a move would leave those pointers aimed at the
        original, which becomes a dangling read the moment the source goes out of
        scope — and one that would surface as intermittently wrong audio rather
        than as a crash. Deleting these turns that mistake into a compile error.

        Copying would also duplicate several megabytes of wavetables, which no
        caller has ever wanted.
    */
    VoiceEngine (const VoiceEngine&) = delete;
    VoiceEngine& operator= (const VoiceEngine&) = delete;
    VoiceEngine (VoiceEngine&&) = delete;
    VoiceEngine& operator= (VoiceEngine&&) = delete;

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

    //==============================================================================
    // Source configuration.

    /** Replaces the whole source section.

        Applied to sounding voices as well as future ones, so a parameter change
        while notes are held takes effect immediately rather than on the next
        note. Cheap enough to call every block: unison layouts are rebuilt only
        when their inputs move, and each voice discards settings identical to
        the ones it already holds.
    */
    void setSourceParameters (const SourceParameters& newParameters) noexcept;

    /** Replaces the amplitude envelope shape on every voice.

        Applied to sounding voices as well as future ones, so a change while
        notes are held is audible immediately rather than on the next note.
        Cheap to call every block: each voice discards settings identical to the
        ones it holds.
    */
    void setAmplitudeEnvelope (const dsp::EnvelopeSettings& newSettings) noexcept;

    /** Replaces the filter section on every voice.

        Resolves cutoff and resonance into coefficients exactly once, here,
        rather than in each of the sixty-four voice filters that would otherwise
        compute the same tangent.
    */
    void setFilterParameters (const FilterParameters& newParameters) noexcept;

    [[nodiscard]] const FilterParameters& getFilterParameters() const noexcept { return filterParameters; }

    [[nodiscard]] const dsp::EnvelopeSettings& getAmplitudeEnvelope() const noexcept
    {
        return amplitudeEnvelope;
    }

    [[nodiscard]] const SourceParameters& getSourceParameters() const noexcept { return parameters; }

    /** Selects oscillator 1's wavetable, by index into the built-in library.

        A convenience over `setSourceParameters` for the common case of moving
        one control. An out-of-range index is clamped, never silencing the
        instrument.
    */
    void setWavetableIndex (int index) noexcept;

    /** Sets oscillator 1's scan position, normalised to [0, 1]. */
    void setWavetablePosition (float normalisedPosition) noexcept;

    [[nodiscard]] int getWavetableIndex() const noexcept { return parameters.osc1.wavetableIndex; }
    [[nodiscard]] float getWavetablePosition() const noexcept { return parameters.osc1.position; }

    /** The built-in tables. Exposed for tests and for the future resource layer. */
    [[nodiscard]] const dsp::WavetableLibrary& getWavetableLibrary() const noexcept { return library; }

    /** The unison layouts in force, for tests and for future visualisation. */
    [[nodiscard]] const dsp::UnisonLayout& getUnisonLayout (int oscillatorIndex) const noexcept
    {
        return oscillatorIndex <= 0 ? unison1 : unison2;
    }

    /** Releases all notes, as an all-notes-off controller would. */
    void allNotesOff() noexcept;

    //==============================================================================

    /** Renders @p numSamples of the summed voices into @p output.

        The buffer region is overwritten, not added to: the engine is the origin
        of the signal, and clearing here means the processor never has to
        remember to.

        Stereo convention: channel 0 is left, channel 1 is right. Width comes
        from the unison spread and the per-oscillator balance; a patch with one
        unison voice and everything centred is genuinely mono, and both channels
        then carry the same signal rather than a fabricated width.
    */
    void render (float* const* output, int numChannels, int startSample, int numSamples) noexcept;

    //==============================================================================
    // Introspection, for tests and metering.

    [[nodiscard]] int getActiveVoiceCount() const noexcept;
    [[nodiscard]] bool isSustainPedalDown() const noexcept { return sustainPedalDown; }
    [[nodiscard]] const Voice& getVoice (int index) const noexcept { return voices[static_cast<std::size_t> (index)]; }

private:
    [[nodiscard]] Voice* findVoiceForNewNote() noexcept;

    /** Resolves `parameters` into voice settings and pushes them to every
        voice, rebuilding the unison layouts if their inputs moved.
    */
    void applySourceParameters() noexcept;

    std::array<Voice, static_cast<std::size_t> (maxPolyphony)> voices {};

    /** Built once at construction, off the audio thread, and immutable
        thereafter — so every voice can read it concurrently without
        synchronisation.
    */
    dsp::WavetableLibrary library;

    /** One layout per primary oscillator, shared by every voice. Mutated on the
        audio thread before any voice reads it in the same callback.
    */
    dsp::UnisonLayout unison1;
    dsp::UnisonLayout unison2;

    SourceParameters parameters;

    dsp::EnvelopeSettings amplitudeEnvelope;

    FilterParameters filterParameters;

    /** Rebuilds the resolved filter settings and pushes them to every voice. */
    void applyFilterParameters() noexcept;

    /** Kept because the filter coefficients are resolved here rather than in the
        voices, and resolving them needs the rate.
    */
    double preparedSampleRate = 44100.0;

    int polyphony = defaultPolyphony;
    bool sustainPedalDown = false;
    float pitchBendSemitones = 0.0f;

    /** Monotonic allocation counter, giving every note start a unique, ordered
        identity so "oldest" is exact rather than approximate.
    */
    std::uint64_t nextStartOrder = 1;
};

} // namespace apollo::engine
