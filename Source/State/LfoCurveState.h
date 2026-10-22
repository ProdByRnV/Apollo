#pragma once

/*
    The four drawn LFO curves: where they are kept, how they are saved, and how
    the audio thread gets them (Phase 13f-1).

    A curve is not a parameter. It is a list of up to thirty-two points, which no
    host can automate and no knob can hold, so it lives where the other things
    that are not parameters live: as a child of the state tree, beside the MIDI
    map and the preset metadata (ADR-0043's argument again). That one decision
    gives it everything else for nothing — a host project saves it, a preset
    saves it, a preset load replaces it, and an older Apollo reading the document
    ignores an element it does not know. No schema bump.

        <LFOCURVES>
          <CURVE lfo="2">
            <POINT x="0" y="-1" bend="0"/>
            ...
          </CURVE>
        </LFOCURVES>

    ONLY CURVES THAT DIFFER FROM THE DEFAULT ARE WRITTEN, and an LFO with no
    `<CURVE>` has the default. So a patch nobody drew on saves exactly the
    document it saved before this existed, and a preset written before it loads
    with every LFO on the default curve rather than on whatever was drawn last.

    THREADS. Writers — the bridge setting a curve, a state load restoring them —
    run on the message thread, or on whatever thread a host calls
    `setStateInformation` from, so they are serialised by a lock the audio
    thread never touches. The audio thread only ever calls `acquireTable`, which
    is the reader side of a triple buffer and waits for nothing.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/LFO/LfoCurve.h"

#include <array>
#include <mutex>
#include <vector>

namespace apollo::state
{

/** LFOs that can carry a drawn curve. */
inline constexpr int numLfoCurves = 4;

class LfoCurveStore
{
public:
    explicit LfoCurveStore (juce::AudioProcessorValueTreeState& stateToUse);

    LfoCurveStore (const LfoCurveStore&) = delete;
    LfoCurveStore& operator= (const LfoCurveStore&) = delete;

    /** Replaces LFO @p lfoIndex's curve (0-based), writes it into the state
        tree and hands it to the audio thread.

        @returns false, changing nothing, if the index is out of range or the
                 curve is not valid as it stands. Refused rather than repaired:
                 a curve the page sent that needed repair is a page defect.
    */
    [[nodiscard]] bool set (int lfoIndex, const dsp::LfoCurve& curve);

    /** @returns LFO @p lfoIndex's curve, or the default for an index out of
        range. */
    [[nodiscard]] dsp::LfoCurve get (int lfoIndex) const;

    /** Rebuilds every curve from the state tree, after a host or preset load
        replaced it. A curve missing from the tree is the default; one that is
        malformed is repaired where it can be and the default where it cannot,
        the way a restored MIDI mapping is — a hand-edited document must not put
        an LFO into a state the editor could not.

        @returns how many curves differed from the default.
    */
    int restoreFromState();

    /** AUDIO THREAD. The newest rendered table for LFO @p lfoIndex, valid until
        the next call for the same index. Never null for an index in range. */
    [[nodiscard]] const float* acquireTable (int lfoIndex) noexcept;

    /** Makes the best curve it can of @p points read from somewhere untrusted:
        values clamped into range, positions sorted, the ends pinned to the
        start and end of the cycle, anything past the limit dropped. @returns
        the default if fewer than two usable points remain. */
    [[nodiscard]] static dsp::LfoCurve repair (const std::vector<dsp::LfoCurvePoint>& points);

    /** Element and property names. Part of the serialized contract. */
    static constexpr const char* curvesTreeType = "LFOCURVES";
    static constexpr const char* curveTreeType = "CURVE";
    static constexpr const char* pointTreeType = "POINT";
    static constexpr const char* lfoProperty = "lfo";
    static constexpr const char* xProperty = "x";
    static constexpr const char* yProperty = "y";
    static constexpr const char* bendProperty = "bend";

private:
    /** Rewrites `<LFOCURVES>` from `curves`. Writer lock held. */
    void writeToState();

    juce::AudioProcessorValueTreeState& apvts;

    mutable std::mutex writerLock;

    std::array<dsp::LfoCurve, static_cast<std::size_t> (numLfoCurves)> curves;
    std::array<dsp::LfoCurveExchange, static_cast<std::size_t> (numLfoCurves)> exchanges;
};

} // namespace apollo::state
