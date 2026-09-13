// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotProjection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gui {
namespace {

// How many samples a pixel column may hold before the envelope takes over.
//
// Two per column is the envelope's own output rate, so below two it would draw
// more vertices than there are samples and there is nothing left for it to do.
// The threshold is twice that, and the slack is deliberate: the models thin a
// line to about two points per column already -- a bucket is a column -- and
// they are working from the frame's width rather than from the plot area inside
// it, because taking it from the area is a binding loop (see
// PlotSurface.pushColumns). So what arrives here is a little denser than two a
// column rather than a little sparser.
//
// Without the slack that overshoot would be expensive out of all proportion:
// the bucket below is rounded up to a power of two, so a hair over two samples
// a column buys a bucket of four and *halves* the horizontal resolution. Four
// stations a column drawn straight is a few thousand vertices more and the
// picture the data actually has.
constexpr double kSamplesPerColumn = 4.0;

// How far outside the pane a projected point is allowed to land.
//
// A sample does not stop existing because the reader zoomed past it: it is
// drawn where it falls and the pane clips it, which is what keeps the line
// entering and leaving at the frame instead of stopping at the last sample
// that happened to be visible. But a value of 1e300 in a window a unit wide
// projects to 1e302 pixels, and that is +/-inf once it is a float32 -- an
// infinity in a vertex buffer is not a point off screen, it is a triangle the
// rasteriser may do anything at all with.
//
// Ten million pixels is four orders of magnitude outside any pane and four
// short of where float32 starts losing whole numbers. Clamping there moves the
// point where such a segment crosses the frame by far less than a pixel, and
// it is the only place in this file where the arithmetic is allowed to lie.
constexpr double kFarAway = 1e7;

/// The y axis, and whether it has a span to divide by at all.
///
/// A degenerate view -- a flat series with no padding, a bound that came
/// through as NaN -- has no answer to "where does this value sit", and every
/// caller has to notice that before it divides. One struct so that they all
/// notice it the same way.
struct YMapping
{
    double low = 0.0;
    double high = 1.0;
    bool usable = false;
};

YMapping mappingFor(const PlotView& view)
{
    const double low = view.yMin;
    const double high = view.yMax;
    return {low, high, std::isfinite(high - low) && std::abs(high - low) > 0.0};
}

} // namespace

std::optional<PlotWindow> windowFor(double low, double high, long long length, long long buckets)
{
    if (length <= 0 || buckets <= 0) {
        return {};
    }
    if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)) {
        return {};
    }

    // A bucket small enough to put `buckets` of them across twice the visible
    // span. A power of two, and ceiling rather than nearest, so that zooming
    // steps from one octave to the next -- an arbitrary bucket would shimmer,
    // because every pixel of zoom would change which samples each of them
    // holds. Bounded rather than open, because a span that has gone to infinity
    // under a degenerate view would otherwise not stop.
    const double wanted = 2.0 * (high - low) / static_cast<double>(buckets);
    long long bucket = 1;
    while (static_cast<double>(bucket) < wanted && bucket < (std::int64_t{1} << 40)) {
        bucket <<= 1;
    }

    const long long span = bucket * buckets;
    if (span >= length) {
        // The whole-line summary already covers this much at this bucket or
        // finer. There is nothing a second read could add.
        return {};
    }

    const long long stride = std::max<long long>(bucket, (buckets / 4) * bucket);
    // Clamped in double before the cast: a view a long way off the data gives a
    // bound that does not fit in a long long at all.
    const double aligned =
        std::floor(low / static_cast<double>(stride)) * static_cast<double>(stride);
    auto first = static_cast<long long>(std::clamp(aligned, 0.0, static_cast<double>(length - 1)));
    first -= first % stride; // back onto the alignment after the clamp

    const long long take = std::min(span, length - first);
    if (take <= 0) {
        return {};
    }
    const auto columns = static_cast<int>((take + bucket - 1) / bucket);
    if (columns <= 0) {
        return {};
    }
    return PlotWindow{first, take, bucket, columns};
}

