// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// What the plot decides before a pixel is touched.
//
// Every case here is a regression the plot library evaluation turned up, and
// three of them are things the plot got wrong for as long as it drew through Qt
// Graphs: a one-sample spike thinned away by a stride, a run of missing data
// drawn as a straight line across the absence, and an epoch timestamp collapsed
// into a staircase by a float32 vertex.
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
#include <random>
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

// --- logarithmic axes -----------------------------------------------------

TEST_CASE("a logarithmic axis places a value by its logarithm", "[plot][log]")
{
    // The whole of what the scale is: the same four bounds, in the data's own
    // units, and a different answer to where a value sits between them. A
    // decade is a decade wherever it falls, which is what a linear axis over
    // the same range cannot say -- there, everything below a hundredth of the
    // top is the bottom pixel row.
    gui::PlotView view = paneOver(1.0, 10000.0, 1.0, 10000.0);
    view.xLog = true;
    view.yLog = true;

    // Four decades across the pane, so each one is a quarter of it.
    CHECK(gui::xFractionOf(1.0, view) == Approx(0.0));
    CHECK(gui::xFractionOf(10.0, view) == Approx(0.25));
    CHECK(gui::xFractionOf(100.0, view) == Approx(0.5));
    CHECK(gui::xFractionOf(10000.0, view) == Approx(1.0));
    CHECK(gui::yFractionOf(1000.0, view) == Approx(0.75));

    // ...and back, exactly. This is the pair a pointer position resolves
    // through, so a crosshair that reads a different x from the one under it
    // would be this failing.
    const gui::AxisMapping mapping = gui::xMappingOf(view);
    CHECK(mapping.valueAt(0.5) == Approx(100.0));
    CHECK(mapping.valueAt(mapping.fractionOf(37.0)) == Approx(37.0));

    // The linear reading of the same window, for contrast: a thousand is
    // nine-tenths of the way *down* it rather than three quarters of the way up.
    const gui::PlotView linear = paneOver(1.0, 10000.0, 1.0, 10000.0);
    CHECK(gui::yFractionOf(1000.0, linear) == Approx(0.0999).margin(0.001));
}

TEST_CASE("a line that says where its points are is drawn where it says", "[plot][log]")
{
    // What a line folded onto a logarithmic axis's columns hands over: one or
    // two points per pixel column, evenly spaced on the pane and nowhere near
    // evenly spaced in position. No start and step can say where those are, so
    // the line carries its x -- and the renderer must take them as they come.
    //
    // Many of them, and clustered, which is the case that matters: the
    // renderer's own envelope buckets by *position*, so if it summarised these
    // again it would put the left of a logarithmic pane back to one bucket.
    std::vector<double> values;
    std::vector<double> xs;
    for (int i = 0; i < 4000; ++i) {
        xs.push_back(std::pow(10.0, 4.0 * i / 3999.0));
        values.push_back(i % 2 == 0 ? 1.0 : 2.0);
    }
    gui::PlotLine line = lineOver(values);
    line.xs = xs.data();
    // A start and a step that would put every point somewhere else entirely.
    line.positionStart = 500.0;
    line.positionStep = 3.0;

    gui::PlotView view = paneOver(1.0, 10000.0, 0.5, 2.5);
    view.xLog = true;
    const Projected drawn = project(line, gui::PlotAxis{}, view);

    // Every point, point for point, where its own x puts it.
    REQUIRE(drawn.runs.size() == 1);
    REQUIRE(drawn.points.size() == xs.size());
    for (std::size_t i = 0; i < xs.size(); i += 97) {
        INFO("point " << i);
        CHECK(drawn.points[i].x() == Approx(gui::xFractionOf(xs[i], view) * view.width));
    }
    CHECK(gui::xOf(line, gui::PlotAxis{}, 1234) == xs[1234]);

    SECTION("and a stated x the axis cannot place is a gap like any other")
    {
        xs[2000] = 0.0;
        xs[2001] = std::numeric_limits<double>::quiet_NaN();
        const Projected broken = project(line, gui::PlotAxis{}, view);
        CHECK(broken.runs.size() == 2);
    }

    SECTION("a time base is not consulted when the line says where it is")
    {
        const std::vector<double> times(4000, 42.0);
        gui::PlotAxis axis;
        axis.values = times.data();
        axis.count = static_cast<qsizetype>(times.size());
        const Projected against = project(line, axis, view);
        REQUIRE(against.points.size() == drawn.points.size());
        CHECK(against.points.back().x() == Approx(drawn.points.back().x()));
    }
}

TEST_CASE("a value at or below zero is a gap on a logarithmic axis", "[plot][log]")
{
    // The one thing a logarithmic axis really does add, and it is the rule
    // this file already had: there is no place on the pane that would be a
    // true reading of a value the axis cannot place, so the stroke ends there
    // and the next one starts after it. Exactly what a NaN does -- and the
    // reason it must not be a clamp to some small positive number, which would
    // draw a line down to a floor the data never reached.
    std::vector<double> values(9, 1.0);
    values[3] = 0.0;
    values[4] = -1.0;
    values[5] = -2.0;

    gui::PlotView view = paneOver(0.0, 9.0, 0.1, 10.0);
    view.yLog = true;
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    // Three before the gap, three after it.
    REQUIRE(drawn.runs.size() == 2);
    CHECK(drawn.runs[0].count == 3);
    CHECK(drawn.runs[1].count == 3);

    // The same line on a linear axis is one unbroken stroke, which is what
    // says the break above is the scale's doing and not the data's.
    const Projected linear =
        project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 9.0, -3.0, 3.0));
    CHECK(linear.runs.size() == 1);
}

