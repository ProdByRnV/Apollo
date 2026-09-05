#pragma once

/*
    Apollo parameter identifier conventions.

    A parameter ID is part of two long-lived external contracts: the host
    automation contract (VST3 parameter identity) and the serialized state
    contract (presets and DAW project state). UI_BINDINGS.md §3 and
    ARCHITECTURE.md §6.1 both require IDs to be stable once released, so the
    rules that govern their shape are fixed here, in one dependency-free place,
    rather than being re-invented per subsystem.

    This header defines the *conventions* only. The authoritative registry of
    actual parameters is a separate concern that belongs to the APVTS layer
    (roadmap Phase 2) and will validate its IDs against these rules.

    The full, human-readable convention — including the semantic naming rules a
    compiler cannot check — is documented in Docs/PARAMETER-CONVENTIONS.md.

    Deliberately free of JUCE and standard-library allocation so that it can be
    used in constant expressions and from any layer, including DSP code.
*/

#include <cstddef>
#include <string_view>

namespace apollo::params
{

/** Maximum permitted parameter identifier length, in characters.

    UI_BINDINGS.md §14 requires the native side to bound the length of every
    string arriving from the WebView. Bounding the identifier itself lets that
    validation reject an oversized ID before any lookup is attempted.
*/
inline constexpr std::size_t maxParameterIdLength = 64;

/** Minimum number of '_'-separated segments in an identifier.

    Every ID carries at least a domain and a name (`master_gain`), so that an ID
    is self-describing when it appears in a preset file or a host's automation
    lane without any surrounding context.
*/
inline constexpr std::size_t minParameterIdSegmentCount = 2;

namespace detail
{
    [[nodiscard]] constexpr bool isLowerAlpha (char c) noexcept { return c >= 'a' && c <= 'z'; }
    [[nodiscard]] constexpr bool isDigit      (char c) noexcept { return c >= '0' && c <= '9'; }
} // namespace detail

/** The reason an identifier was rejected, or `none` if it is well-formed. */
enum class ParameterIdIssue
{
    none = 0,
    empty,                  ///< Identifier contains no characters.
    tooLong,                ///< Longer than maxParameterIdLength.
    invalidFirstCharacter,  ///< Must begin with a lower-case ASCII letter.
    invalidCharacter,       ///< Only [a-z], [0-9] and '_' are permitted.
    trailingSeparator,      ///< Must not end with '_'.
    emptySegment,           ///< Contains "__", i.e. a zero-length segment.
    numericOnlySegment,     ///< A segment such as "12" carries no meaning.
    tooFewSegments          ///< Fewer than minParameterIdSegmentCount segments.
};

/** Validates an identifier against Apollo's mechanical ID rules.

    @returns ParameterIdIssue::none when the identifier is well-formed,
             otherwise the first rule it violates.
*/
[[nodiscard]] constexpr ParameterIdIssue validateParameterId (std::string_view id) noexcept
{
    if (id.empty())
        return ParameterIdIssue::empty;

    if (id.size() > maxParameterIdLength)
        return ParameterIdIssue::tooLong;

    if (! detail::isLowerAlpha (id.front()))
        return ParameterIdIssue::invalidFirstCharacter;

    if (id.back() == '_')
        return ParameterIdIssue::trailingSeparator;

    std::size_t segmentCount = 1;
    std::size_t segmentLength = 0;
    bool segmentHasLetter = false;

    for (const char c : id)
    {
        if (c == '_')
        {
            if (segmentLength == 0)
                return ParameterIdIssue::emptySegment;

            if (! segmentHasLetter)
                return ParameterIdIssue::numericOnlySegment;

            ++segmentCount;
            segmentLength = 0;
            segmentHasLetter = false;
            continue;
        }

        if (detail::isLowerAlpha (c))
            segmentHasLetter = true;
        else if (! detail::isDigit (c))
            return ParameterIdIssue::invalidCharacter;

        ++segmentLength;
    }

    // The trailing-separator check above guarantees a non-empty final segment.
    if (! segmentHasLetter)
        return ParameterIdIssue::numericOnlySegment;

    if (segmentCount < minParameterIdSegmentCount)
        return ParameterIdIssue::tooFewSegments;

    return ParameterIdIssue::none;
}

/** @returns true when the identifier satisfies every mechanical ID rule. */
[[nodiscard]] constexpr bool isValidParameterId (std::string_view id) noexcept
{
    return validateParameterId (id) == ParameterIdIssue::none;
}

/** @returns a short, non-sensitive description of an identifier issue.

    Safe to surface through the UI bridge: the text never contains the offending
    identifier, a path, or any implementation detail (UI_BINDINGS.md §13).
*/
[[nodiscard]] std::string_view describeParameterIdIssue (ParameterIdIssue issue) noexcept;

} // namespace apollo::params
