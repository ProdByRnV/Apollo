#include "Parameters/ParameterLayout.h"

namespace apollo::params
{

juce::String toJuceString (std::string_view text)
{
    return juce::String (juce::CharPointer_UTF8 (text.data()),
                         juce::CharPointer_UTF8 (text.data() + text.size()));
}

namespace
{
juce::ParameterID makeParameterID (const ParameterDefinition& definition)
{
    return juce::ParameterID { toJuceString (definition.id), parameterVersionHint };
}
} // namespace

juce::NormalisableRange<float> makeRange (const ParameterDefinition& definition)
{
    return { definition.minimum, definition.maximum, definition.stepSize, definition.skew };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& definition : parameterDefinitions)
    {
        const auto label = toJuceString (toString (definition.unit));

        switch (definition.type)
        {
            case ParameterType::integer:
                // An integer parameter rather than a float with a step, so the
                // discrete step count survives the round trip through the host
                // and the bridge (UI_BINDINGS.md §4).
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    makeParameterID (definition),
                    toJuceString (definition.name),
                    static_cast<int> (definition.minimum),
                    static_cast<int> (definition.maximum),
                    static_cast<int> (definition.defaultValue),
                    juce::AudioParameterIntAttributes().withLabel (label)));
                break;

            case ParameterType::choice:
                // No choice parameters are registered yet. Reaching this branch
                // means a definition was added without its option names, which
                // is a programming error, not a runtime condition.
                jassertfalse;
                break;

            case ParameterType::floatingPoint:
            default:
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    makeParameterID (definition),
                    toJuceString (definition.name),
                    makeRange (definition),
                    definition.defaultValue,
                    juce::AudioParameterFloatAttributes().withLabel (label)));
                break;
        }
    }

    return layout;
}

} // namespace apollo::params