TEST_CASE("an x at or below zero is a gap too", "[plot][log]")
{
    // The same rule down the other axis, and it is worth its own case because
    // the x path is the one with the arithmetic in it: x is affine in the
    // sample index, so the window narrowing and the envelope both work in
    // indices and neither of them has been told about a scale.
    std::vector<double> values(10, 1.0);

    gui::PlotAxis axis;
    axis.start = -4.0; // samples at -4, -3, ... 5
    axis.step = 1.0;

    gui::PlotView view = paneOver(0.5, 6.0, 0.5, 2.0);
    view.xLog = true;
    const Projected drawn = project(lineOver(values), axis, view);

    // Only the five samples at 1 through 5 have a place on this axis.
    REQUIRE(drawn.runs.size() == 1);
    CHECK(drawn.runs[0].count == 5);
    for (const QPointF& point : drawn.points) {
        CHECK(point.x() >= 0.0);
    }
}

TEST_CASE("a logarithmic axis with a bound at or below zero draws nothing", "[plot][log]")
{
    // There is no nearest honest answer to fall back to. Clamping the bound to
    // some tiny positive number would put the whole of the data in the top few
    // pixels of a pane whose lower half means nothing at all, and every value
    // on it would be drawn somewhere it is not. Refused instead, exactly as a
    // span of zero is -- and keeping the bound positive is the caller's job,
    // which is why PlotSurface takes it off the smallest positive value there
    // is to show.
    const std::vector<double> values{1.0, 2.0, 3.0};

    SECTION("the low end is zero")
    {
        gui::PlotView view = paneOver(0.0, 10.0, 0.0, 10.0);
        view.yLog = true;
        CHECK(project(lineOver(values), gui::PlotAxis{}, view).points.empty());
    }
    SECTION("the low end is negative")
    {
        gui::PlotView view = paneOver(0.0, 10.0, -5.0, 10.0);
        view.yLog = true;
        CHECK(project(lineOver(values), gui::PlotAxis{}, view).points.empty());
        CHECK(!gui::yMappingOf(view).usable);
    }
    SECTION("both ends are above zero and it draws")
    {
        gui::PlotView view = paneOver(0.0, 10.0, 0.5, 10.0);
        view.yLog = true;
        CHECK(!project(lineOver(values), gui::PlotAxis{}, view).points.empty());
    }
}

TEST_CASE("the envelope keeps the extremes the axis can draw", "[plot][log]")
{
    // A bucket's extremes are its extremes *among the values that have a
    // place*. A bucket holding one enormous negative reading and a thousand
    // ordinary ones has not got a top at that reading -- the curve never
    // reaches it, because the curve is not drawn there -- and an envelope that
    // counted it would put the band's edge somewhere the line never goes.
    //
    // Eight thousand samples over a thousand columns, so the envelope runs.
    std::vector<double> values(8000, 100.0);
    values[10] = -1e6; // the largest magnitude in the line, and undrawable
    values[11] = 1000.0;

    gui::PlotView view = paneOver(0.0, 8000.0, 1.0, 10000.0);
    view.yLog = true;
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    // Nothing is drawn below the pane: the negative was skipped rather than
    // projected to some enormous y.
    for (const QPointF& point : drawn.points) {
        CHECK(point.y() >= 0.0);
        CHECK(point.y() <= view.height);
    }
    // ...and the 1000 beside it is still the top of its bucket. Three quarters
    // of the way up a pane showing four decades.
    const double expected = view.height - 0.75 * view.height;
    CHECK(highestPoint(drawn) == Approx(expected).margin(0.5));
}

TEST_CASE("a bound that runs backwards is not a window", "[plot][log]")
{
    // Both axes are held to this and only x used to be. Nothing in the
    // application produces an inverted window -- every path that states one
    // sorts its bounds first -- so the difference was never a picture anybody
    // saw; it is here because one rule that both axes keep is the only kind
    // this file can hold the renderer and the chrome to.
    const std::vector<double> values{1.0, 2.0, 3.0};
    CHECK(project(lineOver(values), gui::PlotAxis{}, paneOver(0.0, 3.0, 3.0, 1.0)).points.empty());
    CHECK(!gui::yMappingOf(paneOver(0.0, 3.0, 3.0, 1.0)).usable);
}

TEST_CASE("an infinity is not a place on a logarithmic axis either", "[plot][log]")
{
    // Positive infinity passes `value > 0` and fails `std::isfinite`, so the
    // order of the two tests in AxisMapping::draws decides this one. It has to
    // be a gap: a logarithm of infinity is an infinity, and an infinity in a
    // vertex buffer is not a point off screen -- it is a triangle the
    // rasteriser may do anything at all with. See kFarAway, which exists for
    // the same hazard reached by a different road.
    const double big = std::numeric_limits<double>::infinity();
    std::vector<double> values(9, 1.0);
    values[4] = big;

    gui::PlotView view = paneOver(0.0, 9.0, 0.1, 10.0);
    view.yLog = true;
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    REQUIRE(drawn.runs.size() == 2);
    for (const QPointF& point : drawn.points) {
        CHECK(std::isfinite(point.x()));
        CHECK(std::isfinite(point.y()));
    }
}

TEST_CASE("a logarithmic axis holds up over the whole of double", "[plot][log]")
{
    // Three hundred decades apiece, which is the widest window there can be.
    // What is being asked is not that it looks like anything -- it cannot --
    // but that every coordinate it produces is a number, because the one thing
    // a renderer must never be handed is a vertex it cannot rasterise.
    std::vector<double> values(64);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::pow(10.0, -300.0 + static_cast<double>(i) * 10.0);
    }

    gui::PlotView view = paneOver(1e-300, 1e300, 1e-300, 1e300);
    view.xLog = true;
    view.yLog = true;
    const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);

    REQUIRE(!drawn.points.empty());
    for (const QPointF& point : drawn.points) {
        CHECK(std::isfinite(point.x()));
        CHECK(std::isfinite(point.y()));
    }

    // ...and the arithmetic is still the arithmetic out there: the middle of a
    // six-hundred-decade window is the three-hundredth decade up it.
    const gui::AxisMapping mapping = gui::yMappingOf(view);
    CHECK(mapping.fractionOf(1.0) == Approx(0.5));
    CHECK(mapping.valueAt(0.5) == Approx(1.0));
    // A denormal is still a number with a logarithm.
    CHECK(mapping.draws(5e-324));
    CHECK(mapping.fractionOf(5e-324) < 0.0);
}

