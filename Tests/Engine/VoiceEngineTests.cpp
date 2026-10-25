/*
    Voice engine tests.

    These cover Phase 3's exit criteria directly: that polyphonic tones render
    correctly, that voice stealing is predictable, and that ordinary note
    transitions contain no discontinuity.

    The engine is JUCE-free, so everything here drives it through plain float
    buffers — no host, no audio device, no message loop, and nothing that could
    make a DSP test flaky.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

#include "Engine/VoiceEngine.h"

using namespace apollo::engine;

namespace
{

constexpr double testSampleRate = 48000.0;

/** A preallocated multi-channel render target with the pointer array the engine
    expects.
*/
class RenderBuffer
{
public:
    RenderBuffer (int numChannels, int numSamples)
        : channels (static_cast<std::size_t> (numChannels),
                    std::vector<float> (static_cast<std::size_t> (numSamples), 0.0f))
    {
        for (auto& channel : channels)
            pointers.push_back (channel.data());
    }

    [[nodiscard]] float* const* data() noexcept { return pointers.data(); }
    [[nodiscard]] int getNumChannels() const noexcept { return static_cast<int> (channels.size()); }
    [[nodiscard]] int getNumSamples() const noexcept { return static_cast<int> (channels[0].size()); }
    [[nodiscard]] const std::vector<float>& channel (int index) const noexcept
    {
        return channels[static_cast<std::size_t> (index)];
    }

    [[nodiscard]] float peak() const noexcept
    {
        float highest = 0.0f;

        for (const auto& c : channels)
            for (const auto sample : c)
                highest = std::max (highest, std::abs (sample));

        return highest;
    }

    [[nodiscard]] bool isFinite() const noexcept
    {
        for (const auto& c : channels)
            for (const auto sample : c)
                if (! std::isfinite (sample))
                    return false;

        return true;
    }

    /** @returns the largest absolute change between consecutive samples.

        A click is a step, so this is the measurable form of "no audible
        artifact at a note transition".
    */
    [[nodiscard]] float largestSampleStep() const noexcept
    {
        float largest = 0.0f;

        for (const auto& c : channels)
            for (std::size_t i = 1; i < c.size(); ++i)
                largest = std::max (largest, std::abs (c[i] - c[i - 1]));

        return largest;
    }

private:
    std::vector<std::vector<float>> channels;
    std::vector<float*> pointers;
};

/** @returns the frequency of a rendered tone, estimated from upward zero
    crossings. Accurate enough to tell semitones apart, which is what the pitch
    tests need.
*/
[[nodiscard]] double estimateFrequency (const std::vector<float>& samples, double sampleRate)
{
    int crossings = 0;
    int firstCrossing = -1;
    int lastCrossing = -1;

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        if (samples[i - 1] <= 0.0f && samples[i] > 0.0f)
        {
            if (firstCrossing < 0)
                firstCrossing = static_cast<int> (i);

            lastCrossing = static_cast<int> (i);
            ++crossings;
        }
    }

    if (crossings < 2 || firstCrossing < 0 || lastCrossing <= firstCrossing)
        return 0.0;

    const auto spanSamples = static_cast<double> (lastCrossing - firstCrossing);
    return static_cast<double> (crossings - 1) * sampleRate / spanSamples;
}

class VoiceEngineTests final : public juce::UnitTest
{
public:
    VoiceEngineTests()
        : juce::UnitTest ("Voice engine", "Engine")
    {
    }

    void runTest() override
    {
        testStartsSilentAndIdle();
        testNoteOnProducesTone();
        testPitchAccuracy();
        testVelocityScalesAmplitude();
        testNoteOffReleasesToSilence();
        testPolyphonicRendering();
        testVoiceAllocationPrefersFreeVoices();
        testPolyphonyIsConfigurableAndClamped();
        testStealingIsDeterministic();
        testStealingPrefersReleasingVoices();
        testStealingDoesNotClick();
        testANoteReleasedDuringItsStealEnds();
        testANoteReleasedDuringItsStealUnderThePedal();
        testTheStolenNotesReleaseDoesNotHoldTheNewOne();
        testAStormOfNotesLeavesNothingHeld();
        testSustainPedalHoldsNotes();
        testPitchBendChangesPitch();
        testVoiceReuseStartsClean();
        testAllNotesOffAndReset();
        testExtremeInputsStayFinite();
        testOutputStaysWithinRange();
    }

private:
    /** Prepares an engine in place.

        By reference rather than by value because VoiceEngine is deliberately
        neither copyable nor movable: its voices hold pointers to unison layouts
        that live inside it.
    */
    static void prepareEngine (VoiceEngine& engine, int polyphony = VoiceEngine::defaultPolyphony)
    {
        engine.prepare (testSampleRate);
        engine.setPolyphony (polyphony);
    }

