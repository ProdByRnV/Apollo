#include "State/EditHistory.h"

#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"

#include <algorithm>
#include <set>

namespace apollo::state
{

namespace
{

/** The children of the state tree that are not the sound. APVTS keeps each
    parameter as a PARAM child, which the values cover; MIDIMAP is
    midi::MidiControlManager::mappingsTreeType, a setting of the instrument
    (the tests hold the two to each other). */
constexpr const char* parameterType = "PARAM";
constexpr const char* mappingsType = "MIDIMAP";

/** @returns true if @p a and @p b hold the same text, without reading it
    when they share it, which a copy of the live state does: a wavetable's
    audio is a few megabytes of text, and comparing it with itself on every
    curve drawn would be most of the cost of drawing one. */
[[nodiscard]] bool sameValue (const juce::var& a, const juce::var& b)
{
    if (a.isString() && b.isString())
    {
        const auto first = a.toString();
        const auto second = b.toString();

        return first.getCharPointer().getAddress() == second.getCharPointer().getAddress() || first == second;
    }

    return a.equalsWithSameType (b);
}

[[nodiscard]] bool sameTree (const juce::ValueTree& a, const juce::ValueTree& b)
{
    if (a.isValid() != b.isValid())
        return false;

    if (! a.isValid())
        return true;

    if (a.getType() != b.getType() || a.getNumProperties() != b.getNumProperties()
        || a.getNumChildren() != b.getNumChildren())
        return false;

    for (int i = 0; i < a.getNumProperties(); ++i)
    {
        const auto name = a.getPropertyName (i);

        if (! b.hasProperty (name) || ! sameValue (a.getProperty (name), b.getProperty (name)))
            return false;
    }

    for (int i = 0; i < a.getNumChildren(); ++i)
        if (! sameTree (a.getChild (i), b.getChild (i)))
            return false;

    return true;
}

/** Adds the text @p tree holds to @p total, counting a buffer it shares with
    something already counted once. */
void countBytes (const juce::ValueTree& tree, std::set<const void*>& seen, std::size_t& total)
{
    if (! tree.isValid())
        return;

    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto value = tree.getProperty (tree.getPropertyName (i));

        if (! value.isString())
        {
            total += sizeof (juce::var);
            continue;
        }

        const auto text = value.toString();

        if (seen.insert (text.getCharPointer().getAddress()).second)
            total += text.getNumBytesAsUTF8();
    }

    for (int i = 0; i < tree.getNumChildren(); ++i)
        countBytes (tree.getChild (i), seen, total);
}

[[nodiscard]] juce::ValueTree childOfType (const juce::ValueTree& parent, const juce::Identifier& type)
{
    return parent.getChildWithName (type);
}

} // namespace

EditHistory::EditHistory (juce::AudioProcessorValueTreeState& stateToUse)
    : apvts (stateToUse)
{
    parameters.reserve (params::parameterCount());

    for (const auto& definition : params::parameterDefinitions)
        parameters.push_back (apvts.getParameter (params::toJuceString (definition.id)));
}

//==============================================================================
// Recording.

bool EditHistory::isSoundChild (const juce::ValueTree& child)
{
    return ! child.hasType (parameterType) && ! child.hasType (mappingsType);
}

float EditHistory::valueOf (int index) const
{
    if (index < 0 || index >= static_cast<int> (parameters.size()) || parameters[static_cast<std::size_t> (index)] == nullptr)
        return 0.0f;

    return parameters[static_cast<std::size_t> (index)]->getValue();
}

EditHistory::Snapshot EditHistory::captureParameters() const
{
    Snapshot snapshot;
    snapshot.values.reserve (parameters.size());

    for (int i = 0; i < static_cast<int> (parameters.size()); ++i)
        snapshot.values.emplace_back (i, valueOf (i));

    return snapshot;
}

EditHistory::Snapshot EditHistory::capture() const
{
    auto snapshot = captureParameters();
    snapshot.wholeDocument = true;

    for (int i = 0; i < apvts.state.getNumChildren(); ++i)
    {
        const auto child = apvts.state.getChild (i);

        if (isSoundChild (child))
            snapshot.document.emplace_back (child.getType(), child.createCopy());
    }

    return snapshot;
}

