#pragma once

/*
    A drawn LFO shape (PRD §15.2, "custom drawable curve").

    WHAT A CURVE IS. A list of points across one cycle, each a position in the
    cycle and a value, joined by segments that each bend by their own amount.
    The first point sits at the start of the cycle and the last at the end, so
    the curve always covers the whole cycle; between them the points can go
    anywhere, and two at the same position make a step. That is the model every
    synthesiser with a drawable LFO uses, because it is what a hand drawing a
    shape actually does: put a point here, drag it there, bend the line between.

    TWO REPRESENTATIONS, ON TWO THREADS. The points are what the user edits and
    what a preset stores; they live on the message thread. The audio thread
    never reads them. It reads a table the points are rendered into, sampled at
    `lfoCurveTableSize` positions across the cycle, and interpolates between
    neighbouring entries — the same arithmetic for any curve, however many
    points it has, so a drawn shape costs what a triangle costs.

    A step is therefore a ramp one table entry wide: a thousandth of a cycle,
    which at one hertz is a millisecond. That is not a defect to hide. A square
    LFO stepping a filter is a click unless something rounds the edge, and here
    something always does, by a fixed and tiny amount.

    HANDING A TABLE ACROSS. `LfoCurveExchange` is a triple buffer: the writer
    renders into a table nobody is reading, then swaps it with the middle one;
    the reader, once a block, swaps the middle one with its own if a new one has
    arrived. Neither side waits for the other, nothing is allocated after
    construction, and the reader's table is never written while it holds it
    (CLAUDE.md §7).

    JUCE-free, so all of it is tested with no host and no message loop.
*/

#include <array>
#include <atomic>
#include <cstddef>

namespace apollo::dsp
{

/** Points a curve may have. Enough for a staircase of sixteen steps (two
    points a step) and anything a hand would draw; few enough that a preset
    holding four curves stays a few kilobytes. */
inline constexpr int maxLfoCurvePoints = 32;

/** Entries across one cycle of the rendered table, not counting the one past
    the end that interpolation reads at the very end of the cycle. */
inline constexpr int lfoCurveTableSize = 1024;

/** One point: where in the cycle (0 to 1), the value there (-1 to +1), and how
    the segment leaving it bends (-1 to +1, 0 straight). The last point's bend
    is never used, because no segment leaves it. */
struct LfoCurvePoint
{
    float x = 0.0f;
    float y = 0.0f;
    float bend = 0.0f;

    [[nodiscard]] bool operator== (const LfoCurvePoint&) const = default;
};

struct LfoCurve
{
    std::array<LfoCurvePoint, static_cast<std::size_t> (maxLfoCurvePoints)> points {};
    int count = 0;

    [[nodiscard]] bool operator== (const LfoCurve& other) const noexcept;
};

/** The curve an LFO starts with: a triangle from the bottom, up to the top
    half way, and back down — the built-in triangle's own shape, so choosing
    the drawn shape before drawing anything changes nothing but the choice. */
[[nodiscard]] LfoCurve defaultLfoCurve() noexcept;

/** True if @p curve is something the engine can render as it stands:
    between two and `maxLfoCurvePoints` points, every number finite and in its
    range, positions never decreasing, the first at 0 and the last at 1. */
[[nodiscard]] bool isValid (const LfoCurve& curve) noexcept;

/** @returns @p u (0 to 1 along a segment) bent by @p bend. Zero is straight;
    positive makes the segment move most of the way early, negative late. A
    power curve, `u ^ 4^-bend`, so the ends stay where they are. */
[[nodiscard]] float bendSegment (float u, float bend) noexcept;

/** @returns the curve's value at @p position (0 to 1), straight from its
    points. Where two points share a position, the later one wins: a step has
    already happened at the position it is drawn at. */
[[nodiscard]] float evaluate (const LfoCurve& curve, float position) noexcept;

using LfoCurveTable = std::array<float, static_cast<std::size_t> (lfoCurveTableSize + 1)>;

/** Renders @p curve into @p table. An invalid curve renders as silence rather
    than as whatever it happens to contain. */
void render (const LfoCurve& curve, LfoCurveTable& table) noexcept;

/** A rendered table, handed from one writer to one reader without either
    waiting. See the header comment. */
class LfoCurveExchange
{
public:
    /** Every table starts as the default curve, so a reader that acquires
        before anything is published reads the shape an LFO starts with. */
    LfoCurveExchange() noexcept;

    LfoCurveExchange (const LfoCurveExchange&) = delete;
    LfoCurveExchange& operator= (const LfoCurveExchange&) = delete;

    /** WRITER. Renders @p curve and makes it the next table the reader takes.
        One writer at a time: the caller serialises writers. */
    void publish (const LfoCurve& curve) noexcept;

    /** READER. Takes the newest published table if there is one, and @returns
        the table the reader now holds. It stays valid, and unwritten, until
        the reader's next call. One reader at a time. */
    [[nodiscard]] const float* acquire() noexcept;

private:
    static constexpr int indexMask = 3;
    static constexpr int freshFlag = 4;

    std::array<LfoCurveTable, 3> tables {};

    /** The writer's table and the reader's. The middle one is in `middle`,
        with `freshFlag` set when the writer has put a new one there. */
    int back = 0;
    int front = 2;
    std::atomic<int> middle { 1 };
};

} // namespace apollo::dsp
