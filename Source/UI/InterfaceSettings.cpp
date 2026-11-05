#include "UI/InterfaceSettings.h"

#include "ApolloVersion.h"
#include "Parameters/ParameterLayout.h"
#include "UI/EditorSizing.h"

namespace apollo::ui
{

namespace
{

constexpr const char* rootTag = "APOLLO_INTERFACE";
constexpr const char* scaleAttribute = "scale";

/** Far beyond one element with one attribute. A file larger than this is not
    one Apollo wrote, and is not read into memory to find that out. */
constexpr juce::int64 maximumSettingsBytes = 16 * 1024;

} // namespace

juce::File defaultInterfaceSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (params::toJuceString (productName))
        .getChildFile ("Interface.settings");
}

int loadInterfaceScale (const juce::File& file)
{
    if (! file.existsAsFile() || file.getSize() > maximumSettingsBytes)
        return defaultInterfaceScale;

    const auto xml = juce::XmlDocument::parse (file.loadFileAsString());

    if (xml == nullptr || ! xml->hasTagName (rootTag))
        return defaultInterfaceScale;

    // Read as text and checked whole, so "125abc" or "1.5" is not a size.
    const auto text = xml->getStringAttribute (scaleAttribute).trim();

    if (text.isEmpty() || ! text.containsOnly ("0123456789") || text.length() > 4)
        return defaultInterfaceScale;

    const auto scale = text.getIntValue();

    return isInterfaceScale (scale) ? scale : defaultInterfaceScale;
}

bool saveInterfaceScale (const juce::File& file, int scalePercent)
{
    if (! isInterfaceScale (scalePercent))
        return false;

    if (! file.getParentDirectory().createDirectory().wasOk())
        return false;

    juce::XmlElement root (rootTag);
    root.setAttribute (scaleAttribute, scalePercent);

    return root.writeTo (file);
}

} // namespace apollo::ui
