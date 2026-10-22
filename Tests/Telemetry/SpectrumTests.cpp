/*
    The spectrum behind the equaliser's curve (CLAUDE.md §22).

    The feature's promise is that the controls visibly change the picture, so the
    test that matters most is the one that turns a band and reads the picture
    back: a bell cut by 12 dB at 1 kHz takes 12 dB out of the output spectrum at
    1 kHz, leaves the input where it was, and leaves the rest of the axis alone.
    Around it:

      - the prepared transform is the existing transform, to rounding;
      - a sine reads at its own level and at its own frequency, and nowhere else;
      - silence falls to the floor rather than hanging, and is said to be silent;
      - an equaliser that is not in the rack is inactive, not quiet, and its taps
        are written with silence so that putting it back shows nothing old;
      - nothing is captured while nothing is watching, and watching does not
        change the sound.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Utilities/Fft.h"
#include "Engine/SpectrumFrame.h"
#include "Telemetry/TelemetryHub.h"
#include "UI/TelemetryBridge.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;
constexpr double twoPi = 6.283185307179586476925286766559;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** The point whose frequency is nearest @p hz. */
[[nodiscard]] std::size_t pointNear (double hz)
{
    std::size_t best = 0;
    auto bestDistance = 1.0e9;

    for (int p = 0; p < engine::spectrumPoints; ++p)
    {
        const auto distance = std::abs (std::log (engine::SpectrumAnalyser::frequencyOfPoint (p) / hz));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<std::size_t> (p);
        }
    }

    return best;
}

/** Fills both of @p hub's equaliser taps with a sine. */
void writeSine (telemetry::TelemetryHub& hub, double hz, float amplitude)
{
    std::vector<float> signal (static_cast<std::size_t> (telemetry::scopeBufferSize));

    for (std::size_t i = 0; i < signal.size(); ++i)
        signal[i] = amplitude * static_cast<float> (std::sin (twoPi * hz * static_cast<double> (i) / rate));

    hub.spectrum (telemetry::SpectrumTap::equaliserInput).write (signal.data(), static_cast<int> (signal.size()));
    hub.spectrum (telemetry::SpectrumTap::equaliserOutput).write (signal.data(), static_cast<int> (signal.size()));
}

/** A processor holding noise, with the equaliser in slot 1, watched. */
struct WatchedNoise
{
    explicit WatchedNoise (const std::function<void (ApolloAudioProcessor&)>& configure)
    {
        setPlain (processor, "osc1_level", 0.0f);
        setPlain (processor, "noise_level", 1.0f);
        setPlain (processor, "env1_attack", 0.0f);
        setPlain (processor, "env1_sustain", 1.0f);
        setPlain (processor, "fx_slot1", 6.0f);
        configure (processor);

        processor.prepareToPlay (rate, block);
        processor.getTelemetry().setCapturing (true);
    }

    /** Plays a held note for @p blocks, then reads the settled frame. */
    engine::SpectrumFrame settle (int blocks = 40)
    {
        juce::AudioBuffer<float> buffer (2, block);

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;

            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

            processor.processBlock (buffer, midi);
        }

        // Read the same window until the smoothing has caught up with it.
        engine::SpectrumAnalyser analyser;
        engine::SpectrumFrame frame;

        for (int f = 0; f < 60; ++f)
            (void) analyser.build (processor.getTelemetry(), frame);

        return frame;
    }

    ApolloAudioProcessor processor;
};

class SpectrumTests final : public juce::UnitTest
{
public:
    SpectrumTests()
        : juce::UnitTest ("Equaliser spectrum", "Telemetry")
    {
    }

