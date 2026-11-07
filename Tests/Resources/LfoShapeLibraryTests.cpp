/*
    The library of drawn LFO shapes (Phase 18g).

    Apollo's own shapes, the file a saved shape is, what a folder somebody else
    filled can and cannot put in the list, saving and refusing to replace, and
    the bridge: a listed shape loaded into an LFO as one step that undoes, a
    shape saved from one, and messages that name what does not exist.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <set>

#include "Audio/ApolloAudioProcessor.h"
#include "Resources/LfoShapeLibrary.h"
#include "State/LfoCurveState.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

/** A folder of its own that removes itself. */
class ScratchFolder
{
public:
    explicit ScratchFolder (const juce::String& label)
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloLfoShapeTests")
                    .getChildFile (label + "-" + juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~ScratchFolder() { root.deleteRecursively(); }

    ScratchFolder (const ScratchFolder&) = delete;
    ScratchFolder& operator= (const ScratchFolder&) = delete;

    [[nodiscard]] juce::File getRoot() const { return root; }

private:
    juce::File root;
};

[[nodiscard]] juce::var parse (const juce::String& message)
{
    juce::var parsed;
    return juce::JSON::parse (message, parsed).wasOk() ? parsed : juce::var();
}

[[nodiscard]] juce::String fieldOf (const juce::String& message, const char* field)
{
    const auto parsed = parse (message);
    auto* object = parsed.getDynamicObject();
    return object != nullptr ? object->getProperty (field).toString() : juce::String();
}

[[nodiscard]] dsp::LfoCurve threePoints()
{
    dsp::LfoCurve curve;
    curve.points[0] = { 0.0f, 0.0f, 0.25f };
    curve.points[1] = { 0.4f, 1.0f, -0.5f };
    curve.points[2] = { 1.0f, -0.75f, 0.0f };
    curve.count = 3;
    return curve;
}

[[nodiscard]] bool near (const dsp::LfoCurve& a, const dsp::LfoCurve& b)
{
    if (a.count != b.count)
        return false;

    for (std::size_t i = 0; i < static_cast<std::size_t> (a.count); ++i)
        if (std::abs (a.points[i].x - b.points[i].x) > 1.0e-6f
            || std::abs (a.points[i].y - b.points[i].y) > 1.0e-6f
            || std::abs (a.points[i].bend - b.points[i].bend) > 1.0e-6f)
            return false;

    return true;
}

class LfoShapeLibraryTests final : public juce::UnitTest
{
public:
    LfoShapeLibraryTests() : juce::UnitTest ("LFO shape library", "Resources") {}

    void runTest() override
    {
        testApollosOwn();
        testTheFile();
        testHostileFiles();
        testTheLibrary();
        testNames();
        testTheBridge();
    }

private:
    void testApollosOwn()
    {
        beginTest ("Apollo's own shapes are valid, named once each, and none is a straight line");

        const auto& shapes = resources::builtInLfoShapes();
        expectEquals (static_cast<int> (shapes.size()), 6);

        std::set<juce::String> names;

        for (const auto& shape : shapes)
        {
            expect (shape.builtIn);
            expect (dsp::isValid (shape.curve), shape.name + " is not a curve the engine can render");
            expect (! (shape.curve == dsp::defaultLfoCurve()), shape.name + " is the default triangle");
            expect (names.insert (shape.name).second, shape.name + " is listed twice");

            // Each survives its own file: what is shipped could be saved.
            const auto read = resources::readLfoShapeText (resources::writeLfoShapeText (shape.curve));
            expect (read.has_value() && near (*read, shape.curve), shape.name + " does not survive a file");
        }
    }

    void testTheFile()
    {
        beginTest ("A shape's file holds its points, and reads back as the same curve");

        const auto curve = threePoints();
        const auto text = resources::writeLfoShapeText (curve);

        expect (text.contains ("APOLLO_LFO_SHAPE"));
        expect (text.contains ("version=\"1\""));
        expect (text.length() < 1000, "three points are a few hundred bytes");

        const auto read = resources::readLfoShapeText (text);
        expect (read.has_value());
        expect (read.has_value() && near (*read, curve));
    }