TEST_CASE("the inverse of a logarithmic axis is its own inverse", "[plot][log]")
{
    // The pair a pointer position resolves through, and the one place a
    // rounding would show as the crosshair reading a different value from the
    // one the tick under it prints. Over four decades and over sixty.
    for (const double top : {1e4, 1e60}) {
        gui::PlotView view = paneOver(1.0, top, 1.0, top);
        view.yLog = true;
        const gui::AxisMapping mapping = gui::yMappingOf(view);
        REQUIRE(mapping.usable);
        for (int i = 0; i <= 10; ++i) {
            const double fraction = static_cast<double>(i) / 10.0;
            CHECK(mapping.fractionOf(mapping.valueAt(fraction)) == Approx(fraction).margin(1e-12));
        }
    }
}

TEST_CASE("a summary that reaches zero is drawn as far as it can be", "[plot][log]")
{
    // The one place the scale and the decimation meet, and it is worth pinning
    // because the answer is a compromise rather than a rule.
    //
    // A model hands over an *envelope*: two values a bucket, its smallest and
    // its largest. That envelope was folded out of the file without knowing
    // which scale it would be drawn on -- it is the same summary either way,
    // and recomputing it would mean re-reading the file every time the box was
    // ticked. So a bucket that dips to zero arrives here with a smallest value
    // the axis cannot place.
    //
    // What is drawn is the half of that bucket that can be: the largest value
    // is a point, and the smallest is a gap. The stroke therefore breaks for
    // one station wherever the data reached zero, which is the honest reading
    // -- that bucket is one the scale cannot show whole -- and it is not the
    // line disappearing for a bucket, which is what dropping the pair would
    // have been.
    std::vector<double> summary{1.0, 4.0, 0.0, 5.0, 2.0, 6.0};
    gui::PlotLine line = lineOver(summary);
    line.summarised = true;

    gui::PlotView view = paneOver(0.0, 6.0, 0.5, 10.0);
    view.yLog = true;
    const Projected drawn = project(line, gui::PlotAxis{}, view);

    // Two strokes: 1, 4 and then 5, 2, 6, with the zero between them.
    REQUIRE(drawn.runs.size() == 2);
    CHECK(drawn.runs[0].count == 2);
    CHECK(drawn.runs[1].count == 3);

    // The bucket's own largest value is still drawn, which is the half of it
    // that matters -- the top of the envelope is what a reader is looking at.
    const double top = gui::yMappingOf(view).fractionOf(6.0) * view.height;
    CHECK(highestPoint(drawn) == Approx(view.height - top).margin(0.5));
}

TEST_CASE("a logarithmic axis is taken to the base it was given", "[plot][log]")
{
    // Ten by default, which is what every axis here was before the choice
    // existed -- so a view that says nothing about a base is the view it
    // always was.
    CHECK(gui::PlotView{}.xLogBase == 10.0);
    CHECK(gui::PlotView{}.yLogBase == 10.0);

    SECTION("base two puts an octave where base ten puts a decade")
    {
        gui::PlotView view = paneOver(1.0, 1024.0, 1.0, 1024.0);
        view.yLog = true;
        view.yLogBase = 2.0;
        const gui::AxisMapping mapping = gui::yMappingOf(view);
        REQUIRE(mapping.usable);
        // Ten octaves across the pane, so each one is a tenth of it.
        for (int octave = 0; octave <= 10; ++octave) {
            CHECK(mapping.fractionOf(std::pow(2.0, octave)) ==
                  Approx(static_cast<double>(octave) / 10.0));
        }
        CHECK(mapping.valueAt(0.5) == Approx(32.0));
    }

    SECTION("the base moves the numbers on the axis, not the curve on it")
    {
        // Worth stating outright, because it is the opposite of what the
        // amount of code below it suggests: where a value sits is a *ratio* of
        // two logarithms, and a change of base multiplies both by the same
        // constant. So the base cancels, and every drawn point of a plot is in
        // exactly the same place whichever base the reader picks.
        //
        // What they pick one for is the axis around the picture: the numbers
        // go at the powers of the base and the rules between them, so base ten
        // marks decades and base two marks octaves over an identical curve.
        //
        // Which is also the answer to why the renderer is told the base at
        // all. Not to place anything -- it is told so that a base of one,
        // where the logarithm is a division by zero, is refused here as well
        // as in the panel that offers the choice.
        std::vector<double> values(64);
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = std::pow(1.3, static_cast<double>(i)) + 0.5;
        }

        gui::PlotView ten = paneOver(0.0, 64.0, 0.4, 1e7);
        ten.yLog = true;
        ten.yLogBase = 10.0;
        gui::PlotView two = ten;
        two.yLogBase = 2.0;
        gui::PlotView odd = ten;
        odd.yLogBase = 1.5;

        const Projected drawnTen = project(lineOver(values), gui::PlotAxis{}, ten);
        REQUIRE(!drawnTen.points.empty());
        for (const gui::PlotView& other : {two, odd}) {
            const Projected drawn = project(lineOver(values), gui::PlotAxis{}, other);
            INFO("base " << other.yLogBase);
            REQUIRE(drawn.points.size() == drawnTen.points.size());
            for (std::size_t i = 0; i < drawn.points.size(); ++i) {
                CHECK(drawn.points[i].x() == Approx(drawnTen.points[i].x()));
                CHECK(drawn.points[i].y() == Approx(drawnTen.points[i].y()));
            }
        }
    }

    SECTION("and a base of its own is its own inverse too")
    {
        for (const double base : {2.0, std::exp(1.0), 4.0, 1.5, 60.0}) {
            gui::PlotView view = paneOver(1.0, 1e6, 1.0, 1e6);
            view.yLog = true;
            view.yLogBase = base;
            const gui::AxisMapping mapping = gui::yMappingOf(view);
            INFO("base " << base);
            REQUIRE(mapping.usable);
            for (int i = 0; i <= 10; ++i) {
                const double fraction = static_cast<double>(i) / 10.0;
                CHECK(mapping.fractionOf(mapping.valueAt(fraction)) ==
                      Approx(fraction).margin(1e-12));
            }
        }
    }
}

