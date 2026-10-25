/*
    Playing from the window (Phase 15, ADR-0111).

    The computer keyboard and the piano strip reach the engine through a queue
    the message thread writes and the audio thread reads, and through three
    bridge commands that only the standalone accepts. Tested here: the queue on
    its own and under two threads, the commands and what they refuse, the rule
    that the page cannot leave a note held, and notes actually sounding and
    stopping through the processor.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "MIDI/PageNoteQueue.h"
#include "UI/BridgeProtocol.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 256;

juce::String play (int note, int velocity)
{
    return R"({"type":"notePlay","version":1,"note":)" + juce::String (note)
         + R"(,"velocity":)" + juce::String (velocity) + "}";
}

juce::String release (int note)
{
    return R"({"type":"noteRelease","version":1,"note":)" + juce::String (note) + "}";
}

const juce::String releaseAll = R"({"type":"noteReleaseAll","version":1})";

/** Everything waiting in @p queue, as (note, velocity) pairs. */
std::vector<std::pair<int, int>> drained (midi::PageNoteQueue& queue)
{
    std::vector<std::pair<int, int>> events;
    queue.drain ([&events] (int note, int velocity) { events.emplace_back (note, velocity); });
    return events;
}

juce::String errorCodeOf (const juce::String& reply)
{
    juce::var parsed;
    (void) juce::JSON::parse (reply, parsed);
    return parsed.getProperty ("code", {}).toString();
}

class NotePlayingTests final : public juce::UnitTest
{
public:
    NotePlayingTests()
        : juce::UnitTest ("Playing from the window", "UI")
    {
    }

    void runTest() override
    {
        testTheQueueKeepsOrderAndRefusesWhatItCannotHold();
        testTheQueueUnderTwoThreads();
        testNotesAreRefusedOutsideTheStandalone();
        testMalformedNotesAreRefused();
        testThePageCannotLeaveANoteHeld();
        testATypedNoteSoundsAndStops();
        testABypassedInstrumentPlaysNothingTyped();
        testAStormOfPressesLeavesNothingHeld();
    }

private:
    void testTheQueueKeepsOrderAndRefusesWhatItCannotHold()
    {
        beginTest ("The queue keeps order, holds 255 events, and refuses what is not a note");

        midi::PageNoteQueue queue;

        expect (drained (queue).empty());

        expect (queue.push (60, 100));
        expect (queue.push (64, 1));
        expect (queue.push (60, 0));

        const auto events = drained (queue);
        expect (events == std::vector<std::pair<int, int>> { { 60, 100 }, { 64, 1 }, { 60, 0 } });
        expect (drained (queue).empty(), "draining empties it");

        expect (! queue.push (-1, 100));
        expect (! queue.push (128, 100));
        expect (! queue.push (60, -1));
        expect (! queue.push (60, 128));
        expect (drained (queue).empty(), "nothing out of range was queued");

        int accepted = 0;
        while (queue.push (accepted % 128, 64))
            ++accepted;

        expectEquals (accepted, static_cast<int> (midi::PageNoteQueue::capacity) - 1);

        const auto full = drained (queue);
        expectEquals (static_cast<int> (full.size()), accepted);
        expect (queue.push (1, 1), "room again once drained");

        // Round the ring many times: the indices wrap and nothing is lost.
        (void) drained (queue);
        int checked = 0;

        for (int round = 0; round < 1000; ++round)
        {
            expect (queue.push (round % 128, 1 + round % 127));
            const auto one = drained (queue);
            if (one.size() == 1 && one.front() == std::pair<int, int> { round % 128, 1 + round % 127 })
                ++checked;
        }

        expectEquals (checked, 1000);
    }

    void testTheQueueUnderTwoThreads()
    {
        beginTest ("Under a writer and a reader at once, every event arrives once, in order");

        midi::PageNoteQueue queue;
        constexpr int total = 200000;

        std::atomic<bool> writerDone { false };

        std::thread writer ([&]
        {
            for (int i = 0; i < total; ++i)
                while (! queue.push (i % 128, 1 + (i / 128) % 127))
                    std::this_thread::yield();

            writerDone.store (true);
        });

        int received = 0;
        int outOfOrder = 0;

        const auto onEvent = [&] (int note, int velocity)
        {
            if (note != received % 128 || velocity != 1 + (received / 128) % 127)
                ++outOfOrder;

            ++received;
        };

        while (! writerDone.load() || received < total)
        {
            queue.drain (onEvent);
            std::this_thread::yield();
        }

        writer.join();
        queue.drain (onEvent);

        expectEquals (received, total);
        expectEquals (outOfOrder, 0);
    }