    void testHostileFiles()
    {
        beginTest ("A file that is not a shape is not read as one");

        const auto refused = [this] (const juce::String& text, const char* why)
        {
            expect (! resources::readLfoShapeText (text).has_value(), why);
        };

        refused ({}, "nothing");
        refused ("not xml at all", "not XML");
        refused (R"(<APOLLO version="1"><POINT x="0" y="0" bend="0"/><POINT x="1" y="0" bend="0"/></APOLLO>)",
                 "somebody else's XML");
        refused (R"(<APOLLO_LFO_SHAPE version="2"><POINT x="0" y="0" bend="0"/><POINT x="1" y="0" bend="0"/></APOLLO_LFO_SHAPE>)",
                 "a later format");
        refused (R"(<APOLLO_LFO_SHAPE><POINT x="0" y="0" bend="0"/><POINT x="1" y="0" bend="0"/></APOLLO_LFO_SHAPE>)",
                 "no version");
        refused (R"(<APOLLO_LFO_SHAPE version="1"><POINT x="0" y="0" bend="0"/></APOLLO_LFO_SHAPE>)",
                 "one point");
        refused (R"(<APOLLO_LFO_SHAPE version="1"><POINT x="0" y="zero" bend="0"/><POINT x="1" y="nan" bend="0"/></APOLLO_LFO_SHAPE>)",
                 "points that are not numbers, which are not read as zero");

        // Out of range and out of order is repaired, as a curve in a
        // hand-edited project is: the ends pinned, values clamped, sorted.
        const auto repaired = resources::readLfoShapeText (
            R"(<APOLLO_LFO_SHAPE version="1"><POINT x="0.9" y="7" bend="0"/><POINT x="0.2" y="-3" bend="5"/><POINT x="0.5" y="bad" bend="0"/></APOLLO_LFO_SHAPE>)");
        expect (repaired.has_value());

        if (repaired.has_value())
        {
            expectEquals (repaired->count, 2, "the point that was not a number is dropped");
            expect (dsp::isValid (*repaired));
            expectEquals (repaired->points[0].y, -1.0f);
            expectEquals (repaired->points[0].bend, 1.0f);
            expectEquals (repaired->points[1].y, 1.0f);
        }

        // A file too large is not read at all, and a missing one is nothing.
        ScratchFolder scratch ("hostile");
        const auto large = scratch.getRoot().getChildFile ("Large.rnvlfo");
        auto text = resources::writeLfoShapeText (threePoints());
        text << juce::String::repeatedString ("<!-- padding -->", 5000);
        expect (large.replaceWithText (text));
        expect (large.getSize() > resources::maximumLfoShapeFileBytes);
        expect (! resources::readLfoShapeFile (large).has_value());
        expect (! resources::readLfoShapeFile (scratch.getRoot().getChildFile ("Missing.rnvlfo")).has_value());
    }

    void testTheLibrary()
    {
        beginTest ("The library lists Apollo's shapes, then the user's in order, and counts what it could not read");

        ScratchFolder scratch ("library");
        resources::LfoShapeLibrary library;
        library.setFolder (scratch.getRoot().getChildFile ("LFO Shapes"));

        // A folder that is not there yet is Apollo's shapes alone.
        library.rescan();
        expectEquals (static_cast<int> (library.getShapes().size()), 6);
        expectEquals (library.getUnreadableCount(), 0);

        // Saving creates the folder.
        expect (library.save ("Wobble", threePoints(), false) == resources::LfoShapeSaveResult::ok);
        expect (library.getFolder().getChildFile ("Wobble.rnvlfo").existsAsFile());
        expect (library.save ("Arp 2", dsp::defaultLfoCurve(), false) == resources::LfoShapeSaveResult::ok);
        expect (library.save ("Arp 10", dsp::defaultLfoCurve(), false) == resources::LfoShapeSaveResult::ok);

        // Somebody else's files: one broken, one not a shape at all, and one
        // with another extension that is not looked at.
        expect (library.getFolder().getChildFile ("Broken.rnvlfo").replaceWithText ("<APOLLO_LFO_SHAPE"));
        expect (library.getFolder().getChildFile ("Notes.rnvlfo").replaceWithText ("shopping list"));
        expect (library.getFolder().getChildFile ("Other.txt").replaceWithText ("hello"));

        library.rescan();
        const auto& shapes = library.getShapes();
        expectEquals (static_cast<int> (shapes.size()), 9);
        expectEquals (library.getUnreadableCount(), 2);

        // Apollo's first, then the user's, in the order a person counts.
        expect (shapes[0].builtIn && shapes[5].builtIn && ! shapes[6].builtIn);
        expectEquals (shapes[6].name, juce::String ("Arp 2"));
        expectEquals (shapes[7].name, juce::String ("Arp 10"));
        expectEquals (shapes[8].name, juce::String ("Wobble"));
        expect (near (shapes[8].curve, threePoints()));

        expect (library.find (8) == &shapes[8]);
        expect (library.find (9) == nullptr);
        expect (library.find (-1) == nullptr);

        // Saving over a shape is refused unless asked for, and then replaces it.
        expect (library.save ("Wobble", dsp::defaultLfoCurve(), false) == resources::LfoShapeSaveResult::exists);
        expect (near (*resources::readLfoShapeFile (library.getFolder().getChildFile ("Wobble.rnvlfo")), threePoints()),
                "a refused save changed the shape");
        expect (library.save ("Wobble", dsp::defaultLfoCurve(), true) == resources::LfoShapeSaveResult::ok);
        expect (near (*resources::readLfoShapeFile (library.getFolder().getChildFile ("Wobble.rnvlfo")), dsp::defaultLfoCurve()));

        // A folder that cannot be created: a file is where it would go.
        resources::LfoShapeLibrary blocked;
        const auto file = scratch.getRoot().getChildFile ("NotAFolder");
        expect (file.replaceWithText ("x"));
        blocked.setFolder (file);
        expect (blocked.save ("Anything", threePoints(), false) != resources::LfoShapeSaveResult::ok);
    }

