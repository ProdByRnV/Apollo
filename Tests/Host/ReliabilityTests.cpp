/*
    Whether Apollo survives being used.

    Every other test asks whether something is correct once. These ask whether
    it stays correct — over a long session, across hundreds of preset changes,
    under automation moving every block, through repeated device changes, and
    while a host hands it documents that are not state at all.

    The failure they look for is **drift and accumulation** rather than a wrong
    value: a level that creeps, a tail that grows, a latency that moves, memory
    that never comes back, a filter that slowly loses its mind. Those are the
    failures a user meets after an hour that nothing meets in a unit test
    (ADR-0080).

    Everything here runs through a real VST3 host, because that is how the
    accumulation actually happens: state arriving through IBStream, automation
    through IParameterChanges, a device change as a deactivate and reactivate.

    They are written in units of work, and `--soak N` multiplies every one of
    them. The defaults are sized for CI; the long runs are a deliberate act.
*/

#include "Host/HostedApollo.h"
#include "Performance/Machine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace apollo::host
{

namespace
{

/** What a long render is watched for, block by block, without keeping the
    audio: a soak cannot hold ten minutes of samples in memory, and the
    questions worth asking do not need them.
*/
struct SoakWatch
{
    bool everyBlockFinite = true;
    float loudest = 0.0f;
    int blocks = 0;
    int silentBlocks = 0;

    /** The peak of each window, so a level that creeps is visible as a trend
        rather than as one number at the end.
    */
    std::vector<float> windowPeaks;
    float currentWindowPeak = 0.0f;
    int blocksPerWindow = 0;
    int blocksThisWindow = 0;

    explicit SoakWatch (int windowBlocks) : blocksPerWindow (juce::jmax (1, windowBlocks)) {}

    void observe (const juce::AudioBuffer<float>& block)
    {
        ++blocks;
        ++blocksThisWindow;

        auto blockPeak = 0.0f;

        for (int channel = 0; channel < block.getNumChannels(); ++channel)
        {
            const auto* samples = block.getReadPointer (channel);

            for (int i = 0; i < block.getNumSamples(); ++i)
            {
                const auto sample = samples[i];

                if (! std::isfinite (sample))
                    everyBlockFinite = false;
                else
                    blockPeak = juce::jmax (blockPeak, std::abs (sample));
            }
        }

        loudest = juce::jmax (loudest, blockPeak);
        currentWindowPeak = juce::jmax (currentWindowPeak, blockPeak);

        if (blockPeak == 0.0f)
            ++silentBlocks;

        if (blocksThisWindow >= blocksPerWindow)
        {
            windowPeaks.push_back (currentWindowPeak);
            currentWindowPeak = 0.0f;
            blocksThisWindow = 0;
        }
    }

    /** @returns the ratio of the loudest window to the quietest sounding one.

        Useful where the patch does not change; a session that keeps changing
        patches varies legitimately, and `trend` is the question to ask there.
    */
    [[nodiscard]] float windowRatio() const
    {
        auto lowest = std::numeric_limits<float>::max();
        auto highest = 0.0f;

        for (const auto peak : windowPeaks)
        {
            if (peak <= 0.0f)
                continue;

            lowest = juce::jmin (lowest, peak);
            highest = juce::jmax (highest, peak);
        }

        return (highest > 0.0f && lowest < std::numeric_limits<float>::max())
                   ? highest / lowest
                   : 1.0f;
    }

    /** @returns how much louder the last third of the run is than the first,
        as a ratio of medians.

        The question a soak actually asks. A range is the wrong statistic when
        the patch keeps changing — a quiet patch and a loud one are both
        correct — while a *trend* upward is what compounding feedback, a stuck
        envelope or an accumulating filter state all look like.
    */
    [[nodiscard]] float trend() const
    {
        if (windowPeaks.size() < 6)
            return 1.0f;

        const auto third = windowPeaks.size() / 3;

        const auto medianOf = [] (std::vector<float> values)
        {
            if (values.empty())
                return 0.0f;

            std::sort (values.begin(), values.end());
            return values[values.size() / 2];
        };

        const auto first = medianOf ({ windowPeaks.begin(), windowPeaks.begin() + static_cast<long> (third) });
        const auto last = medianOf ({ windowPeaks.end() - static_cast<long> (third), windowPeaks.end() });

        return first > 0.0f ? last / first : 1.0f;
    }
};

/** Renders a long stretch block by block, calling @p beforeBlock with the
    block's first sample so a test can do something to the instance as it goes,
    and watching the output rather than keeping it.
*/
void soakRender (juce::AudioPluginInstance& instance,
                 int totalSamples,
                 int blockSize,
                 SoakWatch& watch,
                 const std::vector<TimedMidi>& schedule,
                 const std::function<void (int)>& beforeBlock = {})
{
    // The events are a schedule of absolute sample positions, dispatched into
    // whichever block contains each one.
    //
    // The first version of these tests placed events by testing the block's
    // first sample against a period — `start % 48000 == 0` — which only fires
    // when the period is a multiple of the block size. At 512 samples a block,
    // 48000 is not, so most note-ons and *every* note-off silently never
    // happened, one note was held for the whole run, and the "tail" that
    // followed was that note still playing. It looked exactly like a stuck
    // voice in Apollo (ADR-0080).
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const auto length = juce::jmin (blockSize, totalSamples - start);

        midi.clear();
        block.setSize (2, length, false, false, true);
        block.clear();

        for (const auto& event : schedule)
            if (event.sample >= start && event.sample < start + length)
                midi.addEvent (event.message, event.sample - start);

        if (beforeBlock)
            beforeBlock (start);

        instance.processBlock (block, midi);
        watch.observe (block);
    }
}

/** Notes at a steady rate, each held for @p holdSamples, over @p totalSamples.

    Built as a schedule so that every note-on has its note-off, whatever block
    size the render happens to use.
*/
std::vector<TimedMidi> noteSchedule (int totalSamples, int everySamples, int holdSamples,
                                     int firstNote = 40, int step = 2, int noteCount = 12)
{
    std::vector<TimedMidi> schedule;

    for (int i = 0, at = 0; at < totalSamples; ++i, at += everySamples)
    {
        const auto noteNumber = firstNote + (i % noteCount) * step;

        schedule.push_back ({ at, juce::MidiMessage::noteOn (1, noteNumber, (juce::uint8) 110) });
        schedule.push_back ({ juce::jmin (at + holdSamples, totalSamples - 1),
                              juce::MidiMessage::noteOff (1, noteNumber) });
    }

    return schedule;
}

/** A patch of the given index, distinct from its neighbours in ways a soak
    can hear: a different wavetable, filter, envelope and rack.
*/
void applyPatch (juce::AudioPluginInstance& instance, int index)
{
    const auto fraction = static_cast<float> (index % 8) / 7.0f;

    setPlain (instance, "osc1_wavetable", static_cast<float> (index % 4));
    setPlain (instance, "osc1_position", fraction);
    setPlain (instance, "osc1_unison", 1.0f + static_cast<float> (index % 6));
    setPlain (instance, "osc2_level", fraction * 0.8f);
    setPlain (instance, "sub_level", 0.2f + fraction * 0.5f);
    setPlain (instance, "noise_level", fraction * 0.15f);
    setPlain (instance, "filter1_type", static_cast<float> (index % 5));
    setPlain (instance, "filter1_cutoff", 200.0f + fraction * 12000.0f);
    setPlain (instance, "filter1_resonance", 0.5f + fraction * 3.0f);
    setPlain (instance, "env1_attack", 1.0f + fraction * 200.0f);
    setPlain (instance, "env1_release", 40.0f + fraction * 600.0f);
    setPlain (instance, "fx_slot1", static_cast<float> (1 + (index % 6)));
    setPlain (instance, "fx_slot2", static_cast<float> (1 + ((index + 3) % 6)));
    setPlain (instance, "master_gain", -12.0f + fraction * 6.0f);
}

/** The worst patch Apollo's own controls can build: every source at full, a
    full rack, maximum feedback, a long reverb and both filters resonating.
*/
void applyHostilePatch (juce::AudioPluginInstance& instance)
{
    setPlain (instance, "osc1_level", 1.0f);
    setPlain (instance, "osc1_unison", 16.0f);
    setPlain (instance, "osc2_level", 1.0f);
    setPlain (instance, "osc2_unison", 16.0f);
    setPlain (instance, "sub_level", 1.0f);
    setPlain (instance, "noise_level", 1.0f);

    setPlain (instance, "filter1_resonance", 10.0f);
    setPlain (instance, "filter2_resonance", 10.0f);

    setPlain (instance, "fx_slot1", 1.0f);   // distortion
    setPlain (instance, "fx_distortion_drive", 36.0f);
    setPlain (instance, "fx_distortion_mix", 1.0f);
    setPlain (instance, "fx_slot2", 2.0f);   // delay
    setPlain (instance, "fx_delay_feedback", 1.0f);
    setPlain (instance, "fx_delay_mix", 1.0f);
    setPlain (instance, "fx_slot3", 3.0f);   // reverb
    setPlain (instance, "fx_reverb_decay", 20000.0f);
    setPlain (instance, "fx_reverb_mix", 1.0f);
    setPlain (instance, "fx_slot4", 5.0f);   // compressor
    setPlain (instance, "fx_compressor_makeup", 12.0f);
    setPlain (instance, "master_gain", 6.0f);
}

} // namespace

