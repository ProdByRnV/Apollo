#include "Resources/LfoShapeLibrary.h"

#include "Resources/PresetLibrary.h"
#include "State/LfoCurveState.h"

#include <algorithm>
#include <cmath>

namespace apollo::resources
{

namespace
{

constexpr const char* rootTag = "APOLLO_LFO_SHAPE";
constexpr const char* versionAttribute = "version";
constexpr int formatVersion = 1;

/** Point elements read from one file. Four times what a curve can hold, so
    a file with a few broken points among thirty-two good ones still reads,
    and one with a million is not walked to the end. */
constexpr int maximumPointElements = 4 * dsp::maxLfoCurvePoints;

/** A curve from (x, y, bend) triples, which must already be valid. */
[[nodiscard]] dsp::LfoCurve curveOf (std::initializer_list<dsp::LfoCurvePoint> points)
{
    dsp::LfoCurve curve;
    curve.count = static_cast<int> (points.size());
    std::copy (points.begin(), points.end(), curve.points.begin());
    jassert (dsp::isValid (curve));
    return curve;
}

[[nodiscard]] std::vector<LfoShape> makeBuiltIns()
{
    // A segment's bend is the curve's own: it leaves a point and bends the
    // way u^(4^-bend) does, so a positive bend moves quickly at first and a
    // negative one slowly. None of these is one of the LFO's built-in shapes,
    // which are a click away already.
    std::vector<LfoShape> shapes;

    const auto add = [&shapes] (const char* name, dsp::LfoCurve curve)
    {
        shapes.push_back ({ name, true, curve });
    };

    // Struck and dying away: down fast, then easing into the floor.
    add ("Pluck", curveOf ({ { 0.0f, 1.0f, 0.8f }, { 1.0f, -1.0f, 0.0f } }));

    // Rising slowly, then all at once: a swell into the next bar.
    add ("Swell", curveOf ({ { 0.0f, -1.0f, -0.8f }, { 1.0f, 1.0f, 0.0f } }));

    // Ducked on the beat and recovering quickly, as a sidechained pad does.
    add ("Pump", curveOf ({ { 0.0f, -1.0f, 0.7f }, { 1.0f, 1.0f, 0.0f } }));

    // A dropped ball: each fall accelerates, each rise slows, each lower.
    add ("Bounce", curveOf ({ { 0.00f, 1.00f, -0.6f }, { 0.30f, -1.00f, 0.6f },
                              { 0.48f, 0.30f, -0.6f }, { 0.66f, -1.00f, 0.6f },
                              { 0.77f, -0.40f, -0.6f }, { 0.88f, -1.00f, 0.6f },
                              { 0.94f, -0.75f, -0.6f }, { 1.00f, -1.00f, 0.0f } }));

    // Four steps up, for arpeggiated filter moves.
    add ("Stairs", curveOf ({ { 0.00f, -1.0f, 0.0f }, { 0.25f, -1.0f, 0.0f },
                              { 0.25f, -1.0f / 3.0f, 0.0f }, { 0.50f, -1.0f / 3.0f, 0.0f },
                              { 0.50f, 1.0f / 3.0f, 0.0f }, { 0.75f, 1.0f / 3.0f, 0.0f },
                              { 0.75f, 1.0f, 0.0f }, { 1.00f, 1.0f, 0.0f } }));

    // Eight short gates a cycle: on for a sixteenth, off for a sixteenth, as
    // the trance gate is played. Thirty-two points, the most a curve holds.
    {
        dsp::LfoCurve gate;

        for (int step = 0; step < 8; ++step)
        {
            const auto start = static_cast<float> (step) / 8.0f;
            const auto middle = start + 1.0f / 16.0f;
            const auto end = static_cast<float> (step + 1) / 8.0f;
            const auto at = static_cast<std::size_t> (step * 4);

            gate.points[at] = { start, 1.0f, 0.0f };
            gate.points[at + 1] = { middle, 1.0f, 0.0f };
            gate.points[at + 2] = { middle, -1.0f, 0.0f };
            gate.points[at + 3] = { end, -1.0f, 0.0f };
        }

        gate.count = 32;
        jassert (dsp::isValid (gate));
        add ("Trance Gate", gate);
    }

    return shapes;
}

/** Reads one attribute as a number, refusing text that is not one rather than
    reading it as zero the way JUCE's own conversions would. */
[[nodiscard]] bool readNumber (const juce::XmlElement& element, const char* name, float& out)
{
    if (! element.hasAttribute (name))
        return false;

    const auto text = element.getStringAttribute (name).trim();

    if (text.isEmpty() || ! text.containsOnly ("+-.0123456789eE"))
        return false;

    const auto value = text.getDoubleValue();

    if (! std::isfinite (value))
        return false;

    out = static_cast<float> (value);
    return true;
}

} // namespace

const std::vector<LfoShape>& builtInLfoShapes()
{
    static const auto shapes = makeBuiltIns();
    return shapes;
}

juce::File defaultLfoShapeFolder()
{
    // Beside the presets, so the two libraries are found the same way and the
    // platform's own lookup decides where that is (CLAUDE.md §46).
    return defaultPresetLocations().userDirectory.getSiblingFile ("LFO Shapes");
}

juce::String writeLfoShapeText (const dsp::LfoCurve& curve)
{
    juce::XmlElement root (rootTag);
    root.setAttribute (versionAttribute, formatVersion);

    // Rounded to a millionth, as on the wire: a float spelled as a double is
    // seventeen digits of noise in a file somebody may read.
    const auto rounded = [] (float value)
    {
        return std::round (static_cast<double> (value) * 1.0e6) / 1.0e6;
    };

    for (int i = 0; i < curve.count && i < dsp::maxLfoCurvePoints; ++i)
    {
        const auto& point = curve.points[static_cast<std::size_t> (i)];
        auto* element = root.createNewChildElement (state::LfoCurveStore::pointTreeType);
        element->setAttribute (state::LfoCurveStore::xProperty, rounded (point.x));
        element->setAttribute (state::LfoCurveStore::yProperty, rounded (point.y));
        element->setAttribute (state::LfoCurveStore::bendProperty, rounded (point.bend));
    }

    return root.toString();
}

std::optional<dsp::LfoCurve> readLfoShapeText (const juce::String& text)
{
    const auto root = juce::parseXML (text);

    if (root == nullptr || ! root->hasTagName (rootTag))
        return std::nullopt;

    // A later format is refused rather than guessed at.
    const auto version = root->getStringAttribute (versionAttribute).trim();

    if (version != juce::String (formatVersion))
        return std::nullopt;

    std::vector<dsp::LfoCurvePoint> points;
    int seen = 0;

    for (const auto* element : root->getChildWithTagNameIterator (state::LfoCurveStore::pointTreeType))
    {
        if (++seen > maximumPointElements)
            break;

        dsp::LfoCurvePoint point;

        if (readNumber (*element, state::LfoCurveStore::xProperty, point.x)
            && readNumber (*element, state::LfoCurveStore::yProperty, point.y)
            && readNumber (*element, state::LfoCurveStore::bendProperty, point.bend))
            points.push_back (point);
    }

    // Fewer than two is not a shape. Refused here, because repair would turn
    // it into the default triangle, and a file that is not a shape should not
    // appear in the list as one.
    if (points.size() < 2)
        return std::nullopt;

    return state::LfoCurveStore::repair (points);
}

std::optional<dsp::LfoCurve> readLfoShapeFile (const juce::File& file)
{
    // The size first: pointing a library at something enormous costs a stat.
    if (! file.existsAsFile() || file.getSize() > maximumLfoShapeFileBytes)
        return std::nullopt;

    return readLfoShapeText (file.loadFileAsString());
}

juce::String cleanLfoShapeName (const juce::String& name)
{
    const auto oneLine = name.replaceCharacters ("\r\n\t", "   ").trim();

    if (oneLine.isEmpty() || oneLine.length() > maximumLfoShapeNameLength)
        return {};

    // What the file will be called, which is what the shape will be called.
    const auto file = presetFileFor (juce::File::getCurrentWorkingDirectory(), oneLine);
    return file == juce::File() ? juce::String() : file.getFileNameWithoutExtension();
}

juce::File lfoShapeFileFor (const juce::File& folder, const juce::String& name)
{
    // The preset library's rules for turning a name into a file — characters
    // no file system takes, names Windows reserves, anything resolving outside
    // the folder — with this library's extension.
    const auto asPreset = presetFileFor (folder, name);

    if (asPreset == juce::File())
        return juce::File();

    const auto target = asPreset.withFileExtension (lfoShapeExtension);
    return target.isAChildOf (folder) ? target : juce::File();
}

LfoShapeLibrary::LfoShapeLibrary()
    : folder (defaultLfoShapeFolder())
{
}

void LfoShapeLibrary::setFolder (const juce::File& newFolder)
{
    folder = newFolder;
    shapes.clear();
    unreadable = 0;
}

void LfoShapeLibrary::rescan()
{
    shapes = builtInLfoShapes();
    unreadable = 0;

    if (! folder.isDirectory())
        return;

    auto files = folder.findChildFiles (juce::File::findFiles, /* recursive */ false,
                                        juce::String ("*") + lfoShapeExtension);

    // Alphabetical, so the numbers the page is given are the order it shows.
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    int read = 0;

    for (const auto& file : files)
    {
        if (read >= maximumUserLfoShapes)
        {
            ++unreadable;
            continue;
        }

        if (const auto curve = readLfoShapeFile (file))
        {
            shapes.push_back ({ file.getFileNameWithoutExtension(), false, *curve });
            ++read;
        }
        else
        {
            ++unreadable;
        }
    }
}

const LfoShape* LfoShapeLibrary::find (int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (shapes.size()))
        return nullptr;

