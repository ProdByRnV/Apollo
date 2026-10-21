/*
    LFO tempo sync (Phase 13e-1, PRD §15.2).

    Through a whole processor with a transport a test can set, because sync is
    the processor reading the host and the engine acting on it:

      - the division table is what the labels say it is;
      - unsynced, an LFO runs at its own rate whatever the tempo;
      - synced, it runs at the division at the host's tempo, and at 120 when
        the host gives none;
      - a synced free-running LFO takes its phase from the bar while the
        transport runs: two notes a bar apart have the same LFO, sample for
        sample, and two half a bar apart do not.

    The frequency is measured from the LFO's own output on a sounding voice,
    sampled every 32 samples and counted in rising zero crossings over several
    seconds, which resolves it to well under a per cent.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/LFO/Lfo.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 32;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** A transport that moves: a tempo or none, a beat position that advances
    with every block it is asked about, playing or stopped. */
class Transport final : public juce::AudioPlayHead
{
public:
    double bpm = 120.0;
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
        return info;
    }

    void advance (int samples) noexcept
    {
        if (playing)
            beat += static_cast<double> (samples) / rate * (hasTempo ? bpm : 120.0) / 60.0;
    }
};

struct Spec
{
    bool sync = false;
    int division = 8;
    float rateHz = 1.0f;
    bool retrigger = true;
    int shape = 0;

    double bpm = 120.0;
    bool hasTempo = true;
    double startBeat = 0.0;

    double seconds = 4.0;
};

/** LFO 1's output on the sounding voice, once a block, from a note at the
    first block. */
[[nodiscard]] std::vector<float> trace (const Spec& spec)
{
    ApolloAudioProcessor processor;
    Transport transport;
    transport.bpm = spec.bpm;
    transport.hasTempo = spec.hasTempo;
    transport.beat = spec.startBeat;
    processor.setPlayHead (&transport);

    setPlain (processor, "lfo1_shape", static_cast<float> (spec.shape));
    setPlain (processor, "lfo1_rate", spec.rateHz);
    setPlain (processor, "lfo1_sync", spec.sync ? 1.0f : 0.0f);
    setPlain (processor, "lfo1_division", static_cast<float> (spec.division));
    setPlain (processor, "lfo1_retrigger", spec.retrigger ? 1.0f : 0.0f);
    // Routed somewhere, because an LFO nothing routes is not run at all. LFO 1
    // to oscillator 1's position, lightly.
    setPlain (processor, "mod01_source", 5.0f);
    setPlain (processor, "mod01_destination", 4.0f);
    setPlain (processor, "mod01_depth", 0.1f);
    setPlain (processor, "env1_sustain", 1.0f);
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> values;
    const auto blocks = static_cast<int> (spec.seconds * rate) / block;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

        processor.processBlock (buffer, midi);
        transport.advance (block);

        const auto& engine = processor.getVoiceEngine();

        for (int v = 0; v < engine::VoiceEngine::maxPolyphony; ++v)
            if (engine.getVoice (v).isActive())
            {
                values.push_back (engine.getVoice (v).getLfoValue (0));
                break;
            }
    }

    processor.setPlayHead (nullptr);
    return values;
}

/** Cycles per second, from rising zero crossings between the first and last. */
[[nodiscard]] double frequencyOf (const std::vector<float>& values)
{
    std::vector<double> crossings;

    for (std::size_t i = 1; i < values.size(); ++i)
        if (values[i - 1] < 0.0f && values[i] >= 0.0f)
        {
            // Placed between the two samples by linear interpolation.
            const auto fraction = static_cast<double> (-values[i - 1])
                                / static_cast<double> (values[i] - values[i - 1]);
            crossings.push_back ((static_cast<double> (i - 1) + fraction) * block / rate);
        }

    if (crossings.size() < 2)
        return 0.0;

    return static_cast<double> (crossings.size() - 1) / (crossings.back() - crossings.front());
}

class LfoSyncTests final : public juce::UnitTest
{
public:
    LfoSyncTests()
        : juce::UnitTest ("LFO tempo sync", "Engine")
    {
    }

