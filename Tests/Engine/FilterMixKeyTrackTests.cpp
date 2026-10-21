/*
    A filter's mix and key tracking (Phase 13c-2, PRD §13.2).

    Both are measured through a whole processor playing a note, because both
    live in the voice rather than in a filter: the mix crossfades a slot's input
    against its output, and key tracking re-resolves a voice's coefficients from
    the note it is playing.

      - mix 0 is exactly the unfiltered sound, and a half is half way;
      - a moved mix glides rather than steps;
      - key tracking holds a comb on every note — through a bend, and through
        the matrix's path as well as the plain one;
      - it tracks the other way when negative;
      - and at middle C, or at zero, it changes nothing at all.

    At their defaults neither changes anything, which the regression renders
    hold by passing unregenerated.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Oscillator 1's brightest saw, held, through whatever @p configure sets. */
void makeSaw (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_level", 1.0f);
    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "osc1_position", 1.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
}

struct Render
{
    std::function<void (ApolloAudioProcessor&)> configure {};
    int note = 57;
    int blocks = 40;

    /** Called before the block of that number, to change something mid-note. */
    std::function<void (ApolloAudioProcessor&, int, juce::MidiBuffer&)> during {};
};

[[nodiscard]] std::vector<float> render (const Render& spec)
{
    ApolloAudioProcessor processor;
    makeSaw (processor);

    if (spec.configure)
        spec.configure (processor);

    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < spec.blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, spec.note, 0.9f), 0);

        if (spec.during)
            spec.during (processor, b, midi);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal, std::size_t from)
{
    double sum = 0.0;

    for (auto i = from; i < signal.size(); ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

    return std::sqrt (sum / static_cast<double> (signal.size() - from));
}

[[nodiscard]] double decibels (double ratio)
{
    return 20.0 * std::log10 (std::max (ratio, 1.0e-9));
}

/** A comb on filter 1, at middle C's pitch, ringing hard. */
[[nodiscard]] std::function<void (ApolloAudioProcessor&)> comb (float keyTrack)
{
    return [keyTrack] (ApolloAudioProcessor& processor)
    {
        setPlain (processor, "filter1_type", 5.0f);
        setPlain (processor, "filter1_cutoff", 261.63f);
        setPlain (processor, "filter1_resonance", 5.0f);
        setPlain (processor, "filter1_keytrack", keyTrack);
    };
}

class FilterMixKeyTrackTests final : public juce::UnitTest
{
public:
    FilterMixKeyTrackTests()
        : juce::UnitTest ("Filter mix and key tracking", "Engine")
    {
    }

    void runTest() override
    {
        testMixEnds();
        testMixHalfway();
        testMixGlides();
        testKeyTrackingHoldsTheComb();
        testKeyTrackingFollowsTheBend();
        testKeyTrackingThroughTheMatrixPath();
        testNegativeKeyTracking();
        testKeyTrackingAtMiddleCChangesNothing();
    }

private:
    void testMixEnds()
    {
        beginTest ("A filter at mix 0 is exactly no filter");

        const auto unfiltered = render ({ [] (ApolloAudioProcessor& p)
                                          { setPlain (p, "filter1_type", 0.0f); } });

        const auto dry = render ({ [] (ApolloAudioProcessor& p)
                                   {
                                       setPlain (p, "filter1_cutoff", 300.0f);
                                       setPlain (p, "filter1_resonance", 4.0f);
                                       setPlain (p, "filter1_drive", 0.6f);
                                       setPlain (p, "filter1_mix", 0.0f);
                                   } });

        expect (dry == unfiltered, "a low-pass at 300 Hz with drive, at mix 0, must be sample for sample "
                                   "the same as the filter switched off");

        const auto wet = render ({ [] (ApolloAudioProcessor& p) { setPlain (p, "filter1_cutoff", 300.0f); } });
        expect (wet != unfiltered, "and at the default mix the filter is heard");
    }

    void testMixHalfway()
    {
        beginTest ("A filter at mix 0.5 is half way between its input and its output");

        const auto at = [] (float mix)
        {
            return render ({ [mix] (ApolloAudioProcessor& p)
                             {
                                 setPlain (p, "filter1_cutoff", 400.0f);
                                 setPlain (p, "filter1_mix", mix);
                             } });
        };

        const auto dry = at (0.0f);
        const auto wet = at (1.0f);
        const auto half = at (0.5f);

        float worst = 0.0f;

        for (std::size_t i = 0; i < half.size(); ++i)
            worst = std::max (worst, std::abs (half[i] - 0.5f * (dry[i] + wet[i])));

        logMessage ("    mix 0.5 strays from the midpoint by at most " + juce::String (worst));
        expect (worst < 1.0e-5f, "mix 0.5 strays from the midpoint by " + juce::String (worst));
    }

    void testMixGlides()
    {
        beginTest ("A moved mix glides over 20 ms rather than stepping");

        constexpr int changeAt = 20;

        const auto configure = [] (ApolloAudioProcessor& p) { setPlain (p, "filter1_cutoff", 300.0f); };

        const auto wet = render ({ configure });
        const auto dry = render ({ [configure] (ApolloAudioProcessor& p)
                                   {
                                       configure (p);
                                       setPlain (p, "filter1_mix", 0.0f);
                                   } });

        const auto moved = render ({ configure, 57, 40,
                                     [] (ApolloAudioProcessor& p, int b, juce::MidiBuffer&)
                                     {
                                         if (b == changeAt)
                                             setPlain (p, "filter1_mix", 0.0f);
                                     } });

        // How far along from wet to dry the moved render is, at a sample:
        // `moved = wet + progress * (dry - wet)`, read where the two differ.
        const auto progressAt = [&] (std::size_t from, std::size_t to)
        {
            double num = 0.0, den = 0.0;

            for (auto i = from; i < to; ++i)
            {
                const auto d = static_cast<double> (dry[i]) - static_cast<double> (wet[i]);
                num += (static_cast<double> (moved[i]) - static_cast<double> (wet[i])) * d;
                den += d * d;
            }

            return den > 0.0 ? num / den : 0.0;
        };

        const auto start = static_cast<std::size_t> (changeAt * block);

        expect (moved[start - 1] == wet[start - 1], "before the move nothing has changed");

        const auto early = progressAt (start, start + 48);
        const auto midway = progressAt (start + 432, start + 528);
        const auto after = progressAt (start + 1440, start + 2400);

        logMessage ("    progress after 1 ms " + juce::String (early, 3) + ", 10 ms " + juce::String (midway, 3) + ", 30 ms " + juce::String (after, 4));
        expect (early < 0.06, "the first millisecond is a twentieth of the way, not all of it: "
                                  + juce::String (early, 3));
        expect (midway > 0.4 && midway < 0.6, "ten milliseconds in it is half way: " + juce::String (midway, 3));
        expect (std::abs (after - 1.0) < 1.0e-3, "and after the ramp it is the dry sound: " + juce::String (after, 4));
    }

    void testKeyTrackingHoldsTheComb()
    {
        beginTest ("At full key tracking a comb follows the note");

        // A3 and E4 are off middle C. Without tracking the comb stays at C4 and
        // most of each note's harmonics fall between its teeth; with it, the
        // comb moves to the note and lifts them all.
        for (const auto note : { 57, 64, 45 })
        {
            const auto tracked = rmsOf (render ({ comb (1.0f), note }), 8 * block);
            const auto fixed = rmsOf (render ({ comb (0.0f), note }), 8 * block);
            const auto gain = decibels (tracked / fixed);

            logMessage ("    note " + juce::String (note) + ": tracked comb " + juce::String (gain, 1) + " dB above a fixed one");
            expect (gain > 4.0, "note " + juce::String (note) + ": tracked comb against a fixed one, "
                                    + juce::String (gain, 1) + " dB");
        }
    }

    void testKeyTrackingFollowsTheBend()
    {
        beginTest ("Key tracking follows a bend, so a tuned comb stays tuned");

        const auto bendFrom = [] (int at)
        {
            return [at] (ApolloAudioProcessor&, int b, juce::MidiBuffer& midi)
            {
                // Full wheel: two semitones up, the default range.
                if (b == at)
                    midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
            };
        };

        const auto steady = rmsOf (render ({ comb (1.0f), 60 }), 20 * block);
        const auto trackedBent = rmsOf (render ({ comb (1.0f), 60, 40, bendFrom (10) }), 20 * block);
        const auto fixedBent = rmsOf (render ({ comb (0.0f), 60, 40, bendFrom (10) }), 20 * block);

        logMessage ("    bent a tone: tracked " + juce::String (decibels (trackedBent / steady), 2) + " dB from unbent, " + juce::String (decibels (trackedBent / fixedBent), 1) + " dB above untracked");
        expect (std::abs (decibels (trackedBent / steady)) < 1.5,
                "a tracked comb bent a tone is as loud as unbent: "
                    + juce::String (decibels (trackedBent / steady), 2) + " dB");
        expect (decibels (trackedBent / fixedBent) > 4.0,
                "and much louder than an untracked one bent the same way: "
                    + juce::String (decibels (trackedBent / fixedBent), 1) + " dB");
    }

    void testKeyTrackingThroughTheMatrixPath()
    {
        beginTest ("Key tracking holds when the matrix is also running");

        // Any active routing sends the voice down the modulation path, which
        // resolves its filters every sixteen samples. Velocity to oscillator 1's
        // level by a hair is a routing that changes nothing audible here.
        const auto withRouting = [] (float keyTrack)
        {
            return [keyTrack] (ApolloAudioProcessor& p)
            {
                comb (keyTrack) (p);
                setPlain (p, "mod01_source", 9.0f);
                setPlain (p, "mod01_destination", 6.0f);
                setPlain (p, "mod01_depth", 0.001f);
            };
        };

        const auto tracked = rmsOf (render ({ withRouting (1.0f), 57 }), 8 * block);
        const auto fixed = rmsOf (render ({ withRouting (0.0f), 57 }), 8 * block);

        expect (decibels (tracked / fixed) > 4.0, "through the matrix path the tracked comb is "
                                                      + juce::String (decibels (tracked / fixed), 1)
                                                      + " dB above a fixed one");
    }

    void testNegativeKeyTracking()
    {
        beginTest ("Negative key tracking closes a low-pass as the notes rise");

        const auto lowpass = [] (float keyTrack)
        {
            return [keyTrack] (ApolloAudioProcessor& p)
            {
                setPlain (p, "filter1_cutoff", 1000.0f);
                setPlain (p, "filter1_keytrack", keyTrack);
            };
        };

        // Two octaves above middle C: +1 opens the filter by two octaves, -1
        // closes it by two, and zero sits between.
        const auto opened = rmsOf (render ({ lowpass (1.0f), 84 }), 8 * block);
        const auto neutral = rmsOf (render ({ lowpass (0.0f), 84 }), 8 * block);
        const auto closed = rmsOf (render ({ lowpass (-1.0f), 84 }), 8 * block);

        expect (opened > neutral && neutral > closed,
                "at C6 the low-pass reads " + juce::String (decibels (opened / neutral), 1) + " dB opened and "
                    + juce::String (decibels (closed / neutral), 1) + " dB closed");
    }

    void testKeyTrackingAtMiddleCChangesNothing()
    {
        beginTest ("At middle C, or at zero, key tracking changes nothing");

        const auto plain = render ({ comb (0.0f), 60 });
        const auto tracked = render ({ comb (1.0f), 60 });

        expect (tracked == plain, "middle C is the centre: full tracking there is the untracked sound, "
                                  "sample for sample");

        const auto atZero = render ({ comb (0.0f), 72 });
        const auto lowpassDefault = render ({ [] (ApolloAudioProcessor& p)
                                              {
                                                  comb (0.0f) (p);
                                                  setPlain (p, "filter1_keytrack", 0.0f);
                                              }, 72 });

        expect (atZero == lowpassDefault, "and zero tracking is no tracking at any note");
    }
};

FilterMixKeyTrackTests filterMixKeyTrackTests;

} // namespace
