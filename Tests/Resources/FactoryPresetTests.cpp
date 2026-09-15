/*
    The sounds Apollo ships with.

    These are the tests that make compiled-in factory content safe to have. A
    preset stored as a table of parameter names and numbers is easy to read and
    easy to review, and it has exactly one failure mode a checked-in document
    does not: nothing stops somebody writing a name that is not a parameter, or
    a number outside the range the parameter allows. So that is checked here,
    for every setting of every preset, and it is a build-breaking failure rather
    than a preset that quietly loses half of itself.

    The rest is what "demonstrates the instrument" has to mean if it is to be
    testable at all: every preset renders into a document the ordinary reader
    accepts, loads into a real processor, and **makes a sound** — finite,
    bounded, and not silence. A factory library where one preset is inaudible is
    a library that ships broken, and it is the single most likely thing to be
    wrong about a table of numbers nobody has played.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "Resources/FactoryPresets.h"
#include "Resources/PresetLibrary.h"
#include "State/PresetDocument.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 512;

/** Renders a held note through @p processor and returns the left channel. */
[[nodiscard]] std::vector<float> renderNote (ApolloAudioProcessor& processor, int blocks)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> rendered;

    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();

        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        processor.processBlock (buffer, midi);

        const auto* left = buffer.getReadPointer (0);

        for (int i = 0; i < blockSize; ++i)
            rendered.push_back (left[i]);
    }

    return rendered;
}

[[nodiscard]] float peakOf (const std::vector<float>& signal)
{
    auto peak = 0.0f;

    for (const auto sample : signal)
        peak = std::max (peak, std::abs (sample));

    return peak;
}

[[nodiscard]] bool allFinite (const std::vector<float>& signal)
{
    return std::all_of (signal.begin(), signal.end(),
                        [] (float sample) { return std::isfinite (sample); });
}

class FactoryPresetTests final : public juce::UnitTest
{
public:
    FactoryPresetTests()
        : juce::UnitTest ("Factory presets", "Resources")
    {
    }

    void runTest() override
    {
        testEverySettingIsARealParameterInRange();
        testTheLibraryIsWellFormed();
        testInitIsTheRegistryDefaults();
        testEveryPresetLoads();
        testEveryPresetMakesASound();
        testNoPresetCarriesTheController();
        testTheyReachTheBrowser();
    }

private:
    /** The test that earns the whole design.

        A preset is a list of names and numbers, so the two things that can be
        wrong with one are a name that is not a parameter and a number the
        parameter would not accept. Both are caught here, named, for every
        setting in the library.
    */
    void testEverySettingIsARealParameterInRange()
    {
        beginTest ("Every factory setting names a real parameter and a value it accepts");

        for (const auto& preset : resources::factoryPresets())
        {
            const auto presetName = params::toJuceString (preset.name);

            std::set<std::string_view> seen;

            for (const auto& setting : preset.settings)
            {
                const auto settingId = params::toJuceString (setting.id);
                const auto where = presetName + " / " + settingId;

                const auto* definition = params::findParameter (setting.id);

                expect (definition != nullptr, where + ": no such parameter");

                if (definition == nullptr)
                    continue;

                // A preset that sets the same parameter twice has one of them
                // doing nothing, and which one is an accident of ordering.
                expect (seen.insert (setting.id).second, where + ": set twice");

                expect (std::isfinite (setting.value), where + ": not a finite value");

                expect (setting.value >= definition->minimum
                            && setting.value <= definition->maximum,
                        where + ": " + juce::String (setting.value) + " is outside "
                            + juce::String (definition->minimum) + " to "
                            + juce::String (definition->maximum));

                // A discrete parameter written off its own grid would be
                // snapped on the way into the document, so the number in the
                // table would not be the number in the preset.
                if (definition->stepSize > 0.0f)
                {
                    const auto offGrid = std::fmod (setting.value - definition->minimum,
                                                    definition->stepSize);

                    expect (std::abs (offGrid) < 1.0e-4f
                                || std::abs (std::abs (offGrid) - definition->stepSize) < 1.0e-4f,
                            where + ": " + juce::String (setting.value)
                                + " is not on the parameter's step of "
                                + juce::String (definition->stepSize));
                }

                // A setting that repeats the default is a line that says
                // nothing, and it makes the table lie about what the preset is
                // *for*. Not an error — a preset may want to state something
                // explicitly — but there should be none of them by accident,
                // and there are none today.
                expect (std::abs (setting.value - definition->defaultValue) > 1.0e-6f,
                        where + ": this is already the default");
            }
        }
    }

