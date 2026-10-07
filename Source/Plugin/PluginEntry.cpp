/*
    Plugin entry point.

    Kept in the plugin target rather than alongside the processor so that the
    engine sources stay free of plugin-client symbols and can be compiled into
    the test runner unchanged.
*/

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "Audio/ApolloAudioProcessor.h"
#include "StandaloneMidiAdoption.h"
#include "StandaloneOutputDevice.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    // The standalone has to go and find its own MIDI, because nothing hands it
    // any: JUCE's wrapper opens no input on desktop until somebody ticks one in
    // the settings dialog, so a keyboard plugged into the machine does nothing
    // (ADR-0081). Does nothing in a plugin build, where the host owns the MIDI.
    apollo::standalone::startMidiDeviceAdoption();

    // And it has to follow the machine's output: JUCE reopens the device it
    // saved, so plugging a laptop into speakers leaves the instrument playing
    // out of the laptop (ADR-0082). Does nothing in a plugin, where the device
    // belongs to the host.
    apollo::standalone::startOutputDeviceFollowing();

    return new apollo::ApolloAudioProcessor();
}
