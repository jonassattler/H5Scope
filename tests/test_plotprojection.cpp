// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// What the plot decides before a pixel is touched.
//
// Every case here is a regression the plot library evaluation turned up, and
// four of them are things the plot got wrong for as long as it drew through Qt
// Graphs: a one-sample spike thinned away by a stride, a run of missing data
// drawn as a straight line across the absence, an epoch timestamp collapsed
// into a staircase by a float32 vertex, and a logarithmic axis that could not
// be expressed at all.
//
// No window, no engine, no file. gui::projectLine takes doubles and fills two
// vectors, which is what makes it assertable here rather than through a grab of
// something that was drawn -- and which is why the projection is a separate
// unit from the item that owns a scene graph node.

#include "gui/PlotProjection.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// A pane 1000 by 500, showing exactly the window asked for.
gui::PlotView paneOver(double xMin, double xMax, double yMin, double yMax)
{
    gui::PlotView view;
    view.xMin = xMin;
    view.xMax = xMax;
    view.yMin = yMin;
    view.yMax = yMax;
    view.width = 1000.0;
    view.height = 500.0;
    return view;
}

gui::PlotLine lineOver(const std::vector<double>& values)
{
    gui::PlotLine line;
    line.values = values.data();
    line.count = static_cast<qsizetype>(values.size());
    return line;
}

struct Projected
{
    std::vector<QPointF> points;
    std::vector<gui::PlotRun> runs;
};

Projected project(const gui::PlotLine& line, const gui::PlotAxis& axis, const gui::PlotView& view)
{
    Projected out;
    gui::projectLine(line, axis, view, out.points, out.runs);
    return out;
}

/// The topmost point drawn, in pixels. The smallest y, because y grows
/// downward on screen and upward on the axis.
double highestPoint(const Projected& drawn)
{
    double top = std::numeric_limits<double>::infinity();
    for (const QPointF& point : drawn.points) {
        top = std::min(top, point.y());
    }
    return top;
}

} // namespace

// --- the envelope ---------------------------------------------------------

TEST_CASE("a one-sample spike in a million survives the thinning", "[plot]")
{
    // The case DatasetTableModel.hpp:205-213 names as the cost of thinning by
    // stride: "a spike narrower than one stride is not drawn". A million
    // samples into a thousand columns is a thousand samples a column, so a
    // stride would have to land on this one index out of a thousand by luck.
    std::vector<double> values(1000000, 0.0);
    values[500000] = 10.0;

    const gui::PlotView view = paneOver(0.0, 1000000.0, -1.0, 11.0);
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    REQUIRE(drawn.runs.size() == 1);
    // One run, two points a column, and the pane is a thousand columns wide.
    CHECK(drawn.points.size() <= 2000);

    // Where 10.0 lands: a twelfth of the way down from the top of a 500-pixel
    // pane showing -1 to 11.
    const double expected = 500.0 - (10.0 - (-1.0)) / 12.0 * 500.0;
    CHECK(highestPoint(drawn) == Approx(expected).margin(0.01));
}

TEST_CASE("the point count follows the pane and not the file", "[plot]")
{
    // The property the whole decimation exists for, and the one a change that
    // broke it would break silently: the plot would still be correct, it would
    // just submit four million vertices to draw a thousand pixels.
    const gui::PlotView view = paneOver(0.0, 1.0, -1.0, 1.0);

    std::vector<double> small(10000);
    std::vector<double> large(4000000);
    for (std::size_t i = 0; i < small.size(); ++i) {
        small[i] = std::sin(static_cast<double>(i) / 100.0);
    }
    for (std::size_t i = 0; i < large.size(); ++i) {
        large[i] = std::sin(static_cast<double>(i) / 100.0);
    }

    gui::PlotLine shortLine = lineOver(small);
    shortLine.positionStep = 1.0 / static_cast<double>(small.size());
    gui::PlotLine longLine = lineOver(large);
    longLine.positionStep = 1.0 / static_cast<double>(large.size());

    const Projected few = project(shortLine, gui::PlotAxis{}, view);
    const Projected many = project(longLine, gui::PlotAxis{}, view);

    CHECK(few.points.size() <= 2002);
    CHECK(many.points.size() <= 2002);
    // Four hundred times the samples, and within a couple of points the same
    // amount of drawing.
    CHECK(many.points.size() >= few.points.size() - 2);
}

TEST_CASE("zoomed in far enough, every sample is drawn at its own x", "[plot]")
{
    // The envelope is a saving and not a rule: once there are fewer samples on
    // screen than the pane has columns, an envelope would emit *more* points
    // than there are samples, and the line is drawn sample for sample.
    std::vector<double> values(1000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<double>(i);
    }

    // Twenty samples across a thousand pixels.
    const gui::PlotView view = paneOver(100.0, 120.0, 0.0, 1000.0);
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    REQUIRE(drawn.runs.size() == 1);
    // Twenty-one samples in the window and one either side of it, so that the
    // line enters and leaves the pane at its edges rather than stopping a pixel
    // short of them.
    CHECK(drawn.points.size() == 23);
    for (std::size_t i = 1; i < drawn.points.size(); ++i) {
        CHECK(drawn.points[i].x() > drawn.points[i - 1].x());
    }
}

