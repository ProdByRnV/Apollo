/*
    Plugin entry point.

    Kept in the plugin target rather than alongside the processor so that the
    engine sources stay free of plugin-client symbols and can be compiled into
    the test runner unchanged.
*/

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "Audio/ApolloAudioProcessor.h"
#include "StandaloneMidiAdoption.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    // The standalone has to go and find its own MIDI, because nothing hands it
    // any: JUCE's wrapper opens no input on desktop until somebody ticks one in
    // the settings dialog, so a keyboard plugged into the machine does nothing
    // (ADR-0081). Does nothing in a plugin build, where the host owns the MIDI.
    apollo::standalone::startMidiDeviceAdoption();

    return new apollo::ApolloAudioProcessor();
}
