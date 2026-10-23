/*
    User content in the engine (Phase 13g): each oscillator's own table, and a
    loaded noise sample, through a whole processor.

    What a slot does is tested where the slot is (WavetableResourceTests.cpp).
    This is what the instrument does with it: that the USER choice plays the
    oscillator's own table and nobody else's, that an empty one still sounds,
    and that a noise sample plays once or round and round as asked.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Noise/TransientLibrary.h"
#include "DSP/Oscillators/WavetableBuilder.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 256;
constexpr double twoPi = 6.283185307179586476925286766559;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** A table whose every frame is the third harmonic alone: unmistakable against
    any built-in, because its fundamental is three times the note's. */
[[nodiscard]] std::unique_ptr<dsp::Wavetable> thirdHarmonicTable()
{
    std::vector<std::vector<double>> frames (2, std::vector<double> (2048));

    for (auto& frame : frames)
        for (std::size_t i = 0; i < frame.size(); ++i)
            frame[i] = std::sin (3.0 * twoPi * static_cast<double> (i) / 2048.0);

    auto table = std::make_unique<dsp::Wavetable>();
    dsp::buildWavetable (*table, dsp::analyseFrames (frames, dsp::Wavetable::topLevelHarmonics));
    return table;
}

/** A tenth of a second of 1 kHz, as a noise sample. */
[[nodiscard]] std::unique_ptr<dsp::TransientSample> toneSample()
{
    std::vector<double> tone (4800);

    for (std::size_t n = 0; n < tone.size(); ++n)
        tone[n] = std::sin (twoPi * 1000.0 * static_cast<double> (n) / rate);

    return std::make_unique<dsp::TransientSample> (dsp::makeTransient (tone, tone));
}