// --- gaps -----------------------------------------------------------------

TEST_CASE("a run of missing data is a gap and not a line across it", "[plot]")
{
    // What the old boundary did was drop the non-finite samples and hand the
    // survivors to a line renderer, which joins them: a clean straight diagonal
    // across data that was never taken.
    std::vector<double> values(1000, 1.0);
    for (std::size_t i = 400; i < 600; ++i) {
        values[i] = kNaN;
    }

    const gui::PlotView view = paneOver(0.0, 1000.0, 0.0, 2.0);
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    REQUIRE(drawn.runs.size() == 2);

    const gui::PlotRun& before = drawn.runs[0];
    const gui::PlotRun& after = drawn.runs[1];
    const double endsAt =
        drawn.points[static_cast<std::size_t>(before.first + before.count - 1)].x();
    const double startsAt = drawn.points[static_cast<std::size_t>(after.first)].x();

    // The gap is a fifth of the data, so it is a fifth of the pane.
    CHECK(endsAt == Approx(400.0).margin(2.0));
    CHECK(startsAt == Approx(600.0).margin(2.0));
}

TEST_CASE("a single drawable sample between two gaps draws nothing", "[plot]")
{
    // A stroke needs a direction, and one station has none. Dropping it keeps
    // the counts honest rather than emitting a vertex pair that rasterises to
    // nothing and is counted as if it had drawn.
    std::vector<double> values(11, kNaN);
    values[5] = 1.0;

    const gui::PlotView view = paneOver(0.0, 10.0, 0.0, 2.0);
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    CHECK(drawn.runs.empty());
    CHECK(drawn.points.empty());
}

// --- precision ------------------------------------------------------------

TEST_CASE("an epoch timestamp draws as a line and not as a staircase", "[plot]")
{
    // x near 1.7e9 has a float32 spacing of 128, so a line sampled every
    // millisecond and cast to float before projection collapses into flat
    // treads a hundred and twenty-eight seconds wide. Projecting in double and
    // casting the *pixel* is what keeps it a line -- a pixel coordinate is at
    // most a few thousand, where float has room to spare.
    constexpr double kEpoch = 1.7e9;
    std::vector<double> values(2000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<double>(i);
    }

    gui::PlotAxis axis;
    axis.start = kEpoch;
    axis.step = 0.001;

    gui::PlotView view = paneOver(kEpoch, kEpoch + 2.0, 0.0, 2000.0);
    const Projected drawn = project(lineOver(values), axis, view);

    REQUIRE(drawn.points.size() > 500);
    // Cast exactly as the vertex buffer does. Every step has to survive it.
    int distinct = 1;
    for (std::size_t i = 1; i < drawn.points.size(); ++i) {
        const auto previous = static_cast<float>(drawn.points[i - 1].x());
        const auto current = static_cast<float>(drawn.points[i].x());
        if (current > previous) {
            ++distinct;
        }
    }
    CHECK(distinct == static_cast<int>(drawn.points.size()));

    // And the same values cast before the projection: what the staircase would
    // have been, so the test says what it is defending against.
    int distinctIfCastFirst = 1;
    for (std::size_t i = 1; i < drawn.points.size(); ++i) {
        const auto previous = static_cast<float>(kEpoch + static_cast<double>(i - 1) * 0.001);
        const auto current = static_cast<float>(kEpoch + static_cast<double>(i) * 0.001);
        if (current > previous) {
            ++distinctIfCastFirst;
        }
    }
    CHECK(distinctIfCastFirst < 4);
}

// --- the logarithmic axis --------------------------------------------------

TEST_CASE("a logarithmic axis spaces the decades evenly", "[plot]")
{
    gui::PlotView view = paneOver(0.0, 4.0, 1.0, 10000.0);
    view.logY = true;

    CHECK(gui::yFractionOf(1.0, view) == Approx(0.0));
    CHECK(gui::yFractionOf(10.0, view) == Approx(0.25));
    CHECK(gui::yFractionOf(100.0, view) == Approx(0.5));
    CHECK(gui::yFractionOf(10000.0, view) == Approx(1.0));
}

TEST_CASE("a logarithmic axis treats a value at or below zero as missing", "[plot]")
{
    // Not clamped to the floor, which would draw a reading nobody took at the
    // bottom of the pane. There is no logarithm of zero and the plot says so by
    // leaving a gap.
    const std::vector<double> values{1.0, 10.0, 0.0, -5.0, 100.0, 1000.0};

    gui::PlotView view = paneOver(0.0, 5.0, 1.0, 1000.0);
    view.logY = true;
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    REQUIRE(drawn.runs.size() == 2);
    CHECK(drawn.runs[0].count == 2);
    CHECK(drawn.runs[1].count == 2);
}

