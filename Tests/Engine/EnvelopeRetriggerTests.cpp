/*
    Envelope retrigger options (Phase 13e-2, PRD §15.1).

    Only a voice reused while it sounds can tell the two options apart, and in
    Apollo that is a stolen voice: it fades to silence over two milliseconds,
    then starts the new note. So the tests steal, with one voice of polyphony:

      - restarting gives the new note its whole attack, which a stolen voice's
        note otherwise skips;
      - continuing, the default because it is how Apollo always behaved, picks
        the level up again — coming back from the steal's silence over the
        same two milliseconds, where before 13e-2 it went back in one step;
      - neither steps, measured sample by sample;
      - a modulation envelope does the same, independently of the amplitude's;
      - and a voice that was free sounds the same either way, sample for sample.
*/

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Engine/VoiceEngine.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;

/** A slow attack to full, held: the case a skipped attack is easiest to hear. */
[[nodiscard]] dsp::EnvelopeSettings slowAttack (bool continues)
{
    dsp::EnvelopeSettings settings;
    settings.attackSeconds = 0.2f;
    settings.decaySeconds = 0.1f;
    settings.sustainLevel = 1.0f;
    settings.releaseSeconds = 0.05f;
    settings.curve = 0.0f;
    settings.continueOnRetrigger = continues;
    return settings;
}

struct Run
{
    /** Envelope 1's level after every sample, and envelope 2's. */
    std::vector<float> amplitude;
    std::vector<float> modulation;

    /** The sample at which the second note was played. */
    std::size_t stealAt = 0;
};

/** One voice; a note held long enough to reach full, then a second note that
    steals it. Envelope 2 is routed so that it runs. */
[[nodiscard]] Run steal (bool amplitudeContinues, bool modulationContinues, int polyphony = 1)
{
    engine::VoiceEngine voiceEngine;
    voiceEngine.prepare (rate);
    voiceEngine.setPolyphony (polyphony);
    voiceEngine.setEnvelopeSettings (0, slowAttack (amplitudeContinues));
    voiceEngine.setEnvelopeSettings (1, slowAttack (modulationContinues));

    dsp::ModulationRouting routing;
    routing.slots[0] = { dsp::ModSource::envelope2, dsp::ModDestination::osc1Position, 0.1f };
    voiceEngine.setModulationRouting (routing);

    std::vector<float> left (1), right (1);
    float* channels[2] = { left.data(), right.data() };

    Run run;
    const auto record = [&] (int samples)
    {
        for (int i = 0; i < samples; ++i)
        {
            voiceEngine.render (channels, 2, 0, 1);

            // The voice playing the most recent note.
            const engine::Voice* newest = nullptr;

            for (int v = 0; v < engine::VoiceEngine::maxPolyphony; ++v)
            {
                const auto& voice = voiceEngine.getVoice (v);

                if (voice.isActive() && (newest == nullptr || voice.getStartOrder() > newest->getStartOrder()))
                    newest = &voice;
            }

            run.amplitude.push_back (newest != nullptr ? newest->getEnvelopeLevel() : 0.0f);
            run.modulation.push_back (newest != nullptr ? newest->getEnvelopeValue (1) : 0.0f);
        }
    };

    voiceEngine.noteOn (60, 1.0f);
    record (static_cast<int> (0.5 * rate));

    run.stealAt = run.amplitude.size();
    voiceEngine.noteOn (67, 1.0f);
    record (static_cast<int> (0.3 * rate));

    return run;
}

[[nodiscard]] float largestStep (const std::vector<float>& values, std::size_t from)
{
    auto largest = 0.0f;

    for (auto i = std::max<std::size_t> (from, 1); i < values.size(); ++i)
        largest = std::max (largest, std::abs (values[i] - values[i - 1]));

    return largest;
}

[[nodiscard]] float at (const Run& run, const std::vector<float>& values, double secondsAfterSteal)
{
    return values[run.stealAt + static_cast<std::size_t> (secondsAfterSteal * rate)];
}

class EnvelopeRetriggerTests final : public juce::UnitTest
{
public:
    EnvelopeRetriggerTests()
        : juce::UnitTest ("Envelope retrigger", "Engine")
    {
    }

