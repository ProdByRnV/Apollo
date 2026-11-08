/*
    The audio thread never allocates, frees or locks (Phase 18i, ADR-0127).

    CLAUDE.md §7 is the rule every DSP file in Apollo is written to, and until
    now it was checked by reading. These tests play the instrument the way a
    host does — notes, controllers, automation, presets, a transport, content
    replaced while it plays, the editor watching — with every audio callback
    inside an AudioThreadSection, and fail on anything the callback did that it
    may not. AudioThreadMonitor.h says what "anything" covers in each build.

    The host's side of a callback is outside the section, as it is in a host:
    building the MidiBuffer, sizing the AudioBuffer, setting a parameter from the
    message thread, loading a preset. Only what Apollo does with them is
    Apollo's to answer for.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/LFO/LfoCurve.h"
#include "DSP/Modulation/ModulationTypes.h"
#include "DSP/Noise/TransientLibrary.h"
#include "DSP/Oscillators/WavetableBuilder.h"
#include "RealTime/AudioThreadMonitor.h"
#include "Resources/FactoryPresets.h"
#include "State/PresetDocument.h"
#include "UI/ParameterBridge.h"

namespace
{

using namespace apollo;
using realtime::AudioThreadSection;

constexpr double twoPi = 6.283185307179586476925286766559;

/** A transport that moves, and can be told to stop, jump or lose its tempo. */
class Transport final : public juce::AudioPlayHead
{
public:
    double rate = 48000.0;
    double bpm = 128.0;
    bool hasTempo = true;
    bool playing = true;
    double beat = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;

        if (hasTempo)
            info.setBpm (bpm);

        info.setPpqPosition (beat);
        info.setIsPlaying (playing);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return info;
    }

    void advance (int samples) noexcept
    {
        if (playing)
            beat += static_cast<double> (samples) / rate * bpm / 60.0;
    }
};

/** A table that is not one of Apollo's, to publish while a note plays it. */
[[nodiscard]] std::unique_ptr<dsp::Wavetable> userTable (int harmonic)
{
    std::vector<std::vector<double>> frames (4, std::vector<double> (2048));

    for (std::size_t f = 0; f < frames.size(); ++f)
        for (std::size_t i = 0; i < frames[f].size(); ++i)
            frames[f][i] = std::sin (static_cast<double> (harmonic) * twoPi * static_cast<double> (i) / 2048.0)
                         * (1.0 - 0.2 * static_cast<double> (f));

    auto table = std::make_unique<dsp::Wavetable>();
    dsp::buildWavetable (*table, dsp::analyseFrames (frames, dsp::Wavetable::topLevelHarmonics));
    return table;
}

/** A short noise sample of the user's own. */
[[nodiscard]] std::unique_ptr<dsp::TransientSample> userSample (double frequency)
{
    std::vector<double> tone (4800);

    for (std::size_t n = 0; n < tone.size(); ++n)
        tone[n] = std::sin (twoPi * frequency * static_cast<double> (n) / 48000.0)
                * std::exp (-static_cast<double> (n) / 1200.0);

    return std::make_unique<dsp::TransientSample> (dsp::makeTransient (tone, tone));
}

//==============================================================================
/** One instance, played as a host plays it. */
class Session
{
public:
    Session (juce::UnitTest& testToUse, double sampleRate, int maximumBlock)
        : test (testToUse), processor (std::make_unique<ApolloAudioProcessor>())
    {
        transport.rate = sampleRate;
        processor->setPlayHead (&transport);

        // The editor is open: every scope, meter and spectrum is being taken.
        processor->getTelemetry().setCapturing (true);
        prepare (sampleRate, maximumBlock);
    }

    ~Session()
    {
        processor->setPlayHead (nullptr);
    }

    void prepare (double sampleRate, int maximumBlock)
    {
        transport.rate = sampleRate;
        maxBlock = maximumBlock;
        processor->prepareToPlay (sampleRate, maximumBlock);

        // A host's buffers, sized once. The over-long block test needs room
        // for more than the promise, which a host breaking it would have.
        hostBuffer.setSize (2, maximumBlock * 4 + 17);
    }