    void testTheLibraryIsWellFormed()
    {
        beginTest ("The factory library is named, categorised and free of duplicates");

        const auto presets = resources::factoryPresets();

        expect (! presets.empty(), "a factory library with nothing in it is not one");

        // First in the table, which is what hands the default patch to anything
        // that asks for it. The browser sorts its own list alphabetically, so
        // this says nothing about where the row appears.
        expect (presets.front().name == "Init",
                "Init must be the first entry in the table");

        expect (presets.front().settings.empty(),
                "Init overrides nothing — that is what makes it the defaults");

        std::set<std::string_view> names;

        for (const auto& preset : presets)
        {
            const auto where = params::toJuceString (preset.name);

            expect (! preset.name.empty(), "a preset with no name cannot be found again");
            expect (! preset.category.empty(), where + ": no category");
            expect (! preset.comment.empty(), where + ": no comment");

            // Two presets with one name is a browser with two identical rows.
            expect (names.insert (preset.name).second, where + ": duplicated name");

            const auto metadata = resources::metadataOf (preset);

            // The metadata the browser shows must survive the bounds the reader
            // applies, or the name in the list would not be the name in the
            // file.
            expectEquals (metadata.name, where);
            expect (metadata == presets::sanitise (metadata),
                    where + ": the metadata would be changed by the bounds the reader applies");
        }
    }

    /** Init is not a special case in the code, so this is what keeps it honest. */
    void testInitIsTheRegistryDefaults()
    {
        beginTest ("Loading Init returns every parameter to its registry default");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        // Moved first, so that "everything is at its default afterwards" is a
        // statement about the load rather than about a fresh instance.
        for (const auto& definition : params::parameterDefinitions)
        {
            auto* parameter = apvts.getParameter (params::toJuceString (definition.id));

            if (parameter == nullptr)
                continue;

            // Three quarters of the way along, which is a different value from
            // the default for every parameter in the registry except where the
            // default already sits there.
            parameter->setValueNotifyingHost (0.75f);
        }

        const auto& init = resources::factoryPresets().front();

        expect (presets::read (apvts, resources::render (init)) == state::StateLoadResult::ok,
                "Init must load");

        for (const auto& definition : params::parameterDefinitions)
        {
            // The four that describe the controller are deliberately not in a
            // preset, so Init does not return them either: it is a patch, and
            // somebody's MPE zone is not part of one (ADR-0061).
            if (presets::isExcludedFromPresets (definition.id))
                continue;

            const auto id = params::toJuceString (definition.id);
            auto* parameter = apvts.getParameter (id);

            expect (parameter != nullptr, id + ": missing from APVTS");

            if (parameter == nullptr)
                continue;

            const auto expected = parameter->convertTo0to1 (definition.defaultValue);

            expectWithinAbsoluteError (parameter->getValue(), expected, 1.0e-4f,
                                       id + " did not return to its default");
        }
    }

    void testEveryPresetLoads()
    {
        beginTest ("Every factory preset renders into a document the reader accepts");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        for (const auto& preset : resources::factoryPresets())
        {
            const auto where = params::toJuceString (preset.name);
            const auto text = resources::render (preset);

            expect (text.isNotEmpty(), where + ": rendered nothing");

            // It is a real `.rnv` document, not a private format: text, XML,
            // and recognisable as Apollo's without a parser (ADR-0053).
            expect (text.startsWith ("<?xml"), where + ": not an XML document");
            expect (text.contains (params::stateTreeType), where + ": not an Apollo state tree");

            expect (presets::read (apvts, text) == state::StateLoadResult::ok,
                    where + ": the reader refused it");

            // And the metadata survives the round trip, which is what the
            // browser lists and what the masthead shows.
            const auto metadata = presets::getMetadata (apvts);

            expectEquals (metadata.name, where);
            expectEquals (metadata.category, params::toJuceString (preset.category));
            expectEquals (metadata.author, params::toJuceString (resources::factoryAuthor));
        }
    }

