/*
    Plugin entry point.

    Kept in the plugin target rather than alongside the processor so that the
    engine sources stay free of plugin-client symbols and can be compiled into
    the test runner unchanged.
*/

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "Audio/ApolloAudioProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new apollo::ApolloAudioProcessor();
}
