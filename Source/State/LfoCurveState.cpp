#include "State/LfoCurveState.h"

#include <algorithm>
#include <cmath>

namespace apollo::state
{

namespace
{

[[nodiscard]] bool inRange (int lfoIndex) noexcept
{
    return lfoIndex >= 0 && lfoIndex < numLfoCurves;
}

[[nodiscard]] float clampedOr (const juce::var& value, float low, float high, float fallback)
{
    if (! (value.isDouble() || value.isInt() || value.isInt64() || value.isString()))
        return fallback;

    const auto number = static_cast<float> (static_cast<double> (value));

    if (! std::isfinite (number))
        return fallback;

    return std::clamp (number, low, high);
}

} // namespace

LfoCurveStore::LfoCurveStore (juce::AudioProcessorValueTreeState& stateToUse)
    : apvts (stateToUse)
{
    curves.fill (dsp::defaultLfoCurve());
}

bool LfoCurveStore::set (int lfoIndex, const dsp::LfoCurve& curve)
{
    if (! inRange (lfoIndex) || ! dsp::isValid (curve))
        return false;

    const std::scoped_lock lock (writerLock);

    const auto slot = static_cast<std::size_t> (lfoIndex);
    curves[slot] = curve;
    exchanges[slot].publish (curve);
    writeToState();

    return true;
}

dsp::LfoCurve LfoCurveStore::get (int lfoIndex) const
{
    if (! inRange (lfoIndex))
        return dsp::defaultLfoCurve();

    const std::scoped_lock lock (writerLock);
    return curves[static_cast<std::size_t> (lfoIndex)];
}

const float* LfoCurveStore::acquireTable (int lfoIndex) noexcept
{
    // AUDIO THREAD.
    const auto slot = static_cast<std::size_t> (inRange (lfoIndex) ? lfoIndex : 0);
    return exchanges[slot].acquire();
}

dsp::LfoCurve LfoCurveStore::repair (const std::vector<dsp::LfoCurvePoint>& points)
{
    std::vector<dsp::LfoCurvePoint> usable;

    for (const auto& point : points)
    {
        if (! (std::isfinite (point.x) && std::isfinite (point.y) && std::isfinite (point.bend)))
            continue;

        usable.push_back ({ std::clamp (point.x, 0.0f, 1.0f),
                            std::clamp (point.y, -1.0f, 1.0f),
                            std::clamp (point.bend, -1.0f, 1.0f) });
    }

    // Stable, so two points at one position keep the order they were written
    // in, and a step stays the step it was.
    std::stable_sort (usable.begin(), usable.end(),
                      [] (const auto& a, const auto& b) { return a.x < b.x; });

    if (usable.size() > static_cast<std::size_t> (dsp::maxLfoCurvePoints))
        usable.resize (static_cast<std::size_t> (dsp::maxLfoCurvePoints));

    if (usable.size() < 2)
        return dsp::defaultLfoCurve();

    usable.front().x = 0.0f;
    usable.back().x = 1.0f;

    dsp::LfoCurve curve;
    curve.count = static_cast<int> (usable.size());
    std::copy (usable.begin(), usable.end(), curve.points.begin());

    return dsp::isValid (curve) ? curve : dsp::defaultLfoCurve();
}

int LfoCurveStore::restoreFromState()
{
    const std::scoped_lock lock (writerLock);

    curves.fill (dsp::defaultLfoCurve());

    const auto stored = apvts.state.getChildWithName (curvesTreeType);

    if (stored.isValid())
    {
        for (const auto entry : stored)
        {
            if (! entry.hasType (curveTreeType))
                continue;

            // One-based in the document, as the LFOs are numbered everywhere a
            // person reads them.
            const auto number = static_cast<int> (entry.getProperty (lfoProperty, 0));

            if (! inRange (number - 1))
                continue; // An LFO this build does not have.

            std::vector<dsp::LfoCurvePoint> points;

            for (const auto point : entry)
            {
                if (! point.hasType (pointTreeType))
                    continue;

                points.push_back ({ clampedOr (point.getProperty (xProperty), 0.0f, 1.0f, 0.0f),
                                    clampedOr (point.getProperty (yProperty), -1.0f, 1.0f, 0.0f),
                                    clampedOr (point.getProperty (bendProperty), -1.0f, 1.0f, 0.0f) });

                // Read no further than could ever be kept, whatever the file
                // holds: a curve of a million points is not a curve.
                if (points.size() > static_cast<std::size_t> (dsp::maxLfoCurvePoints) * 4)
                    break;
            }

            curves[static_cast<std::size_t> (number - 1)] = repair (points);
        }
    }

    auto drawn = 0;

    for (std::size_t i = 0; i < curves.size(); ++i)
    {
        exchanges[i].publish (curves[i]);
        drawn += curves[i] == dsp::defaultLfoCurve() ? 0 : 1;
    }

    // Rewritten rather than left as loaded, so the stored form is always what
    // the store actually accepted.
    writeToState();

    return drawn;
}

void LfoCurveStore::writeToState()
{
    auto root = apvts.state;
    root.removeChild (root.getChildWithName (curvesTreeType), nullptr);

    juce::ValueTree stored (curvesTreeType);
    const auto initial = dsp::defaultLfoCurve();

    for (std::size_t i = 0; i < curves.size(); ++i)
    {
        const auto& curve = curves[i];

        if (curve == initial)
            continue;

        juce::ValueTree entry (curveTreeType);
        entry.setProperty (lfoProperty, static_cast<int> (i) + 1, nullptr);

        for (int p = 0; p < curve.count; ++p)
        {
            const auto& point = curve.points[static_cast<std::size_t> (p)];

            juce::ValueTree node (pointTreeType);
            node.setProperty (xProperty, static_cast<double> (point.x), nullptr);
            node.setProperty (yProperty, static_cast<double> (point.y), nullptr);
            node.setProperty (bendProperty, static_cast<double> (point.bend), nullptr);
            entry.appendChild (node, nullptr);
        }

        stored.appendChild (entry, nullptr);
    }

    if (stored.getNumChildren() > 0)
        root.appendChild (stored, nullptr);
}

} // namespace apollo::state
