/*
    Drawn LFO curves through the instrument (Phase 13f-1): kept, saved, loaded,
    set from the page, and heard.

      - a curve set on the store is the curve read back, written into the state
        tree, and refused unchanged when it is not valid;
      - an untouched patch writes no curves at all, so its document is the one
        it always was;
      - a host save and load carries a drawn curve; a preset carries it too, and
        a preset without one puts the LFO back on the default;
      - a hand-edited document is repaired where it can be and defaulted where
        it cannot;
      - the page's command sets a curve and is answered with all four; a curve
        that is not one is refused with its own error and changes nothing;
      - the drawn shape is heard: the default curve sounds as the triangle, a
        drawn curve sounds as drawn, and a curve set while a note plays is heard
        from the next block.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Modulation/ModulationTypes.h"
#include "Resources/FactoryPresets.h"
#include "State/LfoCurveState.h"
#include "State/PresetDocument.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

[[nodiscard]] dsp::LfoCurve curveOf (std::initializer_list<dsp::LfoCurvePoint> points)
{
    dsp::LfoCurve curve;

    for (const auto& point : points)
        curve.points[static_cast<std::size_t> (curve.count++)] = point;

    return curve;
}

/** A ramp that rests at the top with a bend on the way up: unlike the default
    in every way a test can see. */
[[nodiscard]] dsp::LfoCurve drawnCurve()
{
    return curveOf ({ { 0.0f, -1.0f, 0.5f }, { 0.3f, 1.0f, 0.0f }, { 0.8f, 1.0f, 0.0f }, { 1.0f, 0.2f, 0.0f } });
}

/** LFO 1, in @p shape, turning oscillator 1 down by its whole level. */
void lfoOnLevel (ApolloAudioProcessor& processor, float shape)
{
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    setPlain (processor, "lfo1_shape", shape);
    setPlain (processor, "lfo1_rate", 5.0f);
    setPlain (processor, "mod01_source", static_cast<float> (dsp::ModSource::lfo1));
    setPlain (processor, "mod01_destination", static_cast<float> (dsp::ModDestination::osc1Level));
    setPlain (processor, "mod01_depth", -0.5f);
}

/** A held note's left channel, @p blocks long; @p between runs before each
    block, with the block's index. */
[[nodiscard]] std::vector<float> play (ApolloAudioProcessor& processor, int blocks,
                                       const std::function<void (int)>& between = {})
{
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < blocks; ++b)
    {
        if (between)
            between (b);

        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

[[nodiscard]] float worstDifference (const std::vector<float>& a, const std::vector<float>& b,
                                     std::size_t from = 0, std::size_t to = 0)
{
    to = to == 0 ? std::min (a.size(), b.size()) : to;
    auto worst = 0.0f;

    for (auto i = from; i < to; ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));

    return worst;
}

[[nodiscard]] juce::String setCurveMessage (int lfo, const juce::String& points)
{
    return "{\"type\":\"setLfoCurve\",\"version\":1,\"lfo\":" + juce::String (lfo) + ",\"points\":" + points + "}";
}

class LfoCurveStateTests final : public juce::UnitTest
{
public:
    LfoCurveStateTests()
        : juce::UnitTest ("Drawn LFO curves in the instrument", "State")
    {
    }

    void runTest() override
    {
        testTheStore();
        testAnUntouchedPatchWritesNothing();
        testAHostSaveCarriesACurve();
        testAPresetCarriesACurve();
        testAHandEditedDocument();
        testTheCommands();
        testTheParser();
        testTheDefaultSoundsAsTheTriangle();
        testADrawnCurveIsHeard();
        testACurveChangedWhilePlaying();
    }

private:
    void testTheStore()
    {
        beginTest ("A curve set is the curve read back, and in the state tree");

        ApolloAudioProcessor processor;
        auto& store = processor.getLfoCurves();

        expect (store.get (2) == dsp::defaultLfoCurve(), "a fresh LFO is not on the default curve");
        expect (store.set (2, drawnCurve()));
        expect (store.get (2) == drawnCurve());
        expect (store.get (0) == dsp::defaultLfoCurve(), "setting LFO 3 moved LFO 1");

        const auto stored = processor.getValueTreeState().state.getChildWithName (state::LfoCurveStore::curvesTreeType);
        expect (stored.isValid() && stored.getNumChildren() == 1, "the tree does not hold exactly the drawn curve");
        expectEquals (static_cast<int> (stored.getChild (0).getProperty (state::LfoCurveStore::lfoProperty)), 3);
        expectEquals (stored.getChild (0).getNumChildren(), 4);

        expect (! store.set (4, drawnCurve()), "an LFO that does not exist was accepted");
        expect (! store.set (-1, drawnCurve()));
        expect (! store.set (1, curveOf ({ { 0.0f, 0.0f, 0.0f } })), "an invalid curve was accepted");
        expect (store.get (1) == dsp::defaultLfoCurve(), "a refused curve changed something");
    }

