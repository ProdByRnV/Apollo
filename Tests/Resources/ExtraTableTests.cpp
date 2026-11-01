/*
    The tables Apollo ships beyond the oscillator's four (Phase 16d, ADR-0116).

      - each is sixteen frames of a real, finite, audible waveform, under a
        name of its own, and they differ from one another;
      - each is what its name says, measured on its spectrum: ANALOG passes
        through a sine, a triangle and a saw; SYNC starts as a saw and tears
        upwards; ORGAN is drawbars on whole harmonics; CHIME has partials only
        at square harmonic numbers; DIGITAL ends as a square;
      - loading one into an oscillator selects USER and carries the table in
        the patch as audio, marked as Apollo's own; a project that carries one
        plays the same table in another instance;
      - the page asks for one by number, and nothing else: a number that is not
        a table, or the noise slot, is refused.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <set>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Oscillators/ExtraTables.h"
#include "Resources/UserContent.h"
#include "UI/ParameterBridge.h"

using namespace apollo;
using resources::ContentSlot;

namespace
{

[[nodiscard]] double sineOf (const dsp::FrameSpectrum& frame, int harmonic)
{
    return frame[static_cast<std::size_t> (harmonic - 1)].sine;
}

[[nodiscard]] double magnitudeOf (const dsp::FrameSpectrum& frame, int harmonic)
{
    const auto& h = frame[static_cast<std::size_t> (harmonic - 1)];
    return std::sqrt (h.sine * h.sine + h.cosine * h.cosine);
}

/** The power-weighted mean harmonic number over the first 64: where a
    frame's brightness sits, as the ear weighs it. Weighted by amplitude, a
    saw's long tail of quiet harmonics would outweigh the loud ones it starts
    with. */
[[nodiscard]] double centroidOf (const dsp::FrameSpectrum& frame)
{
    double weighted = 0.0;
    double total = 0.0;

    for (int k = 1; k <= 64; ++k)
    {
        const auto power = magnitudeOf (frame, k) * magnitudeOf (frame, k);
        weighted += power * static_cast<double> (k);
        total += power;
    }

    return total > 0.0 ? weighted / total : 0.0;
}

void settle (ApolloAudioProcessor& processor)
{
    processor.getUserContent().waitUntilIdle();
    processor.getUserContent().flushDeliveries();
}

[[nodiscard]] float plainOf (ApolloAudioProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : -1.0f;
}

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

[[nodiscard]] juce::ValueTree itemFor (ApolloAudioProcessor& processor, ContentSlot slot)
{
    const auto content = processor.getValueTreeState().state.getChildWithName (
        resources::UserContent::contentTreeType);

    for (int i = 0; i < content.getNumChildren(); ++i)
        if (content.getChild (i).getProperty (resources::UserContent::slotProperty).toString()
            == resources::toToken (slot))
            return content.getChild (i);

    return {};
}

/** Oscillator 1 on USER, at the middle of its table, with note 57 held: the
    left channel. */
[[nodiscard]] std::vector<float> renderUser (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_wavetable", 4.0f);
    setPlain (processor, "osc1_position", 0.5f);
    setPlain (processor, "filter1_type", 0.0f);
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    std::vector<float> rendered;

    for (int b = 0; b < 20; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
    }

    return rendered;
}

class ExtraTableTests final : public juce::UnitTest
{
public:
    ExtraTableTests()
        : juce::UnitTest ("Extra wavetables", "Resources")
    {
    }

