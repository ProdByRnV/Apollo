#include "State/StateSerialization.h"

#include "ApolloVersion.h"
#include "Parameters/ParameterLayout.h"

namespace apollo::state
{

namespace
{

/** Upgrades a state tree from @p fromVersion to @p fromVersion + 1.

    No steps are registered yet: currentSchemaVersion is still 1, so nothing
    reaches this function today. It is the single extension point for the first
    real schema change, and returning false by default is the safe behaviour —
    an unrecognised version is refused rather than reinterpreted.
*/
bool applyMigrationStep (juce::ValueTree& state, int fromVersion)
{
    juce::ignoreUnused (state, fromVersion);
    return false;
}

} // namespace

juce::String describe (StateLoadResult result)
{
    switch (result)
    {
        case StateLoadResult::ok:                 return "State loaded.";
        case StateLoadResult::emptyData:          return "No state data was supplied.";
        case StateLoadResult::malformed:          return "The state data could not be read.";
        case StateLoadResult::wrongProduct:       return "The state data was not created by Apollo.";
        case StateLoadResult::unsupportedVersion: return "The state data was saved by an incompatible version of Apollo.";
        case StateLoadResult::migrationFailed:    return "The state data could not be updated to the current format.";
    }

    return "The state data could not be read.";
}

void writeState (juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& destination)
{
    auto state = apvts.copyState();

    // Stamped every time rather than assumed: a document without a version is
    // indistinguishable from one written by a future build that dropped it.
    state.setProperty (schemaVersionProperty, currentSchemaVersion, nullptr);
    state.setProperty (productProperty, params::toJuceString (productName), nullptr);
    state.setProperty (productVersionProperty, params::toJuceString (versionString), nullptr);

    if (const auto xml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destination);
}

bool migrate (juce::ValueTree& state, int fromVersion)
{
    if (fromVersion == currentSchemaVersion)
        return true;

    if (fromVersion < minimumSupportedSchemaVersion || fromVersion > currentSchemaVersion)
        return false;

    // Steps are applied in sequence, each upgrading exactly one version, so a
    // document from any supported version reaches the current one by the same
    // path rather than by a matrix of special cases.
    for (int version = fromVersion; version < currentSchemaVersion; ++version)
        if (! applyMigrationStep (state, version))
            return false;

    state.setProperty (schemaVersionProperty, currentSchemaVersion, nullptr);
    return true;
}

StateLoadResult readState (juce::AudioProcessorValueTreeState& apvts,
                           const void* data,
                           int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return StateLoadResult::emptyData;

    const auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return StateLoadResult::malformed;

    if (! xml->hasTagName (params::stateTreeType))
        return StateLoadResult::wrongProduct;

    // The version is read from the XML attribute rather than from the parsed
    // ValueTree. XML carries no type information, so ValueTree::fromXml yields
    // every attribute as a string var; asking such a var whether it holds an int
    // always answers no, and a perfectly valid document Apollo had just written
    // itself would be rejected.
    //
    // A document with no version at all is not assumed to be current: it is
    // either corrupt or predates versioning, and neither can be interpreted
    // safely.
    if (! xml->hasAttribute (schemaVersionProperty))
        return StateLoadResult::unsupportedVersion;

    const auto versionText = xml->getStringAttribute (schemaVersionProperty).trim();

    if (versionText.isEmpty() || ! versionText.containsOnly ("+-0123456789"))
        return StateLoadResult::malformed;

    const int schemaVersion = versionText.getIntValue();

    if (schemaVersion < minimumSupportedSchemaVersion || schemaVersion > currentSchemaVersion)
        return StateLoadResult::unsupportedVersion;

    auto incoming = juce::ValueTree::fromXml (*xml);

    if (! incoming.isValid())
        return StateLoadResult::malformed;

    // Migration runs on the copy. Nothing has touched the live state yet, so a
    // failure here leaves the current sound exactly as it was.
    if (! migrate (incoming, schemaVersion))
        return StateLoadResult::migrationFailed;

    apvts.replaceState (incoming);

    return StateLoadResult::ok;
}

} // namespace apollo::state