    void testNotesAreRefusedOutsideTheStandalone()
    {
        beginTest ("Without the standalone's queue, notes are refused and the page is told it cannot play");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        expectEquals (errorCodeOf (bridge.handleMessage (play (60, 100))), juce::String ("NOTE_PLAYING_UNAVAILABLE"));
        expectEquals (errorCodeOf (bridge.handleMessage (release (60))), juce::String ("NOTE_PLAYING_UNAVAILABLE"));
        expectEquals (errorCodeOf (bridge.handleMessage (releaseAll)), juce::String ("NOTE_PLAYING_UNAVAILABLE"));
        expect (drained (processor.getPageNotes()).empty());

        const auto notePlayingIn = [] (const juce::String& metadata)
        {
            juce::var parsed;
            (void) juce::JSON::parse (metadata, parsed);
            const auto value = parsed.getProperty ("notePlaying", juce::var());
            return value.isBool() ? (static_cast<bool> (value) ? 1 : 0) : -1;
        };

        expectEquals (notePlayingIn (bridge.createParameterMetadata()), 0, "the page is told it cannot");

        bridge.setPageNotes (&processor.getPageNotes());
        expectEquals (notePlayingIn (bridge.createParameterMetadata()), 1, "the page is told it can");

        expect (bridge.handleMessage (play (60, 100)).isEmpty(), "accepted without a reply");
        expect (drained (processor.getPageNotes()) == std::vector<std::pair<int, int>> { { 60, 100 } });
    }

    void testMalformedNotesAreRefused()
    {
        beginTest ("Notes that are not MIDI notes are refused before anything is queued");

        const auto refused = [this] (const juce::String& json)
        {
            const auto result = ui::parseMessage (json);
            expect (! result.ok && result.error == ui::BridgeErrorCode::invalidNote, json);
        };

        refused (play (128, 100));
        refused (play (-1, 100));
        refused (play (60, 0));
        refused (play (60, 128));
        refused (R"({"type":"notePlay","version":1,"note":60.5,"velocity":100})");
        refused (R"({"type":"notePlay","version":1,"note":"60","velocity":100})");
        refused (R"({"type":"notePlay","version":1,"note":60})");
        refused (R"({"type":"notePlay","version":1,"velocity":100})");
        refused (release (128));
        refused (R"({"type":"noteRelease","version":1})");

        const auto ok = ui::parseMessage (play (0, 1));
        expect (ok.ok && ok.command.noteNumber == 0 && ok.command.noteVelocity == 1);
        expect (ui::parseMessage (play (127, 127)).ok);
        expect (ui::parseMessage (releaseAll).ok);
    }

    void testThePageCannotLeaveANoteHeld()
    {
        beginTest ("Whatever the page holds is let go: on request, and when the window goes away");

        ApolloAudioProcessor processor;
        auto& queue = processor.getPageNotes();

        {
            ui::ParameterBridge bridge (processor.getValueTreeState());
            bridge.setPageNotes (&queue);

            (void) bridge.handleMessage (play (60, 100));
            (void) bridge.handleMessage (play (64, 100));
            (void) bridge.handleMessage (play (67, 100));
            (void) bridge.handleMessage (release (64));
            (void) drained (queue);

            (void) bridge.handleMessage (releaseAll);
            expect (drained (queue) == std::vector<std::pair<int, int>> { { 60, 0 }, { 67, 0 } },
                    "release-all lets go of exactly what was held");

            (void) bridge.handleMessage (releaseAll);
            expect (drained (queue).empty(), "and holds nothing afterwards");

            // A release always goes through, held or not, so a key can always
            // stop its note whatever was dropped before it.
            (void) bridge.handleMessage (release (72));
            expect (drained (queue) == std::vector<std::pair<int, int>> { { 72, 0 } });

            (void) bridge.handleMessage (play (48, 90));
            (void) bridge.handleMessage (play (50, 90));
            (void) drained (queue);

            bridge.setPageNotes (nullptr);
            expect (drained (queue) == std::vector<std::pair<int, int>> { { 48, 0 }, { 50, 0 } },
                    "detaching lets go of what was held");

            bridge.setPageNotes (&queue);
            (void) bridge.handleMessage (play (55, 90));
            (void) drained (queue);
        }

        expect (drained (queue) == std::vector<std::pair<int, int>> { { 55, 0 } },
                "a bridge destroyed with its window lets go of what was held");
    }