    void runTest() override
    {
        testTheDivisions();
        testUnsyncedIgnoresTheTempo();
        testSyncedFollowsTheTempo();
        testNoTempoIsOneTwenty();
        testFreeRunningLocksToTheBar();
    }

private:
    void testTheDivisions()
    {
        beginTest ("The divisions are the note values their labels name");

        const double expected[] { 32.0, 16.0, 8.0, 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                  0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };

        expectEquals (static_cast<int> (std::size (expected)), dsp::numLfoDivisions);

        for (int i = 0; i < dsp::numLfoDivisions; ++i)
            expectEquals (dsp::lfoDivisionBeats (i), expected[i], "division " + juce::String (i));

        expectEquals (dsp::lfoDivisionBeats (dsp::defaultLfoDivision), 1.0, "the default is not a quarter note");
        expectEquals (dsp::lfoDivisionBeats (-3), 32.0, "a negative index is not clamped");
        expectEquals (dsp::lfoDivisionBeats (99), 0.125, "an index past the end is not clamped");
    }

    void testUnsyncedIgnoresTheTempo()
    {
        beginTest ("Unsynced, an LFO runs at its own rate whatever the tempo");

        for (const auto bpm : { 60.0, 174.0 })
        {
            const auto hz = frequencyOf (trace ({ .rateHz = 1.5f, .bpm = bpm }));
            logMessage ("  1.5 Hz at " + juce::String (bpm) + " bpm: " + juce::String (hz, 4) + " Hz");
            expectWithinAbsoluteError (hz, 1.5, 0.01, "the tempo moved an unsynced LFO");
        }
    }

    void testSyncedFollowsTheTempo()
    {
        beginTest ("Synced, an LFO runs at its division at the host's tempo");

        struct Case { int division; double bpm; double hz; const char* name; };

        const Case cases[] {
            { 8, 120.0, 2.0, "1/4 at 120" },
            { 11, 90.0, 3.0, "1/8 at 90" },
            { 9, 120.0, 3.0, "1/4T at 120" },
            { 7, 120.0, 4.0 / 3.0, "1/4. at 120" },
            { 3, 120.0, 0.5, "1/1 at 120" },
            { 14, 140.0, 140.0 / 60.0 * 4.0, "1/16 at 140" },
        };

        for (const auto& c : cases)
        {
            // At least four whole cycles, so a slow one has crossings to count.
            const auto seconds = std::max (4.0, 4.5 / c.hz);
            const auto hz = frequencyOf (trace ({ .sync = true, .division = c.division, .rateHz = 0.1f,
                                                  .bpm = c.bpm, .seconds = seconds }));
            logMessage ("  " + juce::String (c.name) + ": " + juce::String (hz, 4) + " Hz, expected "
                        + juce::String (c.hz, 4));
            expectWithinAbsoluteError (hz, c.hz, c.hz * 0.005, c.name);
        }
    }

    void testNoTempoIsOneTwenty()
    {
        beginTest ("A host that gives no tempo is taken as 120");

        const auto hz = frequencyOf (trace ({ .sync = true, .division = 8, .bpm = 77.0, .hasTempo = false }));
        expectWithinAbsoluteError (hz, 2.0, 0.01, "without a tempo, 1/4 is not 2 Hz");
    }

    void testFreeRunningLocksToTheBar()
    {
        beginTest ("A synced free-running LFO takes its phase from the bar");

        // A whole note is four beats. Notes a bar apart start at the same point
        // of the cycle; notes half a bar apart start half a cycle apart.
        const auto at = [] (double beat)
        {
            return trace ({ .sync = true, .division = 3, .retrigger = false, .shape = 2,
                            .startBeat = beat, .seconds = 1.0 });
        };

        const auto first = at (1.0);
        const auto barLater = at (5.0);
        const auto halfBarLater = at (3.0);

        expect (first == barLater, "notes a bar apart have different LFOs");
        expect (first != halfBarLater, "notes half a bar apart have the same LFO");

        // And the saw is where the bar says: a quarter of the way through its
        // cycle, a rising -1 to 1 ramp reads -0.5, plus the one block it has run.
        const auto expected = -0.5 + 2.0 * (static_cast<double> (block) / rate * 2.0 / 4.0);
        logMessage ("  saw a beat into a 1/1 cycle, after one block: " + juce::String (first.front(), 4)
                    + ", expected " + juce::String (expected, 4));
        expectWithinAbsoluteError (static_cast<double> (first.front()), expected, 0.01,
                                   "the LFO is not where the bar puts it");
    }
};

LfoSyncTests lfoSyncTests;

} // namespace
