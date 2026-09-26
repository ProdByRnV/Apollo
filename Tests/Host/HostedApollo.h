#pragma once

/*
    Apollo as a host sees it.

    Everything else in Apollo's test suite drives ApolloAudioProcessor directly,
    through its C++ interface. A DAW never does that. It loads a bundle from
    disk, asks its factory for a class, and talks to it through the VST3 COM
    interfaces — IComponent, IAudioProcessor, IEditController, IMidiMapping —
    by way of a wrapper Apollo did not write. Every guarantee the rest of the
    suite establishes has to survive that wrapper, and nothing else checks
    whether it does (ADR-0075).

    These helpers load the *built* VST3 bundle through JUCE's VST3 hosting
    implementation, which is a VST3 host in the full sense: it scans the
    factory, instantiates the component and the controller separately, passes
    parameter changes as IParameterChanges queues and MIDI as IEventList events,
    converts controllers through the plugin's IMidiMapping, and reads state back
    through IBStream. The harness links no Apollo code at all. What it knows
    about Apollo it knows from the registry header, which is plain data, and
    from what the plugin tells it.

    The harness is headless: it builds against juce_audio_processors_headless,
    so it needs no display and runs on a CI runner. The editor is outside its
    reach, and the manual host pass records what was checked there instead.
*/

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace apollo::host
{

//==============================================================================
// The plugin under test

/** Sets the bundle every test loads. Called once, by main, before any test. */
void setPluginBundle (const juce::File& bundle);

/** @returns the bundle every test loads. */
[[nodiscard]] juce::File getPluginBundle();

/** Scans the bundle, as a host does when it first finds it.

    @returns every class the bundle's factory declares; empty if the scan
             failed, with the reason in @p error.
*/
[[nodiscard]] juce::OwnedArray<juce::PluginDescription> scan (juce::String& error);

/** Loads one instance of Apollo from the bundle.

    Scans on the first call and reuses the description afterwards, the way a
    host reads its plugin cache rather than rescanning every time it inserts a
    plugin. Returns nullptr and fills @p error if the bundle cannot be loaded.

    The instance is NOT prepared. Hosts differ in what they do between
    instantiation and the first prepare, and a test that needs a prepared
    instance says so by calling prepare().
*/
[[nodiscard]] std::unique_ptr<juce::AudioPluginInstance> load (juce::String& error);

/** Loads and prepares an instance with a stereo output, or returns nullptr. */
[[nodiscard]] std::unique_ptr<juce::AudioPluginInstance> loadPrepared (double sampleRate,
                                                                        int blockSize,
                                                                        juce::String& error);

//==============================================================================
// Parameters

/** @returns the host-side parameter whose name matches @p name, or nullptr.

    By name, because the name is all a VST3 host is given besides a number.
    The numbers are pinned separately, by the parameter identity test.
*/
[[nodiscard]] juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& instance,
                                                            juce::StringRef name);

/** @returns the host-side parameter for a registry ID, or nullptr. */
[[nodiscard]] juce::AudioProcessorParameter* findRegistryParameter (juce::AudioPluginInstance& instance,
                                                                    std::string_view registryId);

/** Converts a plain value into the normalised value a host sends, using the
    registry's range and skew. The same arithmetic JUCE's NormalisableRange
    performs inside the plugin, done here from the registry's numbers.
*/
[[nodiscard]] float toNormalised (std::string_view registryId, float plainValue);

/** Sets a parameter the way a host's automation lane or a generic editor does:
    as a normalised value, through the host-side parameter object.

    @returns false if the registry does not know the ID or the plugin does not
             expose it.
*/
bool setPlain (juce::AudioPluginInstance& instance, std::string_view registryId, float plainValue);

/** @returns the parameters a host would list for the user: everything the
    plugin exposes except the parameters the VST3 wrapper adds for itself —
    bypass, program change, and the hidden MIDI-controller parameters that
    carry CCs through IMidiMapping.
*/
[[nodiscard]] std::vector<juce::AudioProcessorParameter*> userParameters (juce::AudioPluginInstance& instance);

/** @returns the VST3 parameter ID a host stores automation against. */
[[nodiscard]] juce::uint32 vst3ParameterId (const juce::AudioProcessorParameter& parameter);

//==============================================================================
// Soak

/** How much longer than its default length a reliability test should run.

    1 by default, so an ordinary run and CI stay quick; `--soak 20` makes every
    reliability test twenty times longer. The tests are written in units of
    work rather than in seconds so that the multiplier means the same thing to
    all of them (ADR-0080).
*/
void setSoakScale (int scale);

/** @returns the multiplier set by `--soak`, at least 1. */
[[nodiscard]] int soakScale();

/** @returns @p units multiplied by the soak scale, at least 1. */
[[nodiscard]] int soaked (int units);

//==============================================================================
// Rendering

/** One MIDI event at an absolute sample position in a render. */
struct TimedMidi
{
    int sample = 0;
    juce::MidiMessage message;
};

/** Called before each block; receives the block's first sample. */
using BlockHook = std::function<void (int blockStart)>;

/** Renders @p numSamples of stereo output through the instance, in blocks of
    @p blockSize (the last may be shorter), delivering each MIDI event in the
    block it falls into at its offset within that block.
*/
[[nodiscard]] juce::AudioBuffer<float> render (juce::AudioPluginInstance& instance,
                                               int numSamples,
                                               int blockSize,
                                               const std::vector<TimedMidi>& midi,
                                               const BlockHook& beforeBlock = {});

/** As render(), but with block sizes taken in turn from @p blockSizes. This is
    how a host that splits its buffers at automation points or loop boundaries
    calls a plugin.
*/
[[nodiscard]] juce::AudioBuffer<float> renderVariable (juce::AudioPluginInstance& instance,
                                                       int numSamples,
                                                       const std::vector<int>& blockSizes,
                                                       const std::vector<TimedMidi>& midi);

/** A note on channel 1, as a pair of events. */
[[nodiscard]] std::vector<TimedMidi> note (int noteNumber, int onSample, int offSample,
                                           juce::uint8 velocity = 100);

//==============================================================================
// Measurement

/** @returns the largest absolute sample in the range, over every channel. */
[[nodiscard]] float peak (const juce::AudioBuffer<float>& buffer, int start = 0, int length = -1);

/** @returns true if every sample in the buffer is finite. */
[[nodiscard]] bool allFinite (const juce::AudioBuffer<float>& buffer);

/** @returns the largest absolute difference between two buffers of the same
    shape, or infinity if their shapes differ.
*/
[[nodiscard]] float maxDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b);

/** @returns the index of the first sample, on any channel, whose magnitude
    exceeds @p threshold; -1 if there is none.
*/
[[nodiscard]] int firstSampleAbove (const juce::AudioBuffer<float>& buffer, float threshold);

/** Estimates the fundamental of channel 0 by counting rising zero crossings
    over the given range, interpolated to a fraction of a sample.
*/
[[nodiscard]] double estimateFrequency (const juce::AudioBuffer<float>& buffer,
                                        double sampleRate, int start, int length);

//==============================================================================
/** Runs the message loop for a while.

    A VST3 plugin tells its host that its latency changed through
    IComponentHandler::restartComponent, which Apollo deliberately issues from
    the message thread rather than from inside its audio callback (ADR-0054).
    A host running a real message loop sees it; a test has to pump one.
*/
void pumpMessages (int milliseconds);

} // namespace apollo::host