double xOf(const PlotLine& line, const PlotAxis& axis, qsizetype at)
{
    const double position = line.positionStart + static_cast<double>(at) * line.positionStep;
    if (!axis.explicitX()) {
        return axis.start + position * axis.step;
    }
    // The time base has a value at each of its own positions and nowhere in
    // between, so a point that falls between two of them takes the nearer.
    const double index = std::round(position);
    if (!(index >= 0.0) || !(index < static_cast<double>(axis.count))) {
        // Past the end of the time base. The line stops here rather than being
        // drawn against an x that does not exist, which is what "align" means
        // when a line and its axis are different lengths.
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double x = axis.values[static_cast<std::size_t>(index)];
    return std::isfinite(x) ? x : std::numeric_limits<double>::quiet_NaN();
}

std::vector<QPointF> samplesOf(const PlotLine& line, const PlotAxis& axis)
{
    std::vector<QPointF> out;
    if (line.values == nullptr || line.count <= 0) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(line.count));
    for (qsizetype i = 0; i < line.count; ++i) {
        // A value that would not read is a gap in the line, not a zero: an
        // invented number at the axis is a reading of the data, and a wrong
        // one.
        const double value = line.values[i];
        if (!std::isfinite(value)) {
            continue;
        }
        const double x = xOf(line, axis, i);
        if (!std::isfinite(x)) {
            continue;
        }
        out.emplace_back(x, value);
    }
    return out;
}

double yFractionOf(double value, const PlotView& view)
{
    const YMapping mapping = mappingFor(view);
    if (!mapping.usable) {
        return 0.0;
    }
    return (value - mapping.low) / (mapping.high - mapping.low);
}