TEST_CASE("a logarithmic axis puts its floor six decades below the top", "[plot]")
{
    // The data cannot say where the bottom of a log axis is when it reaches
    // zero, so the plot states it -- and states it in one place, so that the
    // ticks and the curve agree.
    gui::PlotView view = paneOver(0.0, 4.0, 0.0, 1000.0);
    view.logY = true;

    CHECK(gui::kLogDecades == 6.0);
    CHECK(gui::yFractionOf(1000.0, view) == Approx(1.0));
    CHECK(gui::yFractionOf(0.001, view) == Approx(0.0));
}

// --- where a sample sits along x ------------------------------------------

TEST_CASE("a stride makes each drawn point cover the elements it skipped", "[plot]")
{
    // A thinned line skips `stride` elements between one drawn point and the
    // next, so its x has to skip the same distance -- otherwise a plot of every
    // tenth row is drawn a tenth as wide as the table it came from.
    const std::vector<double> values{0.0, 1.0, 2.0, 3.0};
    gui::PlotLine line = lineOver(values);
    line.positionStep = 10.0;

    gui::PlotAxis axis;
    axis.start = 5.0;
    axis.step = 2.0;

    CHECK(gui::xOf(line, axis, 0) == Approx(5.0));
    CHECK(gui::xOf(line, axis, 1) == Approx(25.0));
    CHECK(gui::xOf(line, axis, 3) == Approx(65.0));
}

TEST_CASE("a time base gives a sample the nearer of its own positions", "[plot]")
{
    // Interpolating would invent an x, which is the same mistake as inventing
    // a y: the time base has a value at each of its own positions and nowhere
    // in between.
    const std::vector<double> times{100.0, 200.0, 400.0};
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0};

    gui::PlotLine line = lineOver(values);
    gui::PlotAxis axis;
    axis.values = times.data();
    axis.count = static_cast<qsizetype>(times.size());

    CHECK(gui::xOf(line, axis, 0) == Approx(100.0));
    CHECK(gui::xOf(line, axis, 2) == Approx(400.0));
    // Past the end of the time base, so it sits nowhere: the line stops rather
    // than being drawn against an x that does not exist.
    CHECK(std::isnan(gui::xOf(line, axis, 3)));
}

TEST_CASE("a line against a time base is drawn sample for sample", "[plot]")
{
    // A time base need not be monotonic, so a pixel column would hold samples
    // from all over the line and the extremes of that are an envelope of
    // nothing. This path draws every sample and says so.
    const std::vector<double> times{0.0, 5.0, 1.0, 4.0, 2.0};
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};

    gui::PlotLine line = lineOver(values);
    gui::PlotAxis axis;
    axis.values = times.data();
    axis.count = static_cast<qsizetype>(times.size());

    const Projected drawn = project(line, axis, paneOver(0.0, 5.0, 0.0, 6.0));
    REQUIRE(drawn.runs.size() == 1);
    CHECK(drawn.points.size() == 5);
    // In the order the samples occur and not in x order, which is what "drawn
    // sample for sample" means.
    CHECK(drawn.points[0].x() == Approx(0.0));
    CHECK(drawn.points[1].x() == Approx(1000.0));
    CHECK(drawn.points[2].x() == Approx(200.0));
}

TEST_CASE("the data-space seam agrees with the projection about what is drawn", "[plot]")
{
    // samplesOf() is what tests/test_customplot.cpp asserts the x arithmetic
    // through. It has to answer the same question projectLine() does, or the
    // suite would be testing a second implementation of the boundary rather
    // than the boundary.
    const std::vector<double> values{1.0, 2.0, kNaN, 4.0, 5.0};
    gui::PlotLine line = lineOver(values);
    gui::PlotAxis axis;
    axis.start = 10.0;
    axis.step = 0.5;

    const std::vector<QPointF> samples = gui::samplesOf(line, axis);
    REQUIRE(samples.size() == 4);
    CHECK(samples[0].x() == Approx(10.0));
    CHECK(samples[0].y() == Approx(1.0));
    CHECK(samples[1].x() == Approx(10.5));
    CHECK(samples[2].x() == Approx(11.5));
    CHECK(samples[3].x() == Approx(12.0));

    const Projected drawn = project(line, axis, paneOver(10.0, 12.0, 0.0, 6.0));
    // The same four samples -- and two strokes rather than one, because the
    // missing sample between them is the difference the seam cannot express:
    // samplesOf() reports what is drawable and the projection reports where
    // one stroke ends and the next begins.
    CHECK(drawn.points.size() == 4);
    CHECK(drawn.runs.size() == 2);
}

// --- the stroke ------------------------------------------------------------

