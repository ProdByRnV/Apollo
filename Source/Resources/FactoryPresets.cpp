#include "Resources/FactoryPresets.h"

#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "State/StateSerialization.h"

#include <algorithm>
#include <cmath>

namespace apollo::resources
{

namespace
{

/*
    A note on the numbers below.

    Discrete parameters are written as the integer the interface shows, and the
    tables they index live in one place — `WebUI/src/params/labels.ts` — so the
    comments here name the position rather than restating the list:

      wavetable    0 SIN-SAW   1 SIN-SQR   2 TRI-SAW   3 SAW-SQR
      filter type  0 off  1 low pass  2 high pass  3 band pass  4 notch
      LFO shape    0 sine  1 tri  2 saw  3 rev saw  4 square  5 S&H  6 step
      mod source   1 env 1 .. 4 env 4, 5 LFO 1 .. 8 LFO 4, 9 velocity,
                   10 key track, 11 mod wheel, 12 bend, 13 aftertouch,
                   14 random, 15 timbre
      mod dest     4 osc 1 position, 6 osc 1 level, 7 osc 2 level,
                   12 filter 1 cutoff, 13 filter 1 resonance, 16 amplitude
      fx slot      1 distortion  2 delay  3 reverb  4 gate  5 compressor  6 EQ
      bypass       0 on, 1 off

    Every effect ships with its mix at zero, so a preset that puts an effect in
    a slot and does not raise its mix has put a silent effect in a slot. Each
    one below says how much of itself it wants.
*/

constexpr FactorySetting sawLead[] {
    { "osc1_position", 0.90f },
    { "osc1_unison", 7.0f },
    { "osc1_detune", 0.28f },
    { "osc1_spread", 0.75f },

    // A second oscillator an octave down, quieter, for weight under the lead
    // rather than a second lead.
    { "osc2_position", 0.85f },
    { "osc2_level", 0.45f },
    { "osc2_unison", 3.0f },
    { "osc2_detune", 0.15f },
    { "osc2_semitones", -12.0f },

    { "filter1_cutoff", 6500.0f },
    { "filter1_resonance", 1.40f },
    { "filter1_drive", 0.25f },

    { "env1_attack", 8.0f },
    { "env1_decay", 600.0f },
    { "env1_sustain", 0.75f },
    { "env1_release", 180.0f },

    // Envelope 2 exists to move the filter. It is the shape a lead gets its
    // attack from, and it is separate from the amplitude envelope so that the
    // note can stay up while the brightness comes down.
    { "env2_attack", 2.0f },
    { "env2_decay", 320.0f },
    { "env2_sustain", 0.15f },
    { "env2_release", 200.0f },

    { "mod01_source", 2.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.45f },
    { "mod02_source", 9.0f }, { "mod02_destination", 16.0f }, { "mod02_depth", 0.35f },
    { "mod03_source", 11.0f }, { "mod03_destination", 4.0f }, { "mod03_depth", 0.30f },

    { "fx_slot1", 1.0f },
    { "fx_distortion_drive", 10.0f },
    { "fx_distortion_tone", 9000.0f },
    { "fx_distortion_mix", 0.35f },
    { "fx_distortion_output", -2.0f },

    { "fx_slot2", 2.0f },
    { "fx_delay_sync", 1.0f },
    { "fx_delay_division", 8.0f },
    { "fx_delay_feedback", 0.32f },
    { "fx_delay_damping", 6500.0f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.22f },

    { "fx_slot3", 3.0f },
    { "fx_reverb_mode", 0.0f },
    { "fx_reverb_size", 0.35f },
    { "fx_reverb_decay", 1400.0f },
    { "fx_reverb_mix", 0.15f },

    { "master_gain", -2.0f },
};

constexpr FactorySetting subWeight[] {
    { "osc1_wavetable", 1.0f },
    { "osc1_position", 0.15f },
    { "osc1_level", 0.85f },

    // The sub is the point of this one: an octave below, nearly as loud as the
    // oscillator above it.
    { "sub_level", 0.90f },

    { "filter1_cutoff", 420.0f },
    { "filter1_resonance", 0.90f },
    { "filter1_drive", 0.35f },

    { "env1_attack", 2.0f },
    { "env1_decay", 450.0f },
    { "env1_sustain", 0.55f },
    { "env1_release", 110.0f },

    { "env2_attack", 1.0f },
    { "env2_decay", 140.0f },
    { "env2_release", 120.0f },

    { "mod01_source", 2.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.50f },
    { "mod02_source", 9.0f }, { "mod02_destination", 12.0f }, { "mod02_depth", 0.25f },

    { "fx_slot1", 5.0f },
    { "fx_compressor_threshold", -20.0f },
    { "fx_compressor_ratio", 4.0f },
    { "fx_compressor_attack", 6.0f },
    { "fx_compressor_release", 90.0f },
    { "fx_compressor_makeup", 4.0f },

    // A high pass steep enough to take the inaudible rumble off without
    // touching the note, and a shelf to keep the top out of the way of
    // everything else in a mix.
    { "fx_slot2", 6.0f },
    { "fx_eq_band1_type", 3.0f },
    { "fx_eq_band1_freq", 28.0f },
    { "fx_eq_band1_order", 2.0f },
    { "fx_eq_band7_type", 7.0f },
    { "fx_eq_band7_freq", 8000.0f },
    { "fx_eq_band7_gain", -3.0f },

    { "master_gain", -3.0f },
};

constexpr FactorySetting slowBloom[] {
    { "osc1_wavetable", 2.0f },
    { "osc1_position", 0.40f },
    { "osc1_unison", 7.0f },
    { "osc1_detune", 0.22f },
    { "osc1_spread", 1.0f },

    { "osc2_position", 0.60f },
    { "osc2_level", 0.60f },
    { "osc2_unison", 5.0f },
    { "osc2_detune", 0.30f },
    { "osc2_spread", 1.0f },
    { "osc2_semitones", 7.0f },

    { "filter1_cutoff", 2800.0f },
    { "filter1_resonance", 0.80f },

    // Over a second to arrive and more than two to leave. A pad is the one
    // sound where the envelope is most of the character.
    { "env1_attack", 1200.0f },
    { "env1_decay", 2000.0f },
    { "env1_sustain", 0.80f },
    { "env1_release", 2600.0f },

    { "lfo1_rate", 0.12f },
    { "lfo1_retrigger", 0.0f },
    { "lfo1_smoothing", 0.30f },

    { "mod01_source", 5.0f }, { "mod01_destination", 4.0f }, { "mod01_depth", 0.35f },
    { "mod02_source", 5.0f }, { "mod02_destination", 12.0f }, { "mod02_depth", 0.20f },
    { "mod03_source", 10.0f }, { "mod03_destination", 12.0f }, { "mod03_depth", 0.30f },

    { "fx_slot1", 3.0f },
    { "fx_reverb_size", 0.85f },
    { "fx_reverb_decay", 6000.0f },
    { "fx_reverb_predelay", 40.0f },
    { "fx_reverb_damping", 5200.0f },
    { "fx_reverb_mix", 0.45f },

    { "fx_slot2", 2.0f },
    { "fx_delay_sync", 1.0f },
    { "fx_delay_division", 4.0f },
    { "fx_delay_feedback", 0.40f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.18f },

    { "master_gain", -4.0f },
};

constexpr FactorySetting glassBell[] {
    { "osc1_wavetable", 1.0f },
    { "osc1_position", 0.08f },

    // A nineteenth above — an octave and a fifth — which is the interval that
    // makes a sine read as a bell rather than as two notes.
    { "osc2_wavetable", 1.0f },
    { "osc2_position", 0.12f },
    { "osc2_level", 0.50f },
    { "osc2_semitones", 19.0f },
    { "osc2_fine", 4.0f },

    { "filter1_cutoff", 9000.0f },
    { "filter1_resonance", 0.50f },

    { "env1_attack", 1.0f },
    { "env1_decay", 1600.0f },
    { "env1_sustain", 0.0f },
    { "env1_release", 1400.0f },
    // Hard off the mark and easing into the tail, which is what a struck thing
    // does.
    { "env1_curve", 0.80f },

    { "env2_attack", 1.0f },
    { "env2_decay", 500.0f },
    { "env2_release", 500.0f },

    // The upper partial dies before the fundamental, which is the whole trick.
    { "mod01_source", 2.0f }, { "mod01_destination", 7.0f }, { "mod01_depth", -0.50f },
    { "mod02_source", 9.0f }, { "mod02_destination", 16.0f }, { "mod02_depth", 0.50f },

    { "fx_slot1", 3.0f },
    { "fx_reverb_size", 0.70f },
    { "fx_reverb_decay", 4500.0f },
    { "fx_reverb_predelay", 25.0f },
    { "fx_reverb_mix", 0.35f },

    { "fx_slot2", 2.0f },
    { "fx_delay_sync", 1.0f },
    { "fx_delay_division", 11.0f },
    { "fx_delay_feedback", 0.25f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.16f },

    { "master_gain", -3.0f },
};

constexpr FactorySetting wirePluck[] {
    { "osc1_wavetable", 3.0f },
    { "osc1_position", 0.25f },
    { "osc1_unison", 3.0f },
    { "osc1_detune", 0.12f },

    { "filter1_cutoff", 3200.0f },
    { "filter1_resonance", 1.80f },
    { "filter1_drive", 0.20f },

    { "env1_attack", 1.0f },
    { "env1_decay", 220.0f },
    { "env1_sustain", 0.0f },
    { "env1_release", 220.0f },

    { "env2_attack", 1.0f },
    { "env2_decay", 130.0f },
    { "env2_release", 130.0f },

    { "mod01_source", 2.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.60f },
    { "mod02_source", 9.0f }, { "mod02_destination", 12.0f }, { "mod02_depth", 0.30f },

    { "fx_slot1", 2.0f },
    { "fx_delay_sync", 1.0f },
    { "fx_delay_division", 8.0f },
    { "fx_delay_feedback", 0.38f },
    { "fx_delay_damping", 5000.0f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.25f },

    { "fx_slot2", 3.0f },
    { "fx_reverb_mode", 0.0f },
    { "fx_reverb_size", 0.40f },
    { "fx_reverb_decay", 1200.0f },
    { "fx_reverb_mix", 0.18f },

    { "master_gain", -2.0f },
};

constexpr FactorySetting dustSweep[] {
    // The noise generator leads this one, with a little oscillator under it so
    // that the sweep has a pitch to belong to.
    { "osc1_level", 0.15f },
    { "osc1_position", 0.50f },
    { "noise_level", 0.90f },

    { "filter1_type", 3.0f },
    { "filter1_cutoff", 800.0f },
    { "filter1_resonance", 3.50f },

    { "env1_attack", 900.0f },
    { "env1_decay", 1500.0f },
    { "env1_sustain", 0.90f },
    { "env1_release", 900.0f },

    // A slow rising saw, retriggered by the note, is a riser.
    { "lfo1_shape", 2.0f },
    { "lfo1_rate", 0.08f },
    { "lfo1_polarity", 0.0f },

    { "mod01_source", 5.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.85f },
    { "mod02_source", 1.0f }, { "mod02_destination", 13.0f }, { "mod02_depth", 0.30f },

    { "fx_slot1", 3.0f },
    { "fx_reverb_size", 0.90f },
    { "fx_reverb_decay", 8000.0f },
    { "fx_reverb_mix", 0.50f },

    { "fx_slot2", 2.0f },
    { "fx_delay_sync", 1.0f },
    { "fx_delay_feedback", 0.50f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.30f },

    { "master_gain", -6.0f },
};

constexpr FactorySetting squareKeys[] {
    { "osc1_wavetable", 1.0f },
    { "osc1_position", 1.0f },
    { "osc1_unison", 3.0f },
    { "osc1_detune", 0.10f },
    { "osc1_spread", 0.40f },

    { "sub_level", 0.35f },

    { "filter1_cutoff", 4200.0f },
    { "filter1_resonance", 0.70f },
    { "filter1_drive", 0.15f },

    { "env1_attack", 3.0f },
    { "env1_decay", 900.0f },
    { "env1_sustain", 0.35f },
    { "env1_release", 260.0f },

    { "mod01_source", 9.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.40f },
    // Leaning on a held key opens the wave out, which is the one thing
    // aftertouch is unarguably for.
    { "mod02_source", 13.0f }, { "mod02_destination", 4.0f }, { "mod02_depth", 0.30f },

    { "fx_slot1", 6.0f },
    { "fx_eq_band2_freq", 220.0f },
    { "fx_eq_band2_gain", -3.0f },
    { "fx_eq_band2_bandwidth", 1.20f },
    { "fx_eq_band6_freq", 3200.0f },
    { "fx_eq_band6_gain", 2.50f },

    { "fx_slot2", 3.0f },
    { "fx_reverb_mode", 0.0f },
    { "fx_reverb_size", 0.45f },
    { "fx_reverb_decay", 1600.0f },
    { "fx_reverb_mix", 0.22f },

    { "master_gain", -3.0f },
};

constexpr FactorySetting raspBass[] {
    { "osc1_wavetable", 3.0f },
    { "osc1_position", 0.55f },
    { "osc1_unison", 3.0f },
    { "osc1_detune", 0.08f },
    { "osc1_spread", 0.30f },

    { "osc2_position", 0.90f },
    { "osc2_level", 0.50f },
    { "osc2_semitones", -12.0f },

    { "sub_level", 0.50f },

    { "filter1_cutoff", 1400.0f },
    { "filter1_resonance", 1.60f },
    { "filter1_drive", 0.50f },

    { "env1_attack", 1.0f },
    { "env1_decay", 700.0f },
    { "env1_sustain", 0.60f },
    { "env1_release", 120.0f },

    { "env2_attack", 1.0f },
    { "env2_decay", 200.0f },
    { "env2_release", 160.0f },

    { "mod01_source", 2.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.55f },

    // The chain in the order it belongs in: clean up the tail, then bite it,
    // then even it out, then shape it.
    { "fx_slot1", 4.0f },
    { "fx_gate_threshold", -45.0f },
    { "fx_gate_range", -30.0f },
    { "fx_gate_hold", 40.0f },
    { "fx_gate_release", 120.0f },

    { "fx_slot2", 1.0f },
    { "fx_distortion_mode", 2.0f },
    { "fx_distortion_drive", 20.0f },
    { "fx_distortion_tone", 5500.0f },
    { "fx_distortion_mix", 0.60f },
    { "fx_distortion_output", -4.0f },

    { "fx_slot3", 5.0f },
    { "fx_compressor_threshold", -16.0f },
    { "fx_compressor_ratio", 6.0f },
    { "fx_compressor_attack", 3.0f },
    { "fx_compressor_release", 80.0f },
    { "fx_compressor_makeup", 3.0f },
    { "fx_compressor_mix", 0.80f },

    { "fx_slot4", 6.0f },
    { "fx_eq_band1_type", 3.0f },
    { "fx_eq_band1_freq", 32.0f },
    { "fx_eq_band1_order", 2.0f },

    { "master_gain", -5.0f },
};

constexpr FactorySetting wideStab[] {
    { "osc1_position", 0.75f },
    { "osc1_unison", 9.0f },
    { "osc1_detune", 0.35f },
    { "osc1_spread", 1.0f },

    { "osc2_wavetable", 3.0f },
    { "osc2_position", 0.40f },
    { "osc2_level", 0.55f },
    { "osc2_unison", 5.0f },
    { "osc2_detune", 0.25f },
    { "osc2_spread", 1.0f },
    { "osc2_semitones", 7.0f },

    // Both filters, in series: a low pass for the shape and a high pass to keep
    // a nine-voice unison stack from filling the bottom of a mix.
    { "filter1_cutoff", 5200.0f },
    { "filter1_resonance", 1.10f },
    { "filter2_type", 2.0f },
    { "filter2_cutoff", 160.0f },

    { "env1_attack", 2.0f },
    { "env1_decay", 420.0f },
    { "env1_sustain", 0.20f },
    { "env1_release", 260.0f },

    { "env2_attack", 1.0f },
    { "env2_decay", 260.0f },
    { "env2_release", 200.0f },

    { "mod01_source", 2.0f }, { "mod01_destination", 12.0f }, { "mod01_depth", 0.50f },
    { "mod02_source", 9.0f }, { "mod02_destination", 16.0f }, { "mod02_depth", 0.40f },

    { "fx_slot1", 1.0f },
    { "fx_distortion_drive", 8.0f },
    { "fx_distortion_tone", 11000.0f },
    { "fx_distortion_mix", 0.30f },

    { "fx_slot2", 2.0f },
    { "fx_delay_sync", 1.0f },
    { "fx_delay_division", 9.0f },
    { "fx_delay_feedback", 0.30f },
    { "fx_delay_pingpong", 1.0f },
    { "fx_delay_mix", 0.20f },

    { "fx_slot3", 3.0f },
    { "fx_reverb_mode", 0.0f },
    { "fx_reverb_decay", 1800.0f },
    { "fx_reverb_mix", 0.20f },

    { "master_gain", -3.0f },
};

/** The library. Init first; the browser re-orders these alphabetically. */
constexpr std::array<FactoryPreset, 10> presets { {
    { "Init", "Basics",
      "Every control at its default. Where a sound starts.",
      {} },

    { "Sub Weight", "Bass",
      "Sine-led bass with the sub almost as loud as the oscillator above it, "
      "compressed and cleared out underneath.",
      subWeight },

    { "Rasp Bass", "Bass",
      "Saw and sub through a diode stage, gated at the tail and evened out "
      "after it. The whole rack in the order it belongs in.",
      raspBass },

    { "Hollow Saw Lead", "Lead",
      "Seven-voice saw with an octave under it, the filter moved by its own "
      "envelope and the wave by the mod wheel.",
      sawLead },

    { "Wide Stab", "Lead",
      "Fourteen voices across two oscillators a fifth apart, through both "
      "filters in series.",
      wideStab },

    { "Slow Bloom", "Pad",
      "Over a second to arrive and nearly three to leave, with the wavetable "
      "drifting under a slow LFO.",
      slowBloom },

    { "Glass Bell", "Keys",
      "A sine and its nineteenth, the upper partial dying first. Velocity is "
      "the striking hand.",
      glassBell },

    { "Square Keys", "Keys",
      "Square wave and a sub, opened by velocity and by leaning on the key.",
      squareKeys },

    { "Wire Pluck", "Pluck",
      "Short, resonant and bright, with a syncopated repeat behind it.",
      wirePluck },

    { "Dust Sweep", "FX",
      "The noise generator through a resonant band pass, swept by a slow "
      "rising LFO into a long hall.",
      dustSweep },
} };

/** @returns the plain value @p definition should take in @p preset.

    Clamped and snapped to the definition's own range and step. A value outside
    the range is an authoring mistake, and a test refuses one — this clamp is
    the belt that makes the braces safe, so a mistake that slipped through could
    never put an out-of-range number into a document.
*/
[[nodiscard]] float valueFor (const FactoryPreset& preset,
                              const params::ParameterDefinition& definition)
{
    auto value = definition.defaultValue;

    for (const auto& setting : preset.settings)
    {
        if (setting.id != definition.id)
            continue;

        value = setting.value;
        break;
    }

    value = std::clamp (value, definition.minimum, definition.maximum);

    if (definition.stepSize > 0.0f)
    {
        const auto steps = std::round ((value - definition.minimum) / definition.stepSize);
        value = std::clamp (definition.minimum + steps * definition.stepSize,
                            definition.minimum,
                            definition.maximum);
    }

    return value;
}

} // namespace

std::span<const FactoryPreset> factoryPresets()
{
    return { presets.data(), presets.size() };
}

presets::Metadata metadataOf (const FactoryPreset& preset)
{
    presets::Metadata metadata;

    metadata.name = params::toJuceString (preset.name);
    metadata.author = params::toJuceString (factoryAuthor);
    metadata.category = params::toJuceString (preset.category);
    metadata.comment = params::toJuceString (preset.comment);

    return metadata;
}

juce::String render (const FactoryPreset& preset)
{
    juce::ValueTree state { juce::Identifier (params::stateTreeType) };

    for (const auto& definition : params::parameterDefinitions)
    {
        // A preset carries the patch and not the controller, so the four
        // parameters that describe somebody's keyboard are left out of the
        // document exactly as `presets::write` leaves them out (ADR-0061).
        // Loading one then keeps whatever this user has set.
        if (presets::isExcludedFromPresets (definition.id))
            continue;

        juce::ValueTree node { juce::Identifier (params::parameterTreeType) };

        node.setProperty (params::parameterIdProperty,
                          params::toJuceString (definition.id), nullptr);
        node.setProperty (params::parameterValueProperty, valueFor (preset, definition), nullptr);

        state.appendChild (node, nullptr);
    }

    state::stamp (state);
    presets::attachMetadata (state, metadataOf (preset));

    const auto xml = state.createXml();

    return xml != nullptr ? xml->toString() : juce::String();
}

} // namespace apollo::resources
