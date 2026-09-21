/*
    State, presets and automation, through a VST3 host.

    The ordinary suite proves that Apollo's state document round-trips. It
    cannot prove that the document survives the trip a host actually gives it:
    IComponent::getState into one stream, IEditController::getState into
    another, both wrapped in the host's own container, handed back in a new
    process to an instance that may not have been prepared yet, and then read
    back out of the *controller* by the host, which draws its automation lanes
    from whatever the controller now says. Every one of those steps belongs to
    the wrapper rather than to Apollo, and a sound that came back right while
    the host believed something else would still be a broken project.
*/

#include "Host/HostedApollo.h"

#include "Parameters/ParameterDefinitions.h"

#include <cmath>
#include <cstring>

namespace apollo::host
{

namespace
{

/** A patch that moves parameters of every kind a host has to carry: skewed
    and linear floats, integers, the rack, and a filter mode.
*/
void applyDistinctivePatch (juce::AudioPluginInstance& instance)
{
    setPlain (instance, "osc1_position", 0.63f);
    setPlain (instance, "osc1_unison", 5.0f);
    setPlain (instance, "osc1_detune", 0.31f);
    setPlain (instance, "osc2_level", 0.55f);
    setPlain (instance, "sub_level", 0.4f);
    setPlain (instance, "filter1_cutoff", 2400.0f);
    setPlain (instance, "filter1_resonance", 0.45f);
    setPlain (instance, "env1_attack", 12.0f);
    setPlain (instance, "env1_release", 180.0f);
    setPlain (instance, "fx_slot1", 1.0f);   // distortion
    setPlain (instance, "fx_slot2", 6.0f);   // equaliser
    setPlain (instance, "fx_distortion_drive", 9.0f);
    setPlain (instance, "master_gain", -4.5f);
}

/** Lets a set of host-side parameter changes reach the processor.

    A VST3 host delivers parameter changes inside process(); a value set on the
    host side is not in the processor until a block has run. A host saving a
    project a moment after a knob moved has run hundreds of blocks since.
*/
void settle (juce::AudioPluginInstance& instance)
{
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer none;

    for (int i = 0; i < 4; ++i)
    {
        block.clear();
        instance.processBlock (block, none);
    }
}

/** @returns the number of Apollo parameters whose host-side values differ. */
int countDifferences (juce::AudioPluginInstance& a, juce::AudioPluginInstance& b, juce::UnitTest& test)
{
    int differences = 0;

    for (const auto& definition : params::parameterDefinitions)
    {
        auto* left = findRegistryParameter (a, definition.id);
        auto* right = findRegistryParameter (b, definition.id);

        if (left == nullptr || right == nullptr)
        {
            ++differences;
            continue;
        }

        if (std::abs (left->getValue() - right->getValue()) > 1.0e-5f)
        {
            ++differences;
            test.logMessage ("    " + juce::String (definition.id.data(), definition.id.size())
                             + ": " + juce::String (left->getValue()) + " became "
                             + juce::String (right->getValue()));
        }
    }

    return differences;
}

} // namespace

class HostStateTests final : public juce::UnitTest
{
public:
    HostStateTests() : juce::UnitTest ("Host state, presets and automation", "Host") {}

    void runTest() override
    {
        testProjectRecall();
        testRecallBeforePrepare();
        testRecallWhilePlaying();
        testHostPreset();
        testForeignStateIsRefused();
        testRecalledChainReportsLatency();
        testAutomationTakesEffect();
        testAutomatedGainDoesNotStep();
    }

private:
    //==========================================================================
    void testProjectRecall()
    {
        beginTest ("A project saved and reopened restores every parameter, in the plugin and in the host");

        juce::String error;
        auto original = loadPrepared (48000.0, 512, error);
        auto reopened = loadPrepared (48000.0, 512, error);
        expect (original != nullptr && reopened != nullptr, error);

        if (original == nullptr || reopened == nullptr)
            return;

        applyDistinctivePatch (*original);
        settle (*original);

        juce::MemoryBlock project;
        original->getStateInformation (project);
        expect (project.getSize() > 0, "The host was given something to save");

        reopened->setStateInformation (project.getData(), static_cast<int> (project.getSize()));

        // The host reads each value back from the controller — this is what
        // its automation lanes and generic editor now show.
        expectEquals (countDifferences (*original, *reopened, *this), 0,
                      "Every parameter the host sees came back");

        // And it is the same sound, sample for sample. Two instances playing
        // the same notes from the same state must agree exactly: voice start
        // phases are fixed and the noise seed resets per voice (ADR-0017).
        original->reset();
        reopened->reset();

        const auto midi = note (48, 100, 30000);
        const auto a = render (*original, 36000, 512, midi);
        const auto b = render (*reopened, 36000, 512, midi);

        expect (peak (a) > 0.01f, "The recalled patch plays");
        expectEquals (maxDifference (a, b), 0.0f, "The recalled patch sounds identical");
    }

