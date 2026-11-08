/*
    Mono, legato and glide (Phase 19e, ADR-0133).

    What these hold:

      - the defaults, Poly with no glide, start every note on its own pitch;
      - in Poly, a glide slides a new note from the last one's pitch, at the
        time set, and arrives exactly; glide to LEGATO slides only between
        held notes;
      - Mono plays one voice, every note starting it again through the steal's
        fade, and a key let go returns to the newest one still held;
      - Legato moves the one voice to a note played while another is held
        without starting its envelope again;
      - the pedal holds the last note of a mono line, and the keys held are
        counted without ever growing past their place;
      - and the pitch heard is the pitch arrived at.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Engine/VoiceEngine.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;

/** Renders @p samples of @p voiceEngine; returns the left channel. */
std::vector<float> render (engine::VoiceEngine& voiceEngine, int samples)
{
    std::vector<float> left (static_cast<std::size_t> (samples)), right (static_cast<std::size_t> (samples));
    float* channels[2] = { left.data(), right.data() };

    for (int start = 0; start < samples; start += 256)
        voiceEngine.render (channels, 2, start, std::min (256, samples - start));

    return left;
}

void prepare (engine::VoiceEngine& voiceEngine, engine::VoiceMode mode, float glideSeconds, bool onlyWhenHeld = false)
{
    voiceEngine.prepare (rate);

    dsp::EnvelopeSettings amplitude;
    amplitude.attackSeconds = 0.0f;
    amplitude.decaySeconds = 0.0f;
    amplitude.sustainLevel = 1.0f;
    amplitude.releaseSeconds = 0.05f;
    voiceEngine.setAmplitudeEnvelope (amplitude);

    voiceEngine.setVoicing (mode, glideSeconds, onlyWhenHeld);
}

/** The newest sounding voice. */
const engine::Voice& newest (const engine::VoiceEngine& voiceEngine)
{
    const engine::Voice* found = &voiceEngine.getVoice (0);

    for (int i = 0; i < engine::VoiceEngine::maxPolyphony; ++i)
    {
        const auto& voice = voiceEngine.getVoice (i);

        if (voice.isActive() && (! found->isActive() || voice.getStartOrder() > found->getStartOrder()))
            found = &voice;
    }

    return *found;
}

/** The frequency of @p signal from its upward zero crossings. */
double frequencyOf (const std::vector<float>& signal, std::size_t from)
{
    std::size_t first = 0, last = 0;
    int crossings = 0;

    for (auto i = from + 1; i < signal.size(); ++i)
    {
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f)
        {
            if (crossings == 0)
                first = i;

            last = i;
            ++crossings;
        }
    }

    return crossings > 1 ? rate * static_cast<double> (crossings - 1) / static_cast<double> (last - first) : 0.0;
}

class GlideTests final : public juce::UnitTest
{
public:
    GlideTests()
        : juce::UnitTest ("Mono, legato and glide", "Engine")
    {
    }

    void runTest() override
    {
        testTheDefaults();
        testPolyGlide();
        testGlideOnlyWhenHeld();
        testMono();
        testLegato();
        testThePedal();
        testTheKeys();
        testThePitchHeard();
        testTheParameters();
    }

private:
    void testTheDefaults()
    {
        beginTest ("Poly with no glide starts every note on its own pitch");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::poly, 0.0f);

        voiceEngine.noteOn (60, 0.8f);
        static_cast<void> (render (voiceEngine, 256));
        voiceEngine.noteOn (72, 0.8f);

