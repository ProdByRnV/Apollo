/*
    The warp engine (Phase 13b, PRD §12).

    What a warp must do, and how each claim is tested:

      - be nothing at all at an amount of zero, in every mode;
      - reshape the cycle the way its name says, checked on the mapping itself;
      - keep the note's period, because a warp reshapes the waveform and must
        not retune it;
      - not alias past the instrument's budget, measured through a processor at
        high notes rather than assumed from the design (PRD §12: "Warp algorithms
        must be implemented in a way that minimizes aliasing");
      - reach the voice as a modulation destination;
      - be drawn: the wave display shows the cycle as the warp plays it.

    And at its default — no warp — leave the sound exactly as it was. That is
    the regression renders' job, and they passed unchanged.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "Analysis/SignalAnalysis.h"
#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Oscillators/UnisonOscillator.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "Engine/WavetableFrame.h"
#include "Telemetry/InstrumentFrame.h"
#include "Telemetry/TelemetryHub.h"

using namespace apollo;

namespace
{

using Warp = dsp::UnisonOscillator::WarpMode;

constexpr double renderRate = 48000.0;
constexpr int blockSize = 512;
constexpr int transformSize = 16384;

constexpr Warp everyWarp[] { Warp::sync, Warp::asym, Warp::pwm, Warp::bend, Warp::mirror };

[[nodiscard]] juce::String nameOf (Warp mode)
{
    switch (mode)
    {
        case Warp::sync:   return "sync";
        case Warp::asym:   return "asym";
        case Warp::pwm:    return "pwm";
        case Warp::bend:   return "bend";
        case Warp::mirror: return "mirror";
        case Warp::off:
        default:           return "off";
    }
}

/** One oscillator, one voice, on the brightest frame of the saw table. */
[[nodiscard]] std::vector<float> renderOscillator (const dsp::Wavetable& table, double frequency,
                                                   Warp mode, float amount, int samples)
{
    dsp::UnisonOscillator oscillator;
    oscillator.setSampleRate (renderRate);
    oscillator.setTable (&table);
    oscillator.setPosition (1.0f);
    oscillator.setFrequency (frequency);
    oscillator.setWarp (mode, amount);
    oscillator.resetPhase (0.0);

    std::vector<float> rendered;
    rendered.reserve (static_cast<std::size_t> (samples));

    for (int i = 0; i < samples; ++i)
    {
        float left = 0.0f, right = 0.0f;
        oscillator.addNextStereoSample (left, right);
        rendered.push_back (left);
    }

    return rendered;
}

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Oscillator 1 alone on its brightest saw, through no filter, flat. */
void makeSawInstrument (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_level", 1.0f);
    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "osc1_position", 1.0f);
    setPlain (processor, "filter1_type", 0.0f);
    setPlain (processor, "filter2_type", 0.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
}

[[nodiscard]] std::vector<float> renderNote (ApolloAudioProcessor& processor, int note, int blocks)
{
    processor.prepareToPlay (renderRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> rendered;

    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);

        processor.processBlock (buffer, midi);

        const auto* left = buffer.getReadPointer (0);
        rendered.insert (rendered.end(), left, left + blockSize);
    }

    return rendered;
}

class WarpTests final : public juce::UnitTest
{
public:
    WarpTests()
        : juce::UnitTest ("Warp", "Engine")
    {
    }

    void runTest() override
    {
        testZeroAmountIsNoWarp();
        testEachMappingIsWhatItsNameSays();
        testWarpKeepsThePeriod();
        testWarpAliasingIsMeasured();
        testWarpLeavesNoDcOffset();
        testWarpIsAModulationDestination();
        testTheDisplayDrawsTheWarp();
    }

private:
    void testZeroAmountIsNoWarp()
    {
        beginTest ("At an amount of zero, every warp mode is no warp");

        dsp::WavetableLibrary library;
        const auto plain = renderOscillator (library.getTable (0), 330.0, Warp::off, 0.0f, 2048);

        for (const auto mode : everyWarp)
        {
            const auto warped = renderOscillator (library.getTable (0), 330.0, mode, 0.0f, 2048);

            float worst = 0.0f;

            for (std::size_t i = 0; i < plain.size(); ++i)
                worst = std::max (worst, std::abs (warped[i] - plain[i]));

            expect (worst < 1.0e-6f, nameOf (mode) + " at zero differs by " + juce::String (worst));
        }
    }