    void testRecallBeforePrepare()
    {
        beginTest ("State restored before the host activates the plugin is still there afterwards");

        // Most hosts open a project by instantiating, restoring state, and only
        // then activating. The restore arrives at an instance that has never
        // been prepared and has no sample rate.
        juce::String error;
        auto original = loadPrepared (48000.0, 512, error);
        auto reopened = load (error);
        expect (original != nullptr && reopened != nullptr, error);

        if (original == nullptr || reopened == nullptr)
            return;

        applyDistinctivePatch (*original);
        settle (*original);

        juce::MemoryBlock project;
        original->getStateInformation (project);
        reopened->setStateInformation (project.getData(), static_cast<int> (project.getSize()));

        reopened->setPlayConfigDetails (0, 2, 44100.0, 256);
        reopened->prepareToPlay (44100.0, 256);

        expectEquals (countDifferences (*original, *reopened, *this), 0,
                      "Nothing was reset by the first prepare");

        const auto output = render (*reopened, 22050, 256, note (60, 0, 20000));
        expect (peak (output) > 0.01f && allFinite (output), "And it plays");
    }

    void testRecallWhilePlaying()
    {
        beginTest ("State restored while a note is sounding replaces the sound without breaking it");

        // A user choosing a host preset during playback, or a host's A/B
        // comparison. The audio thread is mid-note when the state changes.
        juce::String error;
        auto source = loadPrepared (48000.0, 512, error);
        auto target = loadPrepared (48000.0, 512, error);
        expect (source != nullptr && target != nullptr, error);

        if (source == nullptr || target == nullptr)
            return;

        applyDistinctivePatch (*source);
        settle (*source);

        juce::MemoryBlock project;
        source->getStateInformation (project);

        auto swapped = false;
        const auto output = render (*target, 48000, 512, note (60, 0, 40000),
                                    [&] (int blockStart)
                                    {
                                        if (! swapped && blockStart >= 12000)
                                        {
                                            target->setStateInformation (project.getData(),
                                                                         static_cast<int> (project.getSize()));
                                            swapped = true;
                                        }
                                    });

        expect (swapped);
        expect (allFinite (output), "Nothing non-finite reached the output");
        expect (peak (output) < 4.0f, "Nothing ran away: peak " + juce::String (peak (output)));
        expectEquals (countDifferences (*source, *target, *this), 0, "The new state is the one in force");
    }

    void testHostPreset()
    {
        beginTest ("A host's own preset file (.vstpreset) saves and loads the sound");

        // Distinct from a project: the host writes Steinberg's preset container
        // around the component and controller state, and a different instance
        // — possibly in a different host — reads it back. This is the host's
        // preset mechanism, not Apollo's .rnv library, and both must work.
        juce::String error;
        auto original = loadPrepared (48000.0, 512, error);
        auto other = loadPrepared (48000.0, 512, error);
        expect (original != nullptr && other != nullptr, error);

        if (original == nullptr || other == nullptr)
            return;

        applyDistinctivePatch (*original);
        settle (*original);

        auto* client = original->getVST3Client();
        auto* otherClient = other->getVST3Client();
        expect (client != nullptr && otherClient != nullptr, "A VST3 instance exposes its preset interface");

        if (client == nullptr || otherClient == nullptr)
            return;

        const auto preset = client->getPreset();
        expect (preset.getSize() > 48, "A preset file was written");

        // Steinberg's container begins with its own magic, which is how a host
        // recognises a .vstpreset regardless of its file name.
        expect (preset.getSize() >= 4 && std::memcmp (preset.getData(), "VST3", 4) == 0,
                "The file is a VST3 preset container");

        expect (otherClient->setPreset (preset), "The preset loads into another instance");
        expectEquals (countDifferences (*original, *other, *this), 0, "Every parameter came back");
    }

    void testForeignStateIsRefused()
    {
        beginTest ("State that is not Apollo's is refused and the sound is left alone");

        // A corrupted project, or a chunk from another plugin that a host's
        // bug handed to this one. Refusing is the normal path (CLAUDE.md §33).
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        auto reference = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr && reference != nullptr, error);

        if (instance == nullptr || reference == nullptr)
            return;

        applyDistinctivePatch (*instance);
        applyDistinctivePatch (*reference);
        settle (*instance);
        settle (*reference);

        juce::Random random (75);
        juce::MemoryBlock garbage (4096);

        for (std::size_t i = 0; i < garbage.getSize(); ++i)
            garbage[i] = static_cast<char> (random.nextInt (256));