        expectEquals (newest (voiceEngine).getGlidingNote(), 72.0f);
        expectEquals (voiceEngine.getActiveVoiceCount(), 2);
        expect (voiceEngine.getVoiceMode() == engine::VoiceMode::poly);
    }

    void testPolyGlide()
    {
        beginTest ("In Poly, a note slides from the last one's pitch, and arrives");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::poly, 0.1f);

        voiceEngine.noteOn (60, 0.8f);
        static_cast<void> (render (voiceEngine, 256));
        voiceEngine.noteOn (72, 0.8f);

        const auto& voice = newest (voiceEngine);
        expectEquals (voice.getMidiNote(), 72);
        expectWithinAbsoluteError (voice.getGlidingNote(), 60.0f, 1.0e-3f, "the glide did not start at the last note");

        static_cast<void> (render (voiceEngine, static_cast<int> (0.05 * rate)));
        expectWithinAbsoluteError (voice.getGlidingNote(), 66.0f, 0.05f, "half way, half the distance");

        static_cast<void> (render (voiceEngine, static_cast<int> (0.06 * rate)));
        expectEquals (voice.getGlidingNote(), 72.0f, "the glide did not arrive exactly");
        expect (! voice.isGliding());

        // And the first note, still sounding, never moved.
        expectEquals (voiceEngine.getVoice (0).getGlidingNote(), 60.0f);

        // A note after everything has ended still slides from the last.
        voiceEngine.allNotesOff();
        static_cast<void> (render (voiceEngine, static_cast<int> (0.2 * rate)));
        expectEquals (voiceEngine.getActiveVoiceCount(), 0);

        voiceEngine.noteOn (67, 0.8f);
        expectWithinAbsoluteError (newest (voiceEngine).getGlidingNote(), 72.0f, 1.0e-3f);
    }

    void testGlideOnlyWhenHeld()
    {
        beginTest ("Glide to LEGATO slides only between held notes");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::poly, 0.1f, true);

        voiceEngine.noteOn (60, 0.8f);
        voiceEngine.noteOff (60);
        static_cast<void> (render (voiceEngine, 256));

        voiceEngine.noteOn (64, 0.8f);
        expectEquals (newest (voiceEngine).getGlidingNote(), 64.0f, "a detached note slid");

        voiceEngine.noteOn (67, 0.8f);
        expectWithinAbsoluteError (newest (voiceEngine).getGlidingNote(), 64.0f, 1.0e-3f, "a held one did not");
    }

    void testMono()
    {
        beginTest ("Mono plays one voice, and a key let go returns to the one still held");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::mono, 0.0f);

        voiceEngine.noteOn (60, 0.8f);
        static_cast<void> (render (voiceEngine, 512));
        voiceEngine.noteOn (64, 0.8f);
        voiceEngine.noteOn (67, 0.8f);

        // A new note takes the voice through the steal's fade.
        expect (voiceEngine.getVoice (0).isStealing(), "a new note did not start the voice again");
        static_cast<void> (render (voiceEngine, 512));

        expectEquals (voiceEngine.getActiveVoiceCount(), 1, "more than one voice in Mono");
        expectEquals (voiceEngine.getVoice (0).getMidiNote(), 67);

        // Let go of the top: back to 64, the newest still held — not 60.
        voiceEngine.noteOff (67);
        expectEquals (voiceEngine.getVoice (0).getMidiNote(), 64);

        // Let go of a key not sounding: nothing heard changes.
        voiceEngine.noteOff (60);
        expectEquals (voiceEngine.getVoice (0).getMidiNote(), 64);
        expect (! voiceEngine.getVoice (0).isReleasing());

        // The last: the note ends.
        voiceEngine.noteOff (64);
        expect (voiceEngine.getVoice (0).isReleasing(), "the last key let go did not end the note");

        // A key let go while its note still waits on the steal returns too.
        voiceEngine.noteOn (50, 0.8f);
        static_cast<void> (render (voiceEngine, 512));
        voiceEngine.noteOn (55, 0.8f);
        expect (voiceEngine.getVoice (0).isStealing());
        voiceEngine.noteOff (55);
        static_cast<void> (render (voiceEngine, 512));
        expectEquals (voiceEngine.getVoice (0).getMidiNote(), 50, "a quick tap did not return to the held key");
        expect (! voiceEngine.getVoice (0).isReleasing());
    }

    void testLegato()
    {
        beginTest ("Legato moves the voice to a held-over note without starting its envelope again");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::legato, 0.05f);

        dsp::EnvelopeSettings amplitude;
        amplitude.attackSeconds = 0.2f;
        voiceEngine.setAmplitudeEnvelope (amplitude);

        voiceEngine.noteOn (60, 0.8f);
        static_cast<void> (render (voiceEngine, static_cast<int> (0.1 * rate)));

        const auto& voice = voiceEngine.getVoice (0);
        const auto levelBefore = voice.getEnvelopeValue (0);
        const auto order = voice.getStartOrder();

        voiceEngine.noteOn (67, 0.8f);

        expect (! voice.isStealing(), "a held-over note started the voice again");
        expectEquals (voice.getMidiNote(), 67);
        expectEquals (voice.getStartOrder(), order);
        expectWithinAbsoluteError (voice.getGlidingNote(), 60.0f, 1.0e-3f, "it did not glide from 60");
        expect (voice.getEnvelopeStage (0) == dsp::EnvelopeStage::attack, "the attack was interrupted");

        static_cast<void> (render (voiceEngine, 64));
        expect (voice.getEnvelopeValue (0) > levelBefore, "the attack did not carry on rising");

        // A note played alone starts it again.
        voiceEngine.noteOff (67);
        voiceEngine.noteOff (60);
        static_cast<void> (render (voiceEngine, 256));
        voiceEngine.noteOn (72, 0.8f);
        expect (voice.isStealing(), "a note played alone did not start the voice again");
    }

    void testThePedal()
    {
        beginTest ("The pedal holds the last note of a mono line, and lets it go");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::mono, 0.0f);

        voiceEngine.setSustainPedal (true);
        voiceEngine.noteOn (60, 0.8f);
        voiceEngine.noteOff (60);
        static_cast<void> (render (voiceEngine, 256));

        expect (voiceEngine.getVoice (0).isSustainHeld());
        expect (! voiceEngine.getVoice (0).isReleasing());

        voiceEngine.setSustainPedal (false);
        expect (voiceEngine.getVoice (0).isReleasing());
    }

    void testTheKeys()
    {
        beginTest ("The keys held are counted, and never past their place");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::mono, 0.0f);

        for (int note = 20; note < 80; ++note)
            voiceEngine.noteOn (note, 0.5f);

        expectEquals (voiceEngine.getHeldKeyCount(), engine::VoiceEngine::maxPolyphony);

        voiceEngine.noteOn (79, 0.5f);
        expectEquals (voiceEngine.getHeldKeyCount(), engine::VoiceEngine::maxPolyphony, "a key twice is one key");

        for (int note = 20; note < 80; ++note)
            voiceEngine.noteOff (note);

        expectEquals (voiceEngine.getHeldKeyCount(), 0);

        voiceEngine.noteOn (40, 0.5f);
        voiceEngine.allNotesOff();
        expectEquals (voiceEngine.getHeldKeyCount(), 0, "all notes off left a key held");
    }

    void testThePitchHeard()
    {
        beginTest ("The pitch heard is where the glide arrives");

        engine::VoiceEngine voiceEngine;
        prepare (voiceEngine, engine::VoiceMode::legato, 0.05f);

        voiceEngine.noteOn (57, 0.8f);
        static_cast<void> (render (voiceEngine, 2048));
        voiceEngine.noteOn (69, 0.8f);

        // Half way through the glide the pitch is between the two.
        const auto during = render (voiceEngine, static_cast<int> (0.02 * rate));
        const auto after = render (voiceEngine, static_cast<int> (0.2 * rate));

        const auto heard = frequencyOf (after, static_cast<std::size_t> (0.05 * rate));
        logMessage ("  after a glide from 220 Hz to 440 Hz: " + juce::String (heard, 2) + " Hz");
        expectWithinAbsoluteError (heard, 440.0, 1.0);

        const auto between = frequencyOf (during, 0);
        expect (between > 225.0 && between < 435.0,
                "during the glide the pitch was " + juce::String (between, 1) + " Hz");
    }

    void testTheParameters()
    {
        beginTest ("The processor plays the voice mode the parameter sets");

        ApolloAudioProcessor processor;
        auto* mode = processor.getValueTreeState().getParameter ("voice_mode");
        auto* glide = processor.getValueTreeState().getParameter ("glide_time");
        expect (mode != nullptr && glide != nullptr);

        expectEquals (mode->convertFrom0to1 (mode->getDefaultValue()), 0.0f, "Poly is the default");
        expectEquals (glide->convertFrom0to1 (glide->getDefaultValue()), 0.0f, "no glide is the default");

        mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));
        processor.prepareToPlay (rate, 256);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.8f), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 55, 0.8f), 10);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 20);

        for (int b = 0; b < 4; ++b)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
        }

        expect (processor.getVoiceEngine().getVoiceMode() == engine::VoiceMode::mono);
        expectEquals (processor.getVoiceEngine().getActiveVoiceCount(), 1, "a chord in Mono sounded more than one voice");
        expectEquals (processor.getVoiceEngine().getVoice (0).getMidiNote(), 60);
    }
};

static GlideTests glideTests;

} // namespace
