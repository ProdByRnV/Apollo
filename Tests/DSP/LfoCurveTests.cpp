/*
    Drawn LFO curves (Phase 13f-1, PRD §15.2): the curve, its table, the
    handoff to the audio thread, and the LFO shape that reads it.

      - the default curve is the built-in triangle, so choosing the drawn shape
        before drawing changes nothing; the LFO playing it matches the triangle;
      - validity is exactly what the engine can render, refused case by case;
      - a bend leaves a segment's ends where they are and moves its middle, the
        way its sign says;
      - two points at one position are a step, and the step happens at that
        position;
      - the table is the curve, at every entry;
      - a published curve reaches the reader whole, the reader's table is never
        written while it holds it, and nothing published means the default;
      - an LFO with no table is flat rather than reading through a null.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

#include "DSP/LFO/Lfo.h"
#include "DSP/LFO/LfoCurve.h"

using namespace apollo::dsp;

namespace
{

constexpr double rate = 48000.0;

[[nodiscard]] LfoCurve curveOf (std::initializer_list<LfoCurvePoint> points)
{
    LfoCurve curve;

    for (const auto& point : points)
        curve.points[static_cast<std::size_t> (curve.count++)] = point;

    return curve;
}

/** One cycle of an LFO at 1 Hz, retriggered, bipolar, unsmoothed. */
[[nodiscard]] std::vector<float> cycleOf (LfoShape shape, const float* table)
{
    LfoSettings settings;
    settings.shape = shape;
    settings.rateHz = 1.0f;
    settings.curveTable = table;

    Lfo lfo;
    lfo.prepare (rate);
    lfo.setSettings (settings);
    lfo.noteOn (0.0);

    std::vector<float> values (static_cast<std::size_t> (rate));

    for (auto& value : values)
        value = lfo.getNextValue();

    return values;
}

class LfoCurveTests final : public juce::UnitTest
{
public:
    LfoCurveTests()
        : juce::UnitTest ("Drawn LFO curves", "DSP")
    {
    }

    void runTest() override
    {
        testTheDefaultIsTheTriangle();
        testValidity();
        testBend();
        testAStepIsAStep();
        testTheTableIsTheCurve();
        testTheExchange();
        testNoTableIsFlat();
    }

private:
    void testTheDefaultIsTheTriangle()
    {
        beginTest ("The default curve is the triangle, and plays as one");

        const auto curve = defaultLfoCurve();
        expect (isValid (curve));

        LfoCurveTable table;
        render (curve, table);

        const auto drawn = cycleOf (LfoShape::custom, table.data());
        const auto triangle = cycleOf (LfoShape::triangle, nullptr);

        auto worst = 0.0f;

        for (std::size_t i = 0; i < drawn.size(); ++i)
            worst = std::max (worst, std::abs (drawn[i] - triangle[i]));

        logMessage ("  default curve against the triangle over a cycle: worst " + juce::String (worst, 9));
        expect (worst < 1.0e-5f, "the default curve does not play as the triangle");
    }