EditHistory::Snapshot EditHistory::capture (const std::vector<int>& parameterIndices,
                                            const std::vector<juce::Identifier>& children) const
{
    Snapshot snapshot;

    for (const auto index : parameterIndices)
        if (index >= 0 && index < static_cast<int> (parameters.size()))
            snapshot.values.emplace_back (index, valueOf (index));

    for (const auto& type : children)
    {
        const auto child = childOfType (apvts.state, type);
        snapshot.document.emplace_back (type, child.isValid() ? child.createCopy() : juce::ValueTree());
    }

    return snapshot;
}

void EditHistory::commit (const Snapshot& before, const juce::String& name)
{
    Step step;
    step.name = name;

    for (const auto& [index, was] : before.values)
        if (const auto now = valueOf (index); now != was)
            step.values.push_back ({ index, was, now });

    // What the document holds now, of the types the snapshot looked at — and,
    // for a whole one, of any type that has appeared since.
    std::vector<juce::Identifier> types;

    for (const auto& [type, tree] : before.document)
        types.push_back (type);

    if (before.wholeDocument)
        for (int i = 0; i < apvts.state.getNumChildren(); ++i)
            if (const auto child = apvts.state.getChild (i);
                isSoundChild (child) && std::find (types.begin(), types.end(), child.getType()) == types.end())
                types.push_back (child.getType());

    for (const auto& type : types)
    {
        juce::ValueTree was;

        for (const auto& [kept, tree] : before.document)
            if (kept == type)
                was = tree;

        const auto now = childOfType (apvts.state, type);

        if (! sameTree (was, now))
            step.children.push_back ({ type, was, now.isValid() ? now.createCopy() : juce::ValueTree() });
    }

    if (step.values.empty() && step.children.empty())
        return;

    push (std::move (step));
}

juce::String EditHistory::nameOf (const std::vector<Step::Value>& values) const
{
    if (values.empty())
        return {};

    const auto& definition = params::parameterDefinitions[static_cast<std::size_t> (values.front().index)];
    const auto first = params::toJuceString (definition.name);

    if (values.size() == 1)
        return first;

    return first + " and " + juce::String (values.size() - 1) + (values.size() == 2 ? " other" : " others");
}

void EditHistory::gestureBegan (int parameterIndex)
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (parameters.size()))
        return;

    const std::lock_guard<std::recursive_mutex> guard (lock);

    const auto started = std::any_of (gestureStart.begin(), gestureStart.end(),
                                      [parameterIndex] (const auto& entry) { return entry.first == parameterIndex; });

    if (! started)
        gestureStart.emplace_back (parameterIndex, valueOf (parameterIndex));

    openGestures.push_back (parameterIndex);
}

void EditHistory::gestureEnded (int parameterIndex)
{
    const std::lock_guard<std::recursive_mutex> guard (lock);

    const auto open = std::find (openGestures.begin(), openGestures.end(), parameterIndex);

    if (open == openGestures.end())
        return;

    openGestures.erase (open);

    if (openGestures.empty())
        finishGesture();
}

void EditHistory::finishGesture()
{
    openGestures.clear();

    if (gestureStart.empty())
        return;

    Snapshot before;
    before.values = std::exchange (gestureStart, {});

    // Named after what moved, not after what was merely picked up: an
    // equaliser handle dragged sideways moves its frequency and not its gain.
    std::vector<Step::Value> moved;

    for (const auto& [index, was] : before.values)
        if (valueOf (index) != was)
            moved.push_back ({ index, was, valueOf (index) });

    commit (before, nameOf (moved));
}

void EditHistory::hold (int key, Snapshot before, const juce::String& name)
{
    const std::lock_guard<std::recursive_mutex> guard (lock);

    if (const auto existing = held.find (key); existing != held.end())
    {
        existing->second.second = name;
        return;
    }

    held.emplace (key, std::make_pair (std::move (before), name));
}

void EditHistory::release (int key)
{
    const std::lock_guard<std::recursive_mutex> guard (lock);

    const auto found = held.find (key);

    if (found == held.end())
        return;

    const auto [before, name] = std::move (found->second);
    held.erase (found);

    commit (before, name);
}

void EditHistory::drop (int key)
{
    const std::lock_guard<std::recursive_mutex> guard (lock);
    held.erase (key);
}