    void testAnUntouchedPatchWritesNothing()
    {
        beginTest ("A patch nobody drew on saves no curves at all");

        ApolloAudioProcessor processor;
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
        expect (xml != nullptr && xml->getChildByName (state::LfoCurveStore::curvesTreeType) == nullptr,
                "an untouched patch wrote curves");

        // And drawing the default back is the same as never drawing.
        expect (processor.getLfoCurves().set (0, drawnCurve()));
        expect (processor.getLfoCurves().set (0, dsp::defaultLfoCurve()));
        expect (! processor.getValueTreeState().state.getChildWithName (state::LfoCurveStore::curvesTreeType).isValid());
    }

    void testAHostSaveCarriesACurve()
    {
        beginTest ("A host save and load carries a drawn curve");

        juce::MemoryBlock saved;

        {
            ApolloAudioProcessor processor;
            expect (processor.getLfoCurves().set (1, drawnCurve()));
            processor.getStateInformation (saved);
        }

        ApolloAudioProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expect (restored.getLfoCurves().get (1) == drawnCurve(), "the curve did not come back");
        expect (restored.getLfoCurves().get (0) == dsp::defaultLfoCurve());

        // A project saved before curves existed puts a drawn LFO back on the
        // default rather than leaving the drawing in place.
        juce::MemoryBlock plain;
        ApolloAudioProcessor { }.getStateInformation (plain);

        restored.setStateInformation (plain.getData(), static_cast<int> (plain.getSize()));
        expect (restored.getLfoCurves().get (1) == dsp::defaultLfoCurve(), "an older project kept the drawing");
    }

    void testAPresetCarriesACurve()
    {
        beginTest ("A preset carries a drawn curve, and one without puts the default back");

        ApolloAudioProcessor processor;
        expect (processor.getLfoCurves().set (3, drawnCurve()));

        presets::Metadata metadata;
        metadata.name = "Drawn";
        const auto text = presets::write (processor.getValueTreeState(), metadata);
        expect (text.contains (state::LfoCurveStore::curvesTreeType), "the preset left the curve out");

        ApolloAudioProcessor other;
        expect (presets::read (other.getValueTreeState(), text) == state::StateLoadResult::ok);
        other.notifyStateReplaced();
        expect (other.getLfoCurves().get (3) == drawnCurve(), "the preset's curve did not load");

        // A factory preset carries no curves, so loading one clears the drawing.
        const auto factory = resources::factoryPresets();
        expect (! factory.empty());
        expect (presets::read (other.getValueTreeState(), resources::render (factory.front()))
                == state::StateLoadResult::ok);
        other.notifyStateReplaced();
        expect (other.getLfoCurves().get (3) == dsp::defaultLfoCurve(), "a factory preset kept the drawing");
    }

    void testAHandEditedDocument()
    {
        beginTest ("A hand-edited document is repaired where it can be and defaulted where it cannot");

        ApolloAudioProcessor processor;
        auto root = processor.getValueTreeState().state;

        juce::ValueTree curves (state::LfoCurveStore::curvesTreeType);

        const auto addCurve = [&curves] (const juce::var& lfo, std::initializer_list<std::array<double, 3>> points)
        {
            juce::ValueTree curve (state::LfoCurveStore::curveTreeType);
            curve.setProperty (state::LfoCurveStore::lfoProperty, lfo, nullptr);

            for (const auto& p : points)
            {
                juce::ValueTree point (state::LfoCurveStore::pointTreeType);
                point.setProperty (state::LfoCurveStore::xProperty, p[0], nullptr);
                point.setProperty (state::LfoCurveStore::yProperty, p[1], nullptr);
                point.setProperty (state::LfoCurveStore::bendProperty, p[2], nullptr);
                curve.appendChild (point, nullptr);
            }

            curves.appendChild (curve, nullptr);
        };

        // Out of order, out of range, and ends short of the cycle: repairable.
        addCurve (1, { { 0.7, 3.0, 0.0 }, { 0.1, 0.5, -9.0 }, { 0.4, -0.2, 0.0 } });
        // One point: not a curve.
        addCurve (2, { { 0.5, 0.5, 0.0 } });
        // An LFO that does not exist.
        addCurve (9, { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } });

