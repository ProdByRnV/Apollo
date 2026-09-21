/*
    Processing through a VST3 host: MIDI, block sizes, sample rates, offline
    rendering, the tail, the transport and latency.

    Each of these is already tested against the processor directly. What is
    tested here is the part the wrapper adds: MIDI converted into an IEventList
    and back, controllers routed through IMidiMapping into hidden parameters,
    buffers of whatever size the host chooses, a processing mode that says
    "offline", a ProcessContext that may or may not carry a tempo, and latency
    reported by restartComponent rather than by a return value.
*/

#include "Host/HostedApollo.h"

#include <cmath>
#include <iterator>
#include <optional>
#include <set>

namespace apollo::host
{

namespace
{

/** A transport a test can set: a tempo or none, playing or stopped. */
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    std::optional<double> bpm;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;

        if (bpm.has_value())
            info.setBpm (*bpm);

        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

/** Lets parameter changes reach the processor; see StateTests.cpp. */
void settle (juce::AudioPluginInstance& instance, int blocks = 8)
{
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer none;

    for (int i = 0; i < blocks; ++i)
    {
        block.clear();
        instance.processBlock (block, none);
    }
}

/** A pure sine source: the sub oscillator alone, an octave below the note. */
void sineOnly (juce::AudioPluginInstance& instance)
{
    setPlain (instance, "osc1_level", 0.0f);
    setPlain (instance, "sub_level", 1.0f);
}

} // namespace

class HostProcessingTests final : public juce::UnitTest
{
public:
    HostProcessingTests() : juce::UnitTest ("Host processing", "Host") {}

    void runTest() override
    {
        testNoteTimingIsSampleAccurate();
        testNoteOffReachesSilence();
        testSustainPedalThroughMidiMapping();
        testPitchBendThroughMidiMapping();
        testAllNotesOff();
        testVariableBlockSizes();
        testSampleRateChanges();
        testOfflineRendering();
        testTailCoversTheRelease();
        testTempoFromTheHost();
        testNoTempoFallsBack();
        testLatencyIsReportedAndTrue();
    }

private:
    //==========================================================================
    void testNoteTimingIsSampleAccurate()
    {
        beginTest ("A note starts at the sample the host placed it on, in every block position");

        // The host places a note inside a buffer by its sampleOffset. A plugin
        // that started notes at block boundaries would be up to a block late —
        // 10 ms at a common buffer size, and plainly measurable offline.
        //
        // An instrument at rest produces exactly zero, so the first non-zero
        // sample marks where the note began. It is one sample after the event:
        // the attack starts from zero, and the first sample of any attack is
        // that zero. What the host can break is not that constant but its
        // constancy — the same delay wherever in a block the note falls.
        juce::String error;
        std::set<int> delays;
        const int offsets[] = { 0, 1, 137, 255, 256, 400, 511 };

        for (const auto offset : offsets)
        {
            auto instance = loadPrepared (48000.0, 512, error);

            if (instance == nullptr)
                break;

            // Two blocks of nothing, then the note partway into the third.
            const auto onset = 1024 + offset;
            const auto output = render (*instance, 4096, 512, note (60, onset, 4000));
            const auto delay = firstSampleAbove (output, 0.0f) - onset;

            delays.insert (delay);
            logMessage ("    offset " + juce::String (offset) + ": first sample "
                        + juce::String (delay) + " after the event");
        }

        expectEquals (static_cast<int> (delays.size()), 1, "The same delay at every position in the block");
        expect (! delays.empty() && *delays.begin() == 1, "One sample: the attack's own zero");
    }

    void testNoteOffReachesSilence()
    {
        beginTest ("A note-off from the host releases the note to exact silence");

        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const auto output = render (*instance, 48000, 512, note (60, 0, 12000));

        expect (peak (output, 6000, 6000) > 0.01f, "Sounding while held");

        // The default release is 50 ms: after half a second it is gone.
        expectEquals (peak (output, 36000, 12000), 0.0f, "Exact silence after the release");
    }

