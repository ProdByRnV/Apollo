#include "DSP/LFO/LfoCurve.h"

#include <cmath>

namespace apollo::dsp
{

bool LfoCurve::operator== (const LfoCurve& other) const noexcept
{
    if (count != other.count)
        return false;

    // Only the points in use: what lies beyond `count` is not part of the
    // curve, and two curves that differ only there are the same curve.
    for (int i = 0; i < count && i < maxLfoCurvePoints; ++i)
        if (! (points[static_cast<std::size_t> (i)] == other.points[static_cast<std::size_t> (i)]))
            return false;

    return true;
}

LfoCurve defaultLfoCurve() noexcept
{
    LfoCurve curve;
    curve.points[0] = { 0.0f, -1.0f, 0.0f };
    curve.points[1] = { 0.5f, 1.0f, 0.0f };
    curve.points[2] = { 1.0f, -1.0f, 0.0f };
    curve.count = 3;
    return curve;
}

bool isValid (const LfoCurve& curve) noexcept
{
    if (curve.count < 2 || curve.count > maxLfoCurvePoints)
        return false;

    const auto inRange = [] (float value, float low, float high)
    {
        return std::isfinite (value) && value >= low && value <= high;
    };

    auto previous = 0.0f;

    for (int i = 0; i < curve.count; ++i)
    {
        const auto& point = curve.points[static_cast<std::size_t> (i)];

        if (! inRange (point.x, 0.0f, 1.0f) || ! inRange (point.y, -1.0f, 1.0f)
            || ! inRange (point.bend, -1.0f, 1.0f) || point.x < previous)
            return false;

        previous = point.x;
    }

    return curve.points[0].x == 0.0f
        && curve.points[static_cast<std::size_t> (curve.count - 1)].x == 1.0f;
}

float bendSegment (float u, float bend) noexcept
{
    if (bend == 0.0f || u <= 0.0f || u >= 1.0f)
        return u;

    return std::pow (u, std::pow (4.0f, -bend));
}

float evaluate (const LfoCurve& curve, float position) noexcept
{
    if (! isValid (curve))
        return 0.0f;

    const auto last = curve.count - 1;

    if (position >= 1.0f)
        return curve.points[static_cast<std::size_t> (last)].y;

    // The segment whose start is the last point at or before the position, so
    // that of two points at one position the later one starts the segment.
    auto segment = 0;

    while (segment + 1 < last && curve.points[static_cast<std::size_t> (segment + 1)].x <= position)
        ++segment;

    const auto& from = curve.points[static_cast<std::size_t> (segment)];
    const auto& to = curve.points[static_cast<std::size_t> (segment + 1)];

    const auto width = to.x - from.x;

    if (! (width > 0.0f))
        return to.y;

    const auto u = (position - from.x) / width;
    return from.y + (to.y - from.y) * bendSegment (u < 0.0f ? 0.0f : u, from.bend);
}

void render (const LfoCurve& curve, LfoCurveTable& table) noexcept
{
    if (! isValid (curve))
    {
        table.fill (0.0f);
        return;
    }

    for (std::size_t i = 0; i < table.size(); ++i)
        table[i] = evaluate (curve, static_cast<float> (i) / static_cast<float> (lfoCurveTableSize));
}

LfoCurveExchange::LfoCurveExchange() noexcept
{
    const auto curve = defaultLfoCurve();

    for (auto& table : tables)
        render (curve, table);
}

void LfoCurveExchange::publish (const LfoCurve& curve) noexcept
{
    render (curve, tables[static_cast<std::size_t> (back)]);

    // Release, so the reader that takes this table sees it rendered; and
    // acquire, because the table handed back may be one the reader has just
    // finished with.
    back = middle.exchange (back | freshFlag, std::memory_order_acq_rel) & indexMask;
}

const float* LfoCurveExchange::acquire() noexcept
{
    if ((middle.load (std::memory_order_acquire) & freshFlag) != 0)
        front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;

    return tables[static_cast<std::size_t> (front)].data();
}

} // namespace apollo::dsp