TEST_CASE("only a number above one is a base", "[plot][log]")
{
    // At exactly one the logarithm is a division by zero and every value on
    // the axis lands in the same place; below it the axis runs backwards,
    // which is a different request from the one this answers. Refused rather
    // than corrected, because the legal bases are open at one and there is no
    // nearest legal value to correct a bad one to.
    //
    // Refused *here* as well as in the panel that offers the choice, which is
    // the point of this case: the renderer must never be handed a vertex it
    // cannot rasterise, whatever wrote the property.
    const std::vector<double> values{1.0, 2.0, 3.0};

    for (const double base : {1.0, 0.0, -2.0, 0.5, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
        gui::PlotView view = paneOver(1.0, 100.0, 1.0, 100.0);
        view.yLog = true;
        view.yLogBase = base;
        INFO("base " << base);
        CHECK(!gui::yMappingOf(view).usable);
        CHECK(project(lineOver(values), gui::PlotAxis{}, view).points.empty());
        // A refused axis answers zero rather than a number nothing can use.
        CHECK(gui::yFractionOf(10.0, view) == 0.0);
    }

    SECTION("a base just above one is legal, however little use it is")
    {
        gui::PlotView view = paneOver(1.0, 100.0, 1.0, 100.0);
        view.yLog = true;
        view.yLogBase = 1.0000001;
        const gui::AxisMapping mapping = gui::yMappingOf(view);
        REQUIRE(mapping.usable);
        CHECK(std::isfinite(mapping.fractionOf(10.0)));
        CHECK(mapping.fractionOf(10.0) == Approx(0.5));
    }
}

TEST_CASE("a logarithm to a familiar base is exact", "[plot][log]")
{
    // std::log(1000.0) / std::log(10.0) is 2.9999999999999996, so a mark that
    // ought to *be* a power of the base comes out a shade beside one -- and
    // the whole structure of a logarithmic grid is that its majors land
    // exactly on its labels. The two bases a reader is most likely to pick
    // have exact library functions, and logOf takes them.
    CHECK(gui::logOf(1000.0, 10.0) == 3.0);
    CHECK(gui::logOf(0.001, 10.0) == -3.0);
    CHECK(gui::logOf(1024.0, 2.0) == 10.0);
    CHECK(gui::logOf(1.0 / 1024.0, 2.0) == -10.0);
    // ...and the general case still answers, to within a rounding.
    CHECK(gui::logOf(81.0, 3.0) == Approx(4.0));
    CHECK(gui::logOf(std::exp(2.0), std::exp(1.0)) == Approx(2.0));
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

TEST_CASE("a pane of device pixels is summarised at the resolution it has", "[plot]")
{
    // A pane is laid out in logical pixels and drawn into a framebuffer with
    // devicePixelRatio of them for each one. Summarising to the logical count
    // -- which is what this did -- hands a HiDPI display one bucket per two or
    // three physical columns, so a band is drawn at half or a third of the
    // resolution the screen can show, and every line in the application looks
    // slightly coarser than it needed to on the machines most readers have.
    std::vector<double> values(100000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(static_cast<double>(i) / 97.0);
    }
    const gui::PlotLine line = lineOver(values);
    const gui::PlotAxis axis;

    gui::PlotView logical = paneOver(0.0, 100000.0, -1.0, 1.0);
    const Projected coarse = project(line, axis, logical);

    gui::PlotView physical = logical;
    physical.pixelRatio = 2.0;
    const Projected fine = project(line, axis, physical);

    // Both are envelopes -- there are a hundred samples per logical column --
    // and the finer one resolves twice as much of the line, because the bucket
    // halves and the buckets are powers of two.
    // Twice, give or take the one bucket at each end that the halving rounds
    // differently -- the buckets are aligned to the data's own index space, so
    // the count is a ceiling rather than a ratio.
    REQUIRE(coarse.points.size() > 2);
    CHECK(fine.points.size() <= 2 * coarse.points.size());
    CHECK(fine.points.size() > 2 * coarse.points.size() - 8);

    // ...and it is the same line, drawn over the same pane: the extremes do not
    // move, only how many places along it are given one.
    CHECK(highestPoint(fine) == Approx(highestPoint(coarse)).margin(1.0));

    // A ratio below one is a window system saying something this cannot use.
    // It is floored rather than believed, because half a column is not a
    // resolution to summarise to.
    gui::PlotView broken = logical;
    broken.pixelRatio = 0.25;
    CHECK(project(line, axis, broken).points.size() == coarse.points.size());
}

TEST_CASE("a transposed pane draws x up it and y across it", "[plot][flip]")
{
    // The same picture with the axes swapped, and not a rotation of it: the
    // origin stays in the bottom-left corner, x grows up the pane and y grows
    // to the right. A line at a quarter of its y range, drawn upright as a
    // level stroke a quarter of the way up, is a vertical stroke a quarter of
    // the way across.
    std::vector<double> values(11, 0.25);
    gui::PlotView pane = paneOver(0.0, 10.0, 0.0, 1.0); // 1000 across, 500 up
    pane.transposed = true;

    const gui::PlotView upright = gui::uprightView(pane);
    CHECK(upright.width == 500.0);
    CHECK(upright.height == 1000.0);
    CHECK_FALSE(upright.transposed);

    Projected drawn = project(lineOver(values), gui::PlotAxis{}, upright);
    for (QPointF& point : drawn.points) {
        point = gui::transposedPoint(point, pane);
    }
    REQUIRE(drawn.points.size() == values.size());
    for (std::size_t i = 0; i < drawn.points.size(); ++i) {
        CHECK(drawn.points[i].x() == Approx(250.0));
        // Sample i is at x = i of 0..10, a tenth of the way up per sample,
        // from the bottom of a pane 500 high.
        CHECK(drawn.points[i].y() == Approx(500.0 - static_cast<double>(i) * 50.0));
    }

    // And the pointer is taken back the same way, which is what the
    // crosshair's snapping is searched in.
    for (const QPointF& point : {QPointF(0, 0), QPointF(123.5, 456.25), QPointF(1000, 500)}) {
        const QPointF back = gui::transposedPoint(gui::uprightPoint(point, pane), pane);
        CHECK(back.x() == Approx(point.x()));
        CHECK(back.y() == Approx(point.y()));
    }

    // Upright views are untouched, so nothing that never asked for a flip
    // pays for one.
    const gui::PlotView plain = paneOver(0.0, 10.0, 0.0, 1.0);
    CHECK(gui::uprightView(plain).width == plain.width);
}

TEST_CASE("a transposed pane is summarised along its height", "[plot][flip]")
{
    // The envelope folds a line into one bucket per column *along x*, and on a
    // transposed pane x runs up it. Summarising to the width would give a pane
    // 1000 across and 500 up twice the points it can show.
    std::vector<double> values(1000000);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(static_cast<double>(i) / 97.0);
    }
    gui::PlotView pane = paneOver(0.0, 1000000.0, -1.0, 1.0);
    const Projected across = project(lineOver(values), gui::PlotAxis{}, pane);
    pane.transposed = true;
    const Projected up = project(lineOver(values), gui::PlotAxis{}, gui::uprightView(pane));

    REQUIRE(across.points.size() > 1000);
    CHECK(up.points.size() <= 1002);
    CHECK(up.points.size() > 900);
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
    // One point a bucket rather than two: the smallest and the largest sample
    // in a bucket of constants are the same sample, and there is nothing to
    // draw between them.
    //
    // And between half a bucket and one bucket per column, because a bucket is
    // a power of two and the rounding is upward. That is the price of aligning
    // the buckets to the data rather than to the window -- see projectLine --
    // and it is paid in horizontal resolution on a summary, which is the
    // cheapest thing a plot has to spend.
    CHECK(drawn.points.size() <= 1001);
    CHECK(drawn.points.size() >= 500);
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

TEST_CASE("a line says whether its own values are samples", "[plot]")
{
    // The projection summarises what it is given; whether what it was given was
    // already a summary is the model's to say, and it is the other half of the
    // same question. A line of two hundred points fits a pane of a thousand
    // columns whether those points are two hundred elements or the extremes of
    // a hundred buckets, and only one of the two may carry markers.
    std::vector<double> values(200);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(static_cast<double>(i) / 10.0);
    }

    std::vector<QPointF> points;
    std::vector<gui::PlotRun> runs;

    gui::PlotLine drawnLine = lineOver(values);
    const gui::PlotProjected samples = gui::projectLine(
        drawnLine, gui::PlotAxis{}, paneOver(0.0, 200.0, -1.0, 1.0), points, runs);
    CHECK_FALSE(samples.decimated);

    // The same points, said to be a fold of a longer line. Nothing about the
    // projection changes and everything about what a dot would mean does.
    points.clear();
    runs.clear();
    gui::PlotLine folded = lineOver(values);
    folded.summarised = true;
    // A bucket of two: its pair sits one position apart, so the step is exactly
    // what a line of elements has. This is the case the step could not tell
    // apart, and it is the one a reader meets by zooming to the edge of the
    // budget.
    folded.positionStep = 1.0;
    const gui::PlotProjected summary = gui::projectLine(
        folded, gui::PlotAxis{}, paneOver(0.0, 200.0, -1.0, 1.0), points, runs);
    CHECK(summary.decimated);
    CHECK(summary.runs == samples.runs);
}

TEST_CASE("a time base does not turn a summary into samples", "[plot]")
{
    // A line drawn against a time base is drawn sample for sample -- the x of
    // each point is looked up rather than computed -- so this path summarises
    // nothing itself. That says nothing at all about what it was handed: a
    // custom tab's entry is folded on the way out of the file whatever it is
    // drawn against, and this reported every one of them as samples.
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

    points.clear();
    runs.clear();
    gui::PlotLine folded = lineOver(values);
    folded.summarised = true;
    const gui::PlotProjected summary =
        gui::projectLine(folded, axis, paneOver(0.0, 3.0, 0.0, 5.0), points, runs);
    CHECK(summary.decimated);
    CHECK(summary.runs == drawn.runs);
}

// --- the envelope holds still -----------------------------------------------

TEST_CASE("panning moves the line without reshuffling it", "[plot]")
{
    // The property that makes a drag watchable, and the one an envelope
    // derived from the *window* cannot have. Divide the visible range into as
    // many buckets as the pane has columns and every pixel of pan slides every
    // boundary by a fraction of a sample, so the two extremes each column
    // selects keep changing and the line crawls under the pointer.
    //
    // Buckets aligned to the data's own index space cannot do that: panning
    // changes which buckets are on screen and nothing about what is in them.
    std::vector<double> values(200000, 0.0);
    values[120000] = 9.0;

    const auto peakAt = [&](double xMin, double xMax) {
        gui::PlotView view = paneOver(xMin, xMax, -1.0, 10.0);
        const Projected drawn = project(lineOver(values), gui::PlotAxis{}, view);
        double top = std::numeric_limits<double>::infinity();
        double at = 0.0;
        for (const QPointF& point : drawn.points) {
            if (point.y() < top) {
                top = point.y();
                at = point.x();
            }
        }
        // Back into the data's own coordinates, which is where the question is.
        return xMin + at / view.width * (xMax - xMin);
    };

    // The same window, slid along by a third of a bucket and then by a whole
    // one. The spike must stay where it is in the data.
    const double here = peakAt(0.0, 200000.0);
    const double nudged = peakAt(60.0, 200060.0);
    const double further = peakAt(200.0, 200200.0);

    CHECK(here == Approx(120000.0).margin(256.0));
    CHECK(nudged == Approx(here).margin(1.0));
    CHECK(further == Approx(here).margin(1.0));
}

TEST_CASE("a bucket holds the same samples wherever the window is", "[plot]")
{
    // The same statement from the other side: two windows that overlap must
    // agree about the values in the overlap, exactly, and not merely nearly.
    std::vector<double> values(100000);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> spread(-1.0, 1.0);
    for (double& value : values) {
        value = spread(rng);
    }

    const auto valuesIn = [&](double xMin, double xMax) {
        const Projected drawn =
            project(lineOver(values), gui::PlotAxis{}, paneOver(xMin, xMax, -1.5, 1.5));
        std::vector<double> found;
        found.reserve(drawn.points.size());
        for (const QPointF& point : drawn.points) {
            found.push_back(point.y());
        }
        return found;
    };

    const std::vector<double> wide = valuesIn(0.0, 100000.0);
    const std::vector<double> slid = valuesIn(37.0, 100037.0);
    REQUIRE(wide.size() > 100);
    REQUIRE(slid.size() > 100);

    // Every interior value of one appears, in order, in the other. Allowing a
    // couple at each end, which are the buckets the slide moved off the pane.
    std::size_t matched = 0;
    std::size_t at = 0;
    for (const double value : wide) {
        while (at < slid.size() && slid[at] != value) {
            ++at;
        }
        if (at < slid.size()) {
            ++matched;
            ++at;
        }
    }
    CHECK(matched >= wide.size() - 4);
}

TEST_CASE("a bucket is a power of two, so zooming steps by octaves", "[plot]")
{
    // Which is the other half of the bargain: the detail changes once per
    // doubling rather than continuously, and between those steps the picture
    // is completely still.
    std::vector<double> values(65536);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(static_cast<double>(i) / 30.0);
    }

    const auto pointsAcross = [&](double span) {
        const double middle = 32768.0;
        const Projected drawn = project(lineOver(values), gui::PlotAxis{},
                                        paneOver(middle - span / 2, middle + span / 2, -1.5, 1.5));
        return drawn.points.size();
    };

    // Inside one octave the bucket size does not change, so the number of
    // points drawn is simply proportional to how much data is on screen.
    const std::size_t wide = pointsAcross(60000.0);
    const std::size_t narrower = pointsAcross(50000.0);
    CHECK(wide > 0);
    CHECK(static_cast<double>(narrower) ==
          Approx(static_cast<double>(wide) * 50000.0 / 60000.0).margin(4.0));

    // And crossing into the next octave halves the bucket, which is where the
    // extra detail comes from: a slightly *smaller* window draws nearly twice
    // as many points, and every one of them covers half as much data.
    //
    // A thousand columns puts that boundary at thirty-two samples a column.
    const std::size_t justAbove = pointsAcross(33000.0);
    const std::size_t justBelow = pointsAcross(31000.0);
    CHECK(static_cast<double>(justBelow) > 1.5 * static_cast<double>(justAbove));
}

