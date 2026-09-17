#include "Regression/RenderCase.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterLayout.h"
#include "Resources/FactoryPresets.h"
#include "State/PresetDocument.h"

namespace apollo::regression
{

namespace
{

constexpr double standardRate = 48000.0;
constexpr int standardBlock = 512;

/*
    The shape of a preset case.

    A note held for eight tenths of a second and then released, over a render of
    one and six tenths. Both halves matter and the split is deliberate: the
    first half is the attack and the body of the sound, the second is the
    release and whatever the effects are still doing afterwards. A render that
    stopped at the note-off would be blind to a release that changed length and
    to a delay that stopped repeating.

    Sixteen segments over 1.6 s is a hundred milliseconds each, which resolves
    an attack, a decay and a release distinctly for every sound in the library
    except the pad — and the pad is given twice the render for exactly that
    reason.
*/
constexpr double holdSeconds = 0.8;
constexpr double caseSeconds = 1.6;

//==============================================================================
// The factory presets, each played the way its description implies.

constexpr NoteEvent bassNote[] {
    { 0.0, 33, 0.95f, true },
    { holdSeconds, 33, 0.0f, false },
};

constexpr NoteEvent lowNote[] {
    { 0.0, 36, 0.85f, true },
    { holdSeconds, 36, 0.0f, false },
};

constexpr NoteEvent midNote[] {
    { 0.0, 57, 0.80f, true },
    { holdSeconds, 57, 0.0f, false },
};

constexpr NoteEvent keyNote[] {
    { 0.0, 60, 0.65f, true },
    { holdSeconds, 60, 0.0f, false },
};

constexpr NoteEvent brightNote[] {
    { 0.0, 69, 1.00f, true },
    { holdSeconds, 69, 0.0f, false },
};

// The pad is held longer because it takes 1.2 s to arrive. Releasing it before
// it got there would fingerprint the attack twice and the sound never.
constexpr NoteEvent padNote[] {
    { 0.0, 48, 0.75f, true },
    { 1.6, 48, 0.0f, false },
};

//==============================================================================
// The cases that are not presets.

/** Four notes, two of them released early: voice allocation, in one render. */
constexpr NoteEvent chord[] {
    { 0.00, 48, 0.90f, true },
    { 0.05, 52, 0.80f, true },
    { 0.10, 55, 0.70f, true },
    { 0.15, 59, 0.60f, true },
    { 0.60, 52, 0.0f, false },
    { 0.60, 59, 0.0f, false },
    { 1.00, 48, 0.0f, false },
    { 1.00, 55, 0.0f, false },
};

/** The modulation matrix doing something audible.

    Nothing in the factory library routes an LFO to a cutoff at a depth that
    dominates the sound, so nothing in the library would notice if the matrix
    stopped applying depth correctly at the extremes.
*/
constexpr Setting modulated[] {
    { "osc1_level", 0.85f },
    { "osc1_position", 0.55f },

    { "filter1_type", 1.0f },   // low pass
    { "filter1_cutoff", 900.0f },
    { "filter1_resonance", 3.0f },

    { "env1_attack", 5.0f },
    { "env1_sustain", 1.0f },
    { "env1_release", 250.0f },

    // Envelope 2 moves the wavetable position; the LFO sweeps the filter.
    { "env2_attack", 10.0f },
    { "env2_decay", 500.0f },
    { "env2_sustain", 0.20f },

    { "lfo1_shape", 0.0f },     // sine
    { "lfo1_rate", 4.5f },

    { "mod01_source", 5.0f },   // LFO 1
    { "mod01_destination", 12.0f }, // filter 1 cutoff
    { "mod01_depth", 0.80f },

    { "mod02_source", 2.0f },   // envelope 2
    { "mod02_destination", 4.0f },  // oscillator 1 position
    { "mod02_depth", 0.60f },
};

/** A wide stereo image, which a fingerprint of the mono sum could not see.

    Unison spread across seven voices, a ping-pong delay and a reverb at full
    width. The side channel of this case is the only place in the set where
    those three are measured together.
*/
constexpr Setting wideStereo[] {
    { "osc1_level", 0.80f },
    { "osc1_unison", 7.0f },
    { "osc1_detune", 0.30f },
    { "osc1_spread", 1.00f },

    { "env1_attack", 3.0f },
    { "env1_sustain", 0.85f },
    { "env1_release", 120.0f },

    { "fx_slot1", 2.0f },       // delay
    { "fx_delay_time", 120.0f },
    { "fx_delay_feedback", 0.55f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.50f },

    { "fx_slot2", 3.0f },       // reverb
    { "fx_reverb_width", 1.00f },
    { "fx_reverb_mix", 0.35f },
};

constexpr std::array<RenderCase, 15> cases { {
    //==========================================================================
    // The library, in the order it is declared rather than the order the
    // browser shows, so that Init — the patch every other sound is a departure
    // from — is the first thing a reader of the goldens meets.

    { "Init", "The registry's defaults. Any change to a default moves this and nothing else.",
      "Init", {}, midNote, standardRate, standardBlock, caseSeconds },

    { "Sub Weight", "The sub oscillator carrying a bass note, and the equaliser under it.",
      "Sub Weight", {}, bassNote, standardRate, standardBlock, caseSeconds },

    { "Rasp Bass", "Diode distortion, the gate and the compressor, all in one chain.",
      "Rasp Bass", {}, bassNote, standardRate, standardBlock, caseSeconds },

    { "Hollow Saw Lead", "Seven-voice unison, two oscillators an octave apart, an envelope on the filter.",
      "Hollow Saw Lead", {}, midNote, standardRate, standardBlock, caseSeconds },

    { "Wide Stab", "Fourteen unison voices through both filters in series.",
      "Wide Stab", {}, midNote, standardRate, standardBlock, caseSeconds },

    { "Slow Bloom", "The long envelope, rendered long enough to reach the top of it.",
      "Slow Bloom", {}, padNote, standardRate, standardBlock, 3.2 },

    { "Glass Bell", "Two sines a nineteenth apart with independent decays.",
      "Glass Bell", {}, keyNote, standardRate, standardBlock, caseSeconds },

    { "Square Keys", "A square and a sub, with velocity opening the filter.",
      "Square Keys", {}, keyNote, standardRate, standardBlock, caseSeconds },

    { "Wire Pluck", "A short bright transient and a delay behind it.",
      "Wire Pluck", {}, brightNote, standardRate, standardBlock, caseSeconds },

    { "Dust Sweep", "The noise generator through a resonant band pass, swept by an LFO.",
      "Dust Sweep", {}, lowNote, standardRate, standardBlock, caseSeconds },

    //==========================================================================
    // What the library does not cover.

    { "Chord", "Four notes at once and two released early — voice allocation and release overlap.",
      "", {}, chord, standardRate, standardBlock, caseSeconds },

    { "Modulated sweep", "The modulation matrix at a depth that dominates the sound.",
      "", modulated, midNote, standardRate, standardBlock, caseSeconds },

    { "Wide stereo", "Unison spread, a ping-pong delay and a reverb at full width.",
      "", wideStereo, midNote, standardRate, standardBlock, caseSeconds },

    // The same sound as the Wire Pluck case, rendered differently. Its golden
    // is its own, because the two are not required to be sample-identical —
    // what is required is that each stays where it was.
    { "Wire Pluck, 64-sample blocks",
      "A block size a tenth of the usual one: per-block work and event timing.",
      "Wire Pluck", {}, brightNote, standardRate, 64, caseSeconds },

    { "Hollow Saw Lead, 44.1 kHz",
      "A sample rate other than the development one: every coefficient that derives from it.",
      "Hollow Saw Lead", {}, midNote, 44100.0, standardBlock, caseSeconds },
} };

/** @returns the factory preset called @p name, or nullptr. */
[[nodiscard]] const resources::FactoryPreset* factoryPresetNamed (std::string_view name)
{
    for (const auto& preset : resources::factoryPresets())
        if (preset.name == name)
            return &preset;

    return nullptr;
}

} // namespace

std::span<const RenderCase> renderCases()
{
    return { cases.data(), cases.size() };
}

juce::String render (const RenderCase& renderCase, juce::AudioBuffer<float>& destination)
{
    const auto where = params::toJuceString (renderCase.name) + ": ";

    ApolloAudioProcessor processor;
    auto& apvts = processor.getValueTreeState();

    //==========================================================================
    // The starting patch.

    if (! renderCase.preset.empty())
    {
        const auto* preset = factoryPresetNamed (renderCase.preset);

        if (preset == nullptr)
            return where + "no factory preset called " + params::toJuceString (renderCase.preset);

        if (presets::read (apvts, resources::render (*preset)) != state::StateLoadResult::ok)
            return where + "the reader refused the factory preset";
    }

    //==========================================================================
    // What the case moves away from it.

    for (const auto& setting : renderCase.settings)
    {
        const auto id = params::toJuceString (setting.id);
        auto* parameter = apvts.getParameter (id);

        if (parameter == nullptr)
            return where + id + " is not a registered parameter";

        const auto normalised = parameter->convertTo0to1 (setting.value);

        // convertTo0to1 clamps, so a value outside the range would arrive
        // silently as an end stop and the case would quietly stop testing what
        // it says it tests.
        if (std::abs (parameter->convertFrom0to1 (normalised) - setting.value) > 1.0e-3f)
            return where + id + " does not accept " + juce::String (setting.value);

        parameter->setValueNotifyingHost (normalised);
    }

    //==========================================================================
    // The render.

    processor.prepareToPlay (renderCase.sampleRate, renderCase.blockSize);

    const auto totalSamples = static_cast<int> (std::llround (renderCase.seconds * renderCase.sampleRate));
    const auto blocks = (totalSamples + renderCase.blockSize - 1) / renderCase.blockSize;

    destination.setSize (2, blocks * renderCase.blockSize, false, true, false);
    destination.clear();

    juce::AudioBuffer<float> block (2, renderCase.blockSize);

    for (int index = 0; index < blocks; ++index)
    {
        const auto start = index * renderCase.blockSize;

        block.clear();

        juce::MidiBuffer midi;

        for (const auto& event : renderCase.events)
        {
            const auto position = static_cast<int> (std::llround (event.seconds * renderCase.sampleRate));

            if (position < start || position >= start + renderCase.blockSize)
                continue;

            const auto offset = position - start;

            midi.addEvent (event.on ? juce::MidiMessage::noteOn (1, event.note, event.velocity)
                                    : juce::MidiMessage::noteOff (1, event.note),
                           offset);
        }

        processor.processBlock (block, midi);

        for (int channel = 0; channel < 2; ++channel)
            destination.copyFrom (channel, start, block, channel, 0, renderCase.blockSize);
    }

    processor.releaseResources();

    return {};
}

} // namespace apollo::regression
