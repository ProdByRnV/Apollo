#include "Host/HostedApollo.h"

#include "Parameters/ParameterDefinitions.h"

#include <cmath>
#include <limits>

namespace apollo::host
{

namespace
{

juce::File& bundleStorage()
{
    static juce::File bundle;
    return bundle;
}

/** The description of Apollo's instrument class, from the first scan. */
std::unique_ptr<juce::PluginDescription>& cachedDescription()
{
    static std::unique_ptr<juce::PluginDescription> description;
    return description;
}

juce::VST3PluginFormatHeadless& format()
{
    static juce::VST3PluginFormatHeadless instance;
    return instance;
}

} // namespace

//==============================================================================
void setPluginBundle (const juce::File& bundle)
{
    bundleStorage() = bundle;
    cachedDescription().reset();
}

juce::File getPluginBundle()
{
    return bundleStorage();
}

juce::OwnedArray<juce::PluginDescription> scan (juce::String& error)
{
    juce::OwnedArray<juce::PluginDescription> found;
    const auto bundle = getPluginBundle();

    if (! bundle.exists())
    {
        error = "No plugin at " + bundle.getFullPathName();
        return found;
    }

    format().findAllTypesForFile (found, bundle.getFullPathName());

    if (found.isEmpty())
        error = "The bundle was found but declared no plugin classes";

    return found;
}

std::unique_ptr<juce::AudioPluginInstance> load (juce::String& error)
{
    if (cachedDescription() == nullptr)
    {
        auto found = scan (error);

        if (found.isEmpty())
            return nullptr;

        cachedDescription() = std::make_unique<juce::PluginDescription> (*found.getFirst());
    }

    // Synchronous: VST3 does not require the message thread to be free during
    // creation, so the blocking form is the one a host's scanner would use.
    auto result = format().createInstanceFromDescription (*cachedDescription(), 48000.0, 512, error);

    if (result == nullptr && error.isEmpty())
        error = "createPluginInstance returned nothing";

    return result;
}

std::unique_ptr<juce::AudioPluginInstance> loadPrepared (double sampleRate, int blockSize, juce::String& error)
{
    auto instance = load (error);

    if (instance == nullptr)
        return nullptr;

    // Hosts negotiate the layout before activating. Apollo's default is a
    // stereo output and no input, which is what nearly every host asks for.
    instance->setPlayConfigDetails (0, 2, sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);
    return instance;
}

//==============================================================================
juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& instance, juce::StringRef name)
{
    for (auto* parameter : instance.getParameters())
        if (parameter->getName (256) == juce::String (name))
            return parameter;

    return nullptr;
}

juce::AudioProcessorParameter* findRegistryParameter (juce::AudioPluginInstance& instance,
                                                      std::string_view registryId)
{
    const auto* definition = params::findParameter (registryId);

    if (definition == nullptr)
        return nullptr;

    return findParameter (instance, juce::String (definition->name.data(),
                                                  definition->name.size()));
}

float toNormalised (std::string_view registryId, float plainValue)
{
    const auto* definition = params::findParameter (registryId);

    if (definition == nullptr)
        return 0.0f;

    // juce::NormalisableRange::convertTo0to1, for a range with no symmetric
    // skew: the proportion along the range, raised to the skew.
    const auto span = definition->maximum - definition->minimum;
    const auto clamped = juce::jlimit (definition->minimum, definition->maximum, plainValue);
    const auto proportion = span > 0.0f ? (clamped - definition->minimum) / span : 0.0f;

    return definition->skew == 1.0f ? proportion
                                    : std::pow (proportion, definition->skew);
}

bool setPlain (juce::AudioPluginInstance& instance, std::string_view registryId, float plainValue)
{
    auto* parameter = findRegistryParameter (instance, registryId);

    if (parameter == nullptr)
        return false;

    parameter->setValueNotifyingHost (toNormalised (registryId, plainValue));
    return true;
}

std::vector<juce::AudioProcessorParameter*> userParameters (juce::AudioPluginInstance& instance)
{
    std::vector<juce::AudioProcessorParameter*> result;
    const auto* bypass = instance.getBypassParameter();

    for (auto* parameter : instance.getParameters())
    {
        if (parameter == bypass)
            continue;

        // The VST3 wrapper's emulated controllers: sixteen channels of 130
        // parameters each, which exist so a host can route a CC through
        // IMidiMapping. They are the wrapper's, not Apollo's.
        if (parameter->getName (256).startsWith ("MIDI CC "))
            continue;

        result.push_back (parameter);
    }

    return result;
}