    void testSustainPedalThroughMidiMapping()
    {
        beginTest ("The sustain pedal holds a note, arriving as a VST3 parameter via IMidiMapping");

        // VST3 has no MIDI controller events. A host asks the plugin which
        // parameter each controller maps to, and sends CC 64 as a change to
        // that parameter. If the mapping were missing, the pedal would do
        // nothing in every VST3 host and work perfectly in the standalone.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        std::vector<TimedMidi> midi {
            { 0,     juce::MidiMessage::controllerEvent (1, 64, 127) },
            { 100,   juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
            { 4800,  juce::MidiMessage::noteOff (1, 60) },
            { 48000, juce::MidiMessage::controllerEvent (1, 64, 0) },
        };

        const auto output = render (*instance, 96000, 512, midi);

        expect (peak (output, 36000, 9600) > 0.01f,
                "Still sounding 0.65 s after its note-off, because the pedal is down");
        expectEquals (peak (output, 72000, 24000), 0.0f, "Released when the pedal lifts");
    }

    void testPitchBendThroughMidiMapping()
    {
        beginTest ("Pitch bend from the host bends by the default two semitones");

        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        sineOnly (*instance);
        settle (*instance);

        std::vector<TimedMidi> midi {
            { 0,     juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100) },
            { 48000, juce::MidiMessage::pitchWheel (1, 16383) },
            { 95000, juce::MidiMessage::noteOff (1, 69) },
        };

        const auto output = render (*instance, 96000, 512, midi);

        // The sub is an octave down: A4 sounds at 220 Hz.
        const auto centre = estimateFrequency (output, 48000.0, 12000, 24000);
        const auto bent = estimateFrequency (output, 48000.0, 60000, 24000);
        const auto expected = 220.0 * std::pow (2.0, 2.0 / 12.0);

        expectWithinAbsoluteError (centre, 220.0, 0.2, "Unbent: " + juce::String (centre, 3) + " Hz");

        // 16383 is one step short of the top of the 14-bit range; the bend is
        // (16383 - 8192) / 8192 of two semitones, which is 2 - 1/4096.
        expectWithinAbsoluteError (bent, expected, 0.3, "Bent: " + juce::String (bent, 3) + " Hz");
    }