    static void render (VoiceEngine& engine, RenderBuffer& buffer)
    {
        engine.render (buffer.data(), buffer.getNumChannels(), 0, buffer.getNumSamples());
    }

    void testStartsSilentAndIdle()
    {
        beginTest ("A prepared engine is silent and has no active voices");

        VoiceEngine engine;
        prepareEngine (engine);
        RenderBuffer buffer (2, 512);
        render (engine, buffer);

        expectEquals (engine.getActiveVoiceCount(), 0);
        expectEquals (buffer.peak(), 0.0f, "an engine with no notes must output digital silence");
    }

    void testNoteOnProducesTone()
    {
        beginTest ("A note produces audio");

        VoiceEngine engine;
        prepareEngine (engine);
        engine.noteOn (69, 1.0f);

        expectEquals (engine.getActiveVoiceCount(), 1);

        RenderBuffer buffer (2, 4800);
        render (engine, buffer);

        expect (buffer.peak() > 0.05f, "a sounding note must produce a signal");
        expect (buffer.isFinite(), "output must contain no NaN or infinity");

        // Mono/stereo convention: a centred voice is identical on both channels.
        expect (buffer.channel (0) == buffer.channel (1),
                "both channels must carry the same centred signal");
    }

    /** Pitch accuracy is the first thing that makes a synthesiser usable, and
        the easiest to get subtly wrong.
    */
    void testPitchAccuracy()
    {
        beginTest ("Notes render at the correct frequency");

        struct Case { int note; double frequency; };

        static constexpr Case cases[] {
            { 69, 440.0 },   // A4
            { 57, 220.0 },   // A3
            { 81, 880.0 },   // A5
            { 60, 261.6256 } // middle C
        };

        for (const auto& testCase : cases)
        {
            VoiceEngine engine;
            prepareEngine (engine);
            engine.noteOn (testCase.note, 1.0f);

            RenderBuffer buffer (1, static_cast<int> (testSampleRate / 2));
            render (engine, buffer);

            const auto measured = estimateFrequency (buffer.channel (0), testSampleRate);

            // Within a tenth of a semitone (~0.6%), comfortably tight enough to
            // catch an octave, tuning-reference or sample-rate error.
            expect (std::abs (measured - testCase.frequency) < testCase.frequency * 0.006,
                    "note " + juce::String (testCase.note) + " measured "
                        + juce::String (measured, 2) + " Hz, expected "
                        + juce::String (testCase.frequency, 2) + " Hz");
        }
    }

    void testVelocityScalesAmplitude()
    {
        beginTest ("Velocity scales amplitude");

        const auto peakForVelocity = [] (float velocity)
        {
            VoiceEngine engine;
            prepareEngine (engine);
            engine.noteOn (69, velocity);

            RenderBuffer buffer (1, 4800);
            render (engine, buffer);
            return buffer.peak();
        };

        const auto quiet = peakForVelocity (0.25f);
        const auto loud = peakForVelocity (1.0f);

        expect (loud > quiet, "a higher velocity must be louder");
        expectWithinAbsoluteError (loud * 0.25f, quiet, loud * 0.05f,
                                   "velocity should scale amplitude proportionally");

        // Velocity zero is a note-off by convention, not a silent note-on.
        VoiceEngine engine;
        prepareEngine (engine);
        engine.noteOn (69, 0.0f);
        expectEquals (engine.getActiveVoiceCount(), 0,
                      "note-on with zero velocity must not start a voice");
    }

    void testNoteOffReleasesToSilence()
    {
        beginTest ("Note-off releases the voice to silence");

        VoiceEngine engine;
        prepareEngine (engine);
        engine.noteOn (69, 1.0f);

        RenderBuffer sounding (1, 2400);
        render (engine, sounding);
        expect (sounding.peak() > 0.05f);

        engine.noteOff (69);

        // Long enough to cover the release ramp with room to spare.
        RenderBuffer releasing (1, static_cast<int> (testSampleRate * 0.2));
        render (engine, releasing);

        expectEquals (engine.getActiveVoiceCount(), 0,
                      "the voice must return to idle once released");

        // The tail of the release buffer must be exactly silent.
        const auto& samples = releasing.channel (0);
        float tailPeak = 0.0f;

        for (std::size_t i = samples.size() / 2; i < samples.size(); ++i)
            tailPeak = std::max (tailPeak, std::abs (samples[i]));

        expectEquals (tailPeak, 0.0f, "a fully released voice must be digitally silent");
    }

