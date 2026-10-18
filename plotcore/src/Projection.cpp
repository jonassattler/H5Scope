// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Projection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace plotcore {
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

// Where drawn point `at` sits along the axis, before the axis makes it an x.
double positionOf(const PlotLine& line, long long at)
{
    if (line.positions != nullptr) {
        return line.positions[at] * line.positionScale;
    }
    return line.positionStart + static_cast<double>(at) * line.positionStep;
}

// Which drawn point of a time base `position` reads, when the time base's
// drawn points sit at `start + i * step`.
//
// The nearest, except past either end. Rounding puts a position more than half
// a step past the last drawn time one past the end, and the point is then
// dropped -- which is where every line drawn at its own elements ends: its last
// extreme is the line's last element, and the time base's last drawn point is
// half a bucket short of that. A position that has reached the time base's
// *next* point would be a line longer than its axis, and is still past the end.
double timeIndex(double position, double start, double step, long long count)
{
    if (!(std::abs(step) > 0.0) || count <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double exact = (position - start) / step;
    double index = std::round(exact);
    const auto last = static_cast<double>(count - 1);
    if (index > last && exact < static_cast<double>(count)) {
        index = last;
    }
    if (index < 0.0 && exact > -1.0) {
        index = 0.0;
    }
    return index;
}

} // namespace

AxisMapping mappingOver(double low, double high, bool logarithmic, double base)
{
    AxisMapping mapping;
    mapping.logarithmic = logarithmic;
    mapping.base = base;
    if (logarithmic) {
        // Only a number above one is a base. At exactly one the logarithm is a
        // division by zero and every value on the axis sits in the same place;
        // below it the axis runs backwards, which is a different request from
        // the one this answers. Refused rather than corrected, for the reason
        // the bounds below are: there is no nearest legal base to fall back to,
        // because the legal ones are open at one.
        if (!std::isfinite(base) || !(base > 1.0)) {
            return mapping;
        }
        // A bound at or below zero is not a bound this axis can be drawn
        // between, and there is no nearest one worth falling back to: clamping
        // it to some tiny positive number would put the whole of the data in
        // the top few pixels of a pane whose lower half means nothing. The view
        // is refused instead, exactly as a span of zero is, and the caller that
        // chose the bounds is where a positive one has to come from.
        if (!(low > 0.0) || !(high > 0.0)) {
            return mapping;
        }
        mapping.low = logOf(low, base);
        mapping.high = logOf(high, base);
    }
    else {
        mapping.low = low;
        mapping.high = high;
    }
    // A span, running upwards. Both halves are refusals rather than
    // corrections: a collapsed axis has no place to put anything, and an
    // inverted one is not a window the surface can produce -- every path that
    // states a view sorts its bounds first (see PlotSurface.setViewRange). The
    // x axis has always been held to exactly this; the y axis used to be held
    // to the absolute span instead, which differed only in that it would have
    // drawn an upside-down picture for a caller there is none of.
    mapping.usable = std::isfinite(mapping.high - mapping.low) && mapping.high > mapping.low;
    return mapping;
}

AxisMapping xMappingOf(const PlotView& view)
{
    return mappingOver(view.xMin, view.xMax, view.xLog, view.xLogBase);
}

AxisMapping yMappingOf(const PlotView& view)
{
    return mappingOver(view.yMin, view.yMax, view.yLog, view.yLogBase);
}

PlotView lineView(const PlotLine& line, const PlotView& view)
{
    if (!line.ownY) {
        return view;
    }
    PlotView own = view;
    own.yMin = line.yMin;
    own.yMax = line.yMax;
    own.yLog = line.ownYLog;
    return own;
}

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

