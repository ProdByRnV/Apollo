/*
    Per-note expression: polyphonic aftertouch, MPE and the RPNs that configure
    them.

    Three layers, tested where each of them lives:

      - `midi::MpeZone`, which is pure arithmetic over channel numbers, and
        whose off-by-one at either end of a zone would show up as one note in a
        chord not responding;
      - `midi::RpnParser`, which decodes a *sequence* of control changes and is
        exactly where a state machine quietly forgets to reset;
      - the processor, where a real MIDI stream has to produce voices that bend,
        press and slide independently of one another.

    The last of those is the point of the whole phase, so it is asserted on the
    engine's own voices rather than on rendered audio: "voice 0 is bent and
    voice 1 is not" is a claim a spectrum cannot make cleanly.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Modulation/ModulationTypes.h"
#include "MIDI/MpeZone.h"
#include "MIDI/RpnParser.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 128;

/** Renders one block through @p processor carrying the given messages. */
void renderWith (ApolloAudioProcessor& processor, const juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    buffer.clear();

    auto copy = midi;
    processor.processBlock (buffer, copy);
}

void send (ApolloAudioProcessor& processor, const juce::MidiMessage& message)
{
    juce::MidiBuffer midi;
    midi.addEvent (message, 0);
    renderWith (processor, midi);
}

/** Renders an empty block, so parameter changes reach the engine. */
void tick (ApolloAudioProcessor& processor)
{
    renderWith (processor, {});
}

