/*
    Bypass, as a host implements it.

    Apollo has no bypass parameter of its own. The VST3 wrapper adds one, flags
    it kIsBypass so the host's own bypass button drives it, and while it is on
    calls processBlockBypassed instead of processBlock. That substitution is
    invisible to every test that drives the processor directly, and it is where
    the MIDI stream goes: whatever arrives while an instrument is bypassed —
    the note-off for a key that was held, the pedal lifting — has to be
    accounted for, or the instrument comes back with a note that never ends
    (ADR-0075).
*/

#include "Host/HostedApollo.h"

#include <cmath>

namespace apollo::host
{

namespace
{

void setBypass (juce::AudioPluginInstance& instance, bool bypassed)
{
    if (auto* bypass = instance.getBypassParameter())
        bypass->setValueNotifyingHost (bypassed ? 1.0f : 0.0f);
}

/** A hook that bypasses the instance for blocks starting in [from, to). */
BlockHook bypassBetween (juce::AudioPluginInstance& instance, int from, int to)
{
    return [&instance, from, to] (int blockStart)
    {
        setBypass (instance, blockStart >= from && blockStart < to);
    };
}

} // namespace

class HostBypassTests final : public juce::UnitTest
{
public:
    HostBypassTests() : juce::UnitTest ("Host bypass", "Host") {}

    void runTest() override
    {
        testBypassSilences();
        testNoteOffDuringBypass();
        testPedalLiftDuringBypass();
        testNothingReturnsFromBeforeTheBypass();
        testBypassKeepsLatency();
    }

private:
    void testBypassSilences()
    {
        beginTest ("A bypassed instrument is silent, and plays again when the bypass lifts");

        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        expect (instance->getBypassParameter() != nullptr, "The host has a bypass to press");

        std::vector<TimedMidi> midi;

        for (const auto& event : note (60, 0, 20000))      midi.push_back (event);
        for (const auto& event : note (64, 36000, 46000))  midi.push_back (event);

        const auto output = render (*instance, 48000, 512, midi, bypassBetween (*instance, 10240, 30720));

        expect (peak (output, 0, 10240) > 0.01f, "Audible before the bypass");
        expectEquals (peak (output, 10240, 20480), 0.0f, "Silent while bypassed");
        expect (peak (output, 36000, 9000) > 0.01f, "A new note plays after it");
    }

    void testNoteOffDuringBypass()
    {
        beginTest ("A note released while bypassed does not come back when the bypass lifts");

        // Hold a key, press the host's bypass, let go of the key, lift the
        // bypass. The note-off arrived while the instrument was bypassed.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const auto output = render (*instance, 96000, 512, note (60, 0, 20000),
                                    bypassBetween (*instance, 10240, 30720));

        expect (peak (output, 0, 10240) > 0.01f, "Audible before the bypass");
        expectEquals (peak (output, 30720, 65280), 0.0f,
                      "Silent after the bypass: the note-off was not lost");
    }

    void testPedalLiftDuringBypass()
    {
        beginTest ("A sustain pedal lifted while bypassed is lifted when the bypass ends");

        // The pedal is controller state, not a note: if its release is lost,
        // every note played afterwards sustains until the pedal is pressed and
        // released again.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        std::vector<TimedMidi> midi {
            { 0,     juce::MidiMessage::controllerEvent (1, 64, 127) },
            { 10,    juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
            { 4800,  juce::MidiMessage::noteOff (1, 60) },
            { 20000, juce::MidiMessage::controllerEvent (1, 64, 0) },   // while bypassed
            { 40000, juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100) },
            { 44000, juce::MidiMessage::noteOff (1, 67) },
        };

        const auto output = render (*instance, 96000, 512, midi, bypassBetween (*instance, 10240, 30720));

        expect (peak (output, 40000, 4000) > 0.01f, "The later note plays");
        expectEquals (peak (output, 72000, 24000), 0.0f,
                      "And stops when released: the pedal is not still down");
    }

    void testNothingReturnsFromBeforeTheBypass()
    {
        beginTest ("A reverb tail from before the bypass does not resume when it lifts");

        // Bypass switches the instrument off. Switching it back on should give
        // silence until something is played, not the rest of a tail that was
        // frozen mid-decay when the bypass was pressed.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        setPlain (*instance, "fx_slot1", 3.0f);          // reverb
        setPlain (*instance, "fx_reverb_decay", 8000.0f);
        setPlain (*instance, "fx_reverb_mix", 1.0f);

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer none;

        for (int i = 0; i < 20; ++i)
            instance->processBlock (block, none);

        const auto output = render (*instance, 72000, 512, note (60, 0, 4800),
                                    bypassBetween (*instance, 14848, 30720));

        expect (peak (output, 9600, 4800) > 0.001f, "The tail is ringing when the bypass is pressed");
        expectEquals (peak (output, 30720, 41280), 0.0f, "Nothing of it returns afterwards");
    }

    void testBypassKeepsLatency()
    {
        beginTest ("Bypassing does not change the latency the host compensates for");

        // A host's delay compensation is planned around the reported figure.
        // If bypassing changed it, every bypass would shift the other tracks
        // against this one.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        setPlain (*instance, "fx_slot1", 1.0f);          // distortion

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer none;

        for (int i = 0; i < 8; ++i)
            instance->processBlock (block, none);

        pumpMessages (200);
        const auto before = instance->getLatencySamples();

        const auto output = render (*instance, 24000, 512, note (60, 0, 20000),
                                    bypassBetween (*instance, 0, 24000));
        pumpMessages (200);

        expect (before > 0, "The distortion reports latency");
        expectEquals (instance->getLatencySamples(), before, "Unchanged while bypassed");
        expectEquals (peak (output), 0.0f, "Silent while bypassed");
        expect (allFinite (output));
    }
};

static HostBypassTests hostBypassTests;

} // namespace apollo::host