class ReliabilityTests final : public juce::UnitTest
{
public:
    ReliabilityTests() : juce::UnitTest ("Reliability", "Reliability") {}

    void runTest() override
    {
        logMessage ("    soak scale: x" + juce::String (soakScale())
                    + (soakScale() == 1 ? " (the default; --soak N multiplies every case)" : ""));

        testALongSession();
        testRepeatedLoadAndUnload();
        testPresetChangesWhilePlaying();
        testAutomationEveryBlock();
        testRepeatedDeviceChanges();
        testMalformedStateWhilePlaying();
        testExtremeFeedbackSettles();
    }

private:
    //==========================================================================
    void testALongSession()
    {
        beginTest ("A long session stays finite, stays in range, and ends in silence");

        // The central soak: notes arriving and releasing, patches changing, a
        // transport running, for as long as the scale says. What it watches
        // for is drift — a level that creeps window by window — rather than a
        // single wrong sample.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const auto seconds = soaked (20);
        const auto totalSamples = seconds * 48000;

        // One window a second, so the trend has something to be a trend over.
        SoakWatch watch (48000 / 512);

        // A note every half second, held for a third of a second: overlapping,
        // so voices are allocated and stolen throughout rather than only at
        // the start.
        const auto schedule = noteSchedule (totalSamples, 24000, 16000, 36, 7, 24);
        const auto notesPlayed = static_cast<int> (schedule.size()) / 2;

        auto patch = 0;
        auto lastPatchAt = -1;

        soakRender (*instance, totalSamples, 512, watch, schedule,
                    [&] (int start)
                    {
                        // A new patch every two seconds, which is a user
                        // auditioning sounds. Counted in whole seconds rather
                        // than by a block boundary, so it happens whatever the
                        // block size.
                        const auto twoSeconds = start / 96000;

                        if (twoSeconds != lastPatchAt)
                        {
                            applyPatch (*instance, patch++);
                            lastPatchAt = twoSeconds;
                        }
                    });

        expect (watch.everyBlockFinite, "Every sample of " + juce::String (seconds) + " s was finite");
        expect (watch.loudest < 8.0f, "Nothing ran away: loudest " + juce::String (watch.loudest, 3));
        expect (watch.blocks > 0 && watch.silentBlocks < watch.blocks,
                "It was making sound rather than nothing");

        // The level does not creep. A soak that ends louder than it began is
        // the shape of a compounding feedback path or a stuck envelope — and
        // a *trend* is the right statistic here, because the patch changes
        // every two seconds and a quiet patch is not a fault.
        const auto trend = watch.trend();
        logMessage ("    " + juce::String (seconds) + " s, " + juce::String (watch.blocks)
                    + " blocks, " + juce::String (patch) + " patches, " + juce::String (notesPlayed)
                    + " notes; loudest " + juce::String (watch.loudest, 3)
                    + ", last third against first " + juce::String (trend, 2) + "x");

        expect (trend < 8.0f, "The session is not getting louder as it goes: " + juce::String (trend, 2) + "x");

        // And it still stops. Everything above could be true of an instrument
        // that had quietly stopped responding to note-offs — which is what the
        // first version of this test accidentally produced in itself.
        //
        // Nothing but the envelope's own release is asked for here: whatever
        // patch the soak ended on has a rack in it, so the rack is emptied
        // first and the question is only whether the engine let go.
        for (const auto* slot : { "fx_slot1", "fx_slot2", "fx_slot3", "fx_slot4", "fx_slot5", "fx_slot6" })
            setPlain (*instance, slot, 0.0f);

        const auto tail = render (*instance, 48000 * 4, 512, {});
        expectEquals (peak (tail, 48000 * 3, 48000), 0.0f,
                      "Exact silence once the last note has released");
    }