TEST_CASE("a stroke is built out of triangles at the width it was asked for", "[plot]")
{
    // Line width above 1.0 is an optional RHI feature and several backends
    // ignore it without saying so. The highlight -- the one affordance for
    // following a line through a bundle of fifty -- is a line drawn at double
    // width, and a stroke that is two pixels on one machine and one on the next
    // is not an affordance.
    const std::vector<QPointF> along{{0.0, 100.0}, {50.0, 100.0}, {100.0, 100.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(along.data(), static_cast<int>(along.size()), 4.0, stroke);

    REQUIRE(stroke.size() == 6);
    for (std::size_t i = 0; i < along.size(); ++i) {
        const QPointF& above = stroke[i * 2];
        const QPointF& below = stroke[i * 2 + 1];
        CHECK(above.x() == Approx(along[i].x()));
        CHECK(below.x() == Approx(along[i].x()));
        CHECK(std::abs(above.y() - below.y()) == Approx(4.0));
    }
}

TEST_CASE("a reversal does not throw a miter across the pane", "[plot]")
{
    // What an envelope draws at a one-sample spike: up one column and straight
    // back down. The two normals at that join cancel, and an uncut miter would
    // reach to infinity looking for the bisector of an angle of nothing.
    const std::vector<QPointF> spike{{0.0, 100.0}, {0.0, 0.0}, {0.0, 100.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(spike.data(), static_cast<int>(spike.size()), 2.0, stroke);

    REQUIRE(stroke.size() == 6);
    for (const QPointF& vertex : stroke) {
        CHECK(std::abs(vertex.x()) <= 4.0);
        CHECK(std::isfinite(vertex.y()));
    }
}

TEST_CASE("a stroke through a corner stays the width it was asked for", "[plot]")
{
    // A mitred join lengthens the offset so the outside of the corner is as far
    // from the line as the straight is. Cut it and the stroke narrows through
    // every bend; leave the offset unlengthened and it narrows too.
    const std::vector<QPointF> corner{{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(corner.data(), static_cast<int>(corner.size()), 2.0, stroke);

    REQUIRE(stroke.size() == 6);
    // The corner station: a right angle, so the bisector reaches out by root
    // two and the two vertices are that much further apart than the width.
    const double across = std::hypot(stroke[2].x() - stroke[3].x(), stroke[2].y() - stroke[3].y());
    CHECK(across == Approx(2.0 * std::sqrt(2.0)).margin(0.001));
}

// --- the edges, where a hand-written renderer earns its keep ---------------
//
// Everything below is a case a plotting library would have handled for us and
// now does not. None of them are hypothetical: a dataset with one element, a
// dataset that is all fill value, an axis the reader has run off the end of by
// dragging, a column of constants, a value of 1e300 next to a value of 1 --
// every one of these is something an HDF5 file contains on a normal Tuesday.

TEST_CASE("nothing to draw draws nothing", "[plot]")
{
    const gui::PlotView view = paneOver(0.0, 10.0, 0.0, 10.0);

    SECTION("a line with no values")
    {
        gui::PlotLine line;
        const Projected drawn = project(line, gui::PlotAxis{}, view);
        CHECK(drawn.points.empty());
        CHECK(drawn.runs.empty());
        CHECK(gui::samplesOf(line, gui::PlotAxis{}).empty());
    }

    SECTION("a line whose count says none")
    {
        const std::vector<double> values{1.0, 2.0, 3.0};
        gui::PlotLine line = lineOver(values);
        line.count = 0;
        const Projected drawn = project(line, gui::PlotAxis{}, view);
        CHECK(drawn.points.empty());
    }

    SECTION("a line of one sample -- a stroke needs two")
    {
        const std::vector<double> values{5.0};
        const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);
        CHECK(drawn.points.empty());
        CHECK(drawn.runs.empty());
        // ...but the sample is still a sample, and the seam still reports it.
        CHECK(gui::samplesOf(lineOver(values), gui::PlotAxis{}).size() == 1);
    }

    SECTION("a line that is entirely missing")
    {
        const std::vector<double> values(500, kNaN);
        const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);
        CHECK(drawn.points.empty());
        CHECK(drawn.runs.empty());
    }
}

TEST_CASE("a pane with no room in it draws nothing", "[plot]")
{
    // Reached on the first frame, before the item has been given a size, and
    // again whenever a splitter is dragged shut. Dividing by it would put a
    // NaN in every vertex.
    const std::vector<double> values{1.0, 2.0, 3.0};

    gui::PlotView flat = paneOver(0.0, 3.0, 0.0, 3.0);
    flat.height = 0.0;
    CHECK(project(lineOver(values), gui::PlotAxis{}, flat).points.empty());

    gui::PlotView narrow = paneOver(0.0, 3.0, 0.0, 3.0);
    narrow.width = 0.0;
    CHECK(project(lineOver(values), gui::PlotAxis{}, narrow).points.empty());

    gui::PlotView backwards = paneOver(0.0, 3.0, 0.0, 3.0);
    backwards.width = -100.0;
    CHECK(project(lineOver(values), gui::PlotAxis{}, backwards).points.empty());
}

TEST_CASE("a window with no span in it draws nothing", "[plot]")
{
    const std::vector<double> values{1.0, 2.0, 3.0};

    SECTION("x collapsed")
    {
        CHECK(project(lineOver(values), gui::PlotAxis{}, paneOver(1.0, 1.0, 0.0, 3.0))
                  .points.empty());
    }
    SECTION("x inverted")
    {
        CHECK(project(lineOver(values), gui::PlotAxis{}, paneOver(3.0, 1.0, 0.0, 3.0))
                  .points.empty());
    }
    SECTION("y collapsed -- a flat line has no span to be drawn against")
    {
        CHECK(project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 3.0, 2.0, 2.0))
                  .points.empty());
    }
    SECTION("a window that is not a number")
    {
        CHECK(project(lineOver(values), gui::PlotAxis{}, paneOver(kNaN, 3.0, 0.0, 3.0))
                  .points.empty());
    }
}

TEST_CASE("an axis that runs backwards still finds its window", "[plot]")
{
    // A negative step is a legal reading of a stated range -- "start 10, step
    // -1" is what a reader types for a countdown -- and the narrowing turns a
    // window into a range of indices by dividing by that step. Getting the
    // sign wrong there does not draw the line the wrong way round, it draws
    // nothing at all.
    std::vector<double> values(1000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<double>(i);
    }

    gui::PlotAxis axis;
    axis.start = 1000.0;
    axis.step = -1.0;

    // Samples 0..1000 sit at x 1000 down to 0; this window holds the last
    // twenty of them.
    const Projected drawn = project(lineOver(values), axis, paneOver(0.0, 20.0, 0.0, 1000.0));
    REQUIRE(drawn.runs.size() == 1);
    CHECK(drawn.points.size() >= 20);
    // Drawn in sample order, so x decreases along the stroke.
    CHECK(drawn.points.front().x() > drawn.points.back().x());
}

TEST_CASE("a window off the end of the data draws what is nearest", "[plot]")
{
    // Reached by dragging. The pan is clamped so it cannot happen from the
    // plot's own gestures, but a custom tab's axis is stated by the reader and
    // a saved view can outlive the file it was saved against.
    std::vector<double> values(100);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<double>(i);
    }

    // Entirely to the right of the data: the clamp leaves one sample, which is
    // not a stroke.
    const Projected past =
        project(lineOver(values), gui::PlotAxis{}, paneOver(1000.0, 2000.0, 0.0, 100.0));
    CHECK(past.runs.empty());

    // Half on: everything from the window's left edge to the end of the data.
    const Projected partly =
        project(lineOver(values), gui::PlotAxis{}, paneOver(50.0, 200.0, 0.0, 100.0));
    REQUIRE(partly.runs.size() == 1);
    CHECK(partly.points.size() >= 49);
}

TEST_CASE("a value far outside the window is drawn off the pane, not dropped", "[plot]")
{
    // The difference between clipping and censoring. A line that leaves the top
    // of a zoomed-in pane has to leave it -- drawn to where it goes and cut off
    // by the frame -- because a line that stopped at the last visible sample
    // would end in mid-air with nothing to say it had.
    const std::vector<double> values{0.0, 1000.0, 0.0};
    const Projected drawn =
        project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 2.0, -1.0, 1.0));

    REQUIRE(drawn.runs.size() == 1);
    CHECK(drawn.points.size() == 3);
    // Above the top of a 500-pixel pane, which is y < 0.
    CHECK(drawn.points[1].y() < 0.0);
}