    void runTest() override
    {
        testThePreparedTransform();
        testASineReadsAtItsLevel();
        testSilenceFalls();
        testInactiveWithoutAnEqualiser();
        testABandChangesThePicture();
        testABypassedEqualiserChangesNothing();
        testTakenOutOfTheRack();
        testNothingWatchedNothingCaptured();
        testWatchingDoesNotChangeTheSound();
        testTheMessage();
    }

private:
    void testThePreparedTransform()
    {
        beginTest ("The prepared transform is the transform, to rounding");

        for (const std::size_t size : { 8u, 1024u, 4096u })
        {
            juce::Random random (static_cast<juce::int64> (size));
            std::vector<std::complex<double>> reference (size);

            for (auto& value : reference)
                value = { random.nextDouble() * 2.0 - 1.0, random.nextDouble() * 2.0 - 1.0 };

            auto prepared = reference;

            dsp::forwardTransform (reference);

            dsp::PreparedTransform transform;
            transform.prepare (size);
            transform.perform (prepared.data());

            auto worst = 0.0;

            for (std::size_t i = 0; i < size; ++i)
                worst = std::max (worst, std::abs (prepared[i] - reference[i]));

            expect (worst < 1.0e-9, "size " + juce::String (static_cast<int> (size)) + " differs by "
                                        + juce::String (worst));
        }

        dsp::PreparedTransform unprepared;
        unprepared.prepare (1000);
        expect (unprepared.getSize() == 0, "a length that is not a power of two was prepared");
    }

    void testASineReadsAtItsLevel()
    {
        beginTest ("A sine reads at its own level, at its own frequency, and nowhere else");

        auto hub = std::make_unique<telemetry::TelemetryHub>();
        hub->prepare (rate);
        hub->setEqualiserPlaced (true);

        // On a bin, so the reading is the window's peak rather than its
        // scalloping: bin 85 of 4096 at 48 kHz is 996 Hz. Half of full scale.
        const auto hz = 85.0 * rate / engine::spectrumTransformSize;
        writeSine (*hub, hz, 0.5f);

        engine::SpectrumAnalyser analyser;
        engine::SpectrumFrame frame;

        for (int f = 0; f < 40; ++f)
            expect (analyser.build (*hub, frame));

        const auto at = pointNear (hz);
        const auto pointHz = engine::SpectrumAnalyser::frequencyOfPoint (static_cast<int> (at));
        const auto tilt = static_cast<double> (engine::spectrumTiltDbPerOctave) * std::log2 (pointHz / 1000.0);
        const auto expected = 20.0 * std::log10 (0.5) + tilt;

        const auto far = std::max (frame.output[pointNear (150.0)], frame.output[pointNear (8000.0)]);

        logMessage ("  996 Hz at half scale reads " + juce::String (frame.output[at], 2) + " dB against "
                    + juce::String (expected, 2) + "; two octaves either side, at most " + juce::String (far, 1));

        expect (! frame.silent);
        expectWithinAbsoluteError (static_cast<double> (frame.output[at]), expected, 0.3, "the level");
        expectWithinAbsoluteError (static_cast<double> (frame.input[at]), expected, 0.3, "the input's level");
        expect (far < -70.0f, "the sine leaked across the axis");
    }

    void testSilenceFalls()
    {
        beginTest ("A sound that stops falls to the floor, and is said to be silent");

        auto hub = std::make_unique<telemetry::TelemetryHub>();
        hub->prepare (rate);
        hub->setEqualiserPlaced (true);
        writeSine (*hub, 1000.0, 0.5f);

        engine::SpectrumAnalyser analyser;
        engine::SpectrumFrame frame;

        for (int f = 0; f < 20; ++f)
            (void) analyser.build (*hub, frame);

        const auto at = pointNear (1000.0);
        const auto sounding = frame.output[at];

        std::vector<float> silence (static_cast<std::size_t> (telemetry::scopeBufferSize), 0.0f);
        hub->spectrum (telemetry::SpectrumTap::equaliserInput).write (silence.data(), static_cast<int> (silence.size()));
        hub->spectrum (telemetry::SpectrumTap::equaliserOutput).write (silence.data(), static_cast<int> (silence.size()));

        (void) analyser.build (*hub, frame);
        const auto oneFrameLater = frame.output[at];

        for (int f = 0; f < 90; ++f)
            (void) analyser.build (*hub, frame);

        logMessage ("  sounding " + juce::String (sounding, 1) + " dB, a frame after it stops "
                    + juce::String (oneFrameLater, 1) + ", three seconds after " + juce::String (frame.output[at], 1));

        expect (frame.silent, "silence was not reported");
        expect (oneFrameLater < sounding && oneFrameLater > engine::spectrumFloorDb + 20.0f,
                "the picture vanished rather than falling");
        expectWithinAbsoluteError (frame.output[at], engine::spectrumFloorDb, 0.1f, "it did not reach the floor");
    }