// ---------------------------------------------------------------------------
// The closer look: which run of a line a zoomed-in reader is asking for
// ---------------------------------------------------------------------------

TEST_CASE("a reader who has not zoomed in is asking for nothing", "[plot]")
{
    // The whole-line summary a model holds is `buckets` buckets of the whole
    // line. While the pane shows all of it, a second read at the same budget
    // would be the same read -- so there is nothing to ask for, and the answer
    // is "none" rather than a window that happens to be the whole line.
    CHECK_FALSE(gui::windowFor(0.0, 1000000.0, 1000000, 1024).has_value());
    CHECK_FALSE(gui::windowFor(-50.0, 1000050.0, 1000000, 1024).has_value());
    // Nor while the line is short enough to be drawn sample for sample.
    CHECK_FALSE(gui::windowFor(0.0, 500.0, 1000, 1024).has_value());
    // Nor of a degenerate view.
    CHECK_FALSE(gui::windowFor(10.0, 10.0, 1000000, 1024).has_value());
    CHECK_FALSE(gui::windowFor(0.0, 1.0, 0, 1024).has_value());
    CHECK_FALSE(gui::windowFor(0.0, 1.0, 1000000, 0).has_value());
}

TEST_CASE("a closer look is twice the width of the pane", "[plot]")
{
    // Twice, so that the reader can pan off the middle of it and still be
    // looking at data that has been read while the next run is on its way.
    const auto window = gui::windowFor(0.0, 10000.0, 1000000, 1024);
    REQUIRE(window.has_value());

    CHECK(window->bucket == 32); // 2 * 10000 / 1024, to the next power of two
    CHECK(window->span == 32768);
    CHECK(window->columns == 1024);
    CHECK(window->span >= 2 * 10000);
    CHECK(window->covers(0.0, 10000.0));
    // ...and a bucket small enough that the run is drawn at about the
    // resolution the whole line was, over a hundredth of the data.
    CHECK(window->bucket * window->columns == window->span);
}