PlotProjected projectLine(const PlotLine& line, const PlotAxis& axis, const PlotView& view,
                          std::vector<QPointF>& points, std::vector<PlotRun>& runs)
{
    const auto startRuns = static_cast<int>(runs.size());
    const auto added = [&](bool decimated) {
        return PlotProjected{static_cast<int>(runs.size()) - startRuns, decimated};
    };
    if (line.values == nullptr || line.count <= 0) {
        return {};
    }
    const double w = view.width;
    const double h = view.height;
    if (!(w > 0.0) || !(h > 0.0)) {
        return {};
    }
    const double xSpan = view.xMax - view.xMin;
    if (!(xSpan > 0.0)) {
        return {};
    }
    const YMapping mapping = mappingFor(view);
    if (!mapping.usable) {
        return {};
    }

    // Everything below is arithmetic in double, and only the finished pixel
    // coordinate is ever cast down. That is the entire trick behind an epoch
    // timestamp drawing as a line here and as a staircase anywhere that casts
    // first.
    const double ySpan = mapping.high - mapping.low;
    const auto toY = [&](double value) { return h - (value - mapping.low) / ySpan * h; };
    const auto toX = [&](double x) { return (x - view.xMin) / xSpan * w; };

    // Where the current run started, as an index into `points`.
    int open = -1;
    const auto closeRun = [&]() {
        if (open < 0) {
            return;
        }
        const int length = static_cast<int>(points.size()) - open;
        if (length >= 2) {
            runs.push_back({open, length});
        }
        else if (length == 1) {
            // A run of one station is a point, and a stroke needs a direction.
            // Dropping it keeps the counts honest rather than emitting a
            // vertex pair that rasterises to nothing.
            points.pop_back();
        }
        open = -1;
    };
    const auto place = [&](double px, double py) {
        if (open < 0) {
            open = static_cast<int>(points.size());
        }
        points.emplace_back(std::clamp(px, -kFarAway, kFarAway),
                            std::clamp(py, -kFarAway, kFarAway));
    };

    const auto count = static_cast<std::int64_t>(line.count);

    if (axis.explicitX()) {
        // A time base is not required to be monotonic and is not required to
        // be evenly spaced, so neither the window narrowing nor the envelope
        // below applies to it: a pixel column would hold samples from all over
        // the line, and the extremes of that are an envelope of nothing. Every
        // sample is drawn at its own x.
        //
        // That is affordable because a line drawn against a time base is a
        // custom plot entry, and those are thinned on the way out of the file
        // like everything else. It would not be affordable for an arbitrary
        // dataset, which is why this is the one path without a bound.
        for (std::int64_t i = 0; i < count; ++i) {
            const double value = line.values[i];
            const double x = xOf(line, axis, i);
            if (!std::isfinite(value) || !std::isfinite(x)) {
                closeRun();
                continue;
            }
            place(toX(x), toY(value));
        }
        closeRun();
        return added(false);
    }

    // x is affine in the sample index, so the window can be turned back into a
    // range of indices and the samples outside it never touched.
    const double x0 = axis.start + line.positionStart * axis.step;
    const double dx = line.positionStep * axis.step;

    std::int64_t first = 0;
    std::int64_t last = count - 1;
    if (std::abs(dx) > 0.0) {
        // One sample either side of the window, so the line enters and leaves
        // the pane at its edges instead of stopping a pixel short of them.
        double lowAt = (view.xMin - x0) / dx;
        double highAt = (view.xMax - x0) / dx;
        if (lowAt > highAt) {
            std::swap(lowAt, highAt);
        }
        if (!std::isfinite(lowAt) || !std::isfinite(highAt)) {
            return {};
        }
        const double floorLow = std::floor(lowAt) - 1.0;
        const double ceilHigh = std::ceil(highAt) + 1.0;
        // Clamped in double before the cast: a window a long way off the data
        // gives a bound that does not fit in an int64 at all.
        first =
            static_cast<std::int64_t>(std::clamp(floorLow, 0.0, static_cast<double>(count - 1)));
        last = static_cast<std::int64_t>(std::clamp(ceilHigh, 0.0, static_cast<double>(count - 1)));
    }
    if (last < first) {
        return {};
    }

    const std::int64_t visible = last - first + 1;
    // The physical columns the pane has, not the logical ones. See
    // PlotView::pixelRatio: on a HiDPI display those differ by two or three,
    // and summarising to the logical count throws away exactly that factor
    // before a pixel is touched.
    std::int64_t columns =
        static_cast<std::int64_t>(std::ceil(w * std::max(view.pixelRatio, 1.0)));
    if (view.maxColumns > 0) {
        columns = std::min<std::int64_t>(columns, view.maxColumns);
    }
    columns = std::max<std::int64_t>(columns, 1);

    if (visible <= static_cast<std::int64_t>(kSamplesPerColumn) * columns) {
        // Zoomed in far enough that the envelope would emit more vertices than
        // there are values. Draw them.
        for (std::int64_t i = first; i <= last; ++i) {
            const double value = line.values[i];
            if (!std::isfinite(value)) {
                closeRun();
                continue;
            }
            place(toX(x0 + static_cast<double>(i) * dx), toY(value));
        }
        closeRun();
        // ...and they are samples only if the model did not summarise them on
        // the way here. A step of one is one drawn point per element; anything
        // wider means each of these is the extreme of a bucket, and a marker on
        // it would be punctuation on a reading nobody took. This is not the
        // same question as whether the loop above ran: a summary fine enough to
        // fit four points in a column is still a summary.
        return added(line.positionStep > 1.0);
    }

    // The envelope, over buckets that are a power of two wide and aligned to
    // the data's own index space -- bucket b is always exactly the samples
    // [b * size, (b + 1) * size), whatever the reader is looking at.
    //
    // That alignment is the whole point of doing it this way. The obvious
    // arrangement is to divide the *visible* range into as many buckets as the
    // pane has columns, and it looks right in a screenshot and wrong in motion:
    // panning by one pixel slides every bucket boundary by a fraction of a
    // sample, so the two extremes each column selects keep changing and the
    // line crawls and boils under the pointer. Aligned buckets cannot do that.
    // Panning changes which buckets are on screen and nothing about what is in
    // them, so the line translates rigidly; zooming steps from one power of two
    // to the next, which is one honest change of detail per octave instead of a
    // continuous shimmer.
    //
    // It is also what makes each frame cost the pane rather than the data: the
    // scan below touches the visible samples once, and the bucket size is
    // chosen so that there are between one and two buckets per column.
    //
    // Within a bucket the smallest and the largest are emitted in the order
    // they occur, so the stroke keeps the direction the data has -- and this is
    // what stride sampling cannot do at all. A spike one sample wide is the
    // extreme of whatever bucket it lands in, so it is selected *because* it is
    // extreme, where a stride selects by position and reaches it only by luck.
    const double perColumn = static_cast<double>(visible) / static_cast<double>(columns);
    std::int64_t size = 1;
    // Ceiling to a power of two. Bounded rather than open, because a perColumn
    // that has gone to infinity under a degenerate view would otherwise not
    // stop.
    while (static_cast<double>(size) < perColumn && size < (std::int64_t{1} << 40)) {
        size <<= 1;
    }

    const std::int64_t firstBucket = first / size;
    const std::int64_t lastBucket = last / size;
    for (std::int64_t b = firstBucket; b <= lastBucket; ++b) {
        const std::int64_t i0 = b * size;
        // Clamped by the data and not by the window: a bucket at the edge of
        // the pane holds what it holds, or it would change as the reader
        // scrolled it into view.
        const std::int64_t i1 = std::min(i0 + size - 1, count - 1);

        double lowest = std::numeric_limits<double>::infinity();
        double highest = -std::numeric_limits<double>::infinity();
        std::int64_t lowIndex = -1;
        std::int64_t highIndex = -1;
        for (std::int64_t i = i0; i <= i1; ++i) {
            const double value = line.values[i];
            if (!std::isfinite(value)) {
                continue;
            }
            if (value < lowest) {
                lowest = value;
                lowIndex = i;
            }
            if (value > highest) {
                highest = value;
                highIndex = i;
            }
        }
        if (lowIndex < 0) {
            // The whole bucket is a gap.
            closeRun();
            continue;
        }

        // The two extremes occurred somewhere inside the bucket, and a bucket
        // is a pixel or two wide. Putting them at its start and its middle is
        // the nearest thing to where they were that costs nothing to say -- and
        // at a bucket of two samples it is exactly where they were.
        const double atFirst = toX(x0 + static_cast<double>(i0) * dx);
        const double atMiddle =
            toX(x0 + (static_cast<double>(i0) + static_cast<double>(size) / 2.0) * dx);
        if (lowIndex == highIndex) {
            place(atFirst, toY(lowest));
        }
        else if (lowIndex < highIndex) {
            place(atFirst, toY(lowest));
            place(atMiddle, toY(highest));
        }
        else {
            place(atFirst, toY(highest));
            place(atMiddle, toY(lowest));
        }
    }
    closeRun();
    return added(true);
}

