#pragma once

/*
    Documents written by released or frozen builds of Apollo, kept verbatim.

    A compatibility promise is only as good as the oldest file it is tested
    against. Each fixture here is a document an earlier Apollo actually wrote,
    captured from the running application rather than typed to look like one,
    and every later build must go on reading it (Docs/COMPATIBILITY.md).

    NEVER EDIT A FIXTURE. If a later build cannot read one, that build is what
    changed, and the fix is a migration step in State/StateSerialization.cpp,
    not a new fixture. Add fixtures; do not replace them.
*/

namespace apollo::fixtures
{

/** A preset saved by Apollo 0.1.0 (schema version 2) on 2026-10-21, from the
    standalone with Wire Pluck loaded and saved as "Compatibility Fixture". */
inline constexpr const char* preset_0_1_0 = R"APOLLO(<?xml version="1.0" encoding="UTF-8"?>

<ApolloState schemaVersion="2" product="Apollo" productVersion="0.1.0">
  <PARAM id="osc1_wavetable" value="3.0"/>
  <PARAM id="osc1_position" value="0.25"/>
  <PARAM id="osc1_unison" value="3.0"/>
  <PARAM id="osc1_detune" value="0.119999997317791"/>
  <PARAM id="osc1_spread" value="0.5"/>
  <PARAM id="osc1_level" value="1.0"/>
  <PARAM id="osc1_pan" value="0.0"/>
  <PARAM id="osc2_wavetable" value="0.0"/>
  <PARAM id="osc2_position" value="0.0"/>
  <PARAM id="osc2_unison" value="1.0"/>
  <PARAM id="osc2_detune" value="0.2000000029802322"/>
  <PARAM id="osc2_spread" value="0.5"/>
  <PARAM id="osc2_level" value="0.0"/>
  <PARAM id="osc2_pan" value="0.0"/>
  <PARAM id="osc2_semitones" value="0.0"/>
  <PARAM id="osc2_fine" value="0.0"/>
  <PARAM id="sub_level" value="0.0"/>
  <PARAM id="sub_octave" value="-1.0"/>
  <PARAM id="noise_level" value="0.0"/>
  <PARAM id="env1_delay" value="0.0"/>
  <PARAM id="env1_attack" value="1.0"/>
  <PARAM id="env1_hold" value="0.0"/>
  <PARAM id="env1_decay" value="220.0"/>
  <PARAM id="env1_sustain" value="0.0"/>
  <PARAM id="env1_release" value="220.0"/>
  <PARAM id="env1_curve" value="0.5"/>
  <PARAM id="filter1_type" value="1.0"/>
  <PARAM id="filter1_cutoff" value="3200.0"/>
  <PARAM id="filter1_resonance" value="1.799999952316284"/>
  <PARAM id="filter1_drive" value="0.2000000029802322"/>
  <PARAM id="filter2_type" value="0.0"/>
  <PARAM id="filter2_cutoff" value="20000.0"/>
  <PARAM id="filter2_resonance" value="0.7070000171661377"/>
  <PARAM id="filter2_drive" value="0.0"/>
  <PARAM id="filter_routing" value="0.0"/>
  <PARAM id="env2_delay" value="0.0"/>
  <PARAM id="env2_attack" value="1.0"/>
  <PARAM id="env2_hold" value="0.0"/>
  <PARAM id="env2_decay" value="130.0"/>
  <PARAM id="env2_sustain" value="0.0"/>
  <PARAM id="env2_release" value="130.0"/>
  <PARAM id="env2_curve" value="0.5"/>
  <PARAM id="env3_delay" value="0.0"/>
  <PARAM id="env3_attack" value="5.0"/>
  <PARAM id="env3_hold" value="0.0"/>
  <PARAM id="env3_decay" value="300.0"/>
  <PARAM id="env3_sustain" value="0.0"/>
  <PARAM id="env3_release" value="300.0"/>
  <PARAM id="env3_curve" value="0.5"/>
  <PARAM id="env4_delay" value="0.0"/>
  <PARAM id="env4_attack" value="5.0"/>
  <PARAM id="env4_hold" value="0.0"/>
  <PARAM id="env4_decay" value="300.0"/>
  <PARAM id="env4_sustain" value="0.0"/>
  <PARAM id="env4_release" value="300.0"/>
  <PARAM id="env4_curve" value="0.5"/>
  <PARAM id="lfo1_shape" value="0.0"/>
  <PARAM id="lfo1_rate" value="1.000000238418579"/>
  <PARAM id="lfo1_phase" value="0.0"/>
  <PARAM id="lfo1_retrigger" value="1.0"/>
  <PARAM id="lfo1_fade" value="0.0"/>
  <PARAM id="lfo1_smoothing" value="0.0"/>
  <PARAM id="lfo1_polarity" value="1.0"/>
  <PARAM id="lfo1_steps" value="8.0"/>
  <PARAM id="lfo2_shape" value="0.0"/>
  <PARAM id="lfo2_rate" value="1.000000238418579"/>
  <PARAM id="lfo2_phase" value="0.0"/>
  <PARAM id="lfo2_retrigger" value="1.0"/>
  <PARAM id="lfo2_fade" value="0.0"/>
  <PARAM id="lfo2_smoothing" value="0.0"/>
  <PARAM id="lfo2_polarity" value="1.0"/>
  <PARAM id="lfo2_steps" value="8.0"/>
  <PARAM id="lfo3_shape" value="0.0"/>
  <PARAM id="lfo3_rate" value="1.000000238418579"/>
  <PARAM id="lfo3_phase" value="0.0"/>
  <PARAM id="lfo3_retrigger" value="1.0"/>
  <PARAM id="lfo3_fade" value="0.0"/>
  <PARAM id="lfo3_smoothing" value="0.0"/>
  <PARAM id="lfo3_polarity" value="1.0"/>
  <PARAM id="lfo3_steps" value="8.0"/>
  <PARAM id="lfo4_shape" value="0.0"/>
  <PARAM id="lfo4_rate" value="1.000000238418579"/>
  <PARAM id="lfo4_phase" value="0.0"/>
  <PARAM id="lfo4_retrigger" value="1.0"/>
  <PARAM id="lfo4_fade" value="0.0"/>
  <PARAM id="lfo4_smoothing" value="0.0"/>
  <PARAM id="lfo4_polarity" value="1.0"/>
  <PARAM id="lfo4_steps" value="8.0"/>
  <PARAM id="mod01_source" value="2.0"/>
  <PARAM id="mod01_destination" value="12.0"/>
  <PARAM id="mod01_depth" value="0.6000000238418579"/>
  <PARAM id="mod02_source" value="9.0"/>
  <PARAM id="mod02_destination" value="12.0"/>
  <PARAM id="mod02_depth" value="0.2999999523162842"/>
  <PARAM id="mod03_source" value="0.0"/>
  <PARAM id="mod03_destination" value="0.0"/>
  <PARAM id="mod03_depth" value="0.0"/>
  <PARAM id="mod04_source" value="0.0"/>
  <PARAM id="mod04_destination" value="0.0"/>
  <PARAM id="mod04_depth" value="0.0"/>
  <PARAM id="mod05_source" value="0.0"/>
  <PARAM id="mod05_destination" value="0.0"/>
  <PARAM id="mod05_depth" value="0.0"/>
  <PARAM id="mod06_source" value="0.0"/>
  <PARAM id="mod06_destination" value="0.0"/>
  <PARAM id="mod06_depth" value="0.0"/>
  <PARAM id="mod07_source" value="0.0"/>
  <PARAM id="mod07_destination" value="0.0"/>
  <PARAM id="mod07_depth" value="0.0"/>
  <PARAM id="mod08_source" value="0.0"/>
  <PARAM id="mod08_destination" value="0.0"/>
  <PARAM id="mod08_depth" value="0.0"/>
  <PARAM id="mod09_source" value="0.0"/>
  <PARAM id="mod09_destination" value="0.0"/>
  <PARAM id="mod09_depth" value="0.0"/>
  <PARAM id="mod10_source" value="0.0"/>
  <PARAM id="mod10_destination" value="0.0"/>
  <PARAM id="mod10_depth" value="0.0"/>
  <PARAM id="mod11_source" value="0.0"/>
  <PARAM id="mod11_destination" value="0.0"/>
  <PARAM id="mod11_depth" value="0.0"/>
  <PARAM id="mod12_source" value="0.0"/>
  <PARAM id="mod12_destination" value="0.0"/>
  <PARAM id="mod12_depth" value="0.0"/>
  <PARAM id="mod13_source" value="0.0"/>
  <PARAM id="mod13_destination" value="0.0"/>
  <PARAM id="mod13_depth" value="0.0"/>
  <PARAM id="mod14_source" value="0.0"/>
  <PARAM id="mod14_destination" value="0.0"/>
  <PARAM id="mod14_depth" value="0.0"/>
  <PARAM id="mod15_source" value="0.0"/>
  <PARAM id="mod15_destination" value="0.0"/>
  <PARAM id="mod15_depth" value="0.0"/>
  <PARAM id="mod16_source" value="0.0"/>
  <PARAM id="mod16_destination" value="0.0"/>
  <PARAM id="mod16_depth" value="0.0"/>
  <PARAM id="fx_slot1" value="2.0"/>
  <PARAM id="fx_slot2" value="3.0"/>
  <PARAM id="fx_slot3" value="0.0"/>
  <PARAM id="fx_slot4" value="0.0"/>
  <PARAM id="fx_slot5" value="0.0"/>
  <PARAM id="fx_slot6" value="0.0"/>
  <PARAM id="fx_distortion_bypass" value="0.0"/>
  <PARAM id="fx_distortion_mode" value="0.0"/>
  <PARAM id="fx_distortion_drive" value="12.0"/>
  <PARAM id="fx_distortion_tone" value="20000.0"/>
  <PARAM id="fx_distortion_mix" value="0.0"/>
  <PARAM id="fx_distortion_output" value="0.0"/>
  <PARAM id="fx_delay_bypass" value="0.0"/>
  <PARAM id="fx_delay_sync" value="1.0"/>
  <PARAM id="fx_delay_division" value="8.0"/>
  <PARAM id="fx_delay_feedback" value="0.3799999952316284"/>
  <PARAM id="fx_delay_damping" value="5000.0"/>
  <PARAM id="fx_delay_lowcut" value="20.0"/>
  <PARAM id="fx_delay_pingpong" value="1.0"/>
  <PARAM id="fx_delay_mix" value="0.25"/>
  <PARAM id="fx_reverb_bypass" value="0.0"/>
  <PARAM id="fx_reverb_mode" value="0.0"/>
  <PARAM id="fx_reverb_size" value="0.4000000059604645"/>
  <PARAM id="fx_reverb_decay" value="1200.0"/>
  <PARAM id="fx_reverb_damping" value="6000.0"/>
  <PARAM id="fx_reverb_predelay" value="20.0"/>
  <PARAM id="fx_reverb_width" value="1.0"/>
  <PARAM id="fx_reverb_mix" value="0.1800000071525574"/>
  <PARAM id="fx_gate_bypass" value="0.0"/>
  <PARAM id="fx_gate_threshold" value="-60.0"/>
  <PARAM id="fx_gate_attack" value="1.0"/>
  <PARAM id="fx_gate_hold" value="50.0"/>
  <PARAM id="fx_gate_release" value="99.99998474121094"/>
  <PARAM id="fx_gate_range" value="-60.0"/>
  <PARAM id="fx_compressor_bypass" value="0.0"/>
  <PARAM id="fx_compressor_threshold" value="-18.0"/>
  <PARAM id="fx_compressor_ratio" value="2.0"/>
  <PARAM id="fx_compressor_attack" value="10.0"/>
  <PARAM id="fx_compressor_release" value="120.0"/>
  <PARAM id="fx_compressor_makeup" value="0.0"/>
  <PARAM id="fx_compressor_mix" value="1.0"/>
  <PARAM id="fx_delay_time" value="500.0"/>
  <PARAM id="fx_eq_bypass" value="0.0"/>
  <PARAM id="fx_eq_level" value="0.0"/>
  <PARAM id="fx_eq_band1_type" value="6.0"/>
  <PARAM id="fx_eq_band1_freq" value="47.0"/>
  <PARAM id="fx_eq_band1_gain" value="0.0"/>
  <PARAM id="fx_eq_band1_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band1_order" value="1.0"/>
  <PARAM id="fx_eq_band1_mute" value="0.0"/>
  <PARAM id="fx_eq_band2_type" value="6.0"/>
  <PARAM id="fx_eq_band2_freq" value="112.0000305175781"/>
  <PARAM id="fx_eq_band2_gain" value="0.0"/>
  <PARAM id="fx_eq_band2_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band2_order" value="1.0"/>
  <PARAM id="fx_eq_band2_mute" value="0.0"/>
  <PARAM id="fx_eq_band3_type" value="6.0"/>
  <PARAM id="fx_eq_band3_freq" value="267.0"/>
  <PARAM id="fx_eq_band3_gain" value="0.0"/>
  <PARAM id="fx_eq_band3_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band3_order" value="1.0"/>
  <PARAM id="fx_eq_band3_mute" value="0.0"/>
  <PARAM id="fx_eq_band4_type" value="6.0"/>
  <PARAM id="fx_eq_band4_freq" value="632.0003051757812"/>
  <PARAM id="fx_eq_band4_gain" value="0.0"/>
  <PARAM id="fx_eq_band4_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band4_order" value="1.0"/>
  <PARAM id="fx_eq_band4_mute" value="0.0"/>
  <PARAM id="fx_eq_band5_type" value="6.0"/>
  <PARAM id="fx_eq_band5_freq" value="1500.0"/>
  <PARAM id="fx_eq_band5_gain" value="0.0"/>
  <PARAM id="fx_eq_band5_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band5_order" value="1.0"/>
  <PARAM id="fx_eq_band5_mute" value="0.0"/>
  <PARAM id="fx_eq_band6_type" value="6.0"/>
  <PARAM id="fx_eq_band6_freq" value="3549.999267578125"/>
  <PARAM id="fx_eq_band6_gain" value="0.0"/>
  <PARAM id="fx_eq_band6_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band6_order" value="1.0"/>
  <PARAM id="fx_eq_band6_mute" value="0.0"/>
  <PARAM id="fx_eq_band7_type" value="6.0"/>
  <PARAM id="fx_eq_band7_freq" value="8430.001953125"/>
  <PARAM id="fx_eq_band7_gain" value="0.0"/>
  <PARAM id="fx_eq_band7_bandwidth" value="1.0"/>
  <PARAM id="fx_eq_band7_order" value="1.0"/>
  <PARAM id="fx_eq_band7_mute" value="0.0"/>
  <PARAM id="master_gain" value="-2.0"/>
  <PRESET name="Compatibility Fixture" author="ProdByRnV" category="Pluck"
          comment="Short, resonant and bright, with a syncopated repeat behind it."/>
</ApolloState>
)APOLLO";

} // namespace apollo::fixtures