        root.appendChild (curves, nullptr);
        (void) processor.getLfoCurves().restoreFromState();

        const auto repaired = processor.getLfoCurves().get (0);
        expect (dsp::isValid (repaired), "the repair is not a valid curve");
        expectEquals (repaired.count, 3);
        expectEquals (repaired.points[0].x, 0.0f, "the start was not pinned to the cycle's start");
        expectEquals (repaired.points[0].bend, -1.0f, "a bend was not clamped");
        expectEquals (repaired.points[1].y, -0.2f, "the points were not sorted");
        expectEquals (repaired.points[2].x, 1.0f, "the end was not pinned to the cycle's end");
        expectEquals (repaired.points[2].y, 1.0f, "a value was not clamped");

        expect (processor.getLfoCurves().get (1) == dsp::defaultLfoCurve(), "a single point was not defaulted");
    }

    void testTheCommands()
    {
        beginTest ("The page sets a curve and is answered with all four; a bad one changes nothing");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setLfoCurves (&processor.getLfoCurves());

        const auto reply = bridge.handleMessage (setCurveMessage (2, "[[0,0.5,0],[0.25,-0.75,0.5],[1,0.5,0]]"));

        juce::var parsed;
        expect (juce::JSON::parse (reply, parsed).wasOk());
        expectEquals (parsed["type"].toString(), juce::String ("lfoCurves"));

        const auto* curves = parsed["curves"].getArray();
        expect (curves != nullptr && curves->size() == 4, "the answer is not all four curves");

        if (curves != nullptr && curves->size() == 4)
        {
            const auto& second = curves->getReference (1);
            expectEquals (static_cast<int> (second["lfo"]), 2);
            expectEquals (second["points"].size(), 3);
            expectWithinAbsoluteError (static_cast<double> (second["points"][1][1]), -0.75, 1.0e-9);
        }

        const auto stored = processor.getLfoCurves().get (1);
        expectEquals (stored.count, 3);
        expectEquals (stored.points[1].bend, 0.5f);

        const auto refused = bridge.handleMessage (setCurveMessage (2, "[[0,0,0],[0.8,0,0],[0.5,0,0],[1,0,0]]"));
        expect (refused.contains ("INVALID_LFO_CURVE"), "an unordered curve was not refused by name");
        expect (processor.getLfoCurves().get (1) == stored, "a refused curve changed the stored one");

        expect (bridge.handleMessage ("{\"type\":\"requestLfoCurves\",\"version\":1}").contains ("\"lfoCurves\""));

        // Without a store, the commands are refused rather than ignored.
        ui::ParameterBridge headless (processor.getValueTreeState());
        expect (headless.handleMessage ("{\"type\":\"requestLfoCurves\",\"version\":1}").contains ("\"error\""));
    }

    void testTheParser()
    {
        beginTest ("The parser refuses anything that is not a curve for an LFO that exists");

        const auto good = "[[0,0,0],[1,0,0]]";
        expect (ui::parseMessage (setCurveMessage (1, good)).ok);
        expect (ui::parseMessage (setCurveMessage (4, good)).ok);

        const auto refusedAs = [this] (const juce::String& message, ui::BridgeErrorCode code, const juce::String& what)
        {
            const auto result = ui::parseMessage (message);
            expect (! result.ok && result.error == code, what);
        };

        using ui::BridgeErrorCode;
        refusedAs (setCurveMessage (0, good), BridgeErrorCode::invalidLfoCurve, "LFO 0");
        refusedAs (setCurveMessage (5, good), BridgeErrorCode::invalidLfoCurve, "LFO 5");
        refusedAs ("{\"type\":\"setLfoCurve\",\"version\":1,\"lfo\":1.5,\"points\":[[0,0,0],[1,0,0]]}",
                   BridgeErrorCode::invalidLfoCurve, "LFO 1.5");
        refusedAs ("{\"type\":\"setLfoCurve\",\"version\":1,\"lfo\":1}", BridgeErrorCode::malformedMessage, "no points");
        refusedAs (setCurveMessage (1, "[[0,0,0]]"), BridgeErrorCode::invalidLfoCurve, "one point");
        refusedAs (setCurveMessage (1, "[[0,0],[1,0,0]]"), BridgeErrorCode::invalidLfoCurve, "a pair, not a triple");
        refusedAs (setCurveMessage (1, "[[0,\"a\",0],[1,0,0]]"), BridgeErrorCode::invalidLfoCurve, "a string");
        refusedAs (setCurveMessage (1, "[[0,2,0],[1,0,0]]"), BridgeErrorCode::invalidLfoCurve, "a value of 2");
        refusedAs (setCurveMessage (1, "[[0.1,0,0],[1,0,0]]"), BridgeErrorCode::invalidLfoCurve, "a late start");

        juce::String many = "[[0,0,0]";

        for (int i = 1; i < dsp::maxLfoCurvePoints; ++i)
            many << ",[" << juce::String (static_cast<double> (i) / dsp::maxLfoCurvePoints) << ",0,0]";

        expect (ui::parseMessage (setCurveMessage (1, many + ",[1,0,0]]")).ok == false, "more points than the limit");
    }

    void testTheDefaultSoundsAsTheTriangle()
    {
        beginTest ("The drawn shape on its default curve sounds as the triangle");

        ApolloAudioProcessor drawn;
        lfoOnLevel (drawn, static_cast<float> (dsp::LfoShape::custom));

        ApolloAudioProcessor triangle;
        lfoOnLevel (triangle, static_cast<float> (dsp::LfoShape::triangle));

        const auto a = play (drawn, 40);
        const auto b = play (triangle, 40);

        const auto worst = worstDifference (a, b);
        logMessage ("  default curve against the triangle, through the processor: worst " + juce::String (worst, 9));
        expect (worst < 1.0e-5f, "the default curve does not sound as the triangle");
    }

    void testADrawnCurveIsHeard()
    {
        beginTest ("A drawn curve is heard as drawn");

        const auto render = [this] (const dsp::LfoCurve& curve)
        {
            ApolloAudioProcessor processor;
            lfoOnLevel (processor, static_cast<float> (dsp::LfoShape::custom));
            expect (processor.getLfoCurves().set (0, curve));
            return play (processor, 40);
        };

        // Flat curves are constants, so the level they leave is the level a
        // constant routing would: at -1 the routing adds half the level back
        // (depth -0.5), at +1 it takes half away.
        const auto low = render (curveOf ({ { 0.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f } }));
        const auto high = render (curveOf ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }));
        const auto shaped = render (drawnCurve());

        const auto rms = [] (const std::vector<float>& signal)
        {
            double sum = 0.0;

            for (std::size_t i = signal.size() / 2; i < signal.size(); ++i)
                sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

            return std::sqrt (sum / static_cast<double> (signal.size() / 2));
        };

        logMessage ("  held level with the curve flat at -1: " + juce::String (rms (low), 4) + "; flat at +1: "
                    + juce::String (rms (high), 4) + "; drawn: " + juce::String (rms (shaped), 4));

        expect (rms (high) < rms (low) * 0.75, "the curve's value did not reach the level");
        expect (worstDifference (shaped, low) > 0.01f && worstDifference (shaped, high) > 0.01f,
                "the drawn curve sounds like one of the flat ones");
    }

    void testACurveChangedWhilePlaying()
    {
        beginTest ("A curve set while a note plays is heard from the next block");

        const auto flatAt = [] (float y) { return curveOf ({ { 0.0f, y, 0.0f }, { 1.0f, y, 0.0f } }); };

        ApolloAudioProcessor changing;
        lfoOnLevel (changing, static_cast<float> (dsp::LfoShape::custom));
        expect (changing.getLfoCurves().set (0, flatAt (-1.0f)));
        const auto changed = play (changing, 30, [&changing, &flatAt] (int b)
        {
            if (b == 20)
                (void) changing.getLfoCurves().set (0, flatAt (1.0f));
        });

        ApolloAudioProcessor steady;
        lfoOnLevel (steady, static_cast<float> (dsp::LfoShape::custom));
        expect (steady.getLfoCurves().set (0, flatAt (-1.0f)));
        const auto unchanged = play (steady, 30);

        const auto before = worstDifference (changed, unchanged, 0, 20 * block);
        const auto after = worstDifference (changed, unchanged, 20 * block, 30 * block);

        logMessage ("  against a steady curve: before the change " + juce::String (before, 9) + ", after "
                    + juce::String (after, 4));

        expectEquals (before, 0.0f, "the change was heard before it was made");
        expect (after > 0.01f, "the change was not heard");
    }
};

LfoCurveStateTests lfoCurveStateTests;

} // namespace