void setParameter (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** @returns the sounding voice on a channel, or nullptr. */
[[nodiscard]] const engine::Voice* voiceOnChannel (const ApolloAudioProcessor& processor,
                                                   int channel)
{
    const auto& engine = processor.getVoiceEngine();

    for (int i = 0; i < engine::VoiceEngine::maxPolyphony; ++i)
    {
        const auto& voice = engine.getVoice (i);

        if (voice.isActive() && voice.getChannel() == channel)
            return &voice;
    }

    return nullptr;
}

class MpeTests final : public juce::UnitTest
{
public:
    MpeTests()
        : juce::UnitTest ("MPE and per-note expression", "MIDI")
    {
    }

    void runTest() override
    {
        testZoneChannelClassification();
        testZoneMessageRouting();
        testRpnDecoding();
        testRpnResetsAndNullSelection();
        testPolyphonicAftertouch();
        testChannelPressureReachesEveryVoice();
        testPerNoteBendIsIndependent();
        testNoteOffMatchesTheChannel();
        testTimbreIsPerNoteOnlyInAZone();
        testBendRangeIsAControl();
        testMpeConfigurationMessageConfiguresApollo();
        testPressureIsNotInheritedByANewNote();
    }

private:
    static void prepare (ApolloAudioProcessor& processor)
    {
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);
    }

    /** Routes @p source somewhere harmless.

        A voice with no active routing does not evaluate its sources at all —
        that is deliberate, and it is what keeps an unmodulated patch free — so a
        test that wants to read a source value has to give the voice a reason to
        produce one. The destination is oscillator 1's scan position because it
        is continuous, bounded and audible without being load-bearing.
    */
    static void routeSource (ApolloAudioProcessor& processor, dsp::ModSource source)
    {
        setParameter (processor, "mod01_source", static_cast<float> (static_cast<int> (source)));
        setParameter (processor, "mod01_destination",
                      static_cast<float> (static_cast<int> (dsp::ModDestination::osc1Position)));
        setParameter (processor, "mod01_depth", 0.5f);
        tick (processor);
    }

    /** A processor with the lower zone active and a known member bend range. */
    static void enableLowerZone (ApolloAudioProcessor& processor, int members = 15)
    {
        setParameter (processor, "mpe_zone", 1.0f);
        setParameter (processor, "mpe_members", static_cast<float> (members));
        setParameter (processor, "mpe_bend_range", 48.0f);
        tick (processor);
    }

    void testZoneChannelClassification()
    {
        beginTest ("A zone classifies every channel, at both of its ends");

        midi::MpeZone off;
        expect (! off.isActive());
        expectEquals (off.managerChannel(), 0);
        expect (! off.isMemberChannel (2));
        expect (off.appliesTo (5, 1), "with no zone, a channel message reaches every voice");

        midi::MpeZone lower { midi::MpeZoneType::lower, 7 };
        expect (lower.isActive());
        expectEquals (lower.managerChannel(), 1);
        expectEquals (lower.firstMemberChannel(), 2);
        expectEquals (lower.lastMemberChannel(), 8);
        expect (lower.isManagerChannel (1));
        expect (! lower.isMemberChannel (1), "the manager is not one of its own members");
        expect (lower.isMemberChannel (2));
        expect (lower.isMemberChannel (8));
        expect (! lower.isMemberChannel (9), "one past the last member is outside the zone");

        midi::MpeZone upper { midi::MpeZoneType::upper, 7 };
        expectEquals (upper.managerChannel(), 16);
        expectEquals (upper.firstMemberChannel(), 9);
        expectEquals (upper.lastMemberChannel(), 15);
        expect (upper.isManagerChannel (16));
        expect (! upper.isMemberChannel (16));
        expect (upper.isMemberChannel (9));
        expect (! upper.isMemberChannel (8));

        // The full-width lower zone: fifteen members, 2 through 16.
        midi::MpeZone full { midi::MpeZoneType::lower, 15 };
        expectEquals (full.firstMemberChannel(), 2);
        expectEquals (full.lastMemberChannel(), 16);
        expect (full.isMemberChannel (16));
    }

    void testZoneMessageRouting()
    {
        beginTest ("The manager channel addresses the zone; a member addresses its own note");

        const midi::MpeZone lower { midi::MpeZoneType::lower, 15 };

        expect (lower.appliesTo (1, 4), "the manager reaches a voice on channel 4");
        expect (lower.appliesTo (1, 9), "and one on channel 9");
        expect (lower.appliesTo (4, 4), "a member reaches its own voice");
        expect (! lower.appliesTo (4, 9), "and reaches no other");
        expect (! lower.appliesTo (4, 1), "not even one on the manager channel");
    }

    void testRpnDecoding()
    {
        beginTest ("An RPN is decoded from its sequence of control changes");

        midi::RpnParser parser;

        // Nothing until the data entry arrives.
        expect (! parser.process (1, 101, 0).isValid());
        expect (! parser.process (1, 100, 0).isValid());

        const auto sensitivity = parser.process (1, 6, 12);
        expect (sensitivity.isValid());
        expectEquals (static_cast<int> (sensitivity.type),
                      static_cast<int> (midi::RpnType::pitchBendSensitivity));
        expectEquals (sensitivity.channel, 1);
        expectEquals (sensitivity.valueMsb, 12);

        // The fine half completes with the coarse one remembered, rather than
        // reporting a semitone count of zero.
        const auto withCents = parser.process (1, 38, 50);
        expect (withCents.isValid());
        expectEquals (withCents.valueMsb, 12);
        expectEquals (withCents.valueLsb, 50);

        // The MPE Configuration Message.
        expect (! parser.process (1, 101, 0).isValid());
        expect (! parser.process (1, 100, 6).isValid());

        const auto configuration = parser.process (1, 6, 15);
        expect (configuration.isValid());
        expectEquals (static_cast<int> (configuration.type),
                      static_cast<int> (midi::RpnType::mpeConfiguration));
        expectEquals (configuration.valueMsb, 15);

        // Channels are independent: a sequence in flight on one must not be
        // completed by a data entry on another.
        midi::RpnParser perChannel;
        expect (! perChannel.process (1, 101, 0).isValid());
        expect (! perChannel.process (1, 100, 0).isValid());
        expect (! perChannel.process (5, 6, 12).isValid(),
                "channel 5 selected nothing, so its data entry means nothing");
        expect (perChannel.process (1, 6, 12).isValid());
    }

    void testRpnResetsAndNullSelection()
    {
        beginTest ("A data entry with nothing selected is ignored");

        midi::RpnParser parser;

        expect (! parser.process (1, 6, 12).isValid(),
                "the parser must start at the null RPN, not at parameter zero");

        // An unsupported parameter number is ignored rather than guessed at.
        expect (! parser.process (1, 101, 0).isValid());
        expect (! parser.process (1, 100, 3).isValid());
        expect (! parser.process (1, 6, 12).isValid());

        // A non-registered parameter selection cancels whatever was selected, so
        // its data entry cannot be applied to an RPN.
        expect (! parser.process (1, 101, 0).isValid());
        expect (! parser.process (1, 100, 0).isValid());
        expect (! parser.process (1, 99, 1).isValid());
        expect (! parser.process (1, 6, 12).isValid(),
                "an NRPN selection must cancel the RPN, not leave it armed");

        // The null RPN, which is how a controller says it has finished.
        expect (! parser.process (1, 101, 0).isValid());
        expect (! parser.process (1, 100, 0).isValid());
        expect (parser.process (1, 6, 12).isValid());
        expect (! parser.process (1, 101, 127).isValid());
        expect (! parser.process (1, 100, 127).isValid());
        expect (! parser.process (1, 6, 12).isValid(),
                "the null RPN must deselect");

        parser.reset();
        expect (! parser.process (1, 6, 12).isValid());

        // Every RPN controller is recognised as plumbing, and nothing else is.
        for (const int controller : { 6, 38, 98, 99, 100, 101 })
            expect (midi::RpnParser::isRpnController (controller));

        for (const int controller : { 0, 1, 7, 74, 64, 127 })
            expect (! midi::RpnParser::isRpnController (controller));
    }

    void testPolyphonicAftertouch()
    {
        beginTest ("Polyphonic aftertouch presses one note and leaves the others alone");

        ApolloAudioProcessor processor;
        prepare (processor);
        routeSource (processor, dsp::ModSource::aftertouch);

        send (processor, juce::MidiMessage::noteOn (1, 60, 0.8f));
        send (processor, juce::MidiMessage::noteOn (1, 64, 0.8f));

        send (processor, juce::MidiMessage::aftertouchChange (1, 60, 127));

        const auto& engine = processor.getVoiceEngine();

        const engine::Voice* pressed = nullptr;
        const engine::Voice* untouched = nullptr;

        for (int i = 0; i < engine::VoiceEngine::maxPolyphony; ++i)
        {
            const auto& voice = engine.getVoice (i);

            if (! voice.isActive())
                continue;

            if (voice.getMidiNote() == 60) pressed = &voice;
            if (voice.getMidiNote() == 64) untouched = &voice;
        }

        expect (pressed != nullptr && untouched != nullptr, "both notes should be sounding");

        if (pressed == nullptr || untouched == nullptr)
            return;

        expectWithinAbsoluteError (pressed->getPressure(), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (untouched->getPressure(), 0.0f, 1.0e-6f,
                                   "polyphonic aftertouch must not reach a note it did not name");

        // And it is what the aftertouch modulation source reads.
        expectWithinAbsoluteError (pressed->getSourceValue (dsp::ModSource::aftertouch), 1.0f,
                                   1.0e-6f);
        expectWithinAbsoluteError (untouched->getSourceValue (dsp::ModSource::aftertouch), 0.0f,
                                   1.0e-6f);
    }

    void testChannelPressureReachesEveryVoice()
    {
        beginTest ("Channel pressure still presses everything, as it always has");

        ApolloAudioProcessor processor;
        prepare (processor);

        send (processor, juce::MidiMessage::noteOn (1, 60, 0.8f));
        send (processor, juce::MidiMessage::noteOn (1, 64, 0.8f));
        send (processor, juce::MidiMessage::channelPressureChange (1, 64));

        const auto& engine = processor.getVoiceEngine();
        int checked = 0;

        for (int i = 0; i < engine::VoiceEngine::maxPolyphony; ++i)
        {
            const auto& voice = engine.getVoice (i);

            if (! voice.isActive())
                continue;

            expectWithinAbsoluteError (voice.getPressure(), 64.0f / 127.0f, 1.0e-6f);
            ++checked;
        }

        expectEquals (checked, 2);
    }

    void testPerNoteBendIsIndependent()
    {
        beginTest ("Under MPE each note bends on its own channel");

        ApolloAudioProcessor processor;
        prepare (processor);
        enableLowerZone (processor);

        // Two notes, two member channels — which is the entire MPE convention.
        send (processor, juce::MidiMessage::noteOn (2, 60, 0.8f));
        send (processor, juce::MidiMessage::noteOn (3, 60, 0.8f));

        const auto* first = voiceOnChannel (processor, 2);
        const auto* second = voiceOnChannel (processor, 3);

        expect (first != nullptr && second != nullptr,
                "the same note number on two channels must produce two voices");

        if (first == nullptr || second == nullptr)
            return;

        expectWithinAbsoluteError (first->getBentNote(), 60.0f, 1.0e-4f);
        expectWithinAbsoluteError (second->getBentNote(), 60.0f, 1.0e-4f);

        // Bend channel 2 fully up. With a 48-semitone member range that is four
        // octaves, which is unmistakable.
        send (processor, juce::MidiMessage::pitchWheel (2, 16383));

        expectWithinAbsoluteError (first->getBentNote(), 108.0f, 0.05f,
                                   "a full member bend is the whole member range");
        expectWithinAbsoluteError (second->getBentNote(), 60.0f, 1.0e-4f,
                                   "the note on the other channel must not move at all");

        // The manager channel bends the whole zone, through the wheel's own
        // range rather than the member range.
        send (processor, juce::MidiMessage::pitchWheel (1, 16383));

        expectWithinAbsoluteError (second->getBentNote(), 62.0f, 0.01f,
                                   "the manager wheel uses the wheel range, not the member range");
        expectWithinAbsoluteError (first->getBentNote(), 110.0f, 0.05f,
                                   "and it adds to the per-note bend rather than replacing it");
    }

    void testNoteOffMatchesTheChannel()
    {
        beginTest ("A note-off ends the note on its own channel");

        ApolloAudioProcessor processor;
        prepare (processor);
        enableLowerZone (processor);

        send (processor, juce::MidiMessage::noteOn (2, 60, 0.8f));
        send (processor, juce::MidiMessage::noteOn (3, 60, 0.8f));

        send (processor, juce::MidiMessage::noteOff (2, 60));

        const auto* onTwo = voiceOnChannel (processor, 2);
        const auto* onThree = voiceOnChannel (processor, 3);

        expect (onTwo != nullptr && onTwo->isReleasing(),
                "the note on channel 2 should be releasing");
        expect (onThree != nullptr && ! onThree->isReleasing(),
                "the identically numbered note on channel 3 must still be held");
    }

    void testTimbreIsPerNoteOnlyInAZone()
    {
        beginTest ("CC 74 is the timbre axis inside a zone and an ordinary control outside one");

        {
            ApolloAudioProcessor processor;
            prepare (processor);
            enableLowerZone (processor);
            routeSource (processor, dsp::ModSource::timbre);

            send (processor, juce::MidiMessage::noteOn (2, 60, 0.8f));
            send (processor, juce::MidiMessage::noteOn (3, 64, 0.8f));

            send (processor, juce::MidiMessage::controllerEvent (2, midi::timbreController, 127));

            const auto* onTwo = voiceOnChannel (processor, 2);
            const auto* onThree = voiceOnChannel (processor, 3);

            expect (onTwo != nullptr && onThree != nullptr);

            if (onTwo == nullptr || onThree == nullptr)
                return;

            expectWithinAbsoluteError (onTwo->getTimbre(), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (onThree->getTimbre(), 0.5f, 1.0e-6f,
                                       "an untouched note stays at the centre of the axis");

            // Bipolar around the centre, so an untouched note reads zero rather
            // than -1.
            expectWithinAbsoluteError (onTwo->getSourceValue (dsp::ModSource::timbre), 1.0f,
                                       1.0e-6f);
            expectWithinAbsoluteError (onThree->getSourceValue (dsp::ModSource::timbre), 0.0f,
                                       1.0e-6f);
        }

        {
            // With no zone, CC 74 is an ordinary control change and therefore an
            // ordinary MIDI Learn target.
            ApolloAudioProcessor processor;
            prepare (processor);

            auto& control = processor.getMidiControl();
            expect (control.beginLearn ("master_gain"));

            send (processor, juce::MidiMessage::controllerEvent (1, midi::timbreController, 100));
            control.flushPendingChanges();

            const auto mappings = control.getMappings();
            expectEquals (mappings.size(), 1,
                          "CC 74 outside a zone must be learnable like any other");

            const auto* mapping = mappings.findForParameter (params::indexOfParameter ("master_gain"));
            expect (mapping != nullptr && mapping->address.controller == midi::timbreController);
        }
    }

    void testBendRangeIsAControl()
    {
        beginTest ("The pitch-bend range is a control, not a constant");

        ApolloAudioProcessor processor;
        prepare (processor);
        routeSource (processor, dsp::ModSource::pitchBend);

        send (processor, juce::MidiMessage::noteOn (1, 60, 0.8f));

        const auto* voice = voiceOnChannel (processor, 1);
        expect (voice != nullptr);

        if (voice == nullptr)
            return;

        expectWithinAbsoluteError (voice->getBentNote(), 60.0f, 1.0e-4f);

        send (processor, juce::MidiMessage::pitchWheel (1, 16383));

        // Two semitones by default.
        expectWithinAbsoluteError (voice->getBentNote(), 62.0f, 0.01f);

        // Widened to twelve, with the wheel still held: the pitch must follow
        // immediately rather than wait for the next wheel message.
        setParameter (processor, "midi_bend_range", 12.0f);
        tick (processor);

        expectWithinAbsoluteError (voice->getBentNote(), 72.0f, 0.02f,
                                   "a full wheel over a twelve-semitone range is an octave");

        // The modulation source is the wheel position, so it is unchanged by
        // the range: 1.0 at the top of the travel either way.
        expectWithinAbsoluteError (voice->getSourceValue (dsp::ModSource::pitchBend), 1.0f,
                                   0.001f);
    }

    void testMpeConfigurationMessageConfiguresApollo()
    {
        beginTest ("An MPE Configuration Message configures the zone");

        ApolloAudioProcessor processor;
        prepare (processor);

        expect (! processor.getVoiceEngine().getMpeZone().isActive(),
                "MPE is off until something asks for it");

        // RPN 6 on channel 1, value 15: the lower zone with fifteen members.
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 100, 6), 1);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 6, 15), 2);
        renderWith (processor, midi);

        // The message queues a parameter change, exactly as a mapped controller
        // does, so it reaches the engine by way of APVTS and the next block.
        processor.getMidiControl().flushPendingChanges();
        tick (processor);

        const auto& zone = processor.getVoiceEngine().getMpeZone();
        expect (zone.isActive(), "the configuration message should have switched MPE on");
        expectEquals (static_cast<int> (zone.type), static_cast<int> (midi::MpeZoneType::lower));
        expectEquals (zone.memberCount, 15);

        // And it went through the parameter, so the host and the interface see it.
        const auto* parameter = processor.getValueTreeState().getParameter ("mpe_zone");
        expect (parameter != nullptr);
        expectWithinAbsoluteError (parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue())
                                                        : -1.0f,
                                   1.0f, 0.01f);

        // Zero members disables the zone again.
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
        off.addEvent (juce::MidiMessage::controllerEvent (1, 100, 6), 1);
        off.addEvent (juce::MidiMessage::controllerEvent (1, 6, 0), 2);
        renderWith (processor, off);

        processor.getMidiControl().flushPendingChanges();
        tick (processor);

        expect (! processor.getVoiceEngine().getMpeZone().isActive(),
                "a member count of zero disables the zone");

        // RPN 0 sets the wheel range through the same route.
        juce::MidiBuffer sensitivity;
        sensitivity.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
        sensitivity.addEvent (juce::MidiMessage::controllerEvent (1, 100, 0), 1);
        sensitivity.addEvent (juce::MidiMessage::controllerEvent (1, 6, 12), 2);
        renderWith (processor, sensitivity);

        processor.getMidiControl().flushPendingChanges();
        tick (processor);

        const auto* range = processor.getValueTreeState().getParameter ("midi_bend_range");
        expect (range != nullptr);
        expectWithinAbsoluteError (range != nullptr ? range->convertFrom0to1 (range->getValue())
                                                    : -1.0f,
                                   12.0f, 0.01f);
    }

    void testPressureIsNotInheritedByANewNote()
    {
        beginTest ("A new note starts unpressed, and adopts a bend already in force");

        ApolloAudioProcessor processor;
        prepare (processor);
        enableLowerZone (processor);

        send (processor, juce::MidiMessage::noteOn (2, 60, 0.8f));
        send (processor, juce::MidiMessage::channelPressureChange (2, 127));

        const auto* first = voiceOnChannel (processor, 2);
        expect (first != nullptr && first->getPressure() > 0.9f);

        send (processor, juce::MidiMessage::noteOff (2, 60));

        // The controller places the next note's pitch before playing it, which
        // is what every MPE controller does.
        send (processor, juce::MidiMessage::pitchWheel (2, 16383));
        send (processor, juce::MidiMessage::noteOn (2, 72, 0.8f));

        const engine::Voice* fresh = nullptr;
        const auto& engine = processor.getVoiceEngine();

        for (int i = 0; i < engine::VoiceEngine::maxPolyphony; ++i)
        {
            const auto& voice = engine.getVoice (i);

            if (voice.isActive() && voice.getMidiNote() == 72)
                fresh = &voice;
        }

        expect (fresh != nullptr);

        if (fresh == nullptr)
            return;

        expectWithinAbsoluteError (fresh->getPressure(), 0.0f, 1.0e-6f,
                                   "pressure is a force and must not be inherited");

        expectWithinAbsoluteError (fresh->getBentNote(), 120.0f, 0.05f,
                                   "the new note must adopt the bend the controller placed "
                                   "before it");
    }
};

MpeTests mpeTests;

} // namespace
