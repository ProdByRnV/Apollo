#pragma once

/*
    Undo and redo (Phase 18a, ADR-0119).

    WHAT A STEP IS. One thing somebody did in Apollo's window: a knob turned
    from where it was picked up to where it was let go, a routing dropped, a
    curve drawn, a macro named, a wavetable loaded, a preset loaded. A step
    keeps what it changed and nothing else — the parameters it moved, before
    and after, and the parts of the sound that are not parameters (the
    preset's name, the drawn curves, the macros' names, the user's content)
    only where they differ.

    So undoing one step never undoes another. A knob turned while a wavetable
    was still loading is a step of its own, and taking the wavetable back
    leaves the knob where it was turned to.

    WHAT IS NOT IN IT. Host automation, a MIDI controller and the modulation
    matrix move parameters without anybody editing anything, and a history
    that recorded them would fill with a sweep nobody made. MIDI assignments
    are a setting of the instrument rather than part of the sound, and stay
    as they are when a step is undone. A state replaced from outside — the
    host opening a project, a program change — empties the history, which
    describes a sound that is no longer in front of anybody.

    MEMORY. At most `maxSteps` steps, and the oldest go first once what they
    hold passes `maxBytes`: a step that loads a wavetable keeps the audio on
    both sides of it. Text the live state already holds is shared rather than
    copied, and counted once.

    THREADS. The message thread, except `clear`, which a host may call from
    whatever thread it restores state on. The audio thread never touches this.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace apollo::state
{

class EditHistory
{
public:
    /** The most steps kept. */
    static constexpr int maxSteps = 100;

    /** The most the kept steps may hold, in bytes of text, before the oldest
        go. Enough for dozens of steps that each load a wavetable. */
    static constexpr std::size_t maxBytes = 64u * 1024u * 1024u;

    /** Some of the sound, as it was at a moment.

        Parameters are kept as normalised values by registry index. The rest of
        the sound is kept as copies of the state tree's children, by type; an
        invalid tree means there was none of that type.
    */
    struct Snapshot
    {
        std::vector<std::pair<int, float>> values;
        std::vector<std::pair<juce::Identifier, juce::ValueTree>> document;

        /** True when `document` is the whole of it, so that a type which only
            exists afterwards counts as a change too. */
        bool wholeDocument = false;
    };

    explicit EditHistory (juce::AudioProcessorValueTreeState& stateToUse);

    EditHistory (const EditHistory&) = delete;
    EditHistory& operator= (const EditHistory&) = delete;

    //==========================================================================
    // Recording.

    /** Every parameter and the whole of the rest of the sound. */
    [[nodiscard]] Snapshot capture() const;

    /** Every parameter, and nothing else. */
    [[nodiscard]] Snapshot captureParameters() const;

    /** Exactly these parameters, by registry index, and these children. */
    [[nodiscard]] Snapshot capture (const std::vector<int>& parameterIndices,
                                    const std::vector<juce::Identifier>& children) const;

    /** Records what changed between @p before and now, as one step called
        @p name. Nothing changed, nothing recorded. Clears what could be
        redone, as any new edit does. */
    void commit (const Snapshot& before, const juce::String& name);

    /** A gesture on one parameter started: the page picked a control up.
        Gestures that overlap — the two of an equaliser handle — make one
        step, which ends when the last of them does. */
    void gestureBegan (int parameterIndex);

    /** A gesture ended. One that never began is ignored. */
    void gestureEnded (int parameterIndex);

    /** @returns true while any gesture is open. */
    [[nodiscard]] bool isInGesture() const noexcept { return ! openGestures.empty(); }

    /** An edit that finishes later — content, which loads on a worker —
        keyed by @p key. A second for the same key before the first finished
        keeps the first's before: the two are one edit from where the user
        stood. */
    void hold (int key, Snapshot before, const juce::String& name);

    /** The held edit for @p key finished: committed. */
    void release (int key);

    /** The held edit for @p key did not happen. */
    void drop (int key);

    //==========================================================================
    // Undoing.

    /** @returns false, changing nothing, if there is nothing to undo. */
    bool undo();

    /** @returns false, changing nothing, if there is nothing to redo. */
    bool redo();

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    /** What the next undo or redo would take back or put back, or empty. */
    [[nodiscard]] juce::String undoName() const;
    [[nodiscard]] juce::String redoName() const;

    /** @returns how many steps can be undone. Tests. */
    [[nodiscard]] int numUndoable() const;

    /** Forgets everything, and abandons any gesture or held edit. Any
        thread. Tells nobody: whoever replaced the state says so. */
    void clear();

    //==========================================================================

    /** The rest of the sound was put back into the tree by an undo or redo:
        whoever owns what hangs off it rebuilds from it. The processor sets
        this. */
    std::function<void()> onDocumentReplaced;

    /** Something can now be undone or redone that could not, or the names
        changed. MESSAGE THREAD. */
    std::function<void()> onChanged;

private:
    struct Step
    {
        juce::String name;

        struct Value
        {
            int index = 0;
            float before = 0.0f;
            float after = 0.0f;
        };

        struct Child
        {
            juce::Identifier type;
            juce::ValueTree before;
            juce::ValueTree after;
        };

        std::vector<Value> values;
        std::vector<Child> children;
    };

    [[nodiscard]] float valueOf (int index) const;
    void apply (const Step& step, bool forward);
    void push (Step step);
    void trim();
    void finishGesture();
    void announce();

    [[nodiscard]] static bool isSoundChild (const juce::ValueTree& child);
    [[nodiscard]] juce::String nameOf (const std::vector<Step::Value>& values) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<juce::RangedAudioParameter*> parameters;

    mutable std::recursive_mutex lock;
    std::vector<Step> undoable;
    std::vector<Step> redoable;

    /** Parameter indices with a gesture open, one entry per open gesture. */
    std::vector<int> openGestures;

    /** Where each parameter a gesture touched started. */
    std::vector<std::pair<int, float>> gestureStart;

    std::map<int, std::pair<Snapshot, juce::String>> held;
};

} // namespace apollo::state