    void testInactiveWithoutAnEqualiser()
    {
        beginTest ("With no equaliser in the rack, the frame is inactive rather than quiet");

        auto hub = std::make_unique<telemetry::TelemetryHub>();
        hub->prepare (rate);
        writeSine (*hub, 1000.0, 0.5f);

        engine::SpectrumAnalyser analyser;
        engine::SpectrumFrame frame;
        expect (! analyser.build (*hub, frame));
        expect (! frame.active);

        // And an engine not yet prepared has no frequency axis to draw on.
        auto unprepared = std::make_unique<telemetry::TelemetryHub>();
        unprepared->setEqualiserPlaced (true);
        expect (! analyser.build (*unprepared, frame), "an unprepared engine drew a spectrum");
    }

    void testABandChangesThePicture()
    {
        beginTest ("Turning a band changes the output where the band is, and only there");

        const auto bell = [] (float gain)
        {
            return [gain] (ApolloAudioProcessor& processor)
            {
                setPlain (processor, "fx_eq_band4_type", 6.0f);
                setPlain (processor, "fx_eq_band4_freq", 1000.0f);
                setPlain (processor, "fx_eq_band4_gain", gain);
                setPlain (processor, "fx_eq_band4_bandwidth", 1.0f);
            };
        };

        WatchedNoise cut (bell (-12.0f));
        const auto frame = cut.settle();

        WatchedNoise flat (bell (0.0f));
        const auto unchanged = flat.settle();

        const auto difference = [] (const engine::SpectrumFrame& f, double hz)
        {
            const auto at = pointNear (hz);
            return static_cast<double> (f.output[at] - f.input[at]);
        };

        logMessage ("  output against input with a 12 dB cut at 1 kHz: at 1 kHz "
                    + juce::String (difference (frame, 1000.0), 2) + " dB, at 150 Hz "
                    + juce::String (difference (frame, 150.0), 2) + ", at 7 kHz "
                    + juce::String (difference (frame, 7000.0), 2) + "; with the band flat, at 1 kHz "
                    + juce::String (difference (unchanged, 1000.0), 2));

        expect (frame.active && ! frame.silent, "there was no picture to read");
        expect (frame.input[pointNear (1000.0)] > -80.0f, "the noise did not reach the equaliser");
        expectWithinAbsoluteError (difference (frame, 1000.0), -12.0, 1.0, "the cut is not in the picture");
        expectWithinAbsoluteError (difference (frame, 150.0), 0.0, 1.0, "the cut reached 150 Hz");
        expectWithinAbsoluteError (difference (frame, 7000.0), 0.0, 1.0, "the cut reached 7 kHz");
        expectWithinAbsoluteError (difference (unchanged, 1000.0), 0.0, 0.05, "a flat band moved the picture");
    }