    void testAllNotesOff()
    {
        beginTest ("All Notes Off from the host (CC 123) silences a chord");

        // What a host sends when the transport stops. It arrives through the
        // same controller mapping as the pedal does.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        std::vector<TimedMidi> midi {
            { 0,     juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
            { 0,     juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) },
            { 0,     juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100) },
            { 12000, juce::MidiMessage::allNotesOff (1) },
        };

        const auto output = render (*instance, 48000, 512, midi);

        expect (peak (output, 6000, 6000) > 0.01f, "The chord sounds");
        expectEquals (peak (output, 36000, 12000), 0.0f, "All Notes Off released it");
    }

    //==========================================================================
    void testVariableBlockSizes()
    {
        beginTest ("Blocks of any size, including empty ones, give the same audio as fixed blocks");

        // Hosts split buffers at automation points, loop boundaries and tempo
        // changes, and some call process() with no samples at all to deliver
        // parameter changes. None of that may change the sound.
        juce::String error;
        auto fixed = loadPrepared (48000.0, 512, error);
        auto varied = loadPrepared (48000.0, 512, error);
        expect (fixed != nullptr && varied != nullptr, error);

        if (fixed == nullptr || varied == nullptr)
            return;

        std::vector<TimedMidi> midi;

        for (const auto& event : note (48, 300, 20000))  midi.push_back (event);
        for (const auto& event : note (55, 7777, 30000)) midi.push_back (event);
        for (const auto& event : note (64, 15001, 38000)) midi.push_back (event);

        const auto a = render (*fixed, 48000, 512, midi);
        const auto b = renderVariable (*varied, 48000, { 1, 7, 0, 64, 511, 3, 128, 512, 0, 33, 500, 2 }, midi);

        expect (peak (a) > 0.01f, "The reference plays");
        expect (allFinite (b));
        expectEquals (maxDifference (a, b), 0.0f, "Sample for sample identical");
    }

    void testSampleRateChanges()
    {
        beginTest ("The same instance, re-prepared at six sample rates, plays in tune at each");

        // A host re-prepares a plugin when the user changes the device or the
        // project rate, without destroying it. Everything derived from the rate
        // has to be derived again, and nothing from the old rate may survive.
        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        instance->setPlayConfigDetails (0, 2, 48000.0, 512);
        instance->prepareToPlay (48000.0, 512);
        sineOnly (*instance);
        settle (*instance);

        const double rates[] = { 44100.0, 96000.0, 22050.0, 192000.0, 88200.0, 48000.0 };
        int inTune = 0;

        for (const auto rate : rates)
        {
            instance->releaseResources();
            instance->setPlayConfigDetails (0, 2, rate, 512);
            instance->prepareToPlay (rate, 512);

            const auto length = static_cast<int> (rate);
            const auto output = render (*instance, length, 512, note (69, 0, length - 1000));
            const auto frequency = estimateFrequency (output, rate, length / 4, length / 2);

            if (allFinite (output) && std::abs (frequency - 220.0) < 0.2)
                ++inTune;
            else
                logMessage ("    at " + juce::String (rate, 0) + " Hz: " + juce::String (frequency, 3) + " Hz");
        }

        instance->releaseResources();
        expectEquals (inTune, static_cast<int> (std::size (rates)), "In tune at every rate");
    }

    void testOfflineRendering()
    {
        beginTest ("An offline render, at a render-sized block, is the realtime render exactly");

        // Bouncing a track: the host switches the plugin to offline processing
        // and usually to a much larger buffer. What the user hears while
        // playing must be what they get in the file.
        juce::String error;
        auto realtime = loadPrepared (48000.0, 256, error);
        auto offline = load (error);
        expect (realtime != nullptr && offline != nullptr, error);

        if (realtime == nullptr || offline == nullptr)
            return;

        offline->setNonRealtime (true);
        offline->setPlayConfigDetails (0, 2, 48000.0, 8192);
        offline->prepareToPlay (48000.0, 8192);

        expect (offline->isNonRealtime(), "The host is in offline mode");

        std::vector<TimedMidi> midi;

        for (const auto& event : note (45, 0, 30000))     midi.push_back (event);
        for (const auto& event : note (57, 9000, 40000))  midi.push_back (event);

        const auto a = render (*realtime, 60000, 256, midi);
        const auto b = render (*offline, 60000, 8192, midi);

        expect (peak (a) > 0.01f, "The realtime render plays");
        expectEquals (maxDifference (a, b), 0.0f, "Identical, sample for sample");
    }

    void testTailCoversTheRelease()
    {
        beginTest ("The tail the host is told covers the release it renders");

        // A host stops rendering a track's plugin when the tail it reported
        // has elapsed after the last note. A tail shorter than the release cuts
        // the end off every bounce.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        setPlain (*instance, "env1_release", 2000.0f);
        settle (*instance);

        const auto tail = instance->getTailLengthSeconds();
        expectWithinAbsoluteError (tail, 2.0, 0.02, "The host was told 2 s: " + juce::String (tail, 3));

        const auto noteOff = 24000;
        const auto end = noteOff + static_cast<int> (std::ceil (tail * 48000.0));
        const auto output = render (*instance, end + 9600, 512, note (60, 0, noteOff));

        expect (peak (output, noteOff, 4800) > 0.01f, "Still audible just after the note-off");
        expect (peak (output, end - 4800, 4800) > 0.0f, "Still releasing near the end of the tail");

        // The release ends when the tail does: the envelope goes idle at the
        // end of its release time, so everything after the reported tail is
        // exact silence. A host that stops there loses nothing.
        expectEquals (peak (output, end, 9600), 0.0f, "Exact silence after the reported tail");
    }

    //==========================================================================
    /** The first echo of a short note through a fully wet, synced quarter-note
        delay, in samples from the note.
    */
    int firstEchoWith (juce::AudioPlayHead* playHead)
    {
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);

        if (instance == nullptr)
            return -1;

        instance->setPlayHead (playHead);

        setPlain (*instance, "fx_slot1", 2.0f);          // delay
        setPlain (*instance, "fx_delay_sync", 1.0f);
        setPlain (*instance, "fx_delay_division", 5.0f); // quarter
        setPlain (*instance, "fx_delay_feedback", 0.0f);
        setPlain (*instance, "fx_delay_mix", 1.0f);
        settle (*instance, 40);

        const auto output = render (*instance, 60000, 512, note (60, 0, 480));
        return firstSampleAbove (output, 1.0e-4f);
    }

    void testTempoFromTheHost()
    {
        beginTest ("A tempo-synced delay follows the host's tempo, playing or stopped");

        TestPlayHead playHead;

        playHead.bpm = 90.0;
        const auto at90 = firstEchoWith (&playHead);

        playHead.bpm = 140.0;
        const auto at140 = firstEchoWith (&playHead);

        playHead.bpm = 90.0;
        playHead.playing = false;
        const auto stopped = firstEchoWith (&playHead);

        // A quarter note is 60/bpm seconds. The note starts at 0 with a 5 ms
        // attack, so the echo's first audible sample is a little after that.
        const auto expect90 = 48000 * 60 / 90;
        const auto expect140 = 48000 * 60 / 140;

        expect (at90 >= expect90 && at90 < expect90 + 240,
                "90 BPM: echo at " + juce::String (at90) + ", quarter note is " + juce::String (expect90));
        expect (at140 >= expect140 && at140 < expect140 + 240,
                "140 BPM: echo at " + juce::String (at140) + ", quarter note is " + juce::String (expect140));

        // A stopped transport still has a tempo, and a user tweaking a delay
        // with the transport stopped expects to hear the project's tempo.
        expectEquals (stopped, at90, "The tempo is honoured with the transport stopped");
    }

    void testNoTempoFallsBack()
    {
        beginTest ("With no transport, or one without a tempo, the delay falls back to 120 BPM");

        // Some hosts provide no ProcessContext tempo; the standalone has no
        // transport at all (CLAUDE.md §38).
        TestPlayHead noTempo;

        const auto without = firstEchoWith (nullptr);
        const auto tempoless = firstEchoWith (&noTempo);
        const auto expected = 48000 * 60 / 120;

        expect (without >= expected && without < expected + 240,
                "No playhead: echo at " + juce::String (without));
        expectEquals (tempoless, without, "A transport with no tempo behaves the same");
    }

    void testLatencyIsReportedAndTrue()
    {
        beginTest ("The latency the host is told is the latency the audio has, and follows the rack");

        // Reported through restartComponent (kLatencyChanged) from the message
        // thread (ADR-0054). What matters to the host is not only that a
        // number arrives but that it is the right one: compensation by the
        // wrong amount misaligns the track exactly as badly as none.
        juce::String error;
        auto empty = loadPrepared (48000.0, 512, error);
        auto withDistortion = loadPrepared (48000.0, 512, error);
        expect (empty != nullptr && withDistortion != nullptr, error);

        if (empty == nullptr || withDistortion == nullptr)
            return;

        // A dry distortion (mix 0, the default) passes the signal through its
        // latency-matched dry path, so the only thing it changes is timing.
        setPlain (*withDistortion, "fx_slot1", 1.0f);
        settle (*withDistortion);
        pumpMessages (200);

        const auto reported = withDistortion->getLatencySamples();
        expect (reported > 0, "The distortion's oversampling latency reached the host: "
                                  + juce::String (reported));

        empty->reset();
        withDistortion->reset();

        const auto a = render (*empty, 9600, 512, note (60, 1000, 9000));
        const auto b = render (*withDistortion, 9600, 512, note (60, 1000, 9000));

        const auto measured = firstSampleAbove (b, 0.0f) - firstSampleAbove (a, 0.0f);
        expectEquals (measured, reported, "The reported latency is the measured delay");

        // Taking the distortion out again must tell the host so, or it goes on
        // compensating for a delay that is no longer there.
        setPlain (*withDistortion, "fx_slot1", 0.0f);
        settle (*withDistortion);
        pumpMessages (200);

        expectEquals (withDistortion->getLatencySamples(), 0, "Removing it reports zero again");
    }
};

static HostProcessingTests hostProcessingTests;

} // namespace apollo::host