TEST_CASE("a closer look steps by a quarter of itself", "[plot]")
{
    // The property that keeps a slow pan from flickering. The pane is half the
    // run wide, so stepping the run by a quarter leaves the run in hand still
    // covering the pane for one step past the boundary that asked for the next
    // one -- which is the step the read has to land in.
    const auto first = gui::windowFor(0.0, 10000.0, 1000000, 1024);
    REQUIRE(first.has_value());

    // Panning inside the quarter asks for nothing at all: the same run.
    CHECK(gui::windowFor(4000.0, 14000.0, 1000000, 1024) == first);
    CHECK(gui::windowFor(8191.0, 18191.0, 1000000, 1024) == first);

    // Crossing it asks for the next run along, and the one in hand still holds
    // everything on screen while that read is out.
    const auto next = gui::windowFor(9000.0, 19000.0, 1000000, 1024);
    REQUIRE(next.has_value());
    CHECK(next->first == 8192);
    CHECK(next->first != first->first);
    CHECK(first->covers(9000.0, 19000.0));
}

TEST_CASE("a closer look is aligned to the data and not to the view", "[plot]")
{
    // The same argument as the renderer's buckets, one level up: a run whose
    // boundaries moved with the view would re-summarise the same samples
    // differently on every pan, and the line would crawl even though each
    // picture of it was honest.
    for (double at = 0.0; at < 40000.0; at += 137.0) {
        const auto window = gui::windowFor(at, at + 10000.0, 1000000, 1024);
        REQUIRE(window.has_value());
        CHECK(window->bucket == 32);
        CHECK(window->first % window->bucket == 0);
        CHECK(window->first % 8192 == 0); // a quarter of the run
        CHECK(window->covers(at, at + 10000.0));
    }
}