    return &shapes[static_cast<std::size_t> (index)];
}

LfoShapeSaveResult LfoShapeLibrary::save (const juce::String& name, const dsp::LfoCurve& curve, bool overwrite)
{
    const auto cleaned = cleanLfoShapeName (name);

    if (cleaned.isEmpty())
        return LfoShapeSaveResult::invalidName;

    const auto target = lfoShapeFileFor (folder, cleaned);

    if (target == juce::File())
        return LfoShapeSaveResult::invalidName;

    if (target.exists() && ! overwrite)
        return LfoShapeSaveResult::exists;

    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return LfoShapeSaveResult::couldNotCreateFolder;

    if (! folder.isDirectory())
        return LfoShapeSaveResult::couldNotCreateFolder;

    // Beside the target and moved into place once whole, as a preset is: an
    // interrupted save must not leave half a shape where a whole one was.
    juce::TemporaryFile temporary (target);

    if (! temporary.getFile().replaceWithText (writeLfoShapeText (curve), false, false))
        return LfoShapeSaveResult::couldNotWrite;

    if (! temporary.overwriteTargetFileWithTemporary())
        return LfoShapeSaveResult::couldNotWrite;

    return LfoShapeSaveResult::ok;
}

bool LfoShapeLibrary::revealFolder() const
{
    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return false;

    return folder.startAsProcess();
}

} // namespace apollo::resources