TEST_CASE("a value too large to be a float is still a point", "[plot]")
{
    // 1e300 in a window a unit wide projects to 1e302 pixels, and that is
    // infinity once it is a float32. An infinity in a vertex buffer is not a
    // point off screen -- it is a triangle the rasteriser may do anything with.
    const std::vector<double> values{0.0, 1e300, 0.0, -1e300, 0.0};
    const Projected drawn =
        project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 4.0, -1.0, 1.0));

    REQUIRE(drawn.runs.size() == 1);
    REQUIRE(drawn.points.size() == 5);
    for (const QPointF& point : drawn.points) {
        CHECK(std::isfinite(static_cast<float>(point.x())));
        CHECK(std::isfinite(static_cast<float>(point.y())));
    }
    // Still well outside the pane, so the stroke still leaves at the frame.
    CHECK(drawn.points[1].y() < -1000.0);
    CHECK(drawn.points[3].y() > 1500.0);
}

TEST_CASE("an infinity is missing data and not a very large number", "[plot]")
{
    // HDF5 files carry infinities where a division went wrong, and a plot that
    // drew one would rescale the whole pane around a value that is not a
    // reading.
    const std::vector<double> values{1.0, 2.0, std::numeric_limits<double>::infinity(), 4.0, 5.0};
    const Projected drawn =
        project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 4.0, 0.0, 6.0));

    CHECK(drawn.runs.size() == 2);
    CHECK(drawn.points.size() == 4);
}

