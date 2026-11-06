/*
    The envelope follower (Phase 18e): how loud the instrument is, as a value
    the matrix can route.

    The follower on its own — its scale, its timing by the convention the
    dynamics processors use, its gain, and what it does with silence and with
    nonsense — and then the follower in the instrument: routed to an effect
    control, a played note moves that control, and silence lets it go.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <limits>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Dynamics/EnvelopeFollower.h"
#include "DSP/Modulation/ModulationTypes.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr double twoPi = 6.283185307179586476925286766559;

/** Runs @p follower over @p samples of one channel. */
void feed (dsp::EnvelopeFollower& follower, const std::vector<float>& samples)
{
    const float* channels[] = { samples.data() };
    follower.process (channels, 1, 0, static_cast<int> (samples.size()));
}

std::vector<float> sine (double hz, float amplitude, int length)
{
    std::vector<float> out (static_cast<std::size_t> (length));

    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * static_cast<float> (std::sin (twoPi * hz * static_cast<double> (i) / rate));

    return out;
}

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

class EnvelopeFollowerTests final : public juce::UnitTest
{
public:
    EnvelopeFollowerTests() : juce::UnitTest ("Envelope follower", "DSP") {}

    void runTest() override
    {
        testTheScale();
        testTheTiming();
        testTheGain();
        testSilenceAndNonsense();
        testTheLouderChannel();
        testInTheInstrument();
    }

private:
    void testTheScale()
    {
        beginTest ("A steady sound reads on a decibel scale: -60 dBFS is 0, 0 dBFS is 1");

        for (const auto db : { -6.0f, -20.0f, -40.0f })
        {
            dsp::EnvelopeFollower follower;
            follower.prepare (rate);
            follower.setTimes (10.0f, 200.0f);

            feed (follower, sine (1000.0, std::pow (10.0f, db / 20.0f), 24000));

            // Between the sine's RMS and its peak: a peak follower with an
            // attack of its own rises most of the way to each crest, not all of
            // it, so it reads a little under the peak and well over the RMS.
            const auto peak = (db + 60.0f) / 60.0f;
            const auto rms = (db - 3.0f + 60.0f) / 60.0f;
            logMessage ("  a sine peaking at " + juce::String (db) + " dBFS reads " + juce::String (follower.getValue(), 3)
                        + ", between " + juce::String (rms, 3) + " and " + juce::String (peak, 3));
            expect (follower.getValue() <= peak + 0.001f && follower.getValue() >= rms + 0.02f);
        }

        dsp::EnvelopeFollower loud;
        loud.prepare (rate);
        feed (loud, std::vector<float> (4800, 1.5f));
        expectEquals (loud.getValue(), 1.0f, "above full scale is clamped to 1");

        dsp::EnvelopeFollower quiet;
        quiet.prepare (rate);
        feed (quiet, std::vector<float> (48000, 0.0005f));
        expectEquals (quiet.getValue(), 0.0f, "below -60 dBFS is 0");
    }

    void testTheTiming()
    {
        beginTest ("Attack and release take their time to 63 per cent, the dynamics processors' convention");

        dsp::EnvelopeFollower follower;
        follower.prepare (rate);
        follower.setTimes (20.0f, 300.0f);

        // A step to 0.5, held for exactly the attack.
        feed (follower, std::vector<float> (static_cast<std::size_t> (rate * 0.020), 0.5f));
        const auto afterAttack = follower.getLevel() / 0.5f;

        feed (follower, std::vector<float> (static_cast<std::size_t> (rate), 0.5f));
        expectWithinAbsoluteError (follower.getLevel(), 0.5f, 0.001f, "it did not settle on the level");

        // Then silence for exactly the release.
        feed (follower, std::vector<float> (static_cast<std::size_t> (rate * 0.300), 0.0f));
        const auto afterRelease = 1.0f - follower.getLevel() / 0.5f;

        const auto target = 1.0f - std::exp (-1.0f);
        logMessage ("  after the attack " + juce::String (afterAttack * 100.0f, 1) + " % of the way up, after the release "
                    + juce::String (afterRelease * 100.0f, 1) + " % of the way down; 63.2 % expected");

        expectWithinAbsoluteError (afterAttack, target, 0.01f);
        expectWithinAbsoluteError (afterRelease, target, 0.01f);
    }