juce::uint32 vst3ParameterId (const juce::AudioProcessorParameter& parameter)
{
    // A hosted VST3 parameter reports its Vst::ParamID, in decimal, as its ID.
    if (const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (&parameter))
        return static_cast<juce::uint32> (hosted->getParameterID().getLargeIntValue());

    return 0;
}

//==============================================================================
juce::AudioBuffer<float> render (juce::AudioPluginInstance& instance,
                                 int numSamples,
                                 int blockSize,
                                 const std::vector<TimedMidi>& midi,
                                 const BlockHook& beforeBlock)
{
    juce::AudioBuffer<float> output (2, numSamples);
    output.clear();

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer events;

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const auto length = juce::jmin (blockSize, numSamples - start);

        if (beforeBlock)
            beforeBlock (start);

        events.clear();

        for (const auto& event : midi)
            if (event.sample >= start && event.sample < start + length)
                events.addEvent (event.message, event.sample - start);

        // A buffer the size of this block, as a host passes. The contents are
        // filled with garbage first: an instrument must overwrite its output
        // rather than add to whatever the host left in the buffer.
        block.setSize (2, length, false, false, true);

        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill (block.getWritePointer (channel), 0.25f, length);

        instance.processBlock (block, events);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, block, channel, 0, length);
    }

    return output;
}

juce::AudioBuffer<float> renderVariable (juce::AudioPluginInstance& instance,
                                         int numSamples,
                                         const std::vector<int>& blockSizes,
                                         const std::vector<TimedMidi>& midi)
{
    juce::AudioBuffer<float> output (2, numSamples);
    output.clear();

    int largest = 1;

    for (const auto size : blockSizes)
        largest = juce::jmax (largest, size);

    juce::AudioBuffer<float> block (2, largest);
    juce::MidiBuffer events;
    std::size_t next = 0;

    for (int start = 0; start < numSamples;)
    {
        const auto wanted = blockSizes[next++ % blockSizes.size()];
        const auto length = juce::jmin (wanted, numSamples - start);

        events.clear();

        for (const auto& event : midi)
            if (event.sample >= start && event.sample < start + length)
                events.addEvent (event.message, event.sample - start);

        block.setSize (2, length, false, false, true);
        block.clear();
        instance.processBlock (block, events);

        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, start, block, channel, 0, length);

        start += length;
    }

    return output;
}

std::vector<TimedMidi> note (int noteNumber, int onSample, int offSample, juce::uint8 velocity)
{
    return { { onSample, juce::MidiMessage::noteOn (1, noteNumber, velocity) },
             { offSample, juce::MidiMessage::noteOff (1, noteNumber) } };
}

//==============================================================================
float peak (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    if (length < 0)
        length = buffer.getNumSamples() - start;

    float result = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        result = juce::jmax (result, buffer.getMagnitude (channel, start, length));

    return result;
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* samples = buffer.getReadPointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (samples[i]))
                return false;
    }

    return true;
}

float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return std::numeric_limits<float>::infinity();

    float result = 0.0f;

    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        const auto* x = a.getReadPointer (channel);
        const auto* y = b.getReadPointer (channel);

        for (int i = 0; i < a.getNumSamples(); ++i)
            result = juce::jmax (result, std::abs (x[i] - y[i]));
    }

    return result;
}

int firstSampleAbove (const juce::AudioBuffer<float>& buffer, float threshold)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs (buffer.getSample (channel, i)) > threshold)
                return i;

    return -1;
}

double estimateFrequency (const juce::AudioBuffer<float>& buffer, double sampleRate, int start, int length)
{
    const auto* samples = buffer.getReadPointer (0);

    double first = -1.0;
    double last = -1.0;
    int crossings = 0;

    for (int i = start + 1; i < start + length; ++i)
    {
        const auto previous = static_cast<double> (samples[i - 1]);
        const auto current = static_cast<double> (samples[i]);

        if (previous < 0.0 && current >= 0.0)
        {
            // Where between the two samples the waveform crossed zero.
            const auto crossing = static_cast<double> (i - 1) + previous / (previous - current);

            if (first < 0.0)
                first = crossing;
            else
                ++crossings;

            last = crossing;
        }
    }

    if (crossings < 1)
        return 0.0;

    return sampleRate * static_cast<double> (crossings) / (last - first);
}

//==============================================================================
void pumpMessages (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

} // namespace apollo::host