    ApolloAudioProcessor& instrument() noexcept { return *processor; }
    Transport& clock() noexcept { return transport; }

    void setPlain (const juce::String& id, float plain)
    {
        auto* parameter = processor->getValueTreeState().getParameter (id);
        test.expect (parameter != nullptr, "no parameter " + id);

        if (parameter != nullptr)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    }

    /** Every parameter to a value of @p random's choosing, as automation does. */
    void scramble (juce::Random& random)
    {
        for (auto* parameter : processor->getParameters())
            parameter->setValueNotifyingHost (random.nextFloat());

        // Loud enough to hear, and never so quiet the engine has nothing to do.
        setPlain ("master_gain", 0.0f);
    }

    /** One callback of @p numSamples, with @p midi, inside a section. */
    void play (int numSamples, const juce::MidiBuffer& midi = {}, bool bypassed = false)
    {
        // The host's work: a buffer with the sidechain's input in it, and the
        // MIDI for this block.
        for (int channel = 0; channel < hostBuffer.getNumChannels(); ++channel)
            for (int i = 0; i < numSamples; ++i)
                hostBuffer.setSample (channel, i, 0.05f * inputNoise.nextFloat() - 0.025f);

        juce::AudioBuffer<float> block (hostBuffer.getArrayOfWritePointers(), 2, 0, numSamples);
        juce::MidiBuffer events (midi);

        {
            const AudioThreadSection section;

            if (bypassed)
                processor->processBlockBypassed (block, events);
            else
                processor->processBlock (block, events);
        }

        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (block.getSample (channel, i)))
                    ++nonFinite;

        transport.advance (numSamples);
    }

    /** @p count blocks of the prepared size, with nothing new. */
    void hold (int count)
    {
        for (int i = 0; i < count; ++i)
            play (maxBlock);
    }

    int maxBlock = 0;
    int nonFinite = 0;

private:
    juce::UnitTest& test;
    Transport transport;
    std::unique_ptr<ApolloAudioProcessor> processor;
    juce::AudioBuffer<float> hostBuffer;
    juce::Random inputNoise { 18 };
};

[[nodiscard]] juce::MidiBuffer chord (std::initializer_list<int> notes, float velocity, int channel = 1)
{
    juce::MidiBuffer midi;
    int offset = 0;

    for (const auto note : notes)
        midi.addEvent (velocity > 0.0f ? juce::MidiMessage::noteOn (channel, note, velocity)
                                       : juce::MidiMessage::noteOff (channel, note),
                       offset++ * 3);

    return midi;
}

[[nodiscard]] juce::MidiBuffer single (const juce::MidiMessage& message, int offset = 0)
{
    juce::MidiBuffer midi;
    midi.addEvent (message, offset);
    return midi;
}

//==============================================================================
class RealTimeSafetyTests final : public juce::UnitTest
{
public:
    RealTimeSafetyTests()
        : juce::UnitTest ("The audio thread", "Real time")
    {
    }

    void runTest() override
    {
        // Wherever the instrument posts a message to the message thread, it
        // posts it to a real message queue: in a host there always is one, and
        // posting is precisely what may not be done from the callback.
        juce::ignoreUnused (juce::MessageManager::getInstance());

        logMessage ("  watching: " + realtime::describeCoverage());

        testTheMonitorCanFail();
        testPresetsPlayedByHand();
        testMidiOfEveryKind();
        testAutomationOfEverything();
        testTheNewDestinations();
        testTheHostsWaysOfCalling();
        testContentReplacedWhilePlaying();
        testOtherRatesAndSizes();
        testTheInterfacesAutomationListener();
    }

private:
    /** Expects nothing to have happened in a section since the last call. */
    void expectClean (const juce::String& what, const Session* session = nullptr)
    {
        const auto findings = realtime::takeFindings();

        if (! findings.clean())
        {
            auto stack = juce::StringArray::fromLines (findings.firstStack);
            stack.removeRange (24, stack.size());

            logMessage ("  " + what + ": " + juce::String (findings.allocations) + " allocations, "
                        + juce::String (findings.deallocations) + " frees on the audio thread. The first:\n"
                        + stack.joinIntoString ("\n"));
        }

        expect (findings.allocations == 0,
                what + ": the audio thread allocated " + juce::String (findings.allocations) + " times");
        expect (findings.deallocations == 0,
                what + ": the audio thread freed memory " + juce::String (findings.deallocations) + " times");

        if (session != nullptr)
            expectEquals (session->nonFinite, 0, what + ": the output must stay finite");
    }