    void runTest() override
    {
        testTheDefaultIsHowItWas();
        testRestartGivesTheWholeAttack();
        testContinuePicksTheLevelUp();
        testNeitherSteps();
        testModulationEnvelopesChooseForThemselves();
        testAFreeVoiceCannotTellThemApart();
    }

private:
    void testTheDefaultIsHowItWas()
    {
        beginTest ("Continuing is the default, as every envelope behaved before the option");

        expect (dsp::EnvelopeSettings {}.continueOnRetrigger, "the default envelope restarts");
    }

    void testRestartGivesTheWholeAttack()
    {
        beginTest ("Restarting, a stolen voice's note gets its whole attack");

        const auto run = steal (false, false);

        const auto held = run.amplitude[run.stealAt - 1];
        const auto early = at (run, run.amplitude, 0.022);
        const auto halfway = at (run, run.amplitude, 0.102);

        logMessage ("  held at " + juce::String (held, 3) + "; after the steal, 20 ms into a 200 ms attack "
                    + juce::String (early, 3) + ", 100 ms in " + juce::String (halfway, 3));

        expectWithinAbsoluteError (held, 1.0f, 0.01f, "the first note did not reach full");
        expectWithinAbsoluteError (early, 0.1f, 0.02f, "20 ms in is not a tenth of the way up");
        expectWithinAbsoluteError (halfway, 0.5f, 0.02f, "100 ms in is not half way up");
    }

    void testContinuePicksTheLevelUp()
    {
        beginTest ("Continuing, a stolen voice's note picks the level up again");

        const auto run = steal (true, false);
        const auto dip = *std::min_element (run.amplitude.begin() + static_cast<std::ptrdiff_t> (run.stealAt),
                                            run.amplitude.begin() + static_cast<std::ptrdiff_t> (run.stealAt + 480));
        const auto after = at (run, run.amplitude, 0.010);

        logMessage ("  through the steal it dips to " + juce::String (dip, 4) + " and 10 ms later is at "
                    + juce::String (after, 4));

        expect (dip < 0.01f, "the steal no longer fades to silence first");
        expectWithinAbsoluteError (after, 1.0f, 0.01f, "the level was not picked up again");
    }

    void testNeitherSteps()
    {
        beginTest ("Neither option steps through a steal");

        // The ramp moves at most 1 / 96 a sample at 48 kHz.
        const auto limit = 1.0f / static_cast<float> (engine::Voice::stealSeconds * rate) + 1.0e-4f;

        for (const auto continues : { false, true })
        {
            const auto run = steal (continues, false);
            const auto step = largestStep (run.amplitude, run.stealAt);

            logMessage ("  " + juce::String (continues ? "continuing" : "restarting")
                        + ": largest step in the amplitude through the steal " + juce::String (step, 5));
            expect (step <= limit, "the amplitude stepped by " + juce::String (step, 4));
        }
    }

    void testModulationEnvelopesChooseForThemselves()
    {
        beginTest ("A modulation envelope restarts or continues by its own setting");

        // Just after the steal hands over: the amplitude restarts in both, and
        // envelope 2 follows its own option.
        const auto restarts = steal (false, false);
        const auto continues = steal (false, true);

        const auto restarted = at (restarts, restarts.modulation, 0.003);
        const auto continued = at (continues, continues.modulation, 0.003);

        logMessage ("  envelope 2, 1 ms after the new note starts: restarting " + juce::String (restarted, 3)
                    + ", continuing " + juce::String (continued, 3));

        expect (restarted < 0.05f, "a restarting envelope 2 kept its level");
        expect (continued > 0.95f, "a continuing envelope 2 dropped its level");
    }

    void testAFreeVoiceCannotTellThemApart()
    {
        beginTest ("A voice that was free sounds the same with either option");

        // Two voices: the second note takes the free one and steals nothing.
        const auto restarts = steal (false, false, 2);
        const auto continues = steal (true, true, 2);

        expect (restarts.amplitude == continues.amplitude, "the option changed a free voice's amplitude");
        expect (restarts.modulation == continues.modulation, "the option changed a free voice's envelope 2");
    }
};

EnvelopeRetriggerTests envelopeRetriggerTests;

} // namespace