    void runTest() override
    {
        testEveryTableIsATable();
        testAnalog();
        testSync();
        testOrgan();
        testChime();
        testDigital();
        testLoadingOnePlaysItAndCarriesIt();
        testThePageAsksByNumber();
    }

private:
    void testEveryTableIsATable()
    {
        beginTest ("Each extra table is sixteen frames of a real waveform, under a name of its own");

        std::set<juce::String> names;
        std::vector<dsp::FrameSpectrum> middles;

        for (int index = 0; index < dsp::numExtraTables; ++index)
        {
            const juce::String tableName (dsp::extraTableName (index));
            expect (tableName.isNotEmpty(), "a table has no name");
            names.insert (tableName);

            const auto spectrum = dsp::makeExtraTable (index);
            expectEquals (static_cast<int> (spectrum.size()), dsp::extraTableFrames, tableName);

            for (const auto& frame : spectrum)
            {
                expectEquals (static_cast<int> (frame.size()), dsp::Wavetable::topLevelHarmonics, tableName);

                auto audible = false;

                for (const auto& harmonic : frame)
                {
                    expect (std::isfinite (harmonic.sine) && std::isfinite (harmonic.cosine), tableName + " is not finite");
                    audible = audible || ! harmonic.isSilent();
                }

                expect (audible, tableName + " has a silent frame");
            }

            dsp::Wavetable table;
            dsp::buildWavetable (table, spectrum);
            expect (! table.isEmpty(), tableName + " did not build");
            expectEquals (table.getNumFrames(), dsp::extraTableFrames);

            middles.push_back (spectrum[8]);
        }

        expectEquals (static_cast<int> (names.size()), dsp::numExtraTables, "two tables share a name");

        // No two are the same table under different names.
        for (std::size_t a = 0; a < middles.size(); ++a)
            for (std::size_t b = a + 1; b < middles.size(); ++b)
            {
                double difference = 0.0;

                for (int k = 1; k <= 64; ++k)
                    difference += std::abs (magnitudeOf (middles[a], k) - magnitudeOf (middles[b], k));

                expect (difference > 0.1, juce::String (dsp::extraTableName (static_cast<int> (a))) + " and "
                                              + dsp::extraTableName (static_cast<int> (b)) + " are the same");
            }

        expect (dsp::makeExtraTable (-1).empty() && dsp::makeExtraTable (dsp::numExtraTables).empty());
        expectEquals (juce::String (dsp::extraTableName (dsp::numExtraTables)), juce::String());
    }

    void testAnalog()
    {
        beginTest ("ANALOG passes through a sine, a triangle, a square and a saw");

        const auto table = dsp::makeExtraTable (0);

        // Frame 0: a sine, and nothing else.
        for (int k = 2; k <= 64; ++k)
            expect (magnitudeOf (table[0], k) < 1.0e-12, "the first frame is not a sine");

        // A third of the way: a triangle, odd harmonics at 1/k^2, alternating.
        expectWithinAbsoluteError (sineOf (table[5], 3) / sineOf (table[5], 1), -1.0 / 9.0, 1.0e-6);
        expectWithinAbsoluteError (sineOf (table[5], 2), 0.0, 1.0e-9);

        // Two thirds: a square, odd harmonics at 1/k.
        expectWithinAbsoluteError (sineOf (table[10], 3) / sineOf (table[10], 1), 1.0 / 3.0, 1.0e-6);
        expectWithinAbsoluteError (sineOf (table[10], 2), 0.0, 1.0e-9);

        // The end: a saw, every harmonic at 1/k.
        expectWithinAbsoluteError (sineOf (table[15], 2) / sineOf (table[15], 1), -0.5, 1.0e-9);
        expectWithinAbsoluteError (sineOf (table[15], 3) / sineOf (table[15], 1), 1.0 / 3.0, 1.0e-9);
    }

    void testSync()
    {
        beginTest ("SYNC starts as a saw and tears upwards");

        const auto table = dsp::makeExtraTable (1);

        // Drawn rather than written, so measured: a saw's second harmonic is
        // half its first, to what the drawn cycle's resolution allows.
        const auto ratio = magnitudeOf (table[0], 2) / magnitudeOf (table[0], 1);
        logMessage ("  frame 0, second harmonic over first: " + juce::String (ratio, 6));
        expectWithinAbsoluteError (ratio, 0.5, 1.0e-3);

        const auto first = centroidOf (table[0]);
        const auto last = centroidOf (table[15]);
        logMessage ("  brightness, first frame " + juce::String (first, 1) + ", last " + juce::String (last, 1));
        expect (last > first * 1.5, "the last frame is not brighter than the first");
    }

    void testOrgan()
    {
        beginTest ("ORGAN is drawbars on whole harmonics");

        const auto table = dsp::makeExtraTable (2);

        for (int k = 2; k <= 64; ++k)
            expect (magnitudeOf (table[0], k) == 0.0, "the first registration is not a lone flute");

        // Every drawbar out: harmonics 1 to 6 and 8, all at the same level,
        // and nothing between or above them.
        for (const auto k : { 1, 2, 3, 4, 5, 6, 8 })
            expectWithinAbsoluteError (magnitudeOf (table[11], k), 1.0, 1.0e-12);

        expect (magnitudeOf (table[11], 7) == 0.0, "the seventh harmonic is not a drawbar");

        for (int k = 9; k <= 64; ++k)
            expect (magnitudeOf (table[11], k) == 0.0, "something sounds above the 1' drawbar");
    }