    void testTheGain()
    {
        beginTest ("The gain moves the window: +12 dB reads a sound 12 dB louder");

        dsp::EnvelopeFollower plain;
        dsp::EnvelopeFollower boosted;
        plain.prepare (rate);
        boosted.prepare (rate);
        boosted.setGainDb (12.0f);

        const auto signal = sine (500.0, 0.05f, 24000);
        feed (plain, signal);
        feed (boosted, signal);

        expectWithinAbsoluteError (boosted.getValue() - plain.getValue(), 12.0f / 60.0f, 0.001f);

        boosted.setGainDb (std::numeric_limits<float>::quiet_NaN());
        expectWithinAbsoluteError (boosted.getValue(), plain.getValue(), 0.001f, "a NaN gain is no gain");
    }

    void testSilenceAndNonsense()
    {
        beginTest ("Silence reaches exactly zero, and a NaN or an infinity is not a level");

        dsp::EnvelopeFollower follower;
        follower.prepare (rate);
        follower.setTimes (1.0f, 50.0f);

        feed (follower, std::vector<float> (4800, 0.5f));
        feed (follower, std::vector<float> (static_cast<std::size_t> (rate * 2), 0.0f));

        expectEquals (follower.getLevel(), 0.0f, "the level decayed into denormals instead of reaching zero");
        expectEquals (follower.getValue(), 0.0f);

        feed (follower, std::vector<float> (4800, 0.25f));
        const auto before = follower.getLevel();

        feed (follower, { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity() });

        expect (std::isfinite (follower.getLevel()), "nonsense got into the state");
        expect (follower.getLevel() <= before, "nonsense was read as loud");

        // Nothing to follow is not a crash.
        follower.process (nullptr, 2, 0, 64);
        const float* none[] = { nullptr, nullptr };
        follower.process (none, 2, 0, 64);
        expect (std::isfinite (follower.getValue()));
    }

    void testTheLouderChannel()
    {
        beginTest ("Two channels are followed by the louder of them");

        dsp::EnvelopeFollower follower;
        follower.prepare (rate);

        const auto left = std::vector<float> (4800, 0.1f);
        const auto right = std::vector<float> (4800, -0.4f);
        const float* channels[] = { left.data(), right.data() };

        follower.process (channels, 2, 0, 4800);
        expectWithinAbsoluteError (follower.getLevel(), 0.4f, 0.001f);
    }

    void testInTheInstrument()
    {
        beginTest ("Routed to the reverb's mix, a played note moves it, and silence lets it go");

        ApolloAudioProcessor processor;
        setPlain (processor, "env1_attack", 0.0f);
        setPlain (processor, "env1_sustain", 1.0f);
        setPlain (processor, "env1_release", 5.0f);
        setPlain (processor, "follower_release", 50.0f);

        setPlain (processor, "mod01_source", static_cast<float> (dsp::ModSource::follower));
        setPlain (processor, "mod01_destination", static_cast<float> (dsp::ModDestination::fxReverbMix));
        setPlain (processor, "mod01_depth", 1.0f);

        constexpr int block = 512;
        processor.prepareToPlay (rate, block);

        auto& engine = processor.getVoiceEngine();
        juce::AudioBuffer<float> buffer (2, block);

        const auto run = [&] (int blocks, bool startNote, bool stopNote)
        {
            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;

                if (b == 0 && startNote)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

                if (b == 0 && stopNote)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);

                processor.processBlock (buffer, midi);
            }
        };

        run (4, false, false);
        expectEquals (engine.getFollowerValue(), 0.0f, "the follower moved with nothing playing");
        expectEquals (engine.getEffectOffset (dsp::ModDestination::fxReverbMix), 0.0f);

        run (40, true, false);
        const auto playing = engine.getFollowerValue();
        const auto offset = engine.getEffectOffset (dsp::ModDestination::fxReverbMix);

        run (200, false, true);
        const auto after = engine.getFollowerValue();

        logMessage ("  a held C4: follower " + juce::String (playing, 3) + ", the reverb mix moved by "
                    + juce::String (offset, 3) + "; two seconds after it stopped " + juce::String (after, 3));

        expect (playing > 0.3f, "a held note hardly moved the follower");
        expect (offset > 0.0f, "the routing did not reach the reverb's mix");
        expectEquals (after, 0.0f, "the follower did not let go after the note");
        expectEquals (engine.getEffectOffset (dsp::ModDestination::fxReverbMix), 0.0f);
    }
};

static EnvelopeFollowerTests envelopeFollowerTests;

} // namespace