    void testEachMappingIsWhatItsNameSays()
    {
        beginTest ("Each warp reads the cycle where its name says it does");

        dsp::UnisonOscillator oscillator;

        oscillator.setWarp (Warp::sync, 1.0f);
        expectWithinAbsoluteError (oscillator.getWarpSlope(), 8.0, 1.0e-12, "sync reads eight cycles a period");
        expectWithinAbsoluteError (oscillator.warpPhase (0.1), 0.8, 1.0e-12);
        expectWithinAbsoluteError (oscillator.warpPhase (0.2), 0.6, 1.0e-12, "and restarts each time round");

        oscillator.setWarp (Warp::asym, 1.0f);
        expectWithinAbsoluteError (oscillator.warpPhase (0.05), 0.5, 1.0e-12,
                                   "asym moves the midpoint to five per cent of the period");
        expectWithinAbsoluteError (oscillator.warpPhase (0.025), 0.25, 1.0e-12);
        expectWithinAbsoluteError (oscillator.getWarpSlope(), 10.0, 1.0e-9);

        oscillator.setWarp (Warp::pwm, 1.0f);
        expectWithinAbsoluteError (oscillator.warpPhase (0.05), 0.5, 1.0e-12,
                                   "pwm plays the whole cycle in a tenth of the period");
        expectWithinAbsoluteError (oscillator.warpPhase (0.5), 0.0, 1.0e-12,
                                   "and holds its start for the rest");
        expectWithinAbsoluteError (oscillator.getWarpSlope(), 10.0, 1.0e-9);

        oscillator.setWarp (Warp::bend, 1.0f);
        expectWithinAbsoluteError (oscillator.warpPhase (0.0), 0.0, 1.0e-12, "bend keeps the start");
        expectWithinAbsoluteError (oscillator.warpPhase (0.5), 0.5, 1.0e-12, "and the middle");
        expectWithinAbsoluteError (oscillator.getWarpSlope(), 3.0, 1.0e-12);

        auto previous = -1.0;
        auto rising = true;

        for (int i = 0; i < 1000; ++i)
        {
            const auto value = oscillator.warpPhase (static_cast<double> (i) / 1000.0);
            rising = rising && value >= previous;
            previous = value;
        }

        expect (rising, "bend must never read backwards");

        oscillator.setWarp (Warp::mirror, 1.0f);
        expectWithinAbsoluteError (oscillator.warpPhase (0.3), 0.3, 1.0e-12,
                                   "mirror is a crossfade of outputs, not a phase mapping");
    }

    /** A warp reshapes a cycle; it must not change how long one lasts. 375 Hz
        is exactly 128 samples at 48 kHz, so the same sample a period later
        must be the same value. */
    void testWarpKeepsThePeriod()
    {
        beginTest ("Every warp keeps the note's period");

        dsp::WavetableLibrary library;

        for (const auto mode : everyWarp)
        {
            const auto rendered = renderOscillator (library.getTable (0), 375.0, mode, 0.7f, 4096);

            float worst = 0.0f;

            for (std::size_t i = 1024; i + 128 < rendered.size(); ++i)
                worst = std::max (worst, std::abs (rendered[i + 128] - rendered[i]));

            expect (worst < 1.0e-3f, nameOf (mode) + " is not periodic at the note: "
                                         + juce::String (worst) + " apart a period later");
        }
    }

