#include "State/MacroNames.h"

namespace apollo::state
{

namespace
{

[[nodiscard]] bool inRange (int macroIndex) noexcept
{
    return macroIndex >= 0 && macroIndex < numMacroNames;
}

/** Characters that are not part of a one-line name: C0 and C1 controls,
    delete, and anything the platform calls whitespace (tabs, line and
    paragraph separators, the various widths of space). */
[[nodiscard]] bool separates (juce::juce_wchar c) noexcept
{
    return c < 0x20 || (c >= 0x7f && c <= 0x9f) || juce::CharacterFunctions::isWhitespace (c)
        || c == 0x2028 || c == 0x2029;
}

} // namespace

MacroNameStore::MacroNameStore (juce::AudioProcessorValueTreeState& stateToUse)
    : apvts (stateToUse)
{
}

juce::String MacroNameStore::clean (const juce::String& text)
{
    juce::String result;
    auto pendingSpace = false;

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (separates (c))
        {
            pendingSpace = result.isNotEmpty();
            continue;
        }

        if (pendingSpace)
            result += " ";

        pendingSpace = false;
        result += juce::String::charToString (c);
    }

    return result;
}

juce::String MacroNameStore::defaultName (int macroIndex)
{
    return "Macro " + juce::String (macroIndex + 1);
}

juce::String MacroNameStore::normalise (int macroIndex, const juce::String& name)
{
    const auto cleaned = clean (name);
    return cleaned == defaultName (macroIndex) ? juce::String() : cleaned;
}

bool MacroNameStore::set (int macroIndex, const juce::String& name)
{
    if (! inRange (macroIndex))
        return false;

    const auto cleaned = normalise (macroIndex, name);

    if (cleaned.length() > maxMacroNameLength)
        return false;

    {
        const std::scoped_lock held (lock);

        auto& slot = names[static_cast<std::size_t> (macroIndex)];

        if (slot == cleaned)
            return true;

        slot = cleaned;
        writeToState();
    }

    // Outside the lock: the host may call straight back for the state.
    if (onEdited)
        onEdited();

    return true;
}

juce::String MacroNameStore::get (int macroIndex) const
{
    if (! inRange (macroIndex))
        return {};

    const std::scoped_lock held (lock);
    return names[static_cast<std::size_t> (macroIndex)];
}

juce::String MacroNameStore::displayName (int macroIndex) const
{
    const auto name = get (macroIndex);
    return name.isNotEmpty() ? name : defaultName (macroIndex);
}

int MacroNameStore::restoreFromState()
{
    const std::scoped_lock held (lock);

    names.fill ({});

    const auto stored = apvts.state.getChildWithName (namesTreeType);

    if (stored.isValid())
    {
        for (const auto entry : stored)
        {
            if (! entry.hasType (macroTreeType))
                continue;

            // One-based in the document, as the macros are numbered everywhere
            // a person reads them.
            const auto number = static_cast<int> (entry.getProperty (macroProperty, 0));

            if (! inRange (number - 1))
                continue; // A macro this build does not have.

            // Cut before cleaning as well as after, so a hand-edited document
            // with a megabyte in one attribute costs nothing to read.
            const auto raw = entry.getProperty (nameProperty).toString().substring (0, maxMacroNameLength * 8);
            const auto name = normalise (number - 1, raw);

            names[static_cast<std::size_t> (number - 1)] = name.substring (0, maxMacroNameLength).trimEnd();
        }
    }

    // Rewritten rather than left as loaded, so the stored form is always what
    // the store actually accepted.
    writeToState();

    auto named = 0;

    for (const auto& name : names)
        named += name.isNotEmpty() ? 1 : 0;

    return named;
}

void MacroNameStore::writeToState()
{
    auto root = apvts.state;
    root.removeChild (root.getChildWithName (namesTreeType), nullptr);

    juce::ValueTree stored (namesTreeType);

    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (names[i].isEmpty())
            continue;

        juce::ValueTree entry (macroTreeType);
        entry.setProperty (macroProperty, static_cast<int> (i) + 1, nullptr);
        entry.setProperty (nameProperty, names[i], nullptr);
        stored.appendChild (entry, nullptr);
    }

    if (stored.getNumChildren() > 0)
        root.appendChild (stored, nullptr);
}

} // namespace apollo::state