    //==========================================================================
    void testRepeatedLoadAndUnload()
    {
        beginTest ("Loading and unloading many times leaves nothing behind");

        // A host does this while scanning, while a user auditions plugins, and
        // every time a project opens or closes. What it looks for is memory
        // that never comes back.
        const auto cycles = soaked (25);
        auto played = 0;

        // The first few cycles pay for the shared wavetables and whatever the
        // allocator keeps; the measurement starts after them, so what is being
        // watched is growth rather than start-up.
        const auto settleCycles = juce::jmin (5, cycles / 2);
        std::size_t afterSettling = 0;

        for (int i = 0; i < cycles; ++i)
        {
            juce::String error;
            auto instance = loadPrepared (48000.0, 256, error);

            if (instance == nullptr)
                continue;

            const auto output = render (*instance, 12000, 256, note (60, 0, 8000));

            if (peak (output) > 0.01f && allFinite (output))
                ++played;

            instance->releaseResources();
            instance.reset();

            if (i == settleCycles)
                afterSettling = benchmarks::footprintBytes();
        }

        const auto atTheEnd = benchmarks::footprintBytes();

        expectEquals (played, cycles, "Every instance played");

        if (afterSettling == 0 || atTheEnd == 0)
        {
            logMessage ("    this platform does not report a resident footprint; growth not checked");
            return;
        }

        const auto growthMb = (static_cast<double> (atTheEnd) - static_cast<double> (afterSettling))
                            / (1024.0 * 1024.0);

        logMessage ("    " + juce::String (cycles) + " cycles; footprint after settling "
                    + juce::String (static_cast<double> (afterSettling) / (1024.0 * 1024.0), 1)
                    + " MB, at the end "
                    + juce::String (static_cast<double> (atTheEnd) / (1024.0 * 1024.0), 1)
                    + " MB, growth " + juce::String (growthMb, 1) + " MB over "
                    + juce::String (cycles - settleCycles) + " cycles");

        // An instrument is about 2.6 MB (§5b). Leaking one per cycle would
        // show as tens of megabytes here; this bound is loose enough to
        // survive an allocator that holds pages back and tight enough that a
        // leaked instance cannot hide.
        expect (growthMb < 24.0, "The footprint did not grow by an instrument per cycle: "
                                     + juce::String (growthMb, 1) + " MB");
    }