TEST_CASE("a constant line is a flat stroke across the pane", "[plot]")
{
    // The column every file has. Its envelope has the same value for its
    // smallest and its largest, which is the case where emitting both would
    // double the vertex count to draw the same pixel twice.
    const std::vector<double> values(100000, 7.0);
    const Projected drawn =
        project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 100000.0, 6.0, 8.0));

    REQUIRE(drawn.runs.size() == 1);
    // One point a column rather than two: the smallest and the largest sample
    // in a column of constants are the same sample.
    CHECK(drawn.points.size() <= 1001);
    CHECK(drawn.points.size() >= 999);
    const double middle = 250.0;
    for (const QPointF& point : drawn.points) {
        CHECK(point.y() == Approx(middle).margin(0.01));
    }
}

TEST_CASE("the envelope keeps a downward spike as well as an upward one", "[plot]")
{
    // Asserting the maximum alone would pass with an implementation that only
    // tracked one extreme, and the half of the data below the line is the half
    // a reader is usually looking for.
    std::vector<double> values(200000, 0.0);
    values[70000] = -8.0;
    values[140000] = 9.0;

    const gui::PlotView view = paneOver(0.0, 200000.0, -10.0, 10.0);
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    double top = 500.0;
    double bottom = 0.0;
    for (const QPointF& point : drawn.points) {
        top = std::min(top, point.y());
        bottom = std::max(bottom, point.y());
    }
    // -8 and 9 on a pane 500 tall showing -10 to 10.
    CHECK(top == Approx(500.0 - (9.0 + 10.0) / 20.0 * 500.0).margin(0.01));
    CHECK(bottom == Approx(500.0 - (-8.0 + 10.0) / 20.0 * 500.0).margin(0.01));
}

TEST_CASE("the envelope draws a column in the direction the data went", "[plot]")
{
    // Emitting the smallest before the largest regardless would make every
    // rising line a zig-zag at the pixel level, because half its columns would
    // be drawn backwards.
    const gui::PlotView view = paneOver(0.0, 4000.0, -2.0, 2.0);

    std::vector<double> rising(4000);
    std::vector<double> falling(4000);
    for (std::size_t i = 0; i < rising.size(); ++i) {
        rising[i] = -1.0 + 2.0 * static_cast<double>(i) / 4000.0;
        falling[i] = 1.0 - 2.0 * static_cast<double>(i) / 4000.0;
    }

    const Projected up = project(lineOver(rising), gui::PlotAxis{}, view);
    const Projected down = project(lineOver(falling), gui::PlotAxis{}, view);

    // Within a column the pixel y must move the way the data does: downward on
    // screen for a rising line, upward for a falling one.
    REQUIRE(up.points.size() >= 4);
    for (std::size_t i = 0; i + 1 < up.points.size(); i += 2) {
        CHECK(up.points[i].y() >= up.points[i + 1].y());
    }
    for (std::size_t i = 0; i + 1 < down.points.size(); i += 2) {
        CHECK(down.points[i].y() <= down.points[i + 1].y());
    }
}

TEST_CASE("a gap that swallows a whole column is still a gap", "[plot]")
{
    // The envelope's own version of the gap rule: a column with no drawable
    // sample in it ends the stroke, rather than being skipped so that the
    // columns either side join up.
    std::vector<double> values(100000, 1.0);
    for (std::size_t i = 40000; i < 60000; ++i) {
        values[i] = kNaN;
    }

    const Projected drawn =
        project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 100000.0, 0.0, 2.0));
    REQUIRE(drawn.runs.size() == 2);

    const gui::PlotRun& after = drawn.runs[1];
    CHECK(drawn.points[static_cast<std::size_t>(after.first)].x() == Approx(600.0).margin(2.0));
}

TEST_CASE("the column budget bounds the drawing when the lines do not", "[plot]")
{
    // The envelope bounds each line by the pane. It does not bound their sum,
    // and ten thousand lines at two points a column is forty million points
    // however narrow the pane. PlotItem divides a budget between them and
    // passes the share down here.
    std::vector<double> values(50000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(static_cast<double>(i) / 50.0);
    }

    gui::PlotView tight = paneOver(0.0, 50000.0, -1.0, 1.0);
    tight.maxColumns = 50;
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, tight);
    CHECK(drawn.points.size() <= 100);

    // And the extremes survive the coarser bucketing, which is the whole
    // reason the budget is spent on columns rather than on a stride.
    double top = 500.0;
    for (const QPointF& point : drawn.points) {
        top = std::min(top, point.y());
    }
    CHECK(top == Approx(0.0).margin(2.0));
}

TEST_CASE("a stretched line covers the axis whatever its length", "[plot]")
{
    // CustomPlot's "stretch": a line of any length spread over the whole axis,
    // its first sample at the start and its last at the end. The same affine
    // map as a stride, with a fractional step.
    const std::vector<double> values{0.0, 1.0, 2.0, 3.0, 4.0};
    gui::PlotLine line = lineOver(values);
    // Five samples spread over an axis a hundred positions long.
    line.positionStep = 99.0 / 4.0;

    CHECK(gui::xOf(line, gui::PlotAxis{}, 0) == Approx(0.0));
    CHECK(gui::xOf(line, gui::PlotAxis{}, 4) == Approx(99.0));

    const Projected drawn = project(line, gui::PlotAxis{}, paneOver(0.0, 99.0, 0.0, 4.0));
    REQUIRE(drawn.runs.size() == 1);
    CHECK(drawn.points.front().x() == Approx(0.0).margin(0.01));
    CHECK(drawn.points.back().x() == Approx(1000.0).margin(0.01));
}