    void testTheMonitorCanFail()
    {
        beginTest ("The monitor sees an allocation on the audio thread, and only there");

        const auto seen = realtime::coverage();

        if (seen.sanitizer)
        {
            // RTSan ends the run at the first violation, so it cannot be shown
            // one here and carry on. CI shows it one separately, with
            // `ApolloTests --realtime-canary`, and requires the run to stop.
            logMessage ("  under RealtimeSanitizer: proved by the canary run instead");
            expect (true);
            return;
        }

        if (! seen.cppAllocation)
        {
            logMessage ("  this build watches nothing, so there is nothing to prove");
            expect (true);
            return;
        }

        (void) realtime::takeFindings();

        // Outside a section, allocation is anyone's business.
        {
            auto outside = std::make_unique<std::vector<int>> (64);
            juce::ignoreUnused (outside);
        }

        expect (realtime::takeFindings().clean(), "an allocation outside a section was counted");

        realtime::allocateInsideASection();
        const auto inside = realtime::takeFindings();

        expect (inside.allocations >= 1, "an allocation inside a section was not seen");
        expect (inside.deallocations >= 1, "a free inside a section was not seen");

        // And a section ended is ended.
        {
            const AudioThreadSection section;
        }

        {
            auto after = std::make_unique<int> (18);
            juce::ignoreUnused (after);
        }

        expect (realtime::takeFindings().clean(), "the thread stayed marked after its section ended");
    }

    void testPresetsPlayedByHand()
    {
        beginTest ("Every factory preset, played: chords, a bend, the wheel, pressure, release");

        Session session (*this, 48000.0, 256);
        auto& processor = session.instrument();

        // The first callback after prepareToPlay is where lazy work would hide.
        session.hold (2);
        expectClean ("the first blocks after prepareToPlay", &session);

        for (const auto& preset : resources::factoryPresets())
        {
            const auto presetName = juce::String (std::string (preset.name));

            expect (presets::read (processor.getValueTreeState(), resources::render (preset))
                        == state::StateLoadResult::ok,
                    presetName + " did not load");
            processor.notifyStateReplaced();

            session.play (256, chord ({ 48, 55, 60, 64, 67 }, 0.8f));

            for (int i = 0; i < 12; ++i)
            {
                juce::MidiBuffer gesture;
                gesture.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + i * 600), 10);
                gesture.addEvent (juce::MidiMessage::controllerEvent (1, 1, i * 10), 40);
                gesture.addEvent (juce::MidiMessage::channelPressureChange (1, i * 9), 90);
                gesture.addEvent (juce::MidiMessage::aftertouchChange (1, 60, i * 10), 130);
                session.play (256, gesture);
            }

            session.play (256, chord ({ 48, 55, 60, 64, 67 }, 0.0f));
            session.hold (24);

