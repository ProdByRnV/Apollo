/*
    How large the editor opens, on a display of any size.

    These exist because the answer was wrong for five phases and no test could
    see it: the decision lived inside the WebView editor, which the headless
    suite does not build (ADR-0076). It now lives in a header with no JUCE in
    it, and the cases below are the ones that were actually wrong.
*/

#include <juce_core/juce_core.h>

#include "UI/EditorSizing.h"

using namespace apollo::ui;

namespace
{

class EditorSizingTests final : public juce::UnitTest
{
public:
    EditorSizingTests() : juce::UnitTest ("Editor sizing", "UI") {}

    void runTest() override
    {
        testTheDisplayThisWasFoundOn();
        testRoomyDisplaysGetTheDesignedSize();
        testNeverSmallerThanTheLayoutWorksAt();
        testNeverLargerThanTheLimit();
        testAnUnknownDisplayIsNotASmallOne();
        testTheResultAlwaysFits();
    }

private:
    void testTheDisplayThisWasFoundOn()
    {
        beginTest ("A 1080p display at 150% scaling gets an editor that fits on it");

        // 1920x1080 at 150% is 1280x720 in the units the editor is laid out
        // in. The designed height of 760 does not fit in 720, and for five
        // phases the editor opened at it anyway: 102 physical pixels of the
        // interface were below the bottom of the screen.
        const auto size = initialEditorSize (1280, 720);

        expect (size.height <= 720, "The height fits the display: " + juce::String (size.height));
        expect (size.width <= 1280, "The width fits the display: " + juce::String (size.width));
        expectEquals (size.height, 720 - editorChromeAllowance, "It takes what is left after the window frame");
        expectEquals (size.width, designedEditorSize.width, "The width was never the problem");
    }

    void testRoomyDisplaysGetTheDesignedSize()
    {
        beginTest ("A display with room to spare gets the size the interface was drawn for");

        for (const auto display : { std::pair { 1920, 1080 }, std::pair { 2560, 1440 },
                                    std::pair { 3840, 2160 }, std::pair { 1440, 900 } })
        {
            const auto size = initialEditorSize (display.first, display.second);

            expect (size == designedEditorSize,
                    juce::String (display.first) + "x" + juce::String (display.second)
                        + " gave " + juce::String (size.width) + "x" + juce::String (size.height));
        }
    }

    void testNeverSmallerThanTheLayoutWorksAt()
    {
        beginTest ("A display too small for the minimum gets the minimum, not something smaller");

        // Below the minimum the panels overlap rather than reflow. An editor
        // that has to be moved or scrolled is a worse outcome than a resizable
        // window, and a better one than an unusable layout.
        for (const auto display : { std::pair { 800, 600 }, std::pair { 640, 480 },
                                    std::pair { 1024, 600 }, std::pair { 320, 240 } })
        {
            const auto size = initialEditorSize (display.first, display.second);

            expect (size.width >= minimumEditorSize.width && size.height >= minimumEditorSize.height,
                    juce::String (display.first) + "x" + juce::String (display.second)
                        + " gave " + juce::String (size.width) + "x" + juce::String (size.height));
        }
    }

    void testNeverLargerThanTheLimit()
    {
        beginTest ("An enormous display does not produce an enormous editor");

        // The designed size is the cap in practice, but the resize limits are
        // the contract, and a wall-sized display must not produce a window
        // outside them.
        const auto size = initialEditorSize (15360, 8640);

        expect (size.width <= maximumEditorSize.width && size.height <= maximumEditorSize.height);
        expect (size == designedEditorSize, "It opens at the designed size, not the display's");
    }

    void testAnUnknownDisplayIsNotASmallOne()
    {
        beginTest ("A display that reports nothing gets the designed size");

        // Guessing small would shrink the interface on a machine that could
        // show all of it, which is the worse of the two mistakes.
        expect (initialEditorSize (0, 0) == designedEditorSize);
        expect (initialEditorSize (-1920, -1080) == designedEditorSize);
        expect (initialEditorSize (1280, 0) == designedEditorSize);
    }

    void testTheResultAlwaysFits()
    {
        beginTest ("Across every plausible display, the editor fits or is at its minimum");

        // The property the specific cases above are examples of, checked over
        // the whole range rather than at the sizes somebody thought of.
        int checked = 0;

        for (int width = 320; width <= 4096; width += 37)
        {
            for (int height = 240; height <= 2304; height += 29)
            {
                const auto size = initialEditorSize (width, height);
                const auto fits = size.width <= width && size.height <= height;
                const auto atMinimum = size.width == minimumEditorSize.width
                                    || size.height == minimumEditorSize.height;

                if (! (fits || atMinimum))
                {
                    expect (false, juce::String (width) + "x" + juce::String (height)
                                       + " gave " + juce::String (size.width) + "x"
                                       + juce::String (size.height));
                    return;
                }

                ++checked;
            }
        }

        expect (checked > 5000, juce::String (checked) + " display sizes checked");
    }
};

static EditorSizingTests editorSizingTests;

} // namespace
