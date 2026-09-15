#include "State/PresetDocument.h"

#include "ApolloVersion.h"
#include "MIDI/MidiControlManager.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"

namespace apollo::presets
{

namespace
{

/** Everything a preset leaves behind, captured from the live state.

    Taken before a document is applied and put back afterwards, so that loading
    somebody else's sound cannot rewire the controller in front of *this* user
    (see `isExcludedFromPresets`).
*/
struct ControllerState
{
    juce::ValueTree mappings;
    std::vector<std::pair<juce::String, float>> parameters;
};

[[nodiscard]] ControllerState captureController (juce::AudioProcessorValueTreeState& apvts)
{
    ControllerState captured;

    const auto mappings = apvts.state.getChildWithName (
        juce::Identifier (midi::MidiControlManager::mappingsTreeType));

    // A deep copy, because the tree it came from is about to be replaced.
    if (mappings.isValid())
        captured.mappings = mappings.createCopy();

    for (std::size_t i = 0; i < params::parameterCount(); ++i)
    {
        const auto& definition = params::parameterDefinitions[i];

        if (! isExcludedFromPresets (definition.id))
            continue;

        const auto id = params::toJuceString (definition.id);

        if (const auto* parameter = apvts.getParameter (id))
            captured.parameters.emplace_back (id, parameter->getValue());
    }

    return captured;
}

void restoreController (juce::AudioProcessorValueTreeState& apvts,
                        const ControllerState& captured)
{
    auto state = apvts.copyState();

    // Any mapping table the incoming document carried is discarded rather than
    // merged: a preset has no business naming controllers at all, and a
    // hand-written one that does must not quietly take over the user's desk.
    state.removeChild (state.getChildWithName (
                           juce::Identifier (midi::MidiControlManager::mappingsTreeType)),
                       nullptr);

    if (captured.mappings.isValid())
        state.appendChild (captured.mappings.createCopy(), nullptr);

    apvts.replaceState (state);

    // The parameters go back through their own objects rather than through the
    // tree, because a normalised value is what a parameter owns and what the
    // host is told about. A preset omits these entirely, so without this they
    // would come back as *defaults* rather than as they were — which is how the
    // loader treats any parameter a document does not mention.
    for (const auto& [id, value] : captured.parameters)
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (value);
}

/** Trims @p text and cuts it to @p maximum characters.

    Cut rather than rejected: a preset whose comment is too long is still a
    perfectly good sound, and refusing to load it would punish the user for
    something they can neither see nor fix. The name is what the browser draws,
    so it is bounded rather than trusted.
*/
[[nodiscard]] juce::String bounded (const juce::String& text, int maximum)
{
    const auto trimmed = text.trim();

    return trimmed.length() > maximum ? trimmed.substring (0, maximum) : trimmed;
}

/** @returns the `<PRESET>` child of @p state, or an invalid tree. */
[[nodiscard]] juce::ValueTree findMetadata (const juce::ValueTree& state)
{
    return state.getChildWithName (juce::Identifier (presetTreeType));
}

[[nodiscard]] Metadata readFrom (const juce::ValueTree& tree)
{
    Metadata metadata;

    if (! tree.isValid())
        return metadata;

    metadata.name = tree.getProperty (nameProperty).toString();
    metadata.author = tree.getProperty (authorProperty).toString();
    metadata.category = tree.getProperty (categoryProperty).toString();
    metadata.comment = tree.getProperty (commentProperty).toString();

    return sanitise (metadata);
}

} // namespace

void attachMetadata (juce::ValueTree& state, const Metadata& metadata)
{
    const auto clean = sanitise (metadata);

    auto tree = state.getChildWithName (juce::Identifier (presetTreeType));

    if (! tree.isValid())
    {
        tree = juce::ValueTree (juce::Identifier (presetTreeType));
        state.appendChild (tree, nullptr);
    }

    tree.setProperty (nameProperty, clean.name, nullptr);
    tree.setProperty (authorProperty, clean.author, nullptr);
    tree.setProperty (categoryProperty, clean.category, nullptr);
    tree.setProperty (commentProperty, clean.comment, nullptr);
}

bool isExcludedFromPresets (std::string_view parameterId)
{
    const auto index = params::indexOfParameter (parameterId);

    if (index < 0)
        return false;

    // Not a list of ids kept here. A parameter that describes the controller
    // rather than the patch is already marked in the registry as one a host
    // must not automate, for the same reason, and one flag is easier to keep
    // true than two (ADR-0045).
    return ! params::parameterDefinitions[static_cast<std::size_t> (index)].automatable;
}

Metadata sanitise (const Metadata& metadata)
{
    Metadata result;

    result.name = bounded (metadata.name, maximumNameLength);
    result.author = bounded (metadata.author, maximumNameLength);
    result.category = bounded (metadata.category, maximumNameLength);
    result.comment = bounded (metadata.comment, maximumCommentLength);

    return result;
}

Metadata getMetadata (juce::AudioProcessorValueTreeState& apvts)
{
    return readFrom (findMetadata (apvts.copyState()));
}

void setMetadata (juce::AudioProcessorValueTreeState& apvts, const Metadata& metadata)
{
    auto state = apvts.copyState();

    attachMetadata (state, metadata);

    // `copyState` hands back a deep copy, so the edits above touched nothing the
    // instrument is using. Putting it back is what makes them real — and it is
    // the same call a preset load makes, so the two paths cannot diverge.
    apvts.replaceState (state);
}

juce::String write (juce::AudioProcessorValueTreeState& apvts, const Metadata& metadata)
{
    auto state = apvts.copyState();

    // Stamped exactly as a host save is, by the same function, because a preset
    // is the same document: it carries a schema version so an older build knows
    // to refuse it, and a product so anything else wearing the extension is
    // recognisable as not ours.
    state::stamp (state);

    attachMetadata (state, metadata);

    // The controller comes out before the document goes to disk. A shared
    // preset that carried the author's MIDI mappings and MPE zone would
    // reconfigure the keyboard of everyone who opened it.
    state.removeChild (state.getChildWithName (
                           juce::Identifier (midi::MidiControlManager::mappingsTreeType)),
                       nullptr);

    for (auto i = state.getNumChildren(); --i >= 0;)
    {
        const auto child = state.getChild (i);

        if (! child.hasType (juce::Identifier (params::parameterTreeType)))
            continue;

        const auto id = child.getProperty (params::parameterIdProperty).toString();

        if (isExcludedFromPresets (id.toStdString()))
            state.removeChild (i, nullptr);
    }

    const auto xml = state.createXml();

    return xml != nullptr ? xml->toString() : juce::String();
}

namespace
{

/** Parses @p text far enough to know it is an Apollo document.

    The size check comes first and is the only one that looks at the whole
    string: everything after it is about structure, and there is no reason to
    parse four megabytes to discover that the root element is wrong.
*/
[[nodiscard]] state::StateLoadResult parse (const juce::String& text,
                                            std::unique_ptr<juce::XmlElement>& destination)
{
    if (text.isEmpty())
        return state::StateLoadResult::emptyData;

    if (static_cast<int> (text.getNumBytesAsUTF8()) > maximumDocumentBytes)
        return state::StateLoadResult::tooLarge;

    destination = juce::parseXML (text);

    if (destination == nullptr)
        return state::StateLoadResult::malformed;

    return state::StateLoadResult::ok;
}

} // namespace

state::StateLoadResult read (juce::AudioProcessorValueTreeState& apvts, const juce::String& text)
{
    std::unique_ptr<juce::XmlElement> xml;

    if (const auto parsed = parse (text, xml); parsed != state::StateLoadResult::ok)
        return parsed;

    // Captured before anything is applied, because applying a document replaces
    // the whole tree and a preset deliberately does not contain these.
    const auto controller = captureController (apvts);

    // Everything from here is the host-state reader, unchanged and unduplicated:
    // the product check, the version bounds, the migration, and the guarantee
    // that a failure leaves the current sound alone.
    const auto result = state::readStateXml (apvts, *xml);

    if (result == state::StateLoadResult::ok)
        restoreController (apvts, controller);

    return result;
}

state::StateLoadResult readMetadata (const juce::String& text, Metadata& destination)
{
    destination = {};

    std::unique_ptr<juce::XmlElement> xml;

    if (const auto parsed = parse (text, xml); parsed != state::StateLoadResult::ok)
        return parsed;

    if (! xml->hasTagName (params::stateTreeType))
        return state::StateLoadResult::wrongProduct;

    // The same version rules a full read applies. A document this build could
    // not load has nothing worth listing in a browser, and showing it as though
    // it were loadable would be an index that lies.
    if (! xml->hasAttribute (state::schemaVersionProperty))
        return state::StateLoadResult::unsupportedVersion;

    const auto versionText = xml->getStringAttribute (state::schemaVersionProperty).trim();

    if (versionText.isEmpty() || ! versionText.containsOnly ("+-0123456789"))
        return state::StateLoadResult::malformed;

    const auto schemaVersion = versionText.getIntValue();

    if (schemaVersion < state::minimumSupportedSchemaVersion
        || schemaVersion > state::currentSchemaVersion)
        return state::StateLoadResult::unsupportedVersion;

    // Read straight off the XML rather than by building a ValueTree of the whole
    // document. Indexing a folder means doing this once per file, and the
    // parameter list is the expensive part of a preset — the part an index does
    // not need.
    if (const auto* tree = xml->getChildByName (presetTreeType))
    {
        Metadata found;

        found.name = tree->getStringAttribute (nameProperty);
        found.author = tree->getStringAttribute (authorProperty);
        found.category = tree->getStringAttribute (categoryProperty);
        found.comment = tree->getStringAttribute (commentProperty);

        destination = sanitise (found);
    }

    // A preset with no metadata block at all is valid and common — it is what a
    // document written before metadata existed looks like, and what a host
    // project that has never loaded a preset looks like. The caller gets empty
    // fields and names it from its filename.
    return state::StateLoadResult::ok;
}

} // namespace apollo::presets