    void testNames()
    {
        beginTest ("A shape's name becomes a file inside the library, or is refused");

        ScratchFolder scratch ("names");
        const auto folder = scratch.getRoot();

        expect (resources::cleanLfoShapeName ("  Slow pump  ") == "Slow pump");
        expect (resources::cleanLfoShapeName ("").isEmpty());
        expect (resources::cleanLfoShapeName ("   ").isEmpty());
        expect (resources::cleanLfoShapeName (juce::String::repeatedString ("x", 41)).isEmpty(), "too long");

        // Whatever a name says, its file is in the folder.
        for (const auto* attempt : { "../../escape", "a/b\\c", "..", "CON", "name:with*stars?" })
        {
            const auto file = resources::lfoShapeFileFor (folder, attempt);
            expect (file == juce::File() || file.isAChildOf (folder), juce::String ("escaped: ") + attempt);

            if (file != juce::File())
                expect (file.hasFileExtension (resources::lfoShapeExtension));
        }

        resources::LfoShapeLibrary library;
        library.setFolder (folder);
        expect (library.save ("", threePoints(), false) == resources::LfoShapeSaveResult::invalidName);
        expect (library.save ("../../escape", threePoints(), false) == resources::LfoShapeSaveResult::ok);
        expectEquals (folder.findChildFiles (juce::File::findFiles, true, "*.rnvlfo").size(), 1);
        expect (! folder.getParentDirectory().getParentDirectory().getChildFile ("escape.rnvlfo").exists());
    }