    void testABypassedEqualiserChangesNothing()
    {
        beginTest ("A bypassed equaliser is drawn, with nothing between its input and output");

        WatchedNoise bypassed ([] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "fx_eq_band4_gain", -12.0f);
            setPlain (processor, "fx_eq_band4_freq", 1000.0f);
            setPlain (processor, "fx_eq_bypass", 1.0f);
        });

        const auto frame = bypassed.settle();
        const auto at = pointNear (1000.0);

        expect (frame.active, "a bypassed equaliser is still in the rack");
        expectWithinAbsoluteError (frame.output[at], frame.input[at], 0.05f, "bypassed, it still cut");
    }

    void testTakenOutOfTheRack()
    {
        beginTest ("An equaliser taken out of the rack says so, and leaves silence behind it");

        WatchedNoise watched ([] (ApolloAudioProcessor&) {});
        (void) watched.settle();

        setPlain (watched.processor, "fx_slot1", 0.0f);

        juce::AudioBuffer<float> buffer (2, block);

        for (int b = 0; b < telemetry::scopeBufferSize / block + 2; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            watched.processor.processBlock (buffer, midi);
        }

        auto& hub = watched.processor.getTelemetry();
        expect (! hub.isEqualiserPlaced(), "the hub still says the equaliser is placed");

        std::vector<float> window (static_cast<std::size_t> (engine::spectrumTransformSize));
        expect (hub.spectrum (telemetry::SpectrumTap::equaliserOutput).readWindow (window.data(), static_cast<int> (window.size())));

        auto peak = 0.0f;

        for (const auto sample : window)
            peak = std::max (peak, std::abs (sample));

        expectEquals (peak, 0.0f, "the tap still holds audio from before");

        // And put back, it is placed again.
        setPlain (watched.processor, "fx_slot1", 6.0f);
        buffer.clear();
        juce::MidiBuffer midi;
        watched.processor.processBlock (buffer, midi);
        expect (hub.isEqualiserPlaced());
    }

    void testNothingWatchedNothingCaptured()
    {
        beginTest ("Nothing is captured while nothing is watching");

        ApolloAudioProcessor processor;
        setPlain (processor, "noise_level", 1.0f);
        setPlain (processor, "fx_slot1", 6.0f);
        processor.prepareToPlay (rate, block);

        juce::AudioBuffer<float> buffer (2, block);

        for (int b = 0; b < 4; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;

            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

            processor.processBlock (buffer, midi);
        }

        const auto& hub = processor.getTelemetry();
        expect (! hub.spectrum (telemetry::SpectrumTap::equaliserInput).isActive());
        expect (! hub.spectrum (telemetry::SpectrumTap::equaliserOutput).isActive());
    }

    void testWatchingDoesNotChangeTheSound()
    {
        beginTest ("Watching the equaliser does not change what it sounds like");

        const auto render = [] (bool watch)
        {
            ApolloAudioProcessor processor;
            setPlain (processor, "fx_slot1", 6.0f);
            setPlain (processor, "fx_eq_band4_gain", 9.0f);
            processor.prepareToPlay (rate, block);
            processor.getTelemetry().setCapturing (watch);

            juce::AudioBuffer<float> buffer (2, block);
            std::vector<float> rendered;

            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;

                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

                processor.processBlock (buffer, midi);
                rendered.insert (rendered.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + block);
            }

            return rendered;
        };

        const auto plain = render (false);
        const auto watched = render (true);

        auto identical = plain.size() == watched.size();

        for (std::size_t i = 0; identical && i < plain.size(); ++i)
            identical = plain[i] == watched[i];

        expect (identical, "watching changed the sound");
    }

    void testTheMessage()
    {
        beginTest ("The frame the interface receives is the spectrum, in tenths of a decibel");

        WatchedNoise watched ([] (ApolloAudioProcessor&) {});
        (void) watched.settle();

        ui::TelemetryBridge bridge (watched.processor.getTelemetry(),
                                    watched.processor.getVoiceEngine().getWavetableLibrary());

        // Thirty times a second, so its size is a budget like the scope
        // frame's (UI_BINDINGS.md §12): 320 levels of at most five characters.
        const auto text = bridge.createSpectrumFrame();
        logMessage ("  one spectrum frame: " + juce::String (text.getNumBytesAsUTF8()) + " bytes");
        expect (text.getNumBytesAsUTF8() < 2400, "the spectrum frame outgrew its budget");

        juce::var message;
        expect (juce::JSON::parse (text, message).wasOk());

        expectEquals (message["type"].toString(), juce::String ("spectrumFrame"));
        expect (static_cast<bool> (message["active"]));

        const auto* input = message["input"].getArray();
        const auto* output = message["output"].getArray();

        expect (input != nullptr && output != nullptr, "the levels are missing");

        if (input == nullptr || output == nullptr)
            return;

        expectEquals (input->size(), engine::spectrumPoints);
        expectEquals (output->size(), engine::spectrumPoints);

        // The first frame of a new display has risen most of the way from the
        // floor, not all of it, so it is not compared with the settled frame;
        // it must be a level the display can draw.
        for (const auto& value : *output)
        {
            const auto tenths = static_cast<int> (value);
            expect (tenths >= static_cast<int> (engine::spectrumFloorDb * 10.0f) && tenths <= 200);
        }

        // Out of the rack, nothing is sent but the fact.
        setPlain (watched.processor, "fx_slot1", 0.0f);
        juce::AudioBuffer<float> buffer (2, block);
        buffer.clear();
        juce::MidiBuffer midi;
        watched.processor.processBlock (buffer, midi);

        expect (juce::JSON::parse (bridge.createSpectrumFrame(), message).wasOk());
        expect (! static_cast<bool> (message["active"]));
        expect (message["input"].isVoid(), "an inactive frame carried levels");
    }
};

SpectrumTests spectrumTests;

} // namespace