    void testValidity()
    {
        beginTest ("A curve is valid exactly when the engine can render it");

        expect (isValid (curveOf ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } })), "two points");

        expect (! isValid (curveOf ({ { 0.0f, 0.0f, 0.0f } })), "one point");
        expect (! isValid (curveOf ({ { 0.1f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } })), "a start after 0");
        expect (! isValid (curveOf ({ { 0.0f, 0.0f, 0.0f }, { 0.9f, 0.0f, 0.0f } })), "an end before 1");
        expect (! isValid (curveOf ({ { 0.0f, 0.0f, 0.0f }, { 0.6f, 0.0f, 0.0f }, { 0.4f, 0.0f, 0.0f },
                                      { 1.0f, 0.0f, 0.0f } })), "positions out of order");
        expect (! isValid (curveOf ({ { 0.0f, 1.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } })), "a value above 1");
        expect (! isValid (curveOf ({ { 0.0f, 0.0f, -2.0f }, { 1.0f, 0.0f, 0.0f } })), "a bend below -1");
        expect (! isValid (curveOf ({ { 0.0f, std::nanf (""), 0.0f }, { 1.0f, 0.0f, 0.0f } })), "NaN");

        LfoCurve tooMany;
        tooMany.count = maxLfoCurvePoints + 1;
        expect (! isValid (tooMany), "more points than the limit");

        // An invalid curve renders as silence, not as whatever it holds.
        LfoCurveTable table;
        table.fill (0.7f);
        render (curveOf ({ { 0.0f, 1.0f, 0.0f } }), table);
        expectEquals (table[100], 0.0f);
    }

    void testBend()
    {
        beginTest ("A bend keeps a segment's ends and moves its middle");

        expectEquals (bendSegment (0.0f, 0.8f), 0.0f);
        expectEquals (bendSegment (1.0f, -0.8f), 1.0f);
        expectEquals (bendSegment (0.5f, 0.0f), 0.5f);

        // Positive moves most of the way early; negative, late. At ±1 the
        // exponent is a quarter or four.
        expectWithinAbsoluteError (bendSegment (0.5f, 1.0f), std::pow (0.5f, 0.25f), 1.0e-6f);
        expectWithinAbsoluteError (bendSegment (0.5f, -1.0f), 0.0625f, 1.0e-6f);

        const auto rising = curveOf ({ { 0.0f, -1.0f, 0.6f }, { 1.0f, 1.0f, 0.0f } });
        expect (evaluate (rising, 0.5f) > 0.0f, "a positive bend did not lift the middle of a rising segment");
        expectEquals (evaluate (rising, 0.0f), -1.0f);
        expectEquals (evaluate (rising, 1.0f), 1.0f);
    }

    void testAStepIsAStep()
    {
        beginTest ("Two points at one position are a step at that position");

        const auto square = curveOf ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f },
                                       { 0.5f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f } });
        expect (isValid (square));

        expectEquals (evaluate (square, 0.25f), 1.0f);
        expectEquals (evaluate (square, 0.4999f), 1.0f);
        expectEquals (evaluate (square, 0.5f), -1.0f, "the step had not happened at its own position");
        expectEquals (evaluate (square, 0.75f), -1.0f);

        LfoCurveTable table;
        render (square, table);

        // Played, it is the built-in square but for the one table entry the
        // step is spread across.
        const auto drawn = cycleOf (LfoShape::custom, table.data());
        const auto built = cycleOf (LfoShape::square, nullptr);

        auto differing = 0;

        for (std::size_t i = 0; i < drawn.size(); ++i)
            differing += std::abs (drawn[i] - built[i]) > 1.0e-5f ? 1 : 0;

        const auto oneEntry = static_cast<int> (rate / lfoCurveTableSize) + 1;
        logMessage ("  samples where the drawn square differs from the built-in one: "
                    + juce::String (differing) + " (one table entry is " + juce::String (oneEntry) + ")");
        expect (differing <= oneEntry, "the drawn square differs by more than its step's width");
    }

    void testTheTableIsTheCurve()
    {
        beginTest ("Every entry of the table is the curve at that position");

        const auto curve = curveOf ({ { 0.0f, 0.0f, 0.4f }, { 0.2f, 0.9f, -0.7f }, { 0.55f, -0.3f, 0.0f },
                                      { 0.55f, 0.5f, 1.0f }, { 1.0f, -1.0f, 0.0f } });
        expect (isValid (curve));

        LfoCurveTable table;
        render (curve, table);

        auto worst = 0.0f;

        for (std::size_t i = 0; i < table.size(); ++i)
            worst = std::max (worst, std::abs (table[i] - evaluate (curve, static_cast<float> (i) / static_cast<float> (lfoCurveTableSize))));

        expectEquals (worst, 0.0f);
        expectEquals (table[static_cast<std::size_t> (lfoCurveTableSize)], -1.0f, "the entry past the end is not the end");
    }

    void testTheExchange()
    {
        beginTest ("A published curve reaches the reader whole, and the reader's table is left alone");

        LfoCurveExchange exchange;

        LfoCurveTable expectedDefault;
        render (defaultLfoCurve(), expectedDefault);

        const auto* first = exchange.acquire();
        expectEquals (first[256], expectedDefault[256], "before anything is published, the default");
        expect (exchange.acquire() == first, "acquiring with nothing new changed the table");

        const auto flat = curveOf ({ { 0.0f, 0.25f, 0.0f }, { 1.0f, 0.25f, 0.0f } });
        exchange.publish (flat);

        // The reader has not acquired yet, so the table it holds is unchanged.
        expectEquals (first[256], expectedDefault[256], "publishing wrote the table the reader holds");

        const auto* second = exchange.acquire();
        expect (second != first, "a new curve did not arrive as a new table");
        expectEquals (second[256], 0.25f);

        // Several publications before the reader looks: it gets the newest,
        // and never the table it is holding while they happen.
        for (int i = 1; i <= 5; ++i)
        {
            exchange.publish (curveOf ({ { 0.0f, 0.1f * static_cast<float> (i), 0.0f },
                                         { 1.0f, 0.1f * static_cast<float> (i), 0.0f } }));
            expectEquals (second[256], 0.25f, "a publication wrote the reader's table");
        }

        expectWithinAbsoluteError (exchange.acquire()[700], 0.5f, 1.0e-6f, "the newest curve did not win");
    }

    void testNoTableIsFlat()
    {
        beginTest ("The drawn shape with no table is flat, not a read through null");

        for (const auto value : cycleOf (LfoShape::custom, nullptr))
            if (value != 0.0f)
            {
                expect (false, "a custom LFO without a table moved");
                return;
            }
    }
};

LfoCurveTests lfoCurveTests;

} // namespace