    void testPolyphonicRendering()
    {
        beginTest ("Multiple notes sound simultaneously");

        VoiceEngine engine;
        prepareEngine (engine);

        for (const int note : { 60, 64, 67, 72 })
            engine.noteOn (note, 0.8f);

        expectEquals (engine.getActiveVoiceCount(), 4);

        RenderBuffer chord (1, 4800);
        render (engine, chord);

        VoiceEngine single;
        prepareEngine (single);
        single.noteOn (60, 0.8f);

        RenderBuffer one (1, 4800);
        render (single, one);

        expect (chord.peak() > one.peak(),
                "a four-note chord must be louder than one note of it");
        expect (chord.isFinite());
    }

    void testVoiceAllocationPrefersFreeVoices()
    {
        beginTest ("Allocation uses free voices before stealing");

        VoiceEngine engine;

        prepareEngine (engine, 4);

        for (int i = 0; i < 4; ++i)
        {
            engine.noteOn (60 + i, 1.0f);
            expectEquals (engine.getActiveVoiceCount(), i + 1);
        }

        // Every note is on a distinct voice, in order.
        for (int i = 0; i < 4; ++i)
            expectEquals (engine.getVoice (i).getMidiNote(), 60 + i,
                          "voice " + juce::String (i) + " holds the wrong note");
    }

    void testPolyphonyIsConfigurableAndClamped()
    {
        beginTest ("Polyphony is configurable and clamped to a valid range");

        VoiceEngine engine;
        prepareEngine (engine);

        engine.setPolyphony (8);
        expectEquals (engine.getPolyphony(), 8);

        engine.setPolyphony (0);
        expectEquals (engine.getPolyphony(), 1, "polyphony must be at least one voice");

        engine.setPolyphony (1000);
        expectEquals (engine.getPolyphony(), VoiceEngine::maxPolyphony,
                      "polyphony must be capped at the pool size");

        // With one voice, a second note must reuse that voice rather than
        // sounding alongside it.
        engine.setPolyphony (1);
        engine.reset();
        engine.noteOn (60, 1.0f);
        engine.noteOn (67, 1.0f);
        expectEquals (engine.getActiveVoiceCount(), 1);
    }

    /** "Deterministic" is the requirement, so this asserts reproducibility
        rather than a particular victim.
    */
    void testStealingIsDeterministic()
    {
        beginTest ("Voice stealing is deterministic");

        const auto stolenNoteAfterSequence = [] ()
        {
            VoiceEngine engine;
            engine.prepare (testSampleRate);
            engine.setPolyphony (3);

            engine.noteOn (60, 1.0f);
            engine.noteOn (62, 1.0f);
            engine.noteOn (64, 1.0f);
            engine.noteOn (65, 1.0f); // must steal

            juce::String notes;

            for (int i = 0; i < 3; ++i)
                notes += juce::String (engine.getVoice (i).getMidiNote()) + " ";

            return notes;
        };

        const auto first = stolenNoteAfterSequence();

        for (int repeat = 0; repeat < 5; ++repeat)
            expectEquals (stolenNoteAfterSequence(), first,
                          "the same input must always steal the same voice");

        // The oldest note is the one that goes.
        VoiceEngine engine;
        engine.prepare (testSampleRate);
        engine.setPolyphony (3);
        engine.noteOn (60, 1.0f);
        engine.noteOn (62, 1.0f);
        engine.noteOn (64, 1.0f);
        engine.noteOn (65, 1.0f);

        expect (engine.getVoice (0).getStage() == VoiceStage::stealing,
                "the oldest voice should be the one stolen");
    }

    void testStealingPrefersReleasingVoices()
    {
        beginTest ("Stealing takes a releasing voice before a held one");

        VoiceEngine engine;

        prepareEngine (engine, 2);

        engine.noteOn (60, 1.0f);  // oldest, will be released
        engine.noteOn (62, 1.0f);  // still held
        engine.noteOff (60);       // voice 0 now releasing

        engine.noteOn (64, 1.0f);  // must take voice 0, not the held voice 1

        expect (engine.getVoice (0).getStage() == VoiceStage::stealing,
                "the releasing voice should have been taken");
        expectEquals (engine.getVoice (1).getMidiNote(), 62,
                      "the held note must not be disturbed");
    }