        instance->setStateInformation (garbage.getData(), static_cast<int> (garbage.getSize()));
        instance->setStateInformation (nullptr, 0);

        const juce::String notApollo ("<?xml version=\"1.0\"?><SomeOtherSynth gain=\"1\"/>");
        instance->setStateInformation (notApollo.toRawUTF8(), static_cast<int> (notApollo.getNumBytesAsUTF8()));

        settle (*instance);

        expectEquals (countDifferences (*reference, *instance, *this), 0,
                      "Three refused documents moved nothing");

        const auto output = render (*instance, 24000, 512, note (60, 0, 20000));
        expect (peak (output) > 0.01f && allFinite (output), "And it still plays");
    }

    void testRecalledChainReportsLatency()
    {
        beginTest ("A recalled chain reports its latency to the host before the first block");

        // A host plans delay compensation when a project opens, from the
        // latency the plugin reports once prepared. If that figure were the
        // empty rack's until the first block ran, every track would be
        // misaligned by the distortion's oversampling delay.
        juce::String error;
        auto original = loadPrepared (48000.0, 512, error);
        expect (original != nullptr, error);

        if (original == nullptr)
            return;

        setPlain (*original, "fx_slot1", 1.0f);
        settle (*original);
        pumpMessages (100);

        const auto latency = original->getLatencySamples();
        expect (latency > 0, "The distortion reports latency: " + juce::String (latency));

        juce::MemoryBlock project;
        original->getStateInformation (project);

        auto reopened = load (error);
        expect (reopened != nullptr, error);

        if (reopened == nullptr)
            return;

        reopened->setStateInformation (project.getData(), static_cast<int> (project.getSize()));
        reopened->setPlayConfigDetails (0, 2, 48000.0, 512);
        reopened->prepareToPlay (48000.0, 512);

        expectEquals (reopened->getLatencySamples(), latency,
                      "The reopened instance reports it on prepare, before any audio");
    }

    //==========================================================================
    void testAutomationTakesEffect()
    {
        beginTest ("Automation from the host reaches the sound");

        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        auto* gain = findRegistryParameter (*instance, "master_gain");
        expect (gain != nullptr);

        if (gain == nullptr)
            return;

        // An automation lane that drops the master to its floor halfway
        // through a held note, as a host plays it back: a value handed over
        // before the block it belongs to.
        const auto output = render (*instance, 48000, 512, note (60, 0, 47000),
                                    [&] (int blockStart)
                                    {
                                        if (blockStart == 24064)
                                            gain->setValueNotifyingHost (toNormalised ("master_gain", -60.0f));
                                    });

        const auto before = peak (output, 12000, 12000);
        const auto after = peak (output, 36000, 12000);

        expect (before > 0.01f, "Audible before the automation point");
        expect (after < before * 0.002f,
                "About 60 dB down after it: " + juce::String (juce::Decibels::gainToDecibels (after / before), 1) + " dB");

        // And the host's own view of the parameter followed the lane.
        expectWithinAbsoluteError (gain->getValue(), toNormalised ("master_gain", -60.0f), 1.0e-5f);
    }

    void testAutomatedGainDoesNotStep()
    {
        beginTest ("A jump in automated master gain arrives as a ramp, not a click");

        // Hosts send automation as a value per block, so a lane drawn as a step
        // arrives as a step. The gain is declared smoothed in the registry, and
        // this is the case that declaration exists for.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        // The sub oscillator alone, which is a sine: the largest sample-to-
        // sample change of a steady sine is known, so a step stands out.
        setPlain (*instance, "osc1_level", 0.0f);
        setPlain (*instance, "sub_level", 1.0f);
        settle (*instance);

        auto* gain = findRegistryParameter (*instance, "master_gain");

        if (gain == nullptr)
            return;

        const auto output = render (*instance, 48000, 512, note (69, 0, 47000),
                                    [&] (int blockStart)
                                    {
                                        if (blockStart == 24064)
                                            gain->setValueNotifyingHost (toNormalised ("master_gain", 6.0f));
                                    });

        const auto largestStep = [&] (int start, int length)
        {
            float result = 0.0f;
            const auto* samples = output.getReadPointer (0);

            for (int i = start + 1; i < start + length; ++i)
                result = juce::jmax (result, std::abs (samples[i] - samples[i - 1]));

            return result;
        };

        const auto steady = largestStep (12000, 12000);
        const auto atTheJump = largestStep (24000, 2048);

        // +6 dB doubles the amplitude, so a smoothed change can at most double
        // the largest step. An unsmoothed one would add the whole jump.
        expect (atTheJump < steady * 2.2f,
                "Largest step " + juce::String (atTheJump, 5) + " against " + juce::String (steady, 5) + " steady");
    }
};

static HostStateTests hostStateTests;

} // namespace apollo::host