    /** The PRD's requirement, measured, and pinned.

        Through the whole processor, on the brightest saw, at four notes from
        middle C to C7, warp amount 0.6. The unwarped oscillator is measured
        alongside as the floor of the measurement itself.

        The bounds are not the instrument's -60 dBc oscillator budget, and that
        is a recorded decision rather than a lowered bar (ADR-0090). A warp is a
        nonlinear map of the read position, and like FM it spreads a
        band-limited spectrum without limit. The three treatments here — the mip
        level chosen for the warp's fastest read with a margin, polyBLEP and
        polyBLAMP at its edges, and DC removal — took the worst mode at 1 kHz
        from -25 to -40 dBc. Oversampling would take the rest, and would cost a
        warped stack three to five times an unwarped one, which the worst patch
        the controls allow cannot afford.

        So each bound is the figure measured when 13b was built, plus 3 dB: a
        guard that makes any change which makes a warp alias more fail the
        build, and the record of what warp's aliasing is.
    */
    void testWarpAliasingIsMeasured()
    {
        beginTest ("Warp aliasing at four notes, against the figures measured when it was built");

        struct Bound { Warp mode; float note60; float note72; float note84; float note96; };

        // Measured in 13b (dBc), plus 3 dB.
        const Bound bounds[] {
            { Warp::off,    -81.0f, -90.0f, -94.0f, -106.0f },
            { Warp::sync,   -51.0f, -48.0f, -37.0f,  -32.0f },
            { Warp::asym,   -62.0f, -55.0f, -49.0f,  -44.0f },
            { Warp::pwm,    -51.0f, -45.0f, -37.0f,  -32.0f },
            { Warp::bend,   -82.0f, -73.0f, -61.0f,  -49.0f },
            { Warp::mirror, -55.0f, -49.0f, -42.0f,  -37.0f },
        };

        for (const auto& bound : bounds)
        {
            const std::pair<int, float> notes[] {
                { 60, bound.note60 }, { 72, bound.note72 }, { 84, bound.note84 }, { 96, bound.note96 }
            };

            for (const auto& [note, limit] : notes)
            {
                const auto fundamental = 440.0 * std::pow (2.0, (note - 69) / 12.0);

                ApolloAudioProcessor processor;
                makeSawInstrument (processor);
                setPlain (processor, "osc1_warp_mode", static_cast<float> (bound.mode));
                setPlain (processor, "osc1_warp_amount", 0.6f);

                const auto rendered = renderNote (processor, note, transformSize / blockSize + 8);
                const std::vector<float> settled (rendered.begin() + 8 * blockSize, rendered.end());

                const auto spectrum = analysis::analyse (settled, transformSize, renderRate);

                // A harmonic is a window around each multiple, at least 20 Hz
                // wide: narrower than that, a Blackman-Harris window's own spread
                // around a low harmonic is counted as an alias. That is what the
                // first version measured at 262 Hz — -18 dBc in every mode,
                // whatever the warp did.
                const auto tolerance = std::max (0.02, 20.0 / fundamental);
                const auto worst = analysis::worstInharmonicLevel (spectrum, fundamental, tolerance);

                logMessage ("    " + nameOf (bound.mode) + " at note " + juce::String (note) + " ("
                            + juce::String (fundamental, 0) + " Hz): worst inharmonic "
                            + juce::String (worst, 1) + " dBc");

                expect (worst < limit, nameOf (bound.mode) + " at note " + juce::String (note)
                                           + " aliases at " + juce::String (worst, 1)
                                           + " dBc, past its bound of " + juce::String (limit, 1));
            }
        }
    }

    /** A warped waveform is not centred, and Apollo's output must be (§5c). */
    void testWarpLeavesNoDcOffset()
    {
        beginTest ("A warped oscillator carries no DC offset");

        dsp::WavetableLibrary library;

        for (const auto mode : everyWarp)
        {
            const auto rendered = renderOscillator (library.getTable (0), 220.0, mode, 1.0f, 96000);
            const std::vector<float> settled (rendered.begin() + 48000, rendered.end());
            const auto dc = std::abs (analysis::dcOffsetOf (settled));

            // Mirror at full amount on a saw reads only its rising half, which
            // averaged a quarter of full scale before the blocker existed.
            expect (dc < 1.0e-3, nameOf (mode) + " leaves " + juce::String (dc, 5) + " of DC");
        }
    }