    /** Phase 3 exit criterion: no audible artifact at a note transition. A click
        is a step discontinuity, so it is measurable.
    */
    void testStealingDoesNotClick()
    {
        beginTest ("Stealing a sounding voice does not produce a step");

        VoiceEngine engine;

        prepareEngine (engine, 1);
        engine.noteOn (69, 1.0f);

        // Let the note reach full amplitude, so stealing it is the worst case.
        RenderBuffer settle (1, 2400);
        render (engine, settle);

        engine.noteOn (76, 1.0f); // steals the sounding voice

        RenderBuffer transition (1, 4800);
        render (engine, transition);

        // A sine at these frequencies moves by well under 0.02 per sample at
        // this amplitude; an uncrossfaded steal would jump by the full
        // amplitude, roughly 0.25.
        expect (transition.largestSampleStep() < 0.05f,
                "the steal transition contained a step of "
                    + juce::String (transition.largestSampleStep(), 4));

        expect (transition.isFinite());
        expect (transition.peak() > 0.05f, "the stolen voice must play the new note");
    }

    /** Found in Phase 15 (ADR-0111). A stolen voice fades its old note out
        before starting the new one, and while it does it still reports the old
        note. A key let go in those milliseconds found no voice playing its note,
        and the note then started and never ended. */
    void testANoteReleasedDuringItsStealEnds()
    {
        beginTest ("A note released in the milliseconds of its steal still sounds, and ends");

        VoiceEngine engine;
        prepareEngine (engine, 1);

        engine.noteOn (60, 1.0f);
        RenderBuffer settle (1, 2400);
        render (engine, settle);

        engine.noteOn (64, 1.0f);  // steals the only voice
        expect (engine.getVoice (0).isStealing());
        expectEquals (engine.getVoice (0).getPendingNote(), 64);

        engine.noteOff (64);       // its key goes up before it has started

        RenderBuffer transition (1, 2400);
        render (engine, transition);
        expectEquals (engine.getVoice (0).getMidiNote(), 64, "the tapped note still starts");
        expect (engine.getVoice (0).isReleasing(), "and is released as it does");

        RenderBuffer after (1, static_cast<int> (testSampleRate * 2.0));
        render (engine, after);
        expectEquals (engine.getActiveVoiceCount(), 0, "and then it ends");
    }

    void testANoteReleasedDuringItsStealUnderThePedal()
    {
        beginTest ("Under the pedal, a note released during its steal is held until the pedal lifts");

        VoiceEngine engine;
        prepareEngine (engine, 1);

        engine.setSustainPedal (true);
        engine.noteOn (60, 1.0f);
        engine.noteOn (64, 1.0f);
        engine.noteOff (64);

        RenderBuffer held (1, static_cast<int> (testSampleRate * 0.5));
        render (engine, held);
        expectEquals (engine.getVoice (0).getMidiNote(), 64);
        expect (engine.getVoice (0).isSustainHeld(), "the pedal holds it");
        expect (! engine.getVoice (0).isReleasing());

        engine.setSustainPedal (false);
        RenderBuffer after (1, static_cast<int> (testSampleRate * 2.0));
        render (engine, after);
        expectEquals (engine.getActiveVoiceCount(), 0, "lifting the pedal ends it");
    }

    void testTheStolenNotesReleaseDoesNotHoldTheNewOne()
    {
        beginTest ("The old note's release during a steal does not pass its pedal hold to the new note");

        VoiceEngine engine;
        prepareEngine (engine, 1);

        engine.noteOn (60, 1.0f);
        engine.noteOn (64, 1.0f);  // steals 60's voice
        engine.setSustainPedal (true);
        engine.noteOff (60);       // the old key, which no longer owns the voice

        RenderBuffer transition (1, 2400);
        render (engine, transition);
        expectEquals (engine.getVoice (0).getMidiNote(), 64);
        expect (! engine.getVoice (0).isSustainHeld(), "64's key is still down; nothing holds it for the pedal");

        engine.setSustainPedal (false);
        engine.noteOff (64);
        RenderBuffer after (1, static_cast<int> (testSampleRate * 2.0));
        render (engine, after);
        expectEquals (engine.getActiveVoiceCount(), 0, "64 ends when its own key goes up");
    }