    void testChime()
    {
        beginTest ("CHIME has partials only at square harmonic numbers, and brightens");

        const auto table = dsp::makeExtraTable (3);

        for (const auto& frame : table)
            for (int k = 1; k <= dsp::Wavetable::topLevelHarmonics; ++k)
            {
                const auto root = static_cast<int> (std::lround (std::sqrt (static_cast<double> (k))));

                if (root * root != k)
                    expect (magnitudeOf (frame, k) == 0.0, "a partial at harmonic " + juce::String (k));
            }

        expect (centroidOf (table[15]) > centroidOf (table[0]) * 2.0, "the end of the table is not brighter");
    }

    void testDigital()
    {
        beginTest ("DIGITAL is a sine at the start and a square at the end");

        const auto table = dsp::makeExtraTable (4);

        const auto grain = magnitudeOf (table[0], 3) / magnitudeOf (table[0], 1);
        logMessage ("  frame 0, third harmonic over first: " + juce::String (grain, 6));
        expect (grain < 0.02, "sixty-four levels is not close to a sine");

        const auto& square = table[15];
        expectWithinAbsoluteError (magnitudeOf (square, 3) / magnitudeOf (square, 1), 1.0 / 3.0, 2.0e-3);
        expect (magnitudeOf (square, 2) < 1.0e-3 * magnitudeOf (square, 1), "a square has no even harmonics");
    }

    void testLoadingOnePlaysItAndCarriesIt()
    {
        beginTest ("Loading one selects USER, carries it in the patch, and plays the same in another instance");

        ApolloAudioProcessor original;
        auto& content = original.getUserContent();

        std::vector<resources::ContentEvent> events;
        content.onChanged = [&events] (const resources::ContentEvent& event) { events.push_back (event); };

        expect (content.loadExtraTable (ContentSlot::osc1, 0));
        expectEquals (events.back().token, juce::String ("LOADING"));
        settle (original);

        expectEquals (events.back().token, juce::String ("LOADED"));
        expectEquals (events.back().message, juce::String ("Analog is playing on oscillator 1."));

        const auto info = content.getInfo (ContentSlot::osc1);
        expect (info.loaded);
        expect (info.source.origin == resources::ContentOrigin::builtIn);
        expectEquals (info.source.name, juce::String ("Analog"));
        expectEquals (info.frames, dsp::extraTableFrames);
        expectEquals (plainOf (original, "osc1_wavetable"), 4.0f, "loading must select USER");

        const auto item = itemFor (original, ContentSlot::osc1);
        expectEquals (item.getProperty (resources::UserContent::originProperty).toString(), juce::String ("builtin"));
        expect (item.getProperty (resources::UserContent::audioProperty).toString().isNotEmpty(),
                "the patch must carry the table, so an Apollo without it can play it");
        expect (! item.hasProperty (resources::UserContent::pathProperty));

        // What plays is the table its spectrum builds, to the 24 bits the
        // patch keeps.
        dsp::Wavetable direct;
        dsp::buildWavetable (direct, dsp::makeExtraTable (0));
        const auto& playing = original.getVoiceEngine().getWavetableLibrary()
                                  .getTable (dsp::WavetableLibrary::userSlot (1));

        auto worstTable = 0.0f;

        for (int frame = 0; frame < dsp::extraTableFrames; ++frame)
            for (int i = 0; i < dsp::Wavetable::topLevelSamples; ++i)
                worstTable = std::max (worstTable, std::abs (playing.getReadPointer (0, frame)[i]
                                                             - direct.getReadPointer (0, frame)[i]));

        logMessage ("  played against built from its spectrum, worst sample: " + juce::String (worstTable, 9));
        expect (worstTable < 1.0e-5f, "the loaded table is not the table");

        juce::MemoryBlock saved;
        original.getStateInformation (saved);
        logMessage ("  a project carrying one: " + juce::String (static_cast<int> (saved.getSize() / 1024)) + " KB");

        ApolloAudioProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        settle (restored);

        const auto restoredInfo = restored.getUserContent().getInfo (ContentSlot::osc1);
        expect (restoredInfo.source.origin == resources::ContentOrigin::builtIn, "the table forgot it is Apollo's");
        expectEquals (restoredInfo.source.name, juce::String ("Analog"));

        const auto a = renderUser (original);
        const auto b = renderUser (restored);
        auto worst = 0.0f;
        auto loudest = 0.0f;

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            worst = std::max (worst, std::abs (a[i] - b[i]));
            loudest = std::max (loudest, std::abs (a[i]));
        }

