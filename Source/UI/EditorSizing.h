#pragma once

/*
    How large the editor opens.

    Plain arithmetic with no JUCE dependency, deliberately: the editor itself is
    only built when the WebView is, and the test suite builds headless, so
    anything decided inside the editor class is decided where no test can see it
    (ADR-0076). That is not hypothetical — this rule exists because the editor
    opened 102 physical pixels taller than the developer's screen for five
    phases, with the bottom of the interface unreachable, and nothing could have
    caught it but looking.

    The rule: the designed size where it fits, as much of it as the display can
    show where it does not, and never smaller than the size the layout works at.
*/

#include <algorithm>
#include <array>

namespace apollo::ui
{

struct EditorSize
{
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr bool operator== (const EditorSize& other) const noexcept
    {
        return width == other.width && height == other.height;
    }
};

/** What the interface was drawn for. */
inline constexpr EditorSize designedEditorSize { 1180, 760 };

/** The smallest the layout still works at. Below this the panels overlap
    rather than reflow, so an editor that must be scrolled or moved is the
    better outcome.
*/
inline constexpr EditorSize minimumEditorSize { 900, 560 };

/** The largest the editor will grow to, including under Fullscreen. */
inline constexpr EditorSize maximumEditorSize { 3840, 2160 };

/** Room left for what surrounds the editor and is not the editor: a title bar
    and borders in the standalone, a host's frame and toolbar in a plugin.

    A deliberate allowance rather than a measurement. The window does not exist
    when the size is chosen, and a host's frame is the host's business.
*/
inline constexpr int editorChromeAllowance = 64;

/** The interface sizes offered, in per cent of the designed size (18b).

    A fixed list rather than a free number: eight sizes are a menu somebody can
    read at a glance, and a stored or received value that is not one of them is
    refused rather than interpreted.
*/
inline constexpr std::array<int, 8> interfaceScales { 75, 85, 100, 110, 125, 150, 175, 200 };

inline constexpr int defaultInterfaceScale = 100;

[[nodiscard]] constexpr bool isInterfaceScale (int percent) noexcept
{
    return std::find (interfaceScales.begin(), interfaceScales.end(), percent) != interfaceScales.end();
}

/** @returns the size the editor should open at on a display whose usable area
             — excluding the taskbar, dock or menu bar — is the given size, in
             the same logical units the editor is laid out in, with the
             interface drawn at @p scalePercent of its designed size.

    The designed size scaled, as much of it as the display can show, and never
    smaller than the size the layout works at. The page draws at the scale only
    as far as the window leaves it that much room (WebUI interfaceScale.ts), so
    an editor clamped here shows a little less than was asked for rather than a
    layout squeezed below its minimum.

    A display reported as empty or negative yields the scaled designed size: an
    unknown display is not a small one, and guessing small would shrink the
    interface on a machine that could show all of it. A scale that is not one
    offered is taken as the default.
*/
[[nodiscard]] constexpr EditorSize initialEditorSize (int availableWidth, int availableHeight,
                                                      int scalePercent = defaultInterfaceScale) noexcept
{
    const auto scale = isInterfaceScale (scalePercent) ? scalePercent : defaultInterfaceScale;

    const auto known = availableWidth > 0 && availableHeight > 0;

    const auto fit = [scale, known] (int designed, int available, int minimum, int maximum)
    {
        const auto wanted = (designed * scale + 50) / 100;
        const auto shown = known ? std::min (wanted, available - editorChromeAllowance) : wanted;

        return std::clamp (shown, minimum, maximum);
    };

    return { fit (designedEditorSize.width, availableWidth,
                  minimumEditorSize.width, maximumEditorSize.width),
             fit (designedEditorSize.height, availableHeight,
                  minimumEditorSize.height, maximumEditorSize.height) };
}

} // namespace apollo::ui