            expectClean (presetName, &session);
        }
    }

    void testMidiOfEveryKind()
    {
        beginTest ("MIDI of every kind: stealing, the pedal, learn, RPNs, MPE, programs, panic, SysEx");

        Session session (*this, 48000.0, 256);
        auto& processor = session.instrument();

        // More notes than there are voices, in one block, twice over: the
        // engine steals.
        {
            juce::MidiBuffer flood;

            for (int note = 30; note < 90; ++note)
                flood.addEvent (juce::MidiMessage::noteOn (1, note, 0.7f), (note - 30) * 4);

            session.play (256, flood);
            session.hold (4);
            session.play (256, flood);
            session.hold (4);
        }

        expectClean ("more notes than voices", &session);

        // The sustain pedal held through note-offs, then lifted.
        session.play (256, single (juce::MidiMessage::controllerEvent (1, 64, 127)));
        session.play (256, chord ({ 60, 63, 67 }, 0.9f));
        session.play (256, chord ({ 60, 63, 67 }, 0.0f));
        session.hold (4);
        session.play (256, single (juce::MidiMessage::controllerEvent (1, 64, 0)));
        session.hold (4);
        expectClean ("the sustain pedal", &session);

        // MIDI Learn: the audio thread hears the controller that completes it,
        // and then the mapping it made.
        processor.getMidiControl().beginLearn (juce::String ("filter1_cutoff"));
        session.play (256, single (juce::MidiMessage::controllerEvent (1, 74, 40)));
        processor.getMidiControl().flushPendingChanges();
        session.play (256, single (juce::MidiMessage::controllerEvent (1, 74, 90)));

        for (int i = 0; i < 8; ++i)
            session.play (256, single (juce::MidiMessage::controllerEvent (1, 74, i * 15)));

        expectClean ("MIDI Learn and a mapped controller", &session);

        // Pitch-bend range by RPN, the long way, in one block.
        {
            juce::MidiBuffer rpn;
            rpn.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
            rpn.addEvent (juce::MidiMessage::controllerEvent (1, 100, 0), 1);
            rpn.addEvent (juce::MidiMessage::controllerEvent (1, 6, 12), 2);
            rpn.addEvent (juce::MidiMessage::controllerEvent (1, 38, 0), 3);
            rpn.addEvent (juce::MidiMessage::controllerEvent (1, 101, 127), 4);
            rpn.addEvent (juce::MidiMessage::controllerEvent (1, 100, 127), 5);
            session.play (256, rpn);
        }

        // An MPE zone declared by its configuration message, then played on
        // member channels with a bend, pressure and timbre each.
        {
            juce::MidiBuffer mcm;
            mcm.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
            mcm.addEvent (juce::MidiMessage::controllerEvent (1, 100, 6), 1);
            mcm.addEvent (juce::MidiMessage::controllerEvent (1, 6, 7), 2);
            session.play (256, mcm);

            juce::MidiBuffer notes;

            for (int channel = 2; channel <= 8; ++channel)
                notes.addEvent (juce::MidiMessage::noteOn (channel, 50 + channel * 2, 0.8f), channel);

            session.play (256, notes);

            for (int i = 0; i < 10; ++i)
            {
                juce::MidiBuffer expression;

                for (int channel = 2; channel <= 8; ++channel)
                {
                    expression.addEvent (juce::MidiMessage::pitchWheel (channel, 8192 + i * 300 * (channel % 2 ? 1 : -1)), channel);
                    expression.addEvent (juce::MidiMessage::channelPressureChange (channel, i * 12), 20 + channel);
                    expression.addEvent (juce::MidiMessage::controllerEvent (channel, 74, i * 12), 40 + channel);
                }

                session.play (256, expression);
            }

            juce::MidiBuffer off;

            for (int channel = 2; channel <= 8; ++channel)
                off.addEvent (juce::MidiMessage::noteOff (channel, 50 + channel * 2), channel);

            session.play (256, off);
            session.hold (4);
        }

        expectClean ("RPNs and MPE", &session);

        // Program changes and bank selects, with program changes switched on.
        session.setPlain ("midi_program_change", 1.0f);
        {
            juce::MidiBuffer program;
            program.addEvent (juce::MidiMessage::controllerEvent (1, 0, 0), 0);
            program.addEvent (juce::MidiMessage::controllerEvent (1, 32, 1), 1);
            program.addEvent (juce::MidiMessage::programChange (1, 3), 2);
            session.play (256, program);
            session.hold (2);
        }

        expectClean ("program changes", &session);

        // Panic, the clock, the transport's messages, and SysEx — which no
        // part of Apollo reads, and which must cost nothing to ignore.
        {
            session.play (256, chord ({ 40, 52, 64 }, 0.8f));

            juce::MidiBuffer panic;
            panic.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            panic.addEvent (juce::MidiMessage::allSoundOff (1), 10);
            panic.addEvent (juce::MidiMessage::controllerEvent (1, 121, 0), 20);
            panic.addEvent (juce::MidiMessage::midiClock(), 30);
            panic.addEvent (juce::MidiMessage::midiStart(), 40);
            panic.addEvent (juce::MidiMessage::songPositionPointer (16), 50);
            panic.addEvent (juce::MidiMessage::midiStop(), 60);

            const juce::uint8 sysex[] = { 0x7e, 0x7f, 0x06, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70 };
            panic.addEvent (juce::MidiMessage::createSysExMessage (sysex, static_cast<int> (sizeof (sysex))), 70);
            session.play (256, panic);
            session.hold (2);
        }

        expectClean ("panic, clock and SysEx", &session);

        // Notes played on the interface's keyboard.
        for (int note = 60; note < 72; ++note)
        {
            (void) processor.getPageNotes().push (note, 100);
            session.play (256);
            (void) processor.getPageNotes().push (note, 0);
            session.play (256);
        }

        expectClean ("notes from the interface", &session);
    }

    void testAutomationOfEverything()
    {
        beginTest ("Every parameter automated at once, block after block, with notes sounding");

        Session session (*this, 48000.0, 256);
        juce::Random random (1818);

        // The rack rearranged, the matrix rewired, tables and filters and
        // modes switched, on every block: whatever any parameter can make the
        // engine do, some block asks for it.
        for (int round = 0; round < 6; ++round)
        {
            session.scramble (random);
            session.play (256, chord ({ 36 + round, 48 + round, 55 + round, 62 + round }, 0.9f));

            for (int i = 0; i < 16; ++i)
            {
                session.scramble (random);
                session.play (256);
            }

            session.play (256, chord ({ 36 + round, 48 + round, 55 + round, 62 + round }, 0.0f));
            session.hold (4);
        }

        expectClean ("automation of every parameter", &session);
    }

    void testTheNewDestinations()
    {
        beginTest ("The matrix on the master, an envelope's times and sustain, and an LFO's rate and phase");

        // Phase 19b and 19d: the master followed across each block, envelopes
        // retimed mid-stage every control block, and LFO rates and phases
        // moved, while notes start, steal and end.
        Session session (*this, 48000.0, 256);

        const auto route = [&session] (int slot, dsp::ModSource source, dsp::ModDestination destination, float depth)
        {
            const auto prefix = juce::String ("mod") + juce::String (slot).paddedLeft ('0', 2);
            session.setPlain (prefix + "_source", static_cast<float> (source));
            session.setPlain (prefix + "_destination", static_cast<float> (destination));
            session.setPlain (prefix + "_depth", depth);
        };

        session.setPlain ("env1_attack", 400.0f);
        session.setPlain ("env1_decay", 600.0f);
        session.setPlain ("env1_sustain", 0.5f);
        session.setPlain ("lfo1_rate", 3.0f);
        session.setPlain ("lfo2_rate", 0.7f);

        route (1, dsp::ModSource::lfo1, dsp::ModDestination::masterGain, -0.2f);
        route (2, dsp::ModSource::velocity, dsp::ModDestination::envelope1Attack, -0.4f);
        route (3, dsp::ModSource::lfo2, dsp::ModDestination::envelope1Decay, 0.3f);
        route (4, dsp::ModSource::lfo2, dsp::ModDestination::envelope1Sustain, 0.3f);
        route (5, dsp::ModSource::modWheel, dsp::ModDestination::lfo1Rate, 0.4f);
        route (6, dsp::ModSource::lfo1, dsp::ModDestination::lfo2Phase, 0.5f);
        route (7, dsp::ModSource::keyTrack, dsp::ModDestination::envelope2Delay, 0.5f);
        route (8, dsp::ModSource::random, dsp::ModDestination::envelope1Release, 0.3f);

        for (int round = 0; round < 4; ++round)
        {
            session.play (256, chord ({ 40 + round, 52 + round, 59 + round, 64 + round }, 0.3f + 0.2f * static_cast<float> (round)));

            for (int i = 0; i < 12; ++i)
                session.play (256, single (juce::MidiMessage::controllerEvent (1, 1, i * 10)));

            session.play (256, chord ({ 40 + round, 52 + round, 59 + round, 64 + round }, 0.0f));
            session.hold (6);
        }

        // And the routings taken away while notes sound.
        session.play (256, chord ({ 48, 55, 60 }, 0.9f));
        for (int slot = 1; slot <= 8; ++slot)
            route (slot, dsp::ModSource::none, dsp::ModDestination::none, 0.0f);
        session.hold (8);

        expectClean ("the master, envelopes and LFOs as destinations", &session);

        // Phase 19e: one voice, every note taking it or moving it, gliding,
        // with keys let go returning to the ones still held and the pedal.
        for (const auto mode : { 1.0f, 2.0f, 0.0f })
        {
            session.setPlain ("voice_mode", mode);
            session.setPlain ("glide_time", 120.0f);
            session.setPlain ("glide_mode", mode == 2.0f ? 1.0f : 0.0f);

            juce::MidiBuffer line;
            for (int i = 0; i < 8; ++i)
                line.addEvent (juce::MidiMessage::noteOn (1, 48 + i * 3, 0.8f), i * 30);
            session.play (256, line);
            session.hold (4);

            juce::MidiBuffer lift;
            for (int i = 7; i >= 0; --i)
                lift.addEvent (juce::MidiMessage::noteOff (1, 48 + i * 3), (7 - i) * 30);
            session.play (256, lift);

            session.play (256, single (juce::MidiMessage::controllerEvent (1, 64, 127)));
            session.play (256, chord ({ 60, 64 }, 0.9f));
            session.play (256, chord ({ 60, 64 }, 0.0f));
            session.play (256, single (juce::MidiMessage::controllerEvent (1, 64, 0)));
            session.hold (8);
        }

        expectClean ("mono, legato and glide", &session);
    }

    void testTheHostsWaysOfCalling()
    {
        beginTest ("The host's ways of calling: odd sizes, an over-long block, bypass, the transport");

        Session session (*this, 48000.0, 256);

        session.setPlain ("lfo1_sync", 1.0f);
        session.setPlain ("fx_slot1", 2.0f);
        session.setPlain ("fx_slot2", 3.0f);
        session.play (256, chord ({ 45, 57, 64 }, 0.9f));

        // Blocks of every awkward length a host has been seen to send.
        for (const auto size : { 1, 2, 7, 64, 127, 255, 256, 13, 1 })
            session.play (size);

        expectClean ("blocks of odd sizes", &session);

        // A block longer than the host promised, with MIDI spread across it.
        {
            juce::MidiBuffer spread;

            for (int i = 0; i < 24; ++i)
                spread.addEvent (i % 2 == 0 ? juce::MidiMessage::noteOn (1, 50 + i, 0.6f)
                                            : juce::MidiMessage::controllerEvent (1, 1, i * 5),
                                 i * 40);

            session.play (1000, spread);
        }

        expectClean ("a block longer than prepareToPlay promised", &session);

        // Bypassed, with notes arriving and leaving, then back.
        session.play (256, chord ({ 60, 64 }, 0.0f), true);
        session.play (256, chord ({ 62, 65 }, 0.8f), true);
        session.play (256, chord ({ 62, 65 }, 0.0f), true);
        session.play (256, {}, true);
        session.play (256, chord ({ 62, 65 }, 0.8f));
        session.hold (4);
        expectClean ("bypass and back", &session);

        // The transport stops, jumps back, changes tempo, and loses it.
        auto& clock = session.clock();
        clock.playing = false;
        session.hold (2);
        clock.playing = true;
        clock.beat = 0.0;
        session.hold (2);
        clock.bpm = 87.5;
        session.hold (2);
        clock.hasTempo = false;
        session.hold (2);
        session.instrument().setPlayHead (nullptr);
        session.hold (2);
        session.instrument().setPlayHead (&clock);
        clock.hasTempo = true;
        session.hold (2);
        expectClean ("the transport", &session);

        // Nothing watching: the editor closed.
        session.instrument().getTelemetry().setCapturing (false);
        session.hold (4);
        session.instrument().getTelemetry().setCapturing (true);
        session.hold (4);
        expectClean ("the editor closing and opening", &session);
    }

    void testContentReplacedWhilePlaying()
    {
        beginTest ("Content replaced while it plays: a table, a sample, a drawn LFO, a whole state");

        Session session (*this, 48000.0, 256);
        auto& processor = session.instrument();
        auto& library = processor.getVoiceEngine().getWavetableLibrary();

        session.setPlain ("osc1_wavetable", 4.0f);
        session.setPlain ("noise_level", 1.0f);
        session.setPlain ("noise_type", 6.0f);
        session.setPlain ("lfo1_shape", 7.0f);
        session.play (256, chord ({ 52, 59, 64 }, 0.9f));
        session.hold (2);

        for (int round = 0; round < 4; ++round)
        {
            // The loader thread's side: a finished table, published.
            (void) library.publish (dsp::WavetableLibrary::userSlot (1), userTable (round + 2));
            (void) processor.getVoiceEngine().getNoiseSampleSlot().publish (userSample (300.0 + 200.0 * round));
            session.hold (3);

            // The message thread's: what the audio thread has let go of.
            (void) library.collectRetired();
            (void) processor.getVoiceEngine().getNoiseSampleSlot().collectRetired();

            dsp::LfoCurve curve;
            curve.points[0] = { 0.0f, 0.0f, 0.0f };
            curve.points[1] = { 0.3f + 0.1f * static_cast<float> (round), 1.0f, 0.5f };
            curve.points[2] = { 1.0f, 0.0f, -0.4f };
            curve.count = 3;
            (void) processor.getLfoCurves().set (0, curve);
            session.hold (3);
        }

        expectClean ("tables, samples and curves replaced while playing", &session);

        // A whole document replaced, as a project reload does, notes and all.
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        session.setPlain ("osc1_wavetable", 0.0f);
        session.hold (2);
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        session.hold (4);

        // And undone, which replaces part of it again.
        processor.getEditHistory().undo();
        session.hold (4);

        expectClean ("a state replaced while playing", &session);
    }

    void testOtherRatesAndSizes()
    {
        beginTest ("Other sample rates and block sizes, prepared again while the instance lives");

        Session session (*this, 44100.0, 64);
        juce::Random random (44);

        const std::pair<double, int> configurations[] = { { 44100.0, 64 }, { 96000.0, 1024 },
                                                          { 192000.0, 32 }, { 22050.0, 512 } };

        for (const auto& [rate, size] : configurations)
        {
            session.instrument().releaseResources();
            session.prepare (rate, size);
            session.scramble (random);
            session.play (size, chord ({ 41, 53, 60, 65 }, 0.8f));
            session.hold (12);
            session.play (size, chord ({ 41, 53, 60, 65 }, 0.0f));
            session.hold (4);

            expectClean (juce::String (rate, 0) + " Hz in blocks of " + juce::String (size), &session);
        }
    }

    void testTheInterfacesAutomationListener()
    {
        beginTest ("Host automation reaching the open interface allocates nothing of Apollo's");

        // A VST3 host delivers automation on the audio thread, and JUCE's
        // wrapper passes each value to the parameter's listeners there, under a
        // lock of its own. The interface is one of those listeners. The lock is
        // JUCE's; what the interface does with the value is Apollo's, and that
        // is what is checked here.
        auto processor = std::make_unique<ApolloAudioProcessor>();
        processor->prepareToPlay (48000.0, 256);

        ui::ParameterBridge bridge (processor->getValueTreeState());
        auto& parameters = processor->getParameters();

        const auto deliver = [&parameters] (float value)
        {
            for (auto* parameter : parameters)
            {
                parameter->setValue (value);
                parameter->sendValueChangedMessageToListeners (value);
            }
        };

        // Once outside, so that anything JUCE sizes the first time it is
        // used is sized before the question is asked.
        deliver (0.25f);
        (void) realtime::takeFindings();

        {
            const AudioThreadSection section (realtime::Checks::allocationOnly);
            deliver (0.75f);
            deliver (0.5f);
        }

        expectClean ("automation delivered to the interface's listener");
    }
};

RealTimeSafetyTests realTimeSafetyTests;

} // namespace