double xOf(const PlotLine& line, const PlotAxis& axis, long long at)
{
    if (line.xs != nullptr) {
        // Stated by the line itself, which is what a fold onto a logarithmic
        // axis's columns does. See PlotLine::xs.
        const double x = line.xs[at];
        return std::isfinite(x) ? x : std::numeric_limits<double>::quiet_NaN();
    }
    const double position = positionOf(line, at);
    if (!axis.explicitX()) {
        return axis.start + position * axis.step;
    }
    // The time base has a value at each of its own positions and nowhere in
    // between, so a point that falls between two of them takes the nearer.
    //
    // At the share of the axis the position is, not at the position itself:
    // `values` holds the time base's *drawn* points and a position counts the
    // elements they were summarised from. See PlotAxis::valueStep -- with a
    // time base twenty thousand elements long summarised to two thousand
    // points, reading it at the position is reading ten times past its end.
    //
    // The run the reader is looking at first, where there is one and where it
    // reaches this position: it is the same time base read finer, so it is the
    // same answer only sharper. See PlotAxis::closerValues.
    if (axis.hasCloser() && std::abs(axis.closerStep) > 0.0) {
        const double at = timeIndex(position, axis.closerStart, axis.closerStep, axis.closerCount);
        if (at >= 0.0 && at < static_cast<double>(axis.closerCount)) {
            const double x = axis.closerValues[static_cast<std::size_t>(at)];
            return std::isfinite(x) ? x : std::numeric_limits<double>::quiet_NaN();
        }
    }
    if (!(std::abs(axis.valueStep) > 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double index = timeIndex(position, 0.0, axis.valueStep, axis.count);
    if (!(index >= 0.0) || !(index < static_cast<double>(axis.count))) {
        // Past the end of the time base. The line stops here rather than being
        // drawn against an x that does not exist, which is what "align" means
        // when a line and its axis are different lengths.
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double x = axis.values[static_cast<std::size_t>(index)];
    return std::isfinite(x) ? x : std::numeric_limits<double>::quiet_NaN();
}

std::vector<Point> samplesOf(const PlotLine& line, const PlotAxis& axis)
{
    std::vector<Point> out;
    if (line.values == nullptr || line.count <= 0) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(line.count));
    for (long long i = 0; i < line.count; ++i) {
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
    return yMappingOf(view).fractionOf(value);
}

double xFractionOf(double x, const PlotView& view)
{
    return xMappingOf(view).fractionOf(x);
}

PlotView uprightView(const PlotView& view)
{
    if (!view.transposed) {
        return view;
    }
    PlotView upright = view;
    upright.width = view.height;
    upright.height = view.width;
    upright.transposed = false;
    return upright;
}

Point transposedPoint(const Point& upright, const PlotView& view)
{
    // Upright, x = fx * height and y = (1 - fy) * width. On the pane, x is to
    // be fy * width and y (1 - fx) * height -- which is each of those taken
    // away from the side it was measured against.
    return {view.width - upright.y(), view.height - upright.x()};
}

Point uprightPoint(const Point& onPane, const PlotView& view)
{
    return {view.height - onPane.y(), view.width - onPane.x()};
}

PlotProjected projectLine(const PlotLine& line, const PlotAxis& axis, const PlotView& view,
                          std::vector<Point>& points, std::vector<PlotRun>& runs)
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
    const AxisMapping xMap = xMappingOf(view);
    if (!xMap.usable) {
        return {};
    }
    const AxisMapping yMap = yMappingOf(view);
    if (!yMap.usable) {
        return {};
    }

    // Everything below is arithmetic in double, and only the finished pixel
    // coordinate is ever cast down. That is the entire trick behind an epoch
    // timestamp drawing as a line here and as a staircase anywhere that casts
    // first.
    const auto toY = [&](double value) { return h - yMap.fractionOf(value) * h; };
    const auto toX = [&](double x) { return xMap.fractionOf(x) * w; };

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

    if (axis.explicitX() || line.xs != nullptr) {
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
        //
        // A line that states its own x comes here too, for the same reason and
        // with a better bound: it was folded to the pane's columns before it
        // was handed over, so it is two points a column at most.
        for (std::int64_t i = 0; i < count; ++i) {
            const double value = line.values[i];
            const double x = xOf(line, axis, i);
            if (!yMap.draws(value) || !xMap.draws(x)) {
                closeRun();
                continue;
            }
            place(toX(x), toY(value));
        }
        closeRun();
        // Every point here was drawn, so nothing was summarised *by this* --
        // but a time base does not make a line of summaries into a line of
        // samples. A custom tab's entry is folded on the way out of the file
        // like every other line, and the axis it is drawn against has no
        // bearing on what its values are. This said `false` outright, which is
        // how an entry drawn against a time base kept its dots at every zoom.
        return added(line.summarised);
    }

    // x is affine in the sample index, so the window can be turned back into a
    // range of indices and the samples outside it never touched.
    //
    // Still true on a logarithmic x axis, and that is worth saying because it
    // looks as though it should not be: the window's bounds are in the data's
    // own units whichever scale the axis is drawn on -- the logarithm happens
    // between a value and a *pixel*, not between the reader and the data -- so
    // the inversion below is the same subtraction it always was.
    const double x0 = axis.start + line.positionStart * axis.step;
    const double dx = line.positionStep * axis.step;

    std::int64_t first = 0;
    std::int64_t last = count - 1;
    if (line.positions != nullptr) {
        // Not evenly spaced, so the window is not a division: it is a range of
        // positions, and they are written in order. One point either side of
        // it, for the reason the even case below gives.
        if (!(std::abs(axis.step) > 0.0) || !(line.positionScale > 0.0)) {
            return {};
        }
        double low = (view.xMin - axis.start) / axis.step / line.positionScale;
        double high = (view.xMax - axis.start) / axis.step / line.positionScale;
        if (low > high) {
            std::swap(low, high);
        }
        if (std::isnan(low) || std::isnan(high)) {
            return {};
        }
        const double* const begin = line.positions;
        const double* const end = begin + count;
        const auto from = static_cast<std::int64_t>(std::lower_bound(begin, end, low) - begin);
        const auto to = static_cast<std::int64_t>(std::upper_bound(begin, end, high) - begin);
        first = std::max<std::int64_t>(from - 1, 0);
        last = std::min<std::int64_t>(to, count - 1);
    }
    else if (std::abs(dx) > 0.0) {
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
    std::int64_t columns = static_cast<std::int64_t>(std::ceil(w * std::max(view.pixelRatio, 1.0)));
    if (view.maxColumns > 0) {
        columns = std::min<std::int64_t>(columns, view.maxColumns);
    }
    columns = std::max<std::int64_t>(columns, 1);

    if (visible <= static_cast<std::int64_t>(kSamplesPerColumn) * columns) {
        // Zoomed in far enough that the envelope would emit more vertices than
        // there are values. Draw them.
        for (std::int64_t i = first; i <= last; ++i) {
            const double value = line.values[i];
            const double x =
                line.positions != nullptr ? xOf(line, axis, i) : x0 + static_cast<double>(i) * dx;
            if (!yMap.draws(value) || !xMap.draws(x)) {
                closeRun();
                continue;
            }
            place(toX(x), toY(value));
        }
        closeRun();
        // ...and they are samples only if the model did not summarise them on
        // the way here. Each value of an envelope is the extreme of a bucket,
        // and a marker on it would be punctuation on a reading nobody took.
        // This is not the same question as whether the loop above ran: a
        // summary fine enough to fit four points in a column is still a
        // summary.
        //
        // The line says so itself rather than being guessed at from its step,
        // which is where this was read from and which is wrong at both ends --
        // see PlotLine::summarised.
        return added(line.summarised);
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
    // A logarithmic x axis is the one case where "a bucket is a column" stops
    // being true, and the alternative is worse. Buckets are aligned in the
    // data's index space and a decade near the origin holds very few indices,
    // so on a log axis the leftmost buckets are wide on the pane and the
    // rightmost are narrow: the left of such a plot is summarised more coarsely
    // than the pane could show. That is honest -- those really are all the
    // samples that decade has, and each bucket still carries both its extremes
    // -- and it resolves the moment the reader zooms, because the run they are
    // looking at is read again at a finer bucket like any other. Bucketing in
    // log space instead would slide every boundary with the pan, which is the
    // crawling this alignment exists to prevent.
    //
    // That paragraph was wrong about the zoom, and the reader saw it. Every
    // run was bucketed by element too, so the left of the pane stayed an order
    // of magnitude coarser for every decade on screen however far they zoomed,
    // and the first bucket's worth of the axis had nothing drawn in it at all.
    // A window of an octave or more on a logarithmic axis no longer reaches
    // this loop: the models fold it per pixel column on a grid aligned in the
    // logarithm -- which answers the crawling as well, because the grid is
    // fixed and the pan only moves which of it is on screen -- and hand it over
    // with its own x. See LogColumns in Levels.hpp and the branch above.
    // Under an octave, the difference between the widest and narrowest
    // column is under two, and this is still the right place for it.
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
            // The axis decides what counts, which on a logarithmic one takes
            // the non-positive samples out of the extremes as well as out of
            // the drawing. A bucket whose only large value cannot be drawn has
            // not got that value; leaving it in would put the top of the
            // envelope at a place the curve never reaches.
            if (!yMap.draws(value)) {
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

        // Each extreme where it is. The scan above kept its index, so this
        // costs nothing to say. Both used to stand at the bucket's start and
        // its middle, which put a spike on the bucket's last sample half a
        // bucket early and split a straight rise across two columns by distance
        // rather than by where it went. A line that names its positions names
        // them here too -- an envelope folded again for a narrower pane draws
        // each extreme at the element it is, not at its place in the summary.
        const auto xAt = [&](std::int64_t i) {
            return line.positions != nullptr ? xOf(line, axis, i)
                                             : x0 + static_cast<double>(i) * dx;
        };
        const double xLow = xAt(lowIndex);
        const double xHigh = xAt(highIndex);
        if (!xMap.draws(xLow) || !xMap.draws(xHigh)) {
            // Both stations of a bucket are on the axis or the bucket is not
            // drawn, which on a logarithmic axis is every bucket at or below
            // zero. A gap, for the reason every gap here is one.
            closeRun();
            continue;
        }
        if (lowIndex == highIndex) {
            place(toX(xLow), toY(lowest));
        }
        else if (lowIndex < highIndex) {
            place(toX(xLow), toY(lowest));
            place(toX(xHigh), toY(highest));
        }
        else {
            place(toX(xHigh), toY(highest));
            place(toX(xLow), toY(lowest));
        }
    }
    closeRun();
    return added(true);
}

std::optional<NearestSample> nearestSample(std::span<const PlotLine> lines,
                                           std::span<const PlotAxis> axes, const PlotView& pane,
                                           double px, double py)
{
    // Searched upright, as the lines were projected, with the pointer moved
    // there first and the answer moved back: on a transposed pane the pointer's
    // height is what says which x it is over. See PlotView::transposed.
    const PlotView upright = uprightView(pane);
    if (pane.transposed) {
        const Point at = uprightPoint(Point(px, py), pane);
        px = at.x();
        py = at.y();
    }
    const double w = upright.width;
    const double h = upright.height;
    const AxisMapping xMap = xMappingOf(pane);
    if (!(w > 0.0) || !(h > 0.0) || !xMap.usable || lines.empty() || axes.empty()) {
        return std::nullopt;
    }
    // In the data's own units, through the axis's own scale -- so on a
    // logarithmic axis the pointer resolves to the x it is actually over
    // rather than to the one it would be over if the axis were linear.
    const double wanted = xMap.valueAt(px / w);

    double bestDistance = std::numeric_limits<double>::infinity();
    int bestLine = -1;
    double bestX = 0.0;
    double bestY = 0.0;
    double bestPx = 0.0;
    double bestPy = 0.0;

    // Per line, because a line on an axis of its own is drawn through that
    // axis: snapping it through the common one would put the crosshair's dot
    // somewhere the stroke is not.
    AxisMapping yMap;
    const auto consider = [&](int index, long long at, double x, double value) {
        // A sample the axes cannot place is not a sample the crosshair may snap
        // to: it was not drawn, and a readout of it would name a point that is
        // not on the pane.
        if (!xMap.draws(x) || !yMap.draws(value)) {
            return false;
        }
        const double sx = xMap.fractionOf(x) * w;
        const double sy = h - yMap.fractionOf(value) * h;
        const double distance = (sx - px) * (sx - px) + (sy - py) * (sy - py);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestLine = index;
            bestX = x;
            bestY = value;
            bestPx = sx;
            bestPy = sy;
        }
        (void)at;
        return true;
    };

    for (std::size_t slot = 0; slot < lines.size(); ++slot) {
        const auto index = static_cast<int>(slot);
        const PlotLine& line = lines[slot];
        const PlotAxis& axis =
            axes.size() == 1 ? axes.front() : axes[std::min(slot, axes.size() - 1)];
        if (line.values == nullptr || line.count <= 0) {
            continue;
        }
        yMap = yMappingOf(lineView(line, pane));

        if (axis.explicitX() || line.xs != nullptr ||
            !(std::abs(line.positionStep * axis.step) > 0.0)) {
            // A time base need not be monotonic, so there is no index to solve
            // for and the line is searched. Affordable because a line drawn
            // against one is a custom plot entry, and those are thinned on the
            // way out of the file like everything else.
            for (long long i = 0; i < line.count; ++i) {
                const double value = line.values[i];
                if (!std::isfinite(value)) {
                    continue;
                }
                const double x = xOf(line, axis, i);
                if (std::isfinite(x)) {
                    consider(index, i, x, value);
                }
            }
            continue;
        }

        // x is affine in the index, so the nearest one is arithmetic -- or, for
        // a line that names where each point is, a bisection of those, for
        // the reason projectLine bisects them: an envelope's extremes are at
        // their own elements, not half a bucket apart, and a guess from the
        // step can start the walk on the wrong one of a pair.
        long long centre = 0;
        if (line.positions != nullptr) {
            if (!(std::abs(axis.step) > 0.0) || !(line.positionScale > 0.0)) {
                continue;
            }
            const double position = (wanted - axis.start) / axis.step / line.positionScale;
            if (!std::isfinite(position)) {
                continue;
            }
            const double* const end = line.positions + line.count;
            long long after = std::lower_bound(line.positions, end, position) - line.positions;
            after = std::min(after, line.count - 1);
            centre = after;
            if (after > 0 &&
                position - line.positions[after - 1] < line.positions[after] - position) {
                centre = after - 1;
            }
        }
        else {
            const double x0 = axis.start + line.positionStart * axis.step;
            const double dx = line.positionStep * axis.step;
            const double exact = (wanted - x0) / dx;
            if (!std::isfinite(exact)) {
                continue;
            }
            centre = static_cast<long long>(
                std::clamp(std::llround(exact), 0LL, static_cast<long long>(line.count - 1)));
        }

        // Outward from there until a drawable sample turns up. A gap wider than
        // this is a gap the crosshair declines to reach across, which is the
        // same answer the line itself gives: there is nothing drawn there.
        constexpr long long kReach = 64;
        for (long long step = 0; step <= kReach; ++step) {
            bool found = false;
            for (const long long at : {centre - step, centre + step}) {
                if (at < 0 || at >= line.count) {
                    continue;
                }
                const double value = line.values[at];
                if (!std::isfinite(value)) {
                    continue;
                }
                // What the walk is looking for is a sample that could be
                // *taken*, not one that is merely finite. Stopping on a value
                // the axes declined to place -- every non-positive one on a
                // logarithmic axis -- ended the search having considered
                // nothing, and the crosshair then found no sample at all in a
                // pane full of them.
                found = consider(index, at, xOf(line, axis, at), value) || found;
            }
            if (found) {
                break;
            }
        }
    }

    if (bestLine < 0) {
        return std::nullopt;
    }
    if (pane.transposed) {
        const Point drawn = transposedPoint(Point(bestPx, bestPy), pane);
        bestPx = drawn.x();
        bestPy = drawn.y();
    }
    return NearestSample{bestLine, bestX, bestY, bestPx, bestPy};
}

void strokeRun(const Point* points, int count, double width, std::vector<Point>& out)
{
    strokeRunInto(points, count, width, [&out](double x, double y) { out.emplace_back(x, y); });
}

void markerAt(const Point& centre, double radius, std::vector<Point>& out)
{
    // The ring, once. Eight sines and cosines per marker would be sixteen
    // thousand of them on a line of two thousand points, which is a
    // measurable fraction of a frame spent recomputing a constant.
    static const std::vector<Point> ring = [] {
        std::vector<Point> unit;
        unit.reserve(kMarkerSides);
        for (int i = 0; i < kMarkerSides; ++i) {
            const double angle = 2.0 * std::numbers::pi_v<double> * static_cast<double>(i) /
                                 static_cast<double>(kMarkerSides);
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
        const Point& at = ring[static_cast<std::size_t>(fromLow ? low++ : high--)];
        fromLow = !fromLow;
        out.emplace_back(centre.x() + at.x() * radius, centre.y() + at.y() * radius);
    }
}

} // namespace plotcore