        expect (loudest > 0.01f, "nothing played");
        expect (worst < 1.0e-6f, "the restored table does not sound like the original");

        // Refused, changing nothing: the noise, and numbers that are not tables.
        expect (! content.loadExtraTable (ContentSlot::noise, 0));
        expect (! content.loadExtraTable (ContentSlot::osc2, -1));
        expect (! content.loadExtraTable (ContentSlot::osc2, dsp::numExtraTables));
        expect (! content.getInfo (ContentSlot::osc2).loading && ! content.getInfo (ContentSlot::osc2).loaded);
    }

    void testThePageAsksByNumber()
    {
        beginTest ("The page lists the tables and asks for one by number; anything else is refused");

        ApolloAudioProcessor processor;
        processor.getContentLibrary().setLocations ({});

        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setUserContent (&processor.getUserContent(), &processor.getContentLibrary());
        processor.getContentLibrary().waitForScan();

        juce::var index;
        expect (juce::JSON::parse (bridge.createContentIndex(), index).wasOk());

        const auto* tables = index["builtInTables"].getArray();
        expect (tables != nullptr && tables->size() == dsp::numExtraTables, "the index does not list the tables");

        if (tables != nullptr && tables->size() == dsp::numExtraTables)
        {
            expectEquals (static_cast<int> (tables->getReference (2)["table"]), 3);
            expectEquals (tables->getReference (2)["name"].toString(), juce::String ("Organ"));
            expectEquals (static_cast<int> (tables->getReference (2)["frames"]), dsp::extraTableFrames);
        }

        const auto message = [] (const juce::String& slot, const juce::String& table)
        {
            return "{\"type\":\"loadBuiltInTable\",\"version\":1,\"slot\":" + slot + ",\"table\":" + table + "}";
        };

        expectEquals (bridge.handleMessage (message ("\"osc2\"", "3")), juce::String());
        settle (processor);

        expectEquals (plainOf (processor, "osc2_wavetable"), 4.0f);
        expectEquals (processor.getUserContent().getInfo (ContentSlot::osc2).source.name, juce::String ("Organ"));

        juce::var slots;
        expect (juce::JSON::parse (bridge.createContentSlots ("CURRENT", {}, {}), slots).wasOk());
        expectEquals (static_cast<int> (slots["slots"][1]["builtIn"]), 3, "the slot does not say which table");
        expectEquals (slots["slots"][1]["origin"].toString(), juce::String ("builtin"));
        expectEquals (static_cast<int> (slots["slots"][0]["builtIn"]), 0);

        const auto refusedAs = [this] (const juce::String& text, ui::BridgeErrorCode code, const juce::String& what)
        {
            const auto result = ui::parseMessage (text);
            expect (! result.ok && result.error == code, what);
        };

        using ui::BridgeErrorCode;
        refusedAs (message ("\"noise\"", "1"), BridgeErrorCode::invalidContentSlot, "the noise slot");
        refusedAs (message ("\"osc3\"", "1"), BridgeErrorCode::invalidContentSlot, "a slot that is not one");
        refusedAs (message ("\"osc1\"", "0"), BridgeErrorCode::unknownContent, "table 0");
        refusedAs (message ("\"osc1\"", juce::String (dsp::numExtraTables + 1)), BridgeErrorCode::unknownContent,
                   "a table past the last");
        refusedAs (message ("\"osc1\"", "1.5"), BridgeErrorCode::unknownContent, "table 1.5");
        refusedAs (message ("\"osc1\"", "\"Analog\""), BridgeErrorCode::malformedMessage, "a table by name");

        bridge.setUserContent (nullptr, nullptr);
    }
};

static ExtraTableTests extraTableTests;

} // namespace