    //==========================================================================
    void testPresetChangesWhilePlaying()
    {
        beginTest ("Hundreds of state recalls while a note sounds change the sound and nothing else");

        // A user auditioning presets does not stop playing first. Each recall
        // replaces every parameter underneath a sounding voice, which is the
        // moment a half-applied state would show.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        auto source = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr && source != nullptr, error);

        if (instance == nullptr || source == nullptr)
            return;

        // A handful of genuinely different states, captured through the host's
        // own route rather than assembled by hand.
        std::vector<juce::MemoryBlock> states;

        for (int i = 0; i < 6; ++i)
        {
            applyPatch (*source, i * 3);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer none;

            for (int b = 0; b < 4; ++b)
            {
                block.clear();
                source->processBlock (block, none);
            }

            juce::MemoryBlock state;
            source->getStateInformation (state);
            states.push_back (std::move (state));
        }

        const auto recalls = soaked (120);
        const auto totalSamples = recalls * 4096;
        const auto schedule = noteSchedule (totalSamples, 16384, 12000, 48, 1, 24);

        SoakWatch watch (48);
        auto recalled = 0;
        auto lastRecallAt = -1;

        soakRender (*instance, totalSamples, 512, watch, schedule,
                    [&] (int start)
                    {
                        const auto slot = start / 4096;

                        if (slot == lastRecallAt)
                            return;

                        lastRecallAt = slot;

                        const auto& state = states[static_cast<std::size_t> (recalled) % states.size()];
                        instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                        ++recalled;
                    });

        logMessage ("    " + juce::String (recalled) + " state recalls under a sounding voice; loudest "
                    + juce::String (watch.loudest, 3));

        expect (watch.everyBlockFinite, "Every sample stayed finite");
        expect (watch.loudest < 8.0f, "Nothing ran away: " + juce::String (watch.loudest, 3));
        expectEquals (recalled, recalls, "Every recall was applied");

        // And the instrument is still an instrument afterwards.
        const auto after = render (*instance, 24000, 512, note (60, 0, 12000));
        expect (peak (after) > 0.001f && allFinite (after), "It still plays");
    }