    void testTheBridge()
    {
        beginTest ("The page lists shapes, loads one into an LFO as one step, and saves one from an LFO");

        ScratchFolder scratch ("bridge");

        ApolloAudioProcessor processor;
        auto& history = processor.getEditHistory();
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setEditHistory (&history);
        bridge.setLfoCurves (&processor.getLfoCurves());
        bridge.getLfoShapeLibrary().setFolder (scratch.getRoot());

        // The list: Apollo's six, named, with their points.
        const auto list = bridge.handleMessage (R"({"type":"requestLfoShapes","version":1})");
        expectEquals (fieldOf (list, "type"), juce::String ("lfoShapes"));
        const auto shapes = parse (list).getProperty ("shapes", {});
        expectEquals (shapes.size(), 6);
        expectEquals (shapes[0].getProperty ("name", {}).toString(), juce::String ("Pluck"));
        expect (static_cast<bool> (shapes[0].getProperty ("builtIn", false)));
        expectEquals (shapes[0].getProperty ("points", {}).size(), 2);
        expectEquals (static_cast<int> (parse (list).getProperty ("maxNameLength", 0)),
                      resources::maximumLfoShapeNameLength);

        // Pluck into LFO 2: the curve, and one step named for it.
        const auto before = processor.getLfoCurves().get (1);
        const auto loaded = bridge.handleMessage (R"({"type":"loadLfoShape","version":1,"lfo":2,"shape":1})");
        expectEquals (fieldOf (loaded, "type"), juce::String ("lfoCurves"));
        expect (processor.getLfoCurves().get (1) == resources::builtInLfoShapes()[0].curve);
        expect (processor.getLfoCurves().get (0) == dsp::defaultLfoCurve(), "another LFO moved");
        expectEquals (history.undoName(), juce::String ("LFO 2 shape Pluck"));

        expect (history.undo());
        expect (processor.getLfoCurves().get (1) == before, "undoing the load must bring the drawing back");
        expect (history.redo());

        // Saved from LFO 2, it is in the next list.
        const auto saved = bridge.handleMessage (R"({"type":"saveLfoShape","version":1,"lfo":2,"name":"My pluck"})");
        expectEquals (fieldOf (saved, "code"), juce::String ("SAVED"));
        expectEquals (fieldOf (saved, "name"), juce::String ("My pluck"));
        expect (! fieldOf (saved, "message").contains (scratch.getRoot().getFullPathName()),
                "the page is told the name, never where");

        const auto again = bridge.handleMessage (R"({"type":"saveLfoShape","version":1,"lfo":1,"name":"My pluck"})");
        expectEquals (fieldOf (again, "code"), juce::String ("EXISTS"));

        const auto relisted = parse (bridge.handleMessage (R"({"type":"requestLfoShapes","version":1})"))
                                  .getProperty ("shapes", {});
        expectEquals (relisted.size(), 7);
        expectEquals (relisted[6].getProperty ("name", {}).toString(), juce::String ("My pluck"));
        expect (! static_cast<bool> (relisted[6].getProperty ("builtIn", true)));

        // Loaded back into LFO 3, it is the curve it was saved from.
        (void) bridge.handleMessage (R"({"type":"loadLfoShape","version":1,"lfo":3,"shape":7})");
        expect (near (processor.getLfoCurves().get (2), processor.getLfoCurves().get (1)));

        // What does not exist, and what is not a message.
        const auto errorCode = [&bridge] (const char* message)
        {
            return fieldOf (bridge.handleMessage (message), "code");
        };

        expectEquals (errorCode (R"({"type":"loadLfoShape","version":1,"lfo":1,"shape":8})"), juce::String ("UNKNOWN_LFO_SHAPE"));
        expectEquals (errorCode (R"({"type":"loadLfoShape","version":1,"lfo":5,"shape":1})"), juce::String ("UNKNOWN_LFO_SHAPE"));
        expectEquals (errorCode (R"({"type":"loadLfoShape","version":1,"lfo":1,"shape":0})"), juce::String ("UNKNOWN_LFO_SHAPE"));
        expectEquals (errorCode (R"({"type":"loadLfoShape","version":1,"lfo":1,"shape":1.5})"), juce::String ("UNKNOWN_LFO_SHAPE"));
        expectEquals (errorCode (R"({"type":"loadLfoShape","version":1,"lfo":1,"shape":"1"})"), juce::String ("MALFORMED_MESSAGE"));
        expectEquals (errorCode (R"({"type":"saveLfoShape","version":1,"lfo":1})"), juce::String ("INVALID_LFO_SHAPE_NAME"));
        expectEquals (errorCode (R"({"type":"saveLfoShape","version":1,"lfo":1,"name":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"})"),
                      juce::String ("INVALID_LFO_SHAPE_NAME"));
        expectEquals (errorCode (R"({"type":"saveLfoShape","version":1,"lfo":1,"name":"x","overwrite":"yes"})"),
                      juce::String ("MALFORMED_MESSAGE"));
        expectEquals (fieldOf (bridge.handleMessage (R"({"type":"saveLfoShape","version":1,"lfo":1,"name":"   "})"), "code"),
                      juce::String ("INVALID_NAME"));

        // The folder: a payload naming another is not read.
        auto revealed = 0;
        bridge.onRevealLfoShapeFolder = [&revealed] { ++revealed; };
        expect (bridge.handleMessage (R"({"type":"revealLfoShapeFolder","version":1,"folder":"C:/Windows"})").isEmpty());
        expectEquals (revealed, 1);

        bridge.setLfoCurves (nullptr);
        bridge.setEditHistory (nullptr);
    }
};

LfoShapeLibraryTests lfoShapeLibraryTests;

} // namespace