TEST_CASE("zooming a closer look steps one octave at a time", "[plot]")
{
    long long previous = 0;
    for (double span : {40000.0, 20000.0, 10000.0, 5000.0, 2500.0, 1250.0}) {
        const auto window = gui::windowFor(500000.0, 500000.0 + span, 1000000, 1024);
        REQUIRE(window.has_value());
        // A power of two every time, halving as the span halves.
        CHECK((window->bucket & (window->bucket - 1)) == 0);
        if (previous > 0) {
            CHECK(window->bucket == previous / 2);
        }
        previous = window->bucket;
    }
    // ...until the run is drawn sample for sample, which is the end of it.
    const auto closest = gui::windowFor(500000.0, 500100.0, 1000000, 1024);
    REQUIRE(closest.has_value());
    CHECK(closest->bucket == 1);
    CHECK(closest->span == 1024);
}

TEST_CASE("a closer look at the end of a line keeps the stride of the others",
          "[plot]")
{
    // A run clamped by the end of the data is shorter, and a read asked for a
    // fixed budget of points would answer it at a finer stride than every other
    // run -- so its buckets would not line up with theirs, and the crawl this
    // whole arrangement prevents would reappear in the last screenful of every
    // dataset. The bucket count travels with the run for exactly this reason.
    const auto window = gui::windowFor(99000.0, 99900.0, 100000, 1024);
    REQUIRE(window.has_value());

    CHECK(window->bucket == 2);
    CHECK(window->first == 98816);
    CHECK(window->first % window->bucket == 0);
    CHECK(window->span == 100000 - 98816); // what is left, not the whole run
    CHECK(window->span < window->bucket * 1024);
    // Ceiling division of the clamped span, which is what makes a read work out
    // a stride of exactly `bucket` -- the same arithmetic sampleFrom applies.
    CHECK(window->columns == (window->span + window->bucket - 1) / window->bucket);
    CHECK((window->span + window->columns - 1) / window->columns == window->bucket);
}

TEST_CASE("a view off the end of the data asks for what is there", "[plot]")
{
    // Not a crash and not a run past the end: a view a long way outside the
    // line still resolves to indices that exist, or to nothing.
    const auto beyond = gui::windowFor(2000000.0, 2010000.0, 1000000, 1024);
    if (beyond.has_value()) {
        CHECK(beyond->first >= 0);
        CHECK(beyond->first + beyond->span <= 1000000);
    }
    const auto before = gui::windowFor(-50000.0, -40000.0, 1000000, 1024);
    if (before.has_value()) {
        CHECK(before->first == 0);
    }
    // And one written with numbers no arithmetic can use is simply refused.
    const double huge = 1e300;
    const auto silly = gui::windowFor(-huge, huge, 1000000, 1024);
    CHECK_FALSE(silly.has_value());
}

// ---------------------------------------------------------------------------
// The stroke, where an envelope actually puts it
// ---------------------------------------------------------------------------

