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

/** An integer parameter that tells the host it is one.

    juce::AudioParameterInt reports the right number of steps but does not
    override isDiscrete(), and the VST3 wrapper publishes a step count only for
    a parameter that is discrete. Every integer control Apollo has — the rack's
    slots, the filter types, the LFO shapes, the modulation sources and
    destinations — therefore reached every VST3 host as a continuous control: a
    selector drawn as a smooth knob, and automation free to land between its
    values (ADR-0075).

    Nothing a host has saved changes. The normalised value of each step is what
    it was, and so is the parameter's ID; only the step count the host is told
    is new.
*/
class DiscreteIntParameter final : public juce::AudioParameterInt
{
public:
    using juce::AudioParameterInt::AudioParameterInt;

    bool isDiscrete() const override { return true; }
};
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
                // and the bridge (UI_BINDINGS.md §4) — which, for the host, also
                // needs it to say it is discrete. See DiscreteIntParameter.
                layout.add (std::make_unique<DiscreteIntParameter> (
                    makeParameterID (definition),
                    toJuceString (definition.name),
                    static_cast<int> (definition.minimum),
                    static_cast<int> (definition.maximum),
                    static_cast<int> (definition.defaultValue),
                    juce::AudioParameterIntAttributes()
                        .withLabel (label)
                        .withAutomatable (definition.automatable)));
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