    void testWarpIsAModulationDestination()
    {
        beginTest ("Warp amount is a modulation destination, and does nothing with no warp");

        const auto render = [] (float mode, bool routed)
        {
            ApolloAudioProcessor processor;
            makeSawInstrument (processor);
            setPlain (processor, "osc1_warp_mode", mode);
            setPlain (processor, "osc1_warp_amount", 0.0f);

            if (routed)
            {
                // Velocity, the ninth source, to oscillator 1's warp, the
                // seventeenth destination — the numbers a preset stores.
                setPlain (processor, "mod01_source", 9.0f);
                setPlain (processor, "mod01_destination", 17.0f);
                setPlain (processor, "mod01_depth", 1.0f);
            }

            return renderNote (processor, 57, 4);
        };

        const auto sync = static_cast<float> (Warp::sync);

        expect (render (sync, true) != render (sync, false),
                "velocity routed to warp must move a sync warp from zero");
        expect (render (0.0f, true) == render (0.0f, false),
                "with the warp off, a routing to its amount must change nothing");
    }

    /** PRD §30.2: the wave display shows warp state. It draws through the
        voices' own mapping, so the claims are that no warp draws exactly what it
        always drew, that a warp draws the warped cycle, and that the engine
        publishes the warp being *played* — the parameter plus the matrix. */
    void testTheDisplayDrawsTheWarp()
    {
        beginTest ("The wave display draws the warp the oscillator is playing");

        dsp::WavetableLibrary library;
        const auto& table = library.getTable (0);
        // The position as the display holds it, a float, widened as it widens it.
        const auto framePosition = static_cast<double> (0.6f) * static_cast<double> (table.getNumFrames() - 1);

        telemetry::WavetableFrame plain;
        plain.position = 0.6f;
        engine::fillWavetableFrame (library, plain);

        auto identical = plain.valid;

        for (std::size_t i = 0; i < plain.points.size(); ++i)
        {
            const auto phase = static_cast<double> (i) / static_cast<double> (plain.points.size());
            identical = identical
                     && plain.points[i] == table.getSampleAtPosition (0, framePosition, phase);
        }

        expect (identical, "with no warp the display must draw the table's own frame, exactly");

        // Sync at full amount reads the cycle eight times a period: 128 points,
        // so the picture repeats every 16.
        telemetry::WavetableFrame synced = plain;
        synced.warpMode = static_cast<int> (Warp::sync);
        synced.warpAmount = 1.0f;
        engine::fillWavetableFrame (library, synced);

        float worstRepeat = 0.0f;
        float difference = 0.0f;

        for (std::size_t i = 0; i < synced.points.size(); ++i)
        {
            worstRepeat = std::max (worstRepeat,
                                    std::abs (synced.points[i] - synced.points[(i + 16) % synced.points.size()]));
            difference = std::max (difference, std::abs (synced.points[i] - plain.points[i]));
        }

        expect (synced.valid && worstRepeat < 1.0e-5f,
                "a full sync must draw eight cycles: " + juce::String (worstRepeat));
        expect (difference > 0.1f, "and must not draw the unwarped frame");

        // A mode number the engine does not have draws the table, not garbage.
        telemetry::WavetableFrame unknown = plain;
        unknown.warpMode = 99;
        unknown.warpAmount = 1.0f;
        engine::fillWavetableFrame (library, unknown);
        expect (unknown.points == plain.points, "an unknown warp mode must draw no warp");

        // What the engine publishes: asym, at an amount of zero, with velocity
        // routed to it. A display that drew the parameter would draw no warp.
        ApolloAudioProcessor processor;
        makeSawInstrument (processor);
        setPlain (processor, "osc1_warp_mode", static_cast<float> (Warp::asym));
        setPlain (processor, "osc1_warp_amount", 0.0f);
        setPlain (processor, "mod01_source", 9.0f);
        setPlain (processor, "mod01_destination", 17.0f);
        setPlain (processor, "mod01_depth", 1.0f);

        processor.prepareToPlay (renderRate, blockSize);
        processor.getTelemetry().setCapturing (true);

        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int block = 0; block < 8; ++block)
        {
            buffer.clear();
            juce::MidiBuffer midi;

            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

            processor.processBlock (buffer, midi);
        }

        const auto& snapshot = processor.getTelemetry().snapshot();

        expect (snapshot.wavetableWarpMode[0].load() == static_cast<int> (Warp::asym),
                "the display must be told the warp mode");
        expect (snapshot.wavetableWarpAmount[0].load() > 0.5f,
                "and the amount being played, routing included: "
                    + juce::String (snapshot.wavetableWarpAmount[0].load()));
    }
};

WarpTests warpTests;

} // namespace