TEST_CASE("a line can start somewhere other than the beginning of the axis", "[plot]")
{
    const std::vector<double> values{1.0, 2.0, 3.0};
    gui::PlotLine line = lineOver(values);
    line.positionStart = 10.0;
    line.positionStep = 2.0;

    gui::PlotAxis axis;
    axis.start = 100.0;
    axis.step = 0.5;

    CHECK(gui::xOf(line, axis, 0) == Approx(105.0));
    CHECK(gui::xOf(line, axis, 2) == Approx(107.0));
}

TEST_CASE("a time base with a hole in it breaks the line there", "[plot]")
{
    // A time base is read from a file like anything else, so it has the same
    // right to be missing a value -- and a sample with no x is as undrawable as
    // a sample with no y.
    const std::vector<double> times{0.0, 1.0, kNaN, 3.0, 4.0};
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};

    gui::PlotLine line = lineOver(values);
    gui::PlotAxis axis;
    axis.values = times.data();
    axis.count = static_cast<qsizetype>(times.size());

    const Projected drawn = project(line, axis, paneOver(0.0, 4.0, 0.0, 6.0));
    CHECK(drawn.runs.size() == 2);
    CHECK(drawn.points.size() == 4);
    CHECK(gui::samplesOf(line, axis).size() == 4);
}

TEST_CASE("a negative position falls off the front of a time base", "[plot]")
{
    const std::vector<double> times{10.0, 20.0, 30.0};
    const std::vector<double> values{1.0, 2.0, 3.0};

    gui::PlotLine line = lineOver(values);
    line.positionStart = -2.0;
    gui::PlotAxis axis;
    axis.values = times.data();
    axis.count = static_cast<qsizetype>(times.size());

    CHECK(std::isnan(gui::xOf(line, axis, 0)));
    CHECK(std::isnan(gui::xOf(line, axis, 1)));
    CHECK(gui::xOf(line, axis, 2) == Approx(10.0));
}

TEST_CASE("an empty time base is not a time base", "[plot]")
{
    // A custom tab in dataset mode before its x has been read. The axis falls
    // back to positions, which is what the tab shows while it waits.
    const std::vector<double> values{1.0, 2.0, 3.0};
    gui::PlotAxis axis;
    axis.values = nullptr;
    axis.count = 5;
    CHECK_FALSE(axis.explicitX());
    CHECK(gui::xOf(lineOver(values), axis, 1) == Approx(1.0));
}

// --- the stroke, at its edges ---------------------------------------------

TEST_CASE("a stroke of fewer than two stations is not a stroke", "[plot]")
{
    const std::vector<QPointF> one{{0.0, 0.0}};
    std::vector<QPointF> stroke;

    gui::strokeRun(one.data(), 1, 2.0, stroke);
    CHECK(stroke.empty());
    gui::strokeRun(one.data(), 0, 2.0, stroke);
    CHECK(stroke.empty());
    gui::strokeRun(nullptr, 5, 2.0, stroke);
    CHECK(stroke.empty());
}