    /** Renders @p blocks blocks and returns the summed power of the output. */
    static double render (ApolloAudioProcessor& processor, int blocks, bool bypassed = false)
    {
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        double power = 0.0;

        for (int b = 0; b < blocks; ++b)
        {
            block.clear();

            if (bypassed)
                processor.processBlockBypassed (block, midi);
            else
                processor.processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto sample = static_cast<double> (block.getSample (channel, i));
                    power += sample * sample;
                }
        }

        return power;
    }

    void testATypedNoteSoundsAndStops()
    {
        beginTest ("A note played from the window sounds, and stops when it is let go");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        processor.prepareToPlay (testSampleRate, blockSize);

        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setPageNotes (&processor.getPageNotes());

        const auto before = render (processor, 20);
        expect (before == 0.0, "silent before anything is played");

        (void) bridge.handleMessage (play (60, 100));
        const auto held = render (processor, 40);
        expect (held > 1.0, "a held note sounds: " + juce::String (held));

        (void) bridge.handleMessage (release (60));
        (void) render (processor, 1000);
        const auto after = render (processor, 20);
        expect (after < held * 1.0e-6, "silent again once released: " + juce::String (after));

        processor.releaseResources();
    }

    void testABypassedInstrumentPlaysNothingTyped()
    {
        beginTest ("A note pressed while the host bypasses the instrument plays nothing afterwards");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        processor.prepareToPlay (testSampleRate, blockSize);

        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setPageNotes (&processor.getPageNotes());

        (void) bridge.handleMessage (play (60, 100));
        (void) render (processor, 4, true);

        expect (render (processor, 40) == 0.0, "the note-on was refused, as a MIDI one is");
        expect (drained (processor.getPageNotes()).empty(), "and the queue was drained while bypassed");

        processor.releaseResources();
    }

    void testAStormOfPressesLeavesNothingHeld()
    {
        beginTest ("A storm of overlapping presses, released as the page releases them, leaves no voice held");

        // What the page does, in the order it does it: each note counted by
        // its holders, played when the first takes it and released when the
        // last lets go. Between messages, sometimes a block renders and
        // sometimes several messages land in the same one, as they do when
        // somebody plays fast.
        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, blockSize);
        processor.prepareToPlay (testSampleRate, blockSize);

        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setPageNotes (&processor.getPageNotes());

        juce::Random random (20261025);
        std::vector<int> holders (128, 0);
        std::vector<int> pressedKeys;
        int plays = 0;

        for (int step = 0; step < 4000; ++step)
        {
            const auto releasing = ! pressedKeys.empty() && (random.nextInt (3) == 0 || pressedKeys.size() > 10);

            if (releasing)
            {
                const auto which = static_cast<std::size_t> (random.nextInt (static_cast<int> (pressedKeys.size())));
                const auto note = pressedKeys[which];
                pressedKeys.erase (pressedKeys.begin() + static_cast<std::ptrdiff_t> (which));

                if (--holders[static_cast<std::size_t> (note)] == 0)
                    (void) bridge.handleMessage (release (note));
            }
            else
            {
                // A small range, so the same note is often held twice.
                const auto note = 48 + random.nextInt (30);
                pressedKeys.push_back (note);

                if (holders[static_cast<std::size_t> (note)]++ == 0)
                {
                    (void) bridge.handleMessage (play (note, 1 + random.nextInt (127)));
                    ++plays;
                }
            }

            if (random.nextInt (4) == 0)
                (void) render (processor, 1);
        }

        for (const auto note : pressedKeys)
            if (--holders[static_cast<std::size_t> (note)] == 0)
                (void) bridge.handleMessage (release (note));

        (void) render (processor, 2000);

        logMessage ("    " + juce::String (plays) + " notes played");
        expectEquals (processor.getVoiceEngine().getActiveVoiceCount(), 0, "no voice is left held");

        processor.releaseResources();
    }
};

static NotePlayingTests notePlayingTests;

} // namespace
