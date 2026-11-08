/*
    The macros as matrix destinations (Phase 18h, ADR-0126).

    A routing to a macro moves its knob, before the curve and range; whatever
    the macro is routed to follows; a patch without such a routing is
    evaluated exactly as before; a macro routed to a macro reads the other's
    knob as set, so there is no loop; and nonsense moves nothing. Then the
    same through the whole instrument, with nothing sounding and with a note.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <limits>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Modulation/MacroModulation.h"
#include "Engine/VoiceEngine.h"

using namespace apollo;
using dsp::ModDestination;
using dsp::ModSource;

namespace
{

[[nodiscard]] dsp::ModulationSlot slot (ModSource source, ModDestination destination, float depth)
{
    dsp::ModulationSlot s;
    s.source = source;
    s.destination = destination;
    s.depth = depth;
    return s;
}

[[nodiscard]] float at (const dsp::ModDestinationValues& values, ModDestination destination)
{
    return values[static_cast<std::size_t> (destination)];
}

class MacroModulationTests final : public juce::UnitTest
{
public:
    MacroModulationTests() : juce::UnitTest ("Macros as destinations", "DSP") {}

    void runTest() override
    {
        testTheList();
        testUnroutedIsUnchanged();
        testARoutingMovesTheKnob();
        testTheRangeHolds();
        testNoLoops();
        testNonsense();
        testInTheInstrument();
    }

private:
    void testTheList()
    {
        beginTest ("The four macros are appended, and the effect destinations are still the effects");

        // 35 when 18h appended them; the master, the envelopes and the LFOs
        // came after them in 19b and 19d, and none of these moved.
        expect (static_cast<int> (ModDestination::count) > 35);
        expectEquals (static_cast<int> (ModDestination::macro4), 34);
        expectEquals (static_cast<int> (ModDestination::macro1), 31);
        expectEquals (static_cast<int> (ModDestination::fxCompressorMix), 30, "a released number moved");
        expectEquals (dsp::numEffectDestinations, 6);
        expect (dsp::lastEffectDestination == ModDestination::fxCompressorMix);
        expectEquals (dsp::fullDepthRange (ModDestination::macro3), 1.0f, "a depth of 1 is the knob's whole travel");
    }

    void testUnroutedIsUnchanged()
    {
        beginTest ("With no routing to a macro, the matrix reads what it always read");

        dsp::ModulationRouting routing;
        routing.slots[0] = slot (ModSource::macro1, ModDestination::filter1Cutoff, 0.5f);
        routing.slots[1] = slot (ModSource::lfo1, ModDestination::osc1Level, -0.25f);
        routing.slots[2] = slot (ModSource::macro2, ModDestination::fxReverbMix, 1.0f);

        dsp::MacroInputs macros {};
        macros[0] = { 0.7f, { 0.1f, 0.9f, 0.4f } };
        macros[1] = { 0.3f, {} };

        dsp::ModSourceValues sources {};
        sources[static_cast<std::size_t> (ModSource::lfo1)] = 0.6f;

        dsp::ModDestinationValues actual {};
        dsp::evaluateModulationWithMacros (routing, sources, macros, actual);

        // The macros are what the processor used to hand over: each knob
        // through its shape. Within a millionth rather than exactly, because a
        // compiler may work out the test's own pow() of constants while it
        // compiles, exactly rounded, where the engine calls the library's.
        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (0)], dsp::shapeMacro (0.7f, macros[0].settings), 1.0e-6f);
        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (1)], 0.3f, 0.0f);
        expectEquals (sources[dsp::macroSourceIndex (2)], 0.0f);

        // And with them, one pass, as before: exactly what a single
        // evaluation of those sources gives.
        dsp::ModDestinationValues expected {};
        dsp::evaluateModulation (routing, sources, expected);

        expect (actual == expected, "a patch without a macro destination must be evaluated once, exactly as before");
    }

    void testARoutingMovesTheKnob()
    {
        beginTest ("A routing to a macro moves its knob, and what the macro drives follows");

        // An LFO at +0.5, at half depth, onto Macro 1's knob at 0.2: the knob
        // plays at 0.45, and Macro 1 drives the cutoff and the reverb's mix.
        dsp::ModulationRouting routing;
        routing.slots[0] = slot (ModSource::lfo1, ModDestination::macro1, 0.5f);
        routing.slots[1] = slot (ModSource::macro1, ModDestination::filter1Cutoff, 1.0f);
        routing.slots[2] = slot (ModSource::macro1, ModDestination::fxReverbMix, 0.5f);

        dsp::MacroInputs macros {};
        macros[0].knob = 0.2f;

        dsp::ModSourceValues sources {};
        sources[static_cast<std::size_t> (ModSource::lfo1)] = 0.5f;

        dsp::ModDestinationValues destinations {};
        dsp::evaluateModulationWithMacros (routing, sources, macros, destinations);

        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (0)], 0.45f, 1.0e-6f, "the macro the matrix read");
        expectWithinAbsoluteError (at (destinations, ModDestination::macro1), 0.25f, 1.0e-6f,
                                   "the offset on the macro itself, as the first pass found it");
        expectWithinAbsoluteError (at (destinations, ModDestination::filter1Cutoff), 0.45f * 8.0f, 1.0e-5f);
        expectWithinAbsoluteError (at (destinations, ModDestination::fxReverbMix), 0.45f * 0.5f, 1.0e-6f);

        // The other three are where their knobs are.
        expectEquals (sources[dsp::macroSourceIndex (1)], 0.0f);
    }

    void testTheRangeHolds()
    {
        beginTest ("A moved knob goes through the macro's curve and range, so the range still holds");

        dsp::ModulationRouting routing;
        routing.slots[0] = slot (ModSource::modWheel, ModDestination::macro2, 1.0f);

        dsp::MacroInputs macros {};
        macros[1] = { 0.5f, { 0.2f, 0.6f, 0.0f } };

        dsp::ModSourceValues sources {};
        sources[static_cast<std::size_t> (ModSource::modWheel)] = 1.0f;

        dsp::ModDestinationValues destinations {};
        dsp::evaluateModulationWithMacros (routing, sources, macros, destinations);
        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (1)], 0.6f, 1.0e-6f, "pushed past the top, it stops at the range's top");

        routing.slots[0].depth = -1.0f;
        dsp::evaluateModulationWithMacros (routing, sources, macros, destinations);
        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (1)], 0.2f, 1.0e-6f, "and at its bottom");

        // The curve shapes the moved knob as it shapes the knob: half way on a
        // curve of +1 is the fourth root of a half.
        macros[1] = { 0.25f, { 0.0f, 1.0f, 1.0f } };
        routing.slots[0].depth = 0.25f;
        dsp::evaluateModulationWithMacros (routing, sources, macros, destinations);
        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (1)], std::pow (0.5f, 0.25f), 1.0e-5f);
    }

    void testNoLoops()
    {
        beginTest ("A macro routed to a macro reads its knob as set, so two macros cannot feed back");

        dsp::ModulationRouting routing;
        routing.slots[0] = slot (ModSource::macro1, ModDestination::macro2, 1.0f);
        routing.slots[1] = slot (ModSource::macro2, ModDestination::macro1, 1.0f);
        routing.slots[2] = slot (ModSource::macro2, ModDestination::osc1Level, 1.0f);

        dsp::MacroInputs macros {};
        macros[0].knob = 0.3f;
        macros[1].knob = 0.1f;

        dsp::ModSourceValues sources {};
        dsp::ModDestinationValues destinations {};

        // Evaluated many times over, it lands in the same place every time.
        for (int pass = 0; pass < 5; ++pass)
        {
            dsp::evaluateModulationWithMacros (routing, sources, macros, destinations);

            expectWithinAbsoluteError (sources[dsp::macroSourceIndex (0)], 0.4f, 1.0e-6f, "Macro 1, moved by Macro 2's knob");
            expectWithinAbsoluteError (sources[dsp::macroSourceIndex (1)], 0.4f, 1.0e-6f, "Macro 2, moved by Macro 1's knob");
            expectWithinAbsoluteError (at (destinations, ModDestination::osc1Level), 0.4f, 1.0e-6f);
        }

        // A macro routed to itself is one routing as well.
        dsp::ModulationRouting self;
        self.slots[0] = slot (ModSource::macro3, ModDestination::macro3, 1.0f);
        macros[2].knob = 0.25f;
        dsp::evaluateModulationWithMacros (self, sources, macros, destinations);
        expectWithinAbsoluteError (sources[dsp::macroSourceIndex (2)], 0.5f, 1.0e-6f);
    }

    void testNonsense()
    {
        beginTest ("An offset that is not a number moves no macro");

        dsp::ModulationRouting routing;
        routing.slots[0] = slot (ModSource::lfo2, ModDestination::macro4, 1.0f);
        routing.slots[1] = slot (ModSource::macro4, ModDestination::osc2Level, 1.0f);

        dsp::MacroInputs macros {};
        macros[3].knob = 0.6f;

        for (const auto bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            dsp::ModSourceValues sources {};
            sources[static_cast<std::size_t> (ModSource::lfo2)] = bad;

            dsp::ModDestinationValues destinations {};
            dsp::evaluateModulationWithMacros (routing, sources, macros, destinations);

            expectEquals (sources[dsp::macroSourceIndex (3)], 0.6f);
            expectEquals (at (destinations, ModDestination::macro4), 0.0f);
            expectWithinAbsoluteError (at (destinations, ModDestination::osc2Level), 0.6f, 1.0e-6f);
        }
    }

    void testInTheInstrument()
    {
        beginTest ("In the instrument, the mod wheel on Macro 1 moves what Macro 1 drives, with or without a note");

        ApolloAudioProcessor processor;

        const auto setPlain = [&processor] (const juce::String& id, float plain)
        {
            if (auto* parameter = processor.getValueTreeState().getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
        };

        // The wheel onto Macro 1's knob; Macro 1, from 0.1, onto the reverb's
        // mix. A range of 0 to 0.5 on the macro.
        setPlain ("macro1_value", 0.1f);
        setPlain ("macro1_max", 0.5f);
        setPlain ("mod01_source", static_cast<float> (ModSource::modWheel));
        setPlain ("mod01_destination", static_cast<float> (ModDestination::macro1));
        setPlain ("mod01_depth", 1.0f);
        setPlain ("mod02_source", static_cast<float> (ModSource::macro1));
        setPlain ("mod02_destination", static_cast<float> (ModDestination::fxReverbMix));
        setPlain ("mod02_depth", 1.0f);

        constexpr int block = 256;
        processor.prepareToPlay (48000.0, block);
        auto& engine = processor.getVoiceEngine();
        juce::AudioBuffer<float> buffer (2, block);

        const auto run = [&] (juce::MidiBuffer midi)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            buffer.clear();
            juce::MidiBuffer none;
            processor.processBlock (buffer, none);
            return engine.getEffectOffset (ModDestination::fxReverbMix);
        };

        // The wheel at rest: the macro is its knob, 0.1 through 0 to 0.5.
        expectWithinAbsoluteError (run ({}), 0.05f, 1.0e-5f, "the macro at rest");

        // The wheel up: the knob is pushed to the top, and the range stops it
        // at 0.5.
        juce::MidiBuffer wheel;
        wheel.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
        expectWithinAbsoluteError (run (wheel), 0.5f, 1.0e-4f, "nothing sounding");

        // And with a note, through the voice.
        juce::MidiBuffer note;
        note.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        expectWithinAbsoluteError (run (note), 0.5f, 1.0e-4f, "a note sounding");

        // The routing undone: back to the knob.
        setPlain ("mod01_depth", 0.0f);
        expectWithinAbsoluteError (run ({}), 0.05f, 1.0e-5f, "the routing removed");

        processor.releaseResources();
    }
};

MacroModulationTests macroModulationTests;

} // namespace