    //==========================================================================
    void testAutomationEveryBlock()
    {
        beginTest ("Automation moving every parameter every block does not break the sound");

        // A host playing back dense automation, or a user dragging a macro.
        // Every parameter moves, including the ones that reconfigure the rack,
        // so this is also the chain being rebuilt hundreds of times a second.
        juce::String error;
        auto instance = loadPrepared (48000.0, 256, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        auto parameters = userParameters (*instance);
        expect (! parameters.empty());

        const auto blocks = soaked (400);
        const auto totalSamples = blocks * 256;
        const auto schedule = noteSchedule (totalSamples, 12800, 9000, 52, 3, 8);

        SoakWatch watch (64);
        auto moves = 0;

        soakRender (*instance, totalSamples, 256, watch, schedule,
                    [&] (int start)
                    {
                        // A sweep across the whole parameter list, a slice of
                        // it per block, so every parameter is moved many times
                        // over the run.
                        const auto block = start / 256;

                        for (std::size_t i = 0; i < parameters.size(); i += 8)
                        {
                            const auto index = (i + static_cast<std::size_t> (block)) % parameters.size();
                            const auto phase = static_cast<float> ((block + static_cast<int> (index)) % 64) / 63.0f;

                            parameters[index]->setValueNotifyingHost (phase);
                            ++moves;
                        }
                    });

        logMessage ("    " + juce::String (blocks) + " blocks, " + juce::String (moves)
                    + " parameter moves; loudest " + juce::String (watch.loudest, 3));

        expect (watch.everyBlockFinite, "Every sample stayed finite");
        expect (watch.loudest < 16.0f, "Nothing ran away: " + juce::String (watch.loudest, 3));
    }

    //==========================================================================
    void testRepeatedDeviceChanges()
    {
        beginTest ("Repeated sample-rate and block-size changes leave the instrument in tune");

        // A user switching interfaces, or a host changing its buffer while the
        // session is open. Everything derived from the rate has to be derived
        // again, every time, and nothing from the previous rate may survive.
        juce::String error;
        auto instance = load (error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        const double rates[] = { 48000.0, 44100.0, 96000.0, 88200.0, 22050.0, 192000.0 };
        const int blockSizes[] = { 512, 64, 2048, 128, 1024, 32 };

        const auto rounds = soaked (3);
        auto inTune = 0;
        auto attempts = 0;

        for (int round = 0; round < rounds; ++round)
        {
            for (std::size_t i = 0; i < std::size (rates); ++i)
            {
                const auto rate = rates[i];
                const auto blockSize = blockSizes[(i + static_cast<std::size_t> (round)) % std::size (blockSizes)];

                instance->releaseResources();
                instance->setPlayConfigDetails (0, 2, rate, blockSize);
                instance->prepareToPlay (rate, blockSize);

                if (round == 0 && i == 0)
                {
                    // The sub oscillator alone: a sine, and the only source
                    // whose frequency can be read by counting crossings.
                    setPlain (*instance, "osc1_level", 0.0f);
                    setPlain (*instance, "sub_level", 1.0f);
                }

                const auto length = static_cast<int> (rate / 2);
                const auto output = render (*instance, length, blockSize, note (69, 0, length - 200));
                const auto frequency = estimateFrequency (output, rate, length / 4, length / 3);

                ++attempts;

                if (allFinite (output) && std::abs (frequency - 220.0) < 0.5)
                    ++inTune;
                else
                    logMessage ("    round " + juce::String (round) + ", " + juce::String (rate, 0)
                                + " Hz / " + juce::String (blockSize) + ": " + juce::String (frequency, 2) + " Hz");
            }
        }

        instance->releaseResources();

        logMessage ("    " + juce::String (attempts) + " device changes across six rates and six block sizes");
        expectEquals (inTune, attempts, "In tune after every change");
    }

    //==========================================================================
    void testMalformedStateWhilePlaying()
    {
        beginTest ("A stream of malformed documents changes nothing and stops nothing");

        // What a corrupted project, a truncated autosave or another plugin's
        // chunk looks like arriving mid-session. Refusing is the normal path
        // (CLAUDE.md §33); refusing *every time*, without drifting, is what a
        // soak adds.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        auto reference = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr && reference != nullptr, error);

        if (instance == nullptr || reference == nullptr)
            return;

        applyPatch (*instance, 5);
        applyPatch (*reference, 5);

        juce::AudioBuffer<float> settle (2, 512);
        juce::MidiBuffer none;

        for (int i = 0; i < 4; ++i)
        {
            settle.clear();
            instance->processBlock (settle, none);
            reference->processBlock (settle, none);
        }

        // A good document, mutated in every way a damaged one could be.
        juce::MemoryBlock good;
        instance->getStateInformation (good);

        juce::Random random (0x12a);
        const auto attempts = soaked (150);
        const auto totalSamples = attempts * 2048;
        const auto schedule = noteSchedule (totalSamples, 8192, 6000, 55, 2, 6);

        SoakWatch watch (32);
        auto offered = 0;
        auto lastOfferAt = -1;

        soakRender (*instance, totalSamples, 512, watch, schedule,
                    [&] (int start)
                    {
                        const auto slot = start / 2048;

                        if (slot == lastOfferAt)
                            return;

                        lastOfferAt = slot;

                        juce::MemoryBlock corrupted (good);

                        switch (offered % 5)
                        {
                            case 0: // truncated, at a different point each time
                                corrupted.setSize (static_cast<std::size_t> (
                                    random.nextInt (juce::jmax (1, static_cast<int> (good.getSize())))));
                                break;

                            case 1: // a handful of flipped bytes
                                for (int i = 0; i < 8 && corrupted.getSize() > 0; ++i)
                                    corrupted[static_cast<std::size_t> (
                                        random.nextInt (static_cast<int> (corrupted.getSize())))] =
                                            static_cast<char> (random.nextInt (256));
                                break;

                            case 2: // not Apollo's at all
                            {
                                const juce::String foreign ("<?xml version=\"1.0\"?><OtherSynth cutoff=\"1\"/>");
                                corrupted.replaceAll (foreign.toRawUTF8(), foreign.getNumBytesAsUTF8());
                                break;
                            }

                            case 3: // nothing
                                corrupted.reset();
                                break;

                            default: // random bytes of a plausible size
                                corrupted.setSize (2048);

                                for (std::size_t i = 0; i < corrupted.getSize(); ++i)
                                    corrupted[i] = static_cast<char> (random.nextInt (256));

                                break;
                        }

                        instance->setStateInformation (corrupted.getData(),
                                                       static_cast<int> (corrupted.getSize()));
                        ++offered;
                    });

        logMessage ("    " + juce::String (offered) + " malformed documents offered mid-render");

        expect (watch.everyBlockFinite, "Every sample stayed finite");
        expect (watch.loudest < 8.0f, "Nothing ran away");
        expect (watch.silentBlocks < watch.blocks, "It went on making sound");

        // Every parameter is still a legal value.
        //
        // NOT "nothing moved", which is what this asserted first and which is
        // not true of arbitrary corruption: a flipped byte inside a value
        // produces a document that is *still valid* — right root, right schema,
        // well-formed XML, a different number — and a valid document is meant
        // to be applied. Only a longer soak found that, because it takes a few
        // hundred attempts before a flip lands somewhere that parses
        // (ADR-0080).
        //
        // What a refused document must never do is leave a parameter holding
        // something that is not a value at all, and the documents that *are*
        // refusable are checked exactly, below.
        auto illegal = 0;

        for (auto* parameter : userParameters (*instance))
        {
            const auto value = parameter->getValue();

            if (! std::isfinite (value) || value < 0.0f || value > 1.0f)
            {
                ++illegal;

                if (illegal <= 5)
                    logMessage ("    " + parameter->getName (64) + ": " + juce::String (value));
            }
        }

        expectEquals (illegal, 0, "Every parameter still holds a legal value");

        // And the documents that can be refused are refused completely, with
        // nothing of them applied. These are exact rather than random, so the
        // claim is a claim rather than a probability.
        juce::MemoryBlock empty;
        const juce::String foreign ("<?xml version=\"1.0\"?><OtherSynth cutoff=\"1\"/>");
        juce::MemoryBlock foreignDocument (foreign.toRawUTF8(), foreign.getNumBytesAsUTF8());

        juce::MemoryBlock rubbish (2048);

        for (std::size_t i = 0; i < rubbish.getSize(); ++i)
            rubbish[i] = static_cast<char> (random.nextInt (256));

        auto refusedCleanly = 0;
        auto refusals = 0;

        const auto offerAndCompare = [&] (const juce::MemoryBlock& document, const juce::String& what)
        {
            auto subject = loadPrepared (48000.0, 512, error);
            auto untouched = loadPrepared (48000.0, 512, error);

            if (subject == nullptr || untouched == nullptr)
                return;

            applyPatch (*subject, 7);
            applyPatch (*untouched, 7);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer nothing;

            for (int i = 0; i < 4; ++i)
            {
                block.clear();
                subject->processBlock (block, nothing);
                untouched->processBlock (block, nothing);
            }

            subject->setStateInformation (document.getData(), static_cast<int> (document.getSize()));

            auto moved = 0;

            for (auto* parameter : userParameters (*subject))
                if (auto* other = findParameter (*untouched, parameter->getName (256)))
                    if (std::abs (parameter->getValue() - other->getValue()) > 1.0e-5f)
                        ++moved;

            ++refusals;

            if (moved == 0)
                ++refusedCleanly;
            else
                logMessage ("    " + what + " moved " + juce::String (moved) + " parameters");
        };

        offerAndCompare (empty, "an empty document");
        offerAndCompare (foreignDocument, "another product's XML");
        offerAndCompare (rubbish, "two kilobytes of noise");

        for (const auto fraction : { 0.25, 0.5, 0.75, 0.9, 0.99 })
        {
            juce::MemoryBlock truncated (good);
            truncated.setSize (static_cast<std::size_t> (static_cast<double> (good.getSize()) * fraction));
            offerAndCompare (truncated, "a document truncated to "
                                            + juce::String (fraction * 100.0, 0) + "%");
        }

        expectEquals (refusedCleanly, refusals,
                      "Every refusable document was refused with nothing applied");
    }

    //==========================================================================
    void testExtremeFeedbackSettles()
    {
        beginTest ("The worst patch the controls allow stays bounded and still reaches silence");

        // Maximum delay feedback, a twenty-second reverb, two resonant filters,
        // a hard clipper and 12 dB of makeup, held for as long as the scale
        // says. §5c measures a minute of this through the processor; here it
        // runs through a host, with notes arriving throughout.
        juce::String error;
        auto instance = loadPrepared (48000.0, 512, error);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        applyHostilePatch (*instance);

        const auto seconds = soaked (12);
        const auto totalSamples = seconds * 48000;
        const auto schedule = noteSchedule (totalSamples, 48000, 24000, 40, 2, 12);
        const auto notes = static_cast<int> (schedule.size()) / 2;

        SoakWatch watch (48000 / 512);

        soakRender (*instance, totalSamples, 512, watch, schedule);

        logMessage ("    " + juce::String (seconds) + " s of the hostile patch, " + juce::String (notes)
                    + " notes; loudest " + juce::String (watch.loudest, 2)
                    + ", window ratio " + juce::String (watch.windowRatio(), 2));

        expect (watch.everyBlockFinite, "Every sample stayed finite");

        // The patch asks for about 88 dB of deliberate gain, so it is loud by
        // construction (§5c). What must not happen is compounding: the last
        // window must not tower over the first.
        expect (watch.windowRatio() < 100.0f,
                "It did not compound: window ratio " + juce::String (watch.windowRatio(), 2));

        // How long this patch takes to reach silence is arithmetic, not a
        // guess, and the first version of this test guessed.
        //
        // The delay's feedback is 0.95 at its maximum (Delay::maximumFeedback),
        // so a half-second line loses 0.45 dB a repeat: from a peak near 10 to
        // the 1e-18 flush point is about 830 repeats, which is seven minutes.
        // Asserting silence within fifty seconds was asserting something
        // impossible — the same mistake §5c records making and fixing, in the
        // same place.
        //
        // So the question asked here is the one that can be answered in the
        // time: is it *decaying*. Exact silence is asserted below on a patch
        // whose decay is short enough to render.
        const auto decaying = render (*instance, 48000 * 30, 512, {});

        expect (allFinite (decaying), "The tail stayed finite");

        const auto firstSecond = peak (decaying, 0, 48000);
        const auto lastSecond = peak (decaying, 48000 * 29, 48000);

        logMessage ("    tail after the last note: " + juce::String (firstSecond, 3)
                    + " in the first second, " + juce::String (lastSecond, 5) + " in the thirtieth");

        // Measured rather than hoped for: 0.95 feedback on a half-second line
        // loses about 0.45 dB a repeat, and the compressor's 12 dB of makeup
        // lifts the tail as it falls, so the fall is real but slow. What must
        // not happen is that it stops falling.
        expect (lastSecond < firstSecond * 0.5f,
                "It is decaying rather than sustaining: " + juce::String (firstSecond, 3)
                    + " to " + juce::String (lastSecond, 5));

        // And a feedback chain does reach exact silence, on settings whose
        // arithmetic fits: 0.6 feedback on a 150 ms line is 81 repeats to the
        // flush point, about twelve seconds, and a two-second reverb is gone in
        // about the same.
        setPlain (*instance, "fx_delay_feedback", 0.6f);
        setPlain (*instance, "fx_delay_time", 150.0f);
        setPlain (*instance, "fx_reverb_decay", 2000.0f);

        const auto settling = render (*instance, 48000 * 25, 512, {});
        const auto remaining = peak (settling, 48000 * 23, 48000 * 2);

        expect (allFinite (settling));

        // Inaudible rather than exactly zero, and the difference is the point.
        // Exact zero is what the feedback effects reach once their own flush
        // thresholds are crossed, and how long that takes depends on how much
        // energy went in: a soak six times longer puts the tail at 2.6e-37
        // here rather than at zero. That is -727 dBFS, which is silence by any
        // measure a user or a format has, and asserting the stronger thing
        // would make the test fail for running longer (ADR-0080).
        //
        // The exact-zero claim is asserted where it is guaranteed and quick:
        // the engine alone, with the rack emptied, in the long-session test.
        expect (remaining < 1.0e-9f,
                "Inaudible once the arithmetic says it should be: "
                    + juce::String (juce::Decibels::gainToDecibels (remaining), 0) + " dBFS");
    }
};

static ReliabilityTests reliabilityTests;

} // namespace apollo::host
