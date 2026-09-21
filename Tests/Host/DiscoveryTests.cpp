/*
    Discovery, loading and parameter identity, through a VST3 host.

    What a DAW does before it plays a note: find the bundle, read the factory,
    decide what kind of plugin it is, instantiate it — possibly many times over
    while it builds a menu — and list its parameters. A plugin that fails any of
    these never gets as far as producing sound, and the failures are the
    quietest kind: it is simply missing from the list, or it is listed as an
    effect, or its automation lanes are empty in a project saved a week ago.
*/

#include "Host/HostedApollo.h"
#include "Host/ParameterIdentities.h"

#include "Parameters/ParameterDefinitions.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace apollo::host
{

class HostDiscoveryTests final : public juce::UnitTest
{
public:
    HostDiscoveryTests() : juce::UnitTest ("Host discovery and loading", "Host") {}

    void runTest() override
    {
        testScanFindsOneInstrument();
        testScanIsDeterministic();
        testInstanceDescribesItself();
        testRepeatedLoadAndUnload();
        testManyInstancesAtOnce();
        testParameterList();
        testParameterIdentityIsPinned();
        testParameterMetadata();
        testBusLayouts();
    }

private:
    //==========================================================================
    void testScanFindsOneInstrument()
    {
        beginTest ("A scan finds exactly one class, and it is an instrument");

        juce::String error;
        const auto found = scan (error);

        expect (error.isEmpty(), error);
        expectEquals (found.size(), 1, "The bundle declares one plugin");

        if (found.isEmpty())
            return;

        const auto& description = *found.getFirst();

        // What a host puts in its browser, and what decides which list Apollo
        // appears in. Listed as an effect, an instrument cannot be inserted on
        // an instrument track at all in several hosts.
        expectEquals (description.name, juce::String ("Apollo"));
        expectEquals (description.manufacturerName, juce::String ("ProdByRnV"));
        expectEquals (description.pluginFormatName, juce::String ("VST3"));
        expect (description.isInstrument, "The class is categorised as an instrument");
        expect (description.category.contains ("Instrument"), description.category);
        expect (description.category.contains ("Synth"), description.category);
        expectEquals (description.version, juce::String (APOLLO_HOST_TEST_EXPECTED_VERSION));
    }

    void testScanIsDeterministic()
    {
        beginTest ("Two scans of the same bundle describe the same plugin");

        // A host caches what it found, keyed on these, and decides on the next
        // start whether the plugin it knows is the one on disk. Anything that
        // changed between two scans of the same file would look like a
        // different plugin: the user's projects would be missing it.
        juce::String error;
        const auto first = scan (error);
        const auto second = scan (error);

        expect (! first.isEmpty() && ! second.isEmpty(), error);

        if (first.isEmpty() || second.isEmpty())
            return;

        expectEquals (first.getFirst()->uniqueId, second.getFirst()->uniqueId);
        expectEquals (first.getFirst()->deprecatedUid, second.getFirst()->deprecatedUid);
        expectEquals (first.getFirst()->createIdentifierString(),
                      second.getFirst()->createIdentifierString());
        expect (first.getFirst()->uniqueId != 0, "The class has a non-zero identifier");
    }

    void testInstanceDescribesItself()
    {
        beginTest ("A loaded instance accepts MIDI, produces none, and declares no latency");

        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        expectEquals (instance->getName(), juce::String ("Apollo"));
        expect (instance->acceptsMidi(), "An instrument has to accept MIDI");
        expect (! instance->producesMidi(), "Apollo produces no MIDI");

        // Stereo out and no audio in, as the host first finds it. A host that
        // saw an active input would offer Apollo as an effect as well. Read
        // from the instance rather than the scan: a scan reads the factory,
        // which does not describe buses.
        expectEquals (instance->getTotalNumOutputChannels(), 2, "Stereo output by default");
        expectEquals (instance->getTotalNumInputChannels(), 0, "No active audio input by default");

        // An empty rack adds no delay, so a fresh instance must not ask the
        // host to compensate for any.
        instance->setPlayConfigDetails (0, 2, 48000.0, 512);
        instance->prepareToPlay (48000.0, 512);
        expectEquals (instance->getLatencySamples(), 0, "A fresh instance reports no latency");

        // The default release is 50 ms and the rack is empty, so the tail is
        // the release and nothing more.
        expectWithinAbsoluteError (instance->getTailLengthSeconds(), 0.05, 0.002,
                                   "A fresh instance's tail is its release");

        instance->releaseResources();
    }

    void testRepeatedLoadAndUnload()
    {
        beginTest ("Forty loads and unloads, each prepared and played, with nothing left behind");

        // Hosts instantiate a plugin to scan it, again to show it in a menu,
        // again when it is inserted, and tear it down when a project closes or
        // the track is frozen. Apollo's first instance builds the shared
        // wavetables (ADR-0066); every one after must find them, and the last
        // one out must not leave the next one a dangling reference.
        const auto started = juce::Time::getMillisecondCounterHiRes();
        int loaded = 0;
        int sounded = 0;

        for (int i = 0; i < 40; ++i)
        {
            juce::String error;
            auto instance = loadPrepared (48000.0, 256, error);

            if (instance == nullptr)
                continue;

            ++loaded;

            const auto output = render (*instance, 4800, 256, note (60, 0, 4000));

            if (peak (output) > 0.01f && allFinite (output))
                ++sounded;

            instance->releaseResources();
        }

        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - started;

        expectEquals (loaded, 40, "Every load succeeded");
        expectEquals (sounded, 40, "Every instance played its note");

        logMessage ("    forty load-prepare-play-unload cycles: "
                    + juce::String (elapsed, 0) + " ms, "
                    + juce::String (elapsed / 40.0, 1) + " ms each");
    }

    void testManyInstancesAtOnce()
    {
        beginTest ("Sixteen instances live at once are independent of each other");

        // A project with Apollo on sixteen tracks. The instances share the
        // built-in wavetables and nothing else: a parameter moved on one must
        // not move on another, and each must play only its own notes.
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> instances;
        std::vector<double> loadTimes;

        for (int i = 0; i < 16; ++i)
        {
            juce::String error;
            const auto started = juce::Time::getMillisecondCounterHiRes();
            auto instance = loadPrepared (48000.0, 512, error);
            loadTimes.push_back (juce::Time::getMillisecondCounterHiRes() - started);
            expect (instance != nullptr, error);

            if (instance != nullptr)
                instances.push_back (std::move (instance));
        }

        if (instances.size() != 16)
            return;

        // What a host pays per insert once the module is resident. The first
        // load also pays for the module and the shared wavetables (ADR-0066);
        // the rest should pay for neither.
        auto later = std::vector<double> (loadTimes.begin() + 1, loadTimes.end());
        std::sort (later.begin(), later.end());

        logMessage ("    load and prepare, module cold: " + juce::String (loadTimes.front(), 1)
                    + " ms; module resident, median of 15: " + juce::String (later[later.size() / 2], 1)
                    + " ms, worst " + juce::String (later.back(), 1) + " ms");

        // Every other instance is muted by its master gain.
        for (std::size_t i = 0; i < instances.size(); i += 2)
            expect (setPlain (*instances[i], "master_gain", -60.0f));

        int independent = 0;

        for (std::size_t i = 0; i < instances.size(); ++i)
        {
            const auto output = render (*instances[i], 9600, 512, note (57, 0, 9000));
            const auto level = peak (output, 4800, 4800);
            const auto muted = (i % 2) == 0;

            // -60 dB of master gain against a note at about -24 dBFS leaves
            // something near -84 dBFS; unmuted, the note itself.
            if (muted ? level < 0.001f : level > 0.01f)
                ++independent;
        }

        expectEquals (independent, 16, "Each instance obeyed only its own gain");
    }

    //==========================================================================
    void testParameterList()
    {
        beginTest ("The host sees every registry parameter, once, and nothing it should not");

        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const auto listed = userParameters (*instance);

        expectEquals (static_cast<int> (listed.size()),
                      static_cast<int> (params::parameterDefinitions.size()),
                      "One host parameter per registry entry");

        std::set<juce::String> names;

        for (auto* parameter : listed)
            names.insert (parameter->getName (256));

        expectEquals (static_cast<int> (names.size()), static_cast<int> (listed.size()),
                      "No two parameters share a name, so none is ambiguous in a host's list");

        int missing = 0;

        for (const auto& definition : params::parameterDefinitions)
        {
            if (findRegistryParameter (*instance, definition.id) == nullptr)
            {
                ++missing;
                logMessage ("    missing: " + juce::String (definition.id.data(), definition.id.size()));
            }
        }

        expectEquals (missing, 0, "Every registry parameter is reachable by its name");

        // The wrapper's own additions: a bypass, which a host needs, and the
        // emulated MIDI controllers. There is no program-change parameter,
        // because Apollo reports one program — its presets are its own.
        expect (instance->getBypassParameter() != nullptr,
                "The host is given a bypass parameter to drive");

        logMessage ("    host-visible parameters: " + juce::String (instance->getParameters().size())
                    + " (" + juce::String (listed.size()) + " Apollo, 1 bypass, "
                    + juce::String (instance->getParameters().size() - static_cast<int> (listed.size()) - 1)
                    + " emulated MIDI controllers)");
    }

    void testParameterIdentityIsPinned()
    {
        beginTest ("Every parameter answers to the VST3 ID a saved project stores");

        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const auto& pinned = pinnedParameterIdentities();

        // A new parameter must be added to the pinned list as well, or it would
        // be the one parameter whose identity nobody was checking.
        expectEquals (static_cast<int> (pinned.size()),
                      static_cast<int> (params::parameterDefinitions.size()),
                      "Every registry parameter has a pinned identity; regenerate with "
                      "ApolloHostTests --parameter-ids if one was added");

        std::map<juce::uint32, juce::AudioProcessorParameter*> byId;

        for (auto* parameter : userParameters (*instance))
            byId[vst3ParameterId (*parameter)] = parameter;

        int wrong = 0;
        int renumbered = 0;

        for (const auto& identity : pinned)
        {
            const auto* definition = params::findParameter (identity.registryId);
            const auto registryId = juce::String (identity.registryId.data(), identity.registryId.size());

            if (definition == nullptr)
            {
                ++wrong;
                logMessage ("    pinned but not in the registry: " + registryId);
                continue;
            }

            // The number a host has stored must still find the parameter it
            // was stored for.
            const auto found = byId.find (identity.vst3Id);
            const auto expectedName = juce::String (definition->name.data(), definition->name.size());

            if (found == byId.end() || found->second->getName (256) != expectedName)
            {
                ++wrong;
                logMessage ("    " + registryId + " no longer answers to " + juce::String (identity.vst3Id));
            }

            // And the number must be the one JUCE derives from the string ID.
            // If this alone fails, the string IDs are intact and the mapping
            // from them changed underneath — a JUCE upgrade or a build flag.
            if (juce::VST3ClientExtensions::convertJuceParameterId (registryId) != identity.vst3Id)
                ++renumbered;
        }

        expectEquals (wrong, 0, "Every pinned identity resolves to its own parameter");
        expectEquals (renumbered, 0, "Every identity is JUCE's hash of the registry ID");
    }

    void testParameterMetadata()
    {
        beginTest ("Defaults, step counts and automatability reach the host as the registry states them");

        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        int wrongDefault = 0;
        int wrongSteps = 0;
        int wrongAutomation = 0;
        int nonAutomatable = 0;

        for (const auto& definition : params::parameterDefinitions)
        {
            auto* parameter = findRegistryParameter (*instance, definition.id);

            if (parameter == nullptr)
                continue;

            const auto id = juce::String (definition.id.data(), definition.id.size());

            // The host's idea of "default" is what double-click-to-reset and a
            // cleared automation lane return to.
            const auto expectedDefault = toNormalised (definition.id, definition.defaultValue);

            if (std::abs (parameter->getDefaultValue() - expectedDefault) > 1.0e-4f)
            {
                ++wrongDefault;
                logMessage ("    default of " + id + ": host " + juce::String (parameter->getDefaultValue())
                            + ", registry " + juce::String (expectedDefault));
            }

            // A discrete parameter must stay discrete across the wrapper, or a
            // host draws a stepped selector as a smooth ramp and automation
            // lands between values.
            if (definition.stepSize > 0.0f)
            {
                const auto expectedSteps = static_cast<int> (std::round ((definition.maximum - definition.minimum)
                                                                         / definition.stepSize)) + 1;

                if (parameter->getNumSteps() != expectedSteps)
                {
                    ++wrongSteps;
                    logMessage ("    steps of " + id + ": host " + juce::String (parameter->getNumSteps())
                                + ", registry " + juce::String (expectedSteps));
                }
            }

            if (parameter->isAutomatable() != definition.automatable)
                ++wrongAutomation;

            if (! parameter->isAutomatable())
                ++nonAutomatable;
        }

        expectEquals (wrongDefault, 0, "Every default reaches the host");
        expectEquals (wrongSteps, 0, "Every discrete parameter keeps its step count");
        expectEquals (wrongAutomation, 0, "Automatability matches the registry");

        // The bend range and the MPE zone describe the controller on the desk,
        // not the patch, and are the only parameters a host may not automate
        // (ADR-0045).
        expectEquals (nonAutomatable, 4, "Exactly the four MIDI expression settings are not automatable");
    }

    void testBusLayouts()
    {
        beginTest ("The host can have mono or stereo out, and is refused anything wider");

        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const auto request = [&] (const juce::AudioChannelSet& output)
        {
            auto layout = instance->getBusesLayout();

            if (layout.outputBuses.isEmpty())
                return false;

            layout.outputBuses.getReference (0) = output;

            for (auto& input : layout.inputBuses)
                input = juce::AudioChannelSet::disabled();

            return instance->setBusesLayout (layout);
        };

        expect (request (juce::AudioChannelSet::stereo()), "Stereo accepted");
        expect (request (juce::AudioChannelSet::mono()), "Mono accepted");

        // Mono is a real configuration and must sound, not merely be accepted.
        instance->prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> block (1, 512);
        juce::MidiBuffer events;
        events.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        float level = 0.0f;

        for (int i = 0; i < 10; ++i)
        {
            block.clear();
            instance->processBlock (block, events);
            events.clear();
            level = juce::jmax (level, block.getMagnitude (0, 0, 512));
        }

        instance->releaseResources();
        expect (level > 0.01f, "A mono instance sounds: peak " + juce::String (level));

        expect (! request (juce::AudioChannelSet::create5point1()), "5.1 refused");
        expect (request (juce::AudioChannelSet::stereo()), "Back to stereo afterwards");
    }
};

static HostDiscoveryTests hostDiscoveryTests;

} // namespace apollo::host