TEST_CASE("a stroke puts its two sides on opposite sides of the line", "[plot]")
{
    // The property every triangle in the strip depends on. Emitting both
    // vertices on the same side draws a band of no width, which is a plot that
    // renders as nothing at all and looks from the counts as though it worked.
    const std::vector<QPointF> down{{10.0, 0.0}, {10.0, 100.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(down.data(), 2, 3.0, stroke);

    REQUIRE(stroke.size() == 4);
    CHECK(stroke[0].x() == Approx(10.0 - 1.5));
    CHECK(stroke[1].x() == Approx(10.0 + 1.5));
    CHECK(stroke[0].y() == Approx(0.0));
    CHECK(stroke[1].y() == Approx(0.0));
}

TEST_CASE("a repeated station does not undefine the stroke", "[plot]")
{
    // An envelope emits one point for a column that held a single sample and
    // two for a column that held a spread, so consecutive identical points
    // happen. A segment of no length has no direction to take a normal from.
    const std::vector<QPointF> stalled{{0.0, 50.0}, {10.0, 50.0}, {10.0, 50.0}, {20.0, 50.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(stalled.data(), 4, 2.0, stroke);

    REQUIRE(stroke.size() == 8);
    for (const QPointF& vertex : stroke) {
        CHECK(std::isfinite(vertex.x()));
        CHECK(std::isfinite(vertex.y()));
    }
    // The width survives the stalled segment rather than collapsing at it.
    CHECK(std::abs(stroke[4].y() - stroke[5].y()) == Approx(2.0));
}

TEST_CASE("every station of a stroke contributes exactly two vertices", "[plot]")
{
    // What PlotItem's vertex arithmetic assumes when it sizes the buffer, and
    // the sort of assumption that goes wrong silently: an under-sized buffer
    // is a write past the end of a mapped range.
    for (int stations = 2; stations < 40; ++stations) {
        std::vector<QPointF> along;
        along.reserve(static_cast<std::size_t>(stations));
        for (int i = 0; i < stations; ++i) {
            along.emplace_back(static_cast<double>(i) * 3.0,
                               50.0 + 20.0 * std::sin(static_cast<double>(i)));
        }
        std::vector<QPointF> stroke;
        gui::strokeRun(along.data(), stations, 1.5, stroke);
        CHECK(stroke.size() == static_cast<std::size_t>(stations) * 2);
    }
}

TEST_CASE("a stroke appends rather than replacing", "[plot]")
{
    // PlotItem strokes run after run into one buffer. A strokeRun that cleared
    // its output would draw only the last stroke of the last line, which on a
    // single-line plot is indistinguishable from working.
    const std::vector<QPointF> along{{0.0, 0.0}, {10.0, 0.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(along.data(), 2, 1.0, stroke);
    gui::strokeRun(along.data(), 2, 1.0, stroke);
    CHECK(stroke.size() == 8);
}

TEST_CASE("a stroke of no width still has area", "[plot]")
{
    const std::vector<QPointF> along{{0.0, 0.0}, {10.0, 0.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(along.data(), 2, 0.0, stroke);

    REQUIRE(stroke.size() == 4);
    CHECK(std::abs(stroke[0].y() - stroke[1].y()) > 0.0);
}

// --- markers ---------------------------------------------------------------

TEST_CASE("a marker is exactly as many vertices as PlotItem sized room for", "[plot]")
{
    // PlotItem works the vertex count out arithmetically rather than by
    // building into a scratch buffer and measuring it, which would be a second
    // copy of the vertex data at every size. That is only safe while the two
    // things that emit vertices emit a fixed number each, so both are pinned
    // here: this one and "every station of a stroke contributes exactly two".
    std::vector<QPointF> out;
    gui::markerAt(QPointF(10.0, 20.0), 2.0, out);
    CHECK(out.size() == static_cast<std::size_t>(gui::kMarkerSides));

    gui::markerAt(QPointF(0.0, 0.0), 2.0, out);
    CHECK(out.size() == static_cast<std::size_t>(gui::kMarkerSides) * 2);
}

TEST_CASE("a marker is round and centred on its sample", "[plot]")
{
    // What it replaces was a QML Rectangle with a radius of half its width, so
    // it has to read as a dot and not as a lozenge.
    std::vector<QPointF> out;
    gui::markerAt(QPointF(100.0, 50.0), 3.0, out);

    REQUIRE(out.size() == static_cast<std::size_t>(gui::kMarkerSides));
    double sumX = 0.0;
    double sumY = 0.0;
    for (const QPointF& vertex : out) {
        CHECK(std::hypot(vertex.x() - 100.0, vertex.y() - 50.0) == Approx(3.0));
        sumX += vertex.x();
        sumY += vertex.y();
    }
    CHECK(sumX / static_cast<double>(out.size()) == Approx(100.0));
    CHECK(sumY / static_cast<double>(out.size()) == Approx(50.0));
}

TEST_CASE("the projection says whether a point is a sample or a summary", "[plot]")
{
    // Which is what decides whether a line carries markers. A dot on an
    // envelope point marks two samples out of a column of a thousand, which
    // says nothing and is not what the setting means.
    std::vector<double> values(100000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(static_cast<double>(i) / 100.0);
    }

    std::vector<QPointF> points;
    std::vector<gui::PlotRun> runs;

    const gui::PlotProjected whole = gui::projectLine(
        lineOver(values), gui::PlotAxis{}, paneOver(0.0, 100000.0, -1.0, 1.0), points, runs);
    CHECK(whole.decimated);
    CHECK(whole.runs == 1);

    points.clear();
    runs.clear();
    // Zoomed to twenty samples, which is fewer than the pane has columns.
    const gui::PlotProjected close = gui::projectLine(
        lineOver(values), gui::PlotAxis{}, paneOver(500.0, 520.0, -1.0, 1.0), points, runs);
    CHECK_FALSE(close.decimated);
    CHECK(close.runs == 1);
}

TEST_CASE("a line drawn against a time base is never a summary", "[plot]")
{
    // It is drawn sample for sample by construction, so every point on it is a
    // sample and every one of them can carry a marker.
    const std::vector<double> times{0.0, 1.0, 2.0, 3.0};
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0};

    gui::PlotAxis axis;
    axis.values = times.data();
    axis.count = static_cast<qsizetype>(times.size());

    std::vector<QPointF> points;
    std::vector<gui::PlotRun> runs;
    const gui::PlotProjected drawn =
        gui::projectLine(lineOver(values), axis, paneOver(0.0, 3.0, 0.0, 5.0), points, runs);
    CHECK_FALSE(drawn.decimated);
}