void strokeRun(const QPointF* points, int count, double width, std::vector<QPointF>& out)
{
    strokeRunInto(points, count, width, [&out](double x, double y) { out.emplace_back(x, y); });
}

void markerAt(const QPointF& centre, double radius, std::vector<QPointF>& out)
{
    // The ring, once. Eight sines and cosines per marker would be sixteen
    // thousand of them on a line of two thousand points, which is a
    // measurable fraction of a frame spent recomputing a constant.
    static const std::vector<QPointF> ring = [] {
        std::vector<QPointF> unit;
        unit.reserve(kMarkerSides);
        for (int i = 0; i < kMarkerSides; ++i) {
            const double angle =
                2.0 * M_PI * static_cast<double>(i) / static_cast<double>(kMarkerSides);
            unit.emplace_back(std::cos(angle), std::sin(angle));
        }
        return unit;
    }();

    // Zig-zag around the ring -- 0, n-1, 1, n-2, ... -- which is the triangle
    // strip of a convex polygon and is why a marker costs its own side count
    // in vertices and not one more.
    int low = 0;
    int high = kMarkerSides - 1;
    bool fromLow = true;
    for (int i = 0; i < kMarkerSides; ++i) {
        const QPointF& at = ring[static_cast<std::size_t>(fromLow ? low++ : high--)];
        fromLow = !fromLow;
        out.emplace_back(centre.x() + at.x() * radius, centre.y() + at.y() * radius);
    }
}

} // namespace gui