/** A processor with every source silent and the filter out, ready to render. */
void prepareQuiet (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_level", 0.0f);
    setPlain (processor, "filter1_type", 0.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    processor.prepareToPlay (rate, block);
}

/** Renders @p blocks blocks with note 69 held from the first. Left channel. */
[[nodiscard]] std::vector<float> render (ApolloAudioProcessor& processor, int blocks)
{
    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

/** The level of @p frequency in @p signal from @p from on, by Goertzel. */
[[nodiscard]] double levelAt (const std::vector<float>& signal, double frequency, std::size_t from)
{
    const auto coefficient = 2.0 * std::cos (twoPi * frequency / rate);
    double s1 = 0.0, s2 = 0.0;
    std::size_t count = 0;

    for (auto n = from; n < signal.size(); ++n, ++count)
    {
        const auto s0 = static_cast<double> (signal[n]) + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return count > 0 ? 2.0 * std::sqrt (std::max (0.0, power)) / static_cast<double> (count) : 0.0;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    std::size_t count = 0;

    for (auto n = from; n < to && n < signal.size(); ++n, ++count)
        sum += static_cast<double> (signal[n]) * static_cast<double> (signal[n]);

    return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
}

class UserContentEngineTests final : public juce::UnitTest
{
public:
    UserContentEngineTests()
        : juce::UnitTest ("User content in the engine", "Engine")
    {
    }

    void runTest() override
    {
        testChoiceResolvesPerOscillator();
        testEmptyUserSlotPlaysTheFallback();
        testPublishedTablePlaysOnItsOscillatorAlone();
        testNoiseSampleOnceAndLooped();
        testEmptyNoiseSampleIsWhite();
    }

private:
    void testChoiceResolvesPerOscillator()
    {
        beginTest ("The USER choice is each oscillator's own slot, and the built-ins keep their numbers");

        using Library = dsp::WavetableLibrary;

        for (int choice = 0; choice < Library::numTables; ++choice)
        {
            expectEquals (Library::slotForChoice (1, choice), choice);
            expectEquals (Library::slotForChoice (2, choice), choice);
        }

        expectEquals (Library::slotForChoice (1, Library::userChoice), Library::userSlot (1));
        expectEquals (Library::slotForChoice (2, Library::userChoice), Library::userSlot (2));
        expect (Library::userSlot (1) != Library::userSlot (2));
        expectEquals (Library::slotForChoice (1, -3), 0);
        expectEquals (Library::slotForChoice (2, 99), Library::userSlot (2));

        Library library;
        expect (&library.getTable (Library::userSlot (1)) == &library.getTable (Library::userFallbackTable),
                "an empty user slot must be the fallback table itself");
        expect (! library.isReplaced (Library::userSlot (1)));
    }

    void testEmptyUserSlotPlaysTheFallback()
    {
        beginTest ("An oscillator on USER with nothing loaded plays the first built-in, sample for sample");

        const auto renderChoice = [] (float choice)
        {
            ApolloAudioProcessor processor;
            prepareQuiet (processor);
            setPlain (processor, "osc1_level", 1.0f);
            setPlain (processor, "osc1_wavetable", choice);
            return render (processor, 20);
        };

        const auto user = renderChoice (4.0f);
        expect (user == renderChoice (0.0f));
        expect (rmsOf (user, 0, user.size()) > 0.01, "it must sound");
    }

    void testPublishedTablePlaysOnItsOscillatorAlone()
    {
        beginTest ("A table loaded into oscillator 1 plays there, and oscillator 2 does not hear it");

        ApolloAudioProcessor processor;
        prepareQuiet (processor);
        processor.getTelemetry().setCapturing (true);
        setPlain (processor, "osc1_level", 1.0f);
        setPlain (processor, "osc1_wavetable", 4.0f);

        auto& library = processor.getVoiceEngine().getWavetableLibrary();
        expect (library.publish (dsp::WavetableLibrary::userSlot (1), thirdHarmonicTable()));

        const auto first = render (processor, 40);
        const auto fundamental = levelAt (first, 440.0, 2048);
        const auto third = levelAt (first, 1320.0, 2048);

        logMessage ("  oscillator 1 on USER: 440 Hz " + juce::String (fundamental, 5)
                    + ", 1320 Hz " + juce::String (third, 5));
        expect (third > 0.05 && third > 100.0 * fundamental, "oscillator 1 is not playing its table");

        // Oscillator 2 on USER, in an instrument whose oscillator 1 slot holds
        // that table: still the fallback.
        const auto renderOsc2 = [] (float choice, bool loadOsc1)
        {
            ApolloAudioProcessor other;
            prepareQuiet (other);
            setPlain (other, "osc2_level", 1.0f);
            setPlain (other, "osc2_wavetable", choice);

            if (loadOsc1)
                (void) other.getVoiceEngine().getWavetableLibrary().publish (
                    dsp::WavetableLibrary::userSlot (1), thirdHarmonicTable());

            return render (other, 20);
        };

        expect (renderOsc2 (4.0f, true) == renderOsc2 (0.0f, false),
                "oscillator 2 heard oscillator 1's table");

        beginTest ("The display draws the slot an oscillator plays");

        expectEquals (processor.getTelemetry().snapshot().wavetableIndex[0].load(),
                      dsp::WavetableLibrary::userSlot (1));
    }

    void testNoiseSampleOnceAndLooped()
    {
        beginTest ("A loaded noise sample plays once from the note, or round and round");

        const auto renderSample = [this] (bool loop)
        {
            ApolloAudioProcessor processor;
            prepareQuiet (processor);
            setPlain (processor, "noise_level", 1.0f);
            setPlain (processor, "noise_type", 6.0f);
            setPlain (processor, "noise_loop", loop ? 1.0f : 0.0f);

            expect (processor.getVoiceEngine().getNoiseSampleSlot().publish (toneSample()));
            return render (processor, 190); // about a second
        };

        const auto once = renderSample (false);
        const auto looped = renderSample (true);

        expect (levelAt (once, 1000.0, 0) > 0.0 && rmsOf (once, 0, 4800) > 0.05, "the sample did not play");
        expect (rmsOf (once, 6000, once.size()) == 0.0, "a one-shot went on past its end");

        const auto late = rmsOf (looped, 40000, looped.size());
        logMessage ("  looped, after its first ten passes: RMS " + juce::String (late, 4));
        expect (late > 0.05, "a loop stopped");
        expect (once.size() == looped.size());

        beginTest ("Loop applies to the built-in transients too");

        ApolloAudioProcessor processor;
        prepareQuiet (processor);
        setPlain (processor, "noise_level", 1.0f);
        setPlain (processor, "noise_type", 2.0f); // click
        setPlain (processor, "noise_loop", 1.0f);
        const auto clicks = render (processor, 190);
        expect (rmsOf (clicks, 40000, clicks.size()) > 0.0, "a looped click stopped");
    }

    void testEmptyNoiseSampleIsWhite()
    {
        beginTest ("SAMPLE with nothing loaded plays white noise rather than nothing");

        const auto renderType = [] (float type)
        {
            ApolloAudioProcessor processor;
            prepareQuiet (processor);
            setPlain (processor, "noise_level", 1.0f);
            setPlain (processor, "noise_type", type);
            return render (processor, 20);
        };

        const auto empty = renderType (6.0f);
        expect (empty == renderType (0.0f));
        expect (rmsOf (empty, 0, empty.size()) > 0.01);
    }
};

UserContentEngineTests userContentEngineTests;

} // namespace