    void testAStormOfNotesLeavesNothingHeld()
    {
        beginTest ("A storm of overlapping notes, every one released, leaves no voice held");

        // Fast playing against a full polyphony: notes overlap, the same note
        // is pressed again before its last voice has finished, and many steals
        // happen with keys going up during them. Before the fix this left
        // three voices held out of 1,463 notes.
        VoiceEngine engine;
        prepareEngine (engine);

        juce::Random random (20261025);
        std::vector<int> holders (128, 0);
        std::vector<int> pressed;
        RenderBuffer block (2, 256);

        for (int step = 0; step < 4000; ++step)
        {
            if (! pressed.empty() && (random.nextInt (3) == 0 || pressed.size() > 10))
            {
                const auto which = static_cast<std::size_t> (random.nextInt (static_cast<int> (pressed.size())));
                const auto note = pressed[which];
                pressed.erase (pressed.begin() + static_cast<std::ptrdiff_t> (which));

                if (--holders[static_cast<std::size_t> (note)] == 0)
                    engine.noteOff (note, 1);
            }
            else
            {
                const auto note = 48 + random.nextInt (30);
                pressed.push_back (note);

                if (holders[static_cast<std::size_t> (note)]++ == 0)
                    engine.noteOn (note, static_cast<float> (1 + random.nextInt (127)) / 127.0f, 1);
            }

            if (random.nextInt (4) == 0)
                render (engine, block);
        }

        for (const auto note : pressed)
            if (--holders[static_cast<std::size_t> (note)] == 0)
                engine.noteOff (note, 1);

        RenderBuffer tail (2, static_cast<int> (testSampleRate * 10.0));
        render (engine, tail);

        expectEquals (engine.getActiveVoiceCount(), 0);
    }

    void testSustainPedalHoldsNotes()
    {
        beginTest ("The sustain pedal holds notes past note-off");

        VoiceEngine engine;
        prepareEngine (engine);

        engine.setSustainPedal (true);
        engine.noteOn (60, 1.0f);
        engine.noteOff (60);

        RenderBuffer held (1, 4800);
        render (engine, held);

        expectEquals (engine.getActiveVoiceCount(), 1,
                      "a note released under sustain must keep sounding");
        expect (held.peak() > 0.05f);

        engine.setSustainPedal (false);

        RenderBuffer afterPedal (1, static_cast<int> (testSampleRate * 0.2));
        render (engine, afterPedal);

        expectEquals (engine.getActiveVoiceCount(), 0,
                      "releasing the pedal must release the held note");

        // A note pressed and released while the pedal is up behaves normally.
        engine.reset();
        engine.noteOn (60, 1.0f);
        engine.noteOff (60);

        RenderBuffer normal (1, static_cast<int> (testSampleRate * 0.2));
        render (engine, normal);
        expectEquals (engine.getActiveVoiceCount(), 0);
    }

    void testPitchBendChangesPitch()
    {
        beginTest ("Pitch bend shifts sounding and subsequent notes");

        const auto frequencyWithBend = [] (float semitones, bool bendBeforeNote)
        {
            VoiceEngine engine;
            engine.prepare (testSampleRate);

            if (bendBeforeNote)
            {
                engine.setPitchBendSemitones (semitones);
                engine.noteOn (69, 1.0f);
            }
            else
            {
                engine.noteOn (69, 1.0f);
                engine.setPitchBendSemitones (semitones);
            }

            RenderBuffer buffer (1, static_cast<int> (testSampleRate / 2));
            engine.render (buffer.data(), 1, 0, buffer.getNumSamples());
            return estimateFrequency (buffer.channel (0), testSampleRate);
        };

        // +2 semitones from A4 is B4, 493.88 Hz.
        for (const bool bendFirst : { true, false })
        {
            const auto bent = frequencyWithBend (2.0f, bendFirst);
            expect (std::abs (bent - 493.883) < 493.883 * 0.006,
                    juce::String (bendFirst ? "bend before note-on" : "bend during note")
                        + " measured " + juce::String (bent, 2) + " Hz, expected 493.88 Hz");
        }

        // Downward bend, and centre restores the original pitch.
        expect (frequencyWithBend (-2.0f, true) < 440.0);
        expect (std::abs (frequencyWithBend (0.0f, true) - 440.0) < 440.0 * 0.006);
    }