void EditHistory::push (Step step)
{
    {
        const std::lock_guard<std::recursive_mutex> guard (lock);

        undoable.push_back (std::move (step));
        redoable.clear();
        trim();
    }

    announce();
}

void EditHistory::trim()
{
    while (static_cast<int> (undoable.size()) > maxSteps)
        undoable.erase (undoable.begin());

    // Oldest first, until what is left fits. Never the newest: one step that
    // is over the limit on its own can still be undone.
    while (undoable.size() > 1)
    {
        std::set<const void*> seen;
        std::size_t total = 0;

        for (const auto* steps : { &undoable, &redoable })
            for (const auto& kept : *steps)
                for (const auto& child : kept.children)
                {
                    countBytes (child.before, seen, total);
                    countBytes (child.after, seen, total);
                }

        if (total <= maxBytes)
            return;

        undoable.erase (undoable.begin());
    }
}

//==============================================================================
// Undoing.

void EditHistory::apply (const Step& step, bool forward)
{
    // Each as a gesture of its own, so that a host which records parameter
    // edits sees an undo as an edit rather than as automation.
    for (const auto& value : step.values)
    {
        auto* parameter = parameters[static_cast<std::size_t> (value.index)];

        if (parameter == nullptr)
            continue;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (forward ? value.after : value.before);
        parameter->endChangeGesture();
    }

    if (step.children.empty())
        return;

    for (const auto& child : step.children)
    {
        for (int i = apvts.state.getNumChildren(); --i >= 0;)
            if (apvts.state.getChild (i).hasType (child.type))
                apvts.state.removeChild (i, nullptr);

        // A copy, so that editing the live tree afterwards cannot reach back
        // into the step and change what it would put back next time.
        if (const auto& target = forward ? child.after : child.before; target.isValid())
            apvts.state.appendChild (target.createCopy(), nullptr);
    }

    if (onDocumentReplaced)
        onDocumentReplaced();
}

bool EditHistory::undo()
{
    Step step;

    {
        const std::lock_guard<std::recursive_mutex> guard (lock);

        // A control still held is let go of first, so that what it did is
        // what is undone rather than something older.
        if (! openGestures.empty())
            finishGesture();

        // A load still on its way would land on top of the sound the undo
        // put back, and record a step that started somewhere else.
        held.clear();

        if (undoable.empty())
            return false;

        step = std::move (undoable.back());
        undoable.pop_back();
    }

    apply (step, false);

    {
        const std::lock_guard<std::recursive_mutex> guard (lock);
        redoable.push_back (std::move (step));
    }

    announce();
    return true;
}

bool EditHistory::redo()
{
    Step step;

    {
        const std::lock_guard<std::recursive_mutex> guard (lock);

        if (! openGestures.empty())
            finishGesture();

        held.clear();

        if (redoable.empty())
            return false;

        step = std::move (redoable.back());
        redoable.pop_back();
    }

    apply (step, true);

    {
        const std::lock_guard<std::recursive_mutex> guard (lock);
        undoable.push_back (std::move (step));
    }

    announce();
    return true;
}

bool EditHistory::canUndo() const
{
    const std::lock_guard<std::recursive_mutex> guard (lock);
    return ! undoable.empty();
}

bool EditHistory::canRedo() const
{
    const std::lock_guard<std::recursive_mutex> guard (lock);
    return ! redoable.empty();
}

juce::String EditHistory::undoName() const
{
    const std::lock_guard<std::recursive_mutex> guard (lock);
    return undoable.empty() ? juce::String() : undoable.back().name;
}

juce::String EditHistory::redoName() const
{
    const std::lock_guard<std::recursive_mutex> guard (lock);
    return redoable.empty() ? juce::String() : redoable.back().name;
}

int EditHistory::numUndoable() const
{
    const std::lock_guard<std::recursive_mutex> guard (lock);
    return static_cast<int> (undoable.size());
}

void EditHistory::clear()
{
    const std::lock_guard<std::recursive_mutex> guard (lock);

    undoable.clear();
    redoable.clear();
    openGestures.clear();
    gestureStart.clear();
    held.clear();
}

void EditHistory::announce()
{
    if (onChanged)
        onChanged();
}

} // namespace apollo::state