    /** The one that would catch a table of plausible numbers that makes no noise. */
    void testEveryPresetMakesASound()
    {
        beginTest ("Every factory preset plays a note, audibly and without misbehaving");

        for (const auto& preset : resources::factoryPresets())
        {
            const auto where = params::toJuceString (preset.name);

            ApolloAudioProcessor processor;

            expect (presets::read (processor.getValueTreeState(), resources::render (preset))
                        == state::StateLoadResult::ok,
                    where + ": failed to load");

            processor.prepareToPlay (testSampleRate, blockSize);

            // A second, which is chosen by the slowest sound in the library
            // rather than by taste: the pad takes 1.2 s to reach full level, so
            // a test that listened for a tenth of a second would report it as
            // the broken one. A second puts it more than three quarters of the
            // way up, which is far past audible.
            //
            // It is not longer than that on purpose. This is ten processors
            // rendering a full effects rack, and the same suite runs under a
            // sanitiser in CI where every sample costs several times what it
            // costs here.
            const auto rendered = renderNote (processor, 94);

            expect (allFinite (rendered), where + ": produced a non-finite sample");

            const auto peak = peakOf (rendered);

            // A real threshold rather than "not exactly zero": a preset that
            // peaks at -60 dBFS with a note held at velocity 115 is one nobody
            // would hear, and is as broken as silence.
            expect (peak > 0.01f,
                    where + ": peaked at only " + juce::String (peak) + " — inaudible");

            // Every preset ships with its own master gain, so none of them
            // should be arriving at the limit of the format.
            expect (peak < 1.5f,
                    where + ": peaked at " + juce::String (peak) + " — far too hot");

            processor.releaseResources();
        }
    }

    void testNoPresetCarriesTheController()
    {
        beginTest ("A factory preset changes nothing about the user's controller");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        auto* bendRange = apvts.getParameter ("midi_bend_range");
        auto* mpeZone = apvts.getParameter ("mpe_zone");

        expect (bendRange != nullptr && mpeZone != nullptr);

        if (bendRange == nullptr || mpeZone == nullptr)
            return;

        // A keyboard set up the way this user likes it.
        bendRange->setValueNotifyingHost (bendRange->convertTo0to1 (7.0f));
        mpeZone->setValueNotifyingHost (mpeZone->convertTo0to1 (1.0f));

        for (const auto& preset : resources::factoryPresets())
        {
            const auto where = params::toJuceString (preset.name);
            const auto text = resources::render (preset);

            // Not in the document at all, which is the stronger claim: a
            // preset that carried them and had them stripped on the way in
            // would still be a file that told everyone it was shared with how
            // its author's keyboard was set up.
            expect (! text.contains ("midi_bend_range"), where + ": carries the bend range");
            expect (! text.contains ("mpe_zone"), where + ": carries the MPE zone");

            expect (presets::read (apvts, text) == state::StateLoadResult::ok,
                    where + ": failed to load");

            expectWithinAbsoluteError (bendRange->convertFrom0to1 (bendRange->getValue()),
                                       7.0f, 0.01f,
                                       where + " moved the bend range");
            expectWithinAbsoluteError (mpeZone->convertFrom0to1 (mpeZone->getValue()),
                                       1.0f, 0.01f,
                                       where + " moved the MPE zone");
        }
    }

    void testTheyReachTheBrowser()
    {
        beginTest ("The built-in presets appear in the index and load from it");

        resources::PresetIndex index;

        resources::addBuiltInPresets (index);

        expectEquals (static_cast<int> (index.entries.size()),
                      static_cast<int> (resources::factoryPresets().size()));

        ApolloAudioProcessor processor;

        std::set<juce::String> keys;

        for (const auto& entry : index.entries)
        {
            const auto where = entry.name;

            expect (entry.factory, where + ": built-in content is factory content");
            expect (entry.builtIn >= 0, where + ": must say which built-in it is");
            expect (entry.file == juce::File(), where + ": a built-in has no file");
            expectEquals (entry.bank, params::toJuceString (resources::factoryBank));
            expect (entry.id > 0, where + ": must be numbered");

            // The key is what the bridge remembers across a rescan, so two
            // presets sharing one would make the browser highlight the wrong
            // row (ADR-0063).
            expect (keys.insert (entry.key()).second, where + ": duplicated key");

            // And it loads by the route the bridge actually uses, which is the
            // one that has to work for a preset with no file.
            juce::String text;

            expect (resources::readPreset (entry, text) == state::StateLoadResult::ok,
                    where + ": readPreset refused it");

            expect (presets::read (processor.getValueTreeState(), text)
                        == state::StateLoadResult::ok,
                    where + ": the document it produced was refused");
        }

        // Found by id, the way a load does.
        const auto* first = resources::findPreset (index, index.entries.front().id);

        expect (first != nullptr, "a built-in must resolve by its id");
    }
};

FactoryPresetTests factoryPresetTests;

} // namespace