    void testVoiceReuseStartsClean()
    {
        beginTest ("A reused voice starts from a clean state");

        VoiceEngine engine;

        prepareEngine (engine, 1);

        engine.noteOn (60, 1.0f);
        engine.noteOff (60);

        RenderBuffer settle (1, static_cast<int> (testSampleRate * 0.2));
        render (engine, settle);
        expectEquals (engine.getActiveVoiceCount(), 0);

        // The same voice, now idle, must render the new note exactly as a fresh
        // engine would — no leftover phase or envelope state.
        engine.noteOn (69, 1.0f);
        RenderBuffer reused (1, 4800);
        render (engine, reused);

        VoiceEngine fresh;

        prepareEngine (fresh, 1);
        fresh.noteOn (69, 1.0f);
        RenderBuffer first (1, 4800);
        render (fresh, first);

        expect (reused.channel (0) == first.channel (0),
                "a reused voice must render identically to a fresh one");
    }

    void testAllNotesOffAndReset()
    {
        beginTest ("All-notes-off releases and reset silences immediately");

        VoiceEngine engine;
        prepareEngine (engine);

        for (const int note : { 60, 62, 64, 65, 67 })
            engine.noteOn (note, 1.0f);

        expectEquals (engine.getActiveVoiceCount(), 5);

        engine.allNotesOff();

        RenderBuffer releasing (1, static_cast<int> (testSampleRate * 0.2));
        render (engine, releasing);
        expectEquals (engine.getActiveVoiceCount(), 0);

        // reset is immediate, not a release.
        for (const int note : { 60, 62, 64 })
            engine.noteOn (note, 1.0f);

        expectEquals (engine.getActiveVoiceCount(), 3);
        engine.reset();
        expectEquals (engine.getActiveVoiceCount(), 0);

        RenderBuffer afterReset (1, 512);
        render (engine, afterReset);
        expectEquals (afterReset.peak(), 0.0f);
    }

    /** Hostile and boundary input must not produce NaN or infinity: either would
        propagate through the whole mix and, in a host, out to the speakers.
    */
    void testExtremeInputsStayFinite()
    {
        beginTest ("Extreme input stays finite");

        VoiceEngine engine;
        prepareEngine (engine);

        // Range boundaries and out-of-range notes.
        for (const int note : { -1, 0, 1, 126, 127, 128, 1000 })
            engine.noteOn (note, 1.0f);

        // Extreme bends, well past any real controller.
        for (const float bend : { -48.0f, -2.0f, 0.0f, 2.0f, 48.0f })
        {
            engine.setPitchBendSemitones (bend);

            RenderBuffer buffer (2, 2048);
            render (engine, buffer);

            expect (buffer.isFinite(),
                    "output became non-finite at bend " + juce::String (bend));
        }

        // Rapid note churn, far faster than any player.
        for (int i = 0; i < 500; ++i)
        {
            engine.noteOn (60 + (i % 24), 1.0f);

            if (i % 3 == 0)
                engine.noteOff (60 + (i % 24));

            RenderBuffer buffer (2, 64);
            render (engine, buffer);
            expect (buffer.isFinite(), "output became non-finite during rapid note changes");
        }
    }

    /** The engine's gain staging is a claim about worst-case level, so it is
        measured rather than reasoned about. Several voicings are tried, because
        one chord shape proves nothing: how voices sum depends on their frequency
        relationships as much as their number.
    */
    void testOutputStaysWithinRange()
    {
        beginTest ("Maximum polyphony stays inside full scale");

        struct Voicing { const char* name; int firstNote; int step; };

        static constexpr Voicing voicings[] {
            { "chromatic cluster", 36, 1 },   // adjacent semitones, dense beating
            { "whole tones",       36, 2 },
            { "octaves",           24, 12 },  // harmonically locked
            { "fifths",            24, 7 },
            { "unison",            60, 0 }    // identical frequency, phases spread
        };

        for (const auto& voicing : voicings)
        {
            VoiceEngine engine;
            prepareEngine (engine);

            for (int i = 0; i < VoiceEngine::maxPolyphony; ++i)
                engine.noteOn (juce::jlimit (0, 127, voicing.firstNote + i * voicing.step), 1.0f);

            RenderBuffer buffer (2, static_cast<int> (testSampleRate * 0.25));
            render (engine, buffer);

            expect (buffer.isFinite(),
                    juce::String (voicing.name) + ": output must stay finite");
            expect (buffer.peak() <= 1.0f,
                    juce::String (voicing.name) + " peaked at "
                        + juce::String (buffer.peak(), 3) + ", which would clip");
        }
    }
};

VoiceEngineTests voiceEngineTests;

} // namespace