TEST_CASE("an envelope's zigzag strokes as a band, not as a comb", "[plot]")
{
    // The bug that made /plotting/adc_10M -- ten million samples, drawn as a
    // min/max band -- come out as a comb of spindles with black between them.
    //
    // An envelope reverses direction at *every* station: up to the bucket's
    // high, straight back down to the next one's low. The two normals at such a
    // join are very nearly opposite, so their bisector points *along* the
    // stroke rather than across it -- and the join mitred that bisector and cut
    // it to the limit, which put the station's two vertices four half-widths
    // apart in y instead of one half-width either side in x. Every quad between
    // two stations became a sliver. Measured on the picture, a column of the
    // band carried 38 pixels of ink where the band was 86 pixels tall.
    //
    // Two properties say it is right, and the first is the one that failed:
    // each pair straddles its own station by the width asked for, and
    // consecutive pairs are the same way round, so the strip is quads rather
    // than hourglasses.
    std::vector<QPointF> zigzag;
    for (int column = 0; column < 40; ++column) {
        const double x = static_cast<double>(column);
        zigzag.emplace_back(x, 100.0);       // the bucket's low
        zigzag.emplace_back(x + 0.5, 10.0);  // and its high, half a bucket along
    }

    std::vector<QPointF> stroke;
    gui::strokeRun(zigzag.data(), static_cast<int>(zigzag.size()), 1.0, stroke);
    REQUIRE(stroke.size() == zigzag.size() * 2);

    for (std::size_t station = 0; station < zigzag.size(); ++station) {
        const QPointF above = stroke[station * 2];
        const QPointF below = stroke[station * 2 + 1];
        INFO("station " << station);
        // The width it was asked for, across the line. Four half-widths of
        // whisker is what this used to be.
        CHECK(std::hypot(above.x() - below.x(), above.y() - below.y()) == Approx(1.0).margin(0.001));
        // Centred on the station rather than reaching past it.
        CHECK((above.x() + below.x()) / 2.0 == Approx(zigzag[station].x()).margin(0.001));
        CHECK((above.y() + below.y()) / 2.0 == Approx(zigzag[station].y()).margin(0.001));

        if (station > 0) {
            const QPointF previous = stroke[(station - 1) * 2] - stroke[(station - 1) * 2 + 1];
            const QPointF current = above - below;
            INFO("the strip turns inside out here");
            CHECK(previous.x() * current.x() + previous.y() * current.y() > 0.0);
        }
    }
}

TEST_CASE("a gentle corner still gets its miter", "[plot]")
{
    // The other side of the same decision: giving up past the limit must not
    // mean giving up at every bend. A corner the miter can reach round keeps
    // the stroke square through it, which is what the right-angle case above
    // measures and what this one holds at a shallower angle.
    const std::vector<QPointF> bend{{0.0, 0.0}, {100.0, 0.0}, {200.0, 20.0}};
    std::vector<QPointF> stroke;
    gui::strokeRun(bend.data(), static_cast<int>(bend.size()), 2.0, stroke);

    REQUIRE(stroke.size() == 6);
    const double corner = std::hypot(stroke[2].x() - stroke[3].x(), stroke[2].y() - stroke[3].y());
    CHECK(corner > 2.0);
    CHECK(corner < 2.0 * gui::kMiterLimit);
}

// --- a line on an axis of its own -------------------------------------------
TEST_CASE("a line on an axis of its own is placed by that axis", "[plot][axes]")
{
    // A pressure of about a thousand beside a temperature of about twenty. On
    // the pressures' axis the temperatures are a flat stroke along the bottom;
    // on one of their own they span the pane, which is the whole of what the
    // feature is for.
    const std::vector<double> temperature{10.0, 30.0};
    gui::PlotLine line = lineOver(temperature);
    const gui::PlotView view = paneOver(0.0, 1.0, 0.0, 1000.0);

    const Projected shared = project(line, {}, view);
    REQUIRE(shared.points.size() == 2);
    CHECK(shared.points[0].y() == Approx(495.0));
    CHECK(shared.points[1].y() == Approx(485.0));

    line.ownY = true;
    line.yMin = 0.0;
    line.yMax = 40.0;
    const Projected own = project(line, {}, gui::lineView(line, view));
    REQUIRE(own.points.size() == 2);
    CHECK(own.points[0].y() == Approx(375.0));
    CHECK(own.points[1].y() == Approx(125.0));
    // The x axis is every line's: an axis of its own is a y axis and nothing
    // else.
    CHECK(own.points[0].x() == Approx(shared.points[0].x()));
    CHECK(own.points[1].x() == Approx(shared.points[1].x()));
}

TEST_CASE("an axis of its own is linear whatever the common one is", "[plot][axes][log]")
{
    // The plot settings are the common axis's, logarithm included. A line on
    // an axis of its own is the line as it would be drawn alone, and a value
    // at or below zero is therefore a point on it and not a gap.
    const std::vector<double> values{-5.0, 0.0, 5.0};
    gui::PlotLine line = lineOver(values);
    line.ownY = true;
    line.yMin = -10.0;
    line.yMax = 10.0;
    gui::PlotView view = paneOver(0.0, 2.0, 1.0, 1000.0);
    view.yLog = true;

    const gui::PlotView own = gui::lineView(line, view);
    CHECK_FALSE(own.yLog);
    CHECK(gui::yFractionOf(0.0, own) == Approx(0.5));
    // The x half of the view is untouched.
    CHECK(own.xMin == view.xMin);
    CHECK(own.xMax == view.xMax);

    const Projected drawn = project(line, {}, own);
    CHECK(drawn.runs.size() == 1);
    CHECK(drawn.points.size() == 3);

    // ...and a line with no axis of its own is handed the view it was given.
    const gui::PlotLine common = lineOver(values);
    CHECK(gui::lineView(common, view).yLog);
    CHECK(gui::lineView(common, view).yMax == view.yMax);
}

TEST_CASE("an axis of its own with no span draws nothing", "[plot][axes]")
{
    // The same refusal every other window gets: a span of zero has no answer.
    const std::vector<double> values{1.0, 2.0};
    gui::PlotLine line = lineOver(values);
    line.ownY = true;
    line.yMin = 3.0;
    line.yMax = 3.0;
    CHECK_FALSE(gui::yMappingOf(gui::lineView(line, paneOver(0.0, 1.0, 0.0, 1.0))).usable);
}
