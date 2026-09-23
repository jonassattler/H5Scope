// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Where the points go, with no renderer attached.
//
// Everything a line plot decides before a pixel is touched is here: which
// sample lands at which x, which samples are drawable at all, where a gap in
// the data ends one stroke and starts the next, how a million samples become
// two thousand vertices without losing the one that matters, and how a stroke
// a chosen number of pixels wide is built out of triangles.
//
// Split out of PlotItem so that it can be tested without a window. The whole
// of it is arithmetic over doubles and two output vectors; it links QtGui for
// QPointF and QColor and nothing else. tests/test_customplot.cpp asserts the x
// arithmetic through samplesOf() below, which is the shape the boundary used to
// have -- it filled a QXYSeries with data-space points -- and which survives as
// a seam precisely because the renderer no longer needs it.
//
// Three things in here are the reason the plot is ours rather than a library's,
// and all three are cheap once the projection is:
//
// 1. Decimation by min/max envelope rather than by stride. Stride selects by
//    position and reaches an extremum only by luck;
//    DatasetTableModel.hpp:205-213 names the cost, "a spike narrower than one
//    stride is not drawn". An envelope selects the extremum *because* it is
//    extreme, draws the same number of points, and cannot lose one.
//
// 2. Double precision all the way to the vertex. QSGGeometry stores float32.
//    An x of 1.7e9 -- seconds since the epoch, which is what every logger in
//    the world writes -- has a float spacing of 128, so a line sampled every
//    millisecond collapses into a staircase of flat treads. The fix is not to
//    store doubles; it is to project in double and cast the *result*, because
//    a pixel coordinate is at most a few thousand. That is why the projection
//    happens here rather than in a vertex shader fed with data coordinates,
//    which is the faster arrangement and the one that loses the data.
//
// 3. A gap where the data is absent. Dropping the non-finite samples and
//    handing the survivors to a line renderer draws a straight line *across*
//    the missing data, which is a reading of data that was never taken. A run
//    of them ends the stroke here and the next run starts a new one.
//
// A logarithmic axis is a fourth thing and is not in that list, because it is
// not a design decision at all -- it is the observation that every one of the
// three above is written in terms of *where a value sits*, and that there is
// exactly one function per axis that answers it. AxisMapping is that function.
// Put log10 inside it and the curve, the crosshair, the ticks, the rules, the
// band and the readouts all follow, because none of them ever subtracted two
// bounds for themselves. The one thing a log axis really does add is a second
// way for a sample to be undrawable: a value at or below zero has no logarithm,
// and it is a gap for exactly the reason a NaN is one -- there is no place on
// the pane that would be a true reading of it.

#include <QtCore/QPointF>
#include <QtGui/QColor>

#include <cmath>

#include <optional>
#include <vector>

namespace gui {

/// One line to draw.
///
/// `values` is **borrowed**: the renderer reads it on every frame and never
/// copies it. The owner is whoever handed it over -- DatasetPlot and CustomPlot
/// both already hold their lines as `std::vector<double>`, and a copy of a
/// ten-thousand-line selection would be a hundred and sixty megabytes of it --
/// so the contract is that the owner calls PlotItem::clear() before it touches
/// those vectors again. There is no way for the item to notice; that is the
/// price of not copying, and it is why this says so here rather than leaving it
/// to be discovered.
struct PlotLine
{
    const double* values = nullptr;
    qsizetype count = 0;

    /// Where sample `i` sits along the shared axis, before the axis turns a
    /// position into an x: `positionStart + i * positionStep`.
    ///
    /// Two callers need the two degrees of freedom for different reasons.
    /// DatasetPlot thins by stride and a drawn point therefore covers `stride`
    /// axis positions. CustomPlot's "stretch" spreads a line of any length over
    /// the whole axis, which is the same affine map with a fractional step. A
    /// line that is simply itself leaves these at 0 and 1.
    double positionStart = 0.0;
    double positionStep = 1.0;

    /// The x of every sample, when the line states them rather than leaving
    /// them to the axis: `count` of them, borrowed on the same terms as
    /// `values`.
    ///
    /// What a line folded onto a logarithmic axis's columns is drawn from --
    /// see LogColumns in PlotLevels.hpp. Its points are one or two per pixel
    /// column, so they are evenly spaced on the *pane* and nowhere near evenly
    /// spaced in position, and no start and step can say where they are. When
    /// this is set the two above and the axis are not consulted: the model that
    /// folded the line has already put each point where the axis would.
    ///
    /// Drawn point for point, never summarised again, because they are already
    /// a summary at the pane's own resolution: the renderer's envelope buckets
    /// by position, which is the very thing that went wrong on this axis.
    const double* xs = nullptr;

    /// Whether these values are a *summary* of the line rather than the line.
    ///
    /// Set by whoever read them. A model that folded the file into an envelope
    /// hands over two values per bucket, and neither of them is a sample taken
    /// at the x it is drawn at -- they are the largest and the smallest of
    /// everything that bucket covered. Markers are why this is here: a dot
    /// marks a measurement, and a dot on a bucket marks a reading nobody took.
    ///
    /// It cannot be worked out from `positionStep`, which is what was tried and
    /// is wrong in both directions. The step says where a drawn point *sits*,
    /// not what it is: an envelope at bucket two puts its pair one position
    /// apart, so the step is exactly 1 and every value is still a summary --
    /// which is how a plot zoomed to the edge of its budget grew a dot on every
    /// envelope point. And a custom tab's Stretch spreads a line of real
    /// samples over a longer axis, where the step is above one and every value
    /// is a measurement that deserves its dot.
    bool summarised = false;

    /// Supplied by the caller rather than chosen here: every colour in this
    /// application resolves through Theme.qml, and a renderer that picked its
    /// own would be the one place that did not.
    QColor colour;

    /// How strongly the line is drawn, multiplied into the colour's own alpha.
    ///
    /// Separate from the colour rather than folded into it, because the two
    /// are set by different things and in no fixed order: the palette decides
    /// the hue, and whether a line is the highlighted one decides the strength.
    /// Folding them would make `setSeriesColor` after `setSeriesOpacity` quietly
    /// undo it.
    double opacity = 1.0;

    /// The stroke, in device-independent pixels. Per line because the
    /// highlight is a line drawn at double width, and because a stroke built
    /// out of triangles can honour a width that the graphics API cannot --
    /// line width above 1 is an optional RHI feature that several backends
    /// silently ignore, which is why this is not a call to setLineWidth().
    double width = 1.0;

    /// Whether this line is drawn against a y axis of its own rather than the
    /// view's, and that axis's window.
    ///
    /// A custom tab can put a line of pressures beside a line of temperatures,
    /// and on one axis the smaller of them is a flat stroke along the bottom.
    /// A separate axis is the same values under a different *map*, so it is
    /// here beside the colour -- a styling of the line, set after it is handed
    /// over and changed without re-reading anything -- rather than a second
    /// view. The window is always linear: the plot settings, logarithm
    /// included, are the common axis's, and a separate axis is the line as it
    /// would be drawn if it were the only one.
    ///
    /// Everything that turns a value into a place asks lineView() rather than
    /// reading the view's y bounds, so the curve, the crosshair and the side
    /// axis's ticks cannot disagree about where this line is.
    bool ownY = false;
    double yMin = 0.0;
    double yMax = 1.0;
};

/// One closer look at a line: an aligned run of it, to be summarised at a
/// bucket finer than the whole of it could be.
///
/// The whole-line summary a model holds is fixed -- so many points however long
/// the line is -- which means zooming into it stretches what is drawn rather
/// than resolving it. Reading the run the reader is looking at again, at a
/// finer bucket, is what turns an octave of zoom into an octave of detail. This
/// is where that run is decided; who reads it, and when, is the model's own
/// business.
///
/// **Aligned**, for the same reason the renderer's buckets are aligned -- see
/// the long note in projectLine(). A run derived from the view slides its
/// boundaries with every pixel of pan, so the two extremes each bucket selects
/// keep changing and the line crawls and boils under the pointer. These are
/// powers of two in the data's own index space: panning translates the line
/// rigidly and zooming steps one octave at a time.
struct PlotWindow
{
    /// The first element of the line this covers, in the line's own indices.
    long long first = 0;
    /// How many elements: `bucket * columns`, except at the end of the line
    /// where it is what is left.
    long long span = 0;
    /// Elements per bucket. A power of two, and 1 when the reader is close
    /// enough that the run is drawn sample for sample.
    long long bucket = 1;
    /// Buckets. Asked of a read as its cap, so that the stride it works out is
    /// exactly `bucket` -- a run clamped by the end of the line would otherwise
    /// be read at a finer stride than every other one, and its buckets would
    /// not line up with theirs.
    int columns = 0;

    [[nodiscard]] bool operator==(const PlotWindow&) const = default;

    /// Whether this run holds everything between `low` and `high`, in the same
    /// indices. Callers clamp those two to the data first: a pane showing the
    /// end of a line shows some empty axis past it, and a run reaching the last
    /// element covers everything there is to draw out there.
    [[nodiscard]] bool covers(double low, double high) const
    {
        return static_cast<double>(first) <= low && static_cast<double>(first + span) >= high;
    }
};

/// The closer look at `low`..`high` of a line of `length` elements, given a
/// budget of `buckets` buckets -- or nothing when the whole-line summary is
/// already at least as fine, which is the answer whenever the reader is zoomed
/// out.
///
/// The run is twice the width of what is visible, so the reader can pan off the
/// middle of it and still be looking at data that has been read while the next
/// one is on its way, and it steps by a quarter of itself rather than by the
/// whole: the pane is half the run wide, so a quarter-run step leaves the run in
/// hand still covering the pane for one step past the boundary that asked for
/// the next one. Without that, every crossing would fall back to the coarse
/// summary for as long as the read took and a slow pan would flicker between
/// the two.
[[nodiscard]] std::optional<PlotWindow> windowFor(double low, double high, long long length,
                                                  long long buckets);

/// How the shared x axis turns a position into a value.
struct PlotAxis
{
    /// x = start + position * step.
    double start = 0.0;
    double step = 1.0;

    /// ...unless there is a time base, and then
    /// x = values[round(position / valueStep)].
    ///
    /// Borrowed on the same terms as PlotLine::values. A time base has a value
    /// at each of its own positions and nowhere in between, so a point that
    /// falls between two of them takes the nearer; interpolating would invent
    /// an x, which is the same mistake as inventing a y.
    const double* values = nullptr;
    qsizetype count = 0;

    /// Axis positions between one of those values and the next.
    ///
    /// A time base is a line like any other and is read like one: summarised to
    /// about a pane's worth of points however many elements it has. So `values`
    /// is indexed by *its own drawn point*, and a position is an index into the
    /// elements it was summarised from -- the two are the same number only
    /// while the axis is short enough not to be thinned, which is the case
    /// every short fixture is and no real log is.
    ///
    /// Divided out rather than folded into PlotLine::positionStep, because the
    /// line and its axis are thinned by their own strides and there is no
    /// reason for the two to agree: a tab can draw a thousand-element entry
    /// against a million-element time base, and each is summarised against the
    /// same pane.
    double valueStep = 1.0;

    /// The run of the time base the reader is looking at, read finer.
    ///
    /// The three above are the *whole* time base, summarised to about a pane's
    /// worth of points however long it is -- so at ten million elements one of
    /// its drawn points stands for five thousand of them, and a reader zoomed
    /// in past that is being handed one x for every column of the pane. The
    /// line resolves as they zoom and the axis under it does not, which is a
    /// plot that cannot be zoomed: the curve collapses onto a handful of x and
    /// draws as a staircase of vertical treads.
    ///
    /// So the time base is folded again over the run on screen, exactly as the
    /// lines are, and that run is here. Where it reaches it is the better
    /// answer and is the one taken; outside it the whole-line summary above is
    /// still a correct one, which is what keeps a line whose own closer look
    /// has not landed yet drawn against an axis that has.
    ///
    /// Borrowed on the same terms as everything else here.
    const double* closerValues = nullptr;
    qsizetype closerCount = 0;
    /// The axis position `closerValues[0]` is the x of. A run does not start at
    /// the beginning of the line, which is the whole of what makes it a run.
    double closerStart = 0.0;
    /// Axis positions between one of those values and the next, as `valueStep`
    /// is for the whole.
    double closerStep = 1.0;

    [[nodiscard]] bool explicitX() const { return values != nullptr && count > 0; }
    [[nodiscard]] bool hasCloser() const { return closerValues != nullptr && closerCount > 0; }
};

/// The window being shown and the pane it is shown in.
struct PlotView
{
    double xMin = 0.0;
    double xMax = 1.0;
    double yMin = 0.0;
    double yMax = 1.0;

    /// Whether each axis places a value by its logarithm rather than by itself.
    ///
    /// The bounds above stay in the data's own units whichever this is -- they
    /// are what the reader typed, what the ticks print and what the models are
    /// asked to read between. Only the map from a value to a place on the pane
    /// changes, and it changes in one place (AxisMapping).
    ///
    /// A logarithmic axis whose bounds are not both above zero is not a view at
    /// all and nothing is drawn against it; see mappingOver(). Keeping those
    /// bounds positive is the caller's job, because only the caller knows what
    /// the data has -- PlotSurface.qml takes them off the smallest positive
    /// value the plot object reports.
    bool xLog = false;
    bool yLog = false;

    /// What base each logarithm is taken to. Ten unless the reader says
    /// otherwise, which is what every axis in the application was before the
    /// choice existed and is what a plot opens on.
    ///
    /// **It does not move anything that is drawn**, and that is worth knowing
    /// before reading further. Where a value sits is a ratio of two
    /// logarithms, and a change of base multiplies both by the same constant,
    /// so it cancels: base ten and base two put every point of a curve in
    /// exactly the same place. What a reader picks a base for is the axis
    /// *around* the picture -- the numbers go at the powers of the base and
    /// the rules between them, so ten marks decades where two marks octaves --
    /// and that is PlotFrame.qml's business rather than this file's.
    ///
    /// So the renderer is told for one reason: to refuse a base that is not
    /// one. Only a number above one is. At exactly one the logarithm is a
    /// division by zero and every value would land in the same place; below it
    /// the axis runs backwards, which is a different request from the one this
    /// answers. mappingOver() refuses both outright, so a view carrying such a
    /// base draws nothing rather than drawing something wrong -- whatever
    /// wrote the property.
    double xLogBase = 10.0;
    double yLogBase = 10.0;

    /// The pane, in item coordinates.
    double width = 0.0;
    double height = 0.0;

    /// Device pixels per item coordinate: the window's devicePixelRatio.
    ///
    /// A pane is measured in logical pixels and drawn into a framebuffer with
    /// this many physical ones for each of them, so on a HiDPI display "one
    /// bucket per column" over the logical width is one bucket per *two*
    /// physical columns, and half the resolution the screen can show is thrown
    /// away before anything is drawn. Every column count below is therefore
    /// taken over `width * pixelRatio`, which is what the reader's screen
    /// actually has.
    ///
    /// One by default, so a test that describes a pane without a window gets
    /// the arithmetic it would have had.
    double pixelRatio = 1.0;

    /// Most envelope columns one line may spend, or 0 for one per pixel.
    ///
    /// The envelope bounds each *line* by the pane's width, which is the whole
    /// point of it -- but it does not bound their sum, and ten thousand lines
    /// at two vertices a column is forty million vertices whatever the pane
    /// measures. PlotItem divides a fixed budget between the lines and passes
    /// the share down here. See kMaxVertices in PlotItem.cpp.
    int maxColumns = 0;
};

/// The logarithm of `value` to `base`.
///
/// The two bases a reader is most likely to pick have exact library functions
/// of their own, and they are not the same answer as the division: std::log(
/// 1000.0) / std::log(10.0) is 2.9999999999999996, so a mark that ought to
/// *be* a power of the base comes out a shade beside one. The whole structure
/// of a logarithmic grid is that its majors land exactly on its labels, so the
/// shade matters.
[[nodiscard]] inline double logOf(double value, double base)
{
    if (base == 10.0) {
        return std::log10(value);
    }
    if (base == 2.0) {
        return std::log2(value);
    }
    return std::log(value) / std::log(base);
}

/// One axis's map from a value to a fraction of the pane, and back.
///
/// The one piece of arithmetic every part of the plot has to agree about.
/// `low` and `high` are already *in the axis's own scale* -- the logarithms,
/// when the axis is logarithmic -- so that a fraction costs one transform and
/// one subtraction rather than a branch per value in the hot loop.
///
/// Built by mappingOver(), which is also where a view that has no answer is
/// refused. That is a wider question on a logarithmic axis than on a linear
/// one: a span of zero has no answer on either, and a bound at or below zero
/// has none on this one, nor has a base that is not above one.
struct AxisMapping
{
    /// The bounds, transformed. Not the data's own units when `logarithmic`.
    double low = 0.0;
    double high = 1.0;
    bool logarithmic = false;
    /// What the logarithm is taken to. Meaningless unless `logarithmic`, and
    /// never anything but a number above one when it is -- see mappingOver.
    double base = 10.0;
    /// Whether there is a span to divide by at all. Every caller has to notice
    /// that before it divides, and one struct is how they all notice it the
    /// same way.
    bool usable = false;

    /// Whether `value` has a place on this axis.
    ///
    /// Two ways to fail and they are the same failure: a value that is not
    /// finite was never measured, and a value at or below zero on a
    /// logarithmic axis has no logarithm to be placed by. Both are a gap --
    /// the stroke ends and the next one starts after them -- because the
    /// alternative in either case is to draw a line across a place where the
    /// data says nothing.
    [[nodiscard]] bool draws(double value) const
    {
        return std::isfinite(value) && (!logarithmic || value > 0.0);
    }

    /// Where `value` sits along the axis, as a fraction from `low`.
    ///
    /// Zero for a view with no span, which is what every degenerate view here
    /// resolves to. Undrawable values are *not* special-cased: log10 of zero is
    /// negative infinity and of a negative number is NaN, and either one lands
    /// the point off the pane where it belongs. Callers that must tell a gap
    /// from a point outside the window ask draws() first.
    [[nodiscard]] double fractionOf(double value) const
    {
        if (!usable) {
            return 0.0;
        }
        return ((logarithmic ? logOf(value, base) : value) - low) / (high - low);
    }

    /// ...and back, which is what a pointer position resolves through.
    [[nodiscard]] double valueAt(double fraction) const
    {
        const double at = low + fraction * (high - low);
        return logarithmic ? std::pow(base, at) : at;
    }
};

/// The map from `low`..`high` in the data's own units, on either scale.
[[nodiscard]] AxisMapping mappingOver(double low, double high, bool logarithmic,
                                      double base = 10.0);

/// The two a view carries.
[[nodiscard]] AxisMapping xMappingOf(const PlotView& view);
[[nodiscard]] AxisMapping yMappingOf(const PlotView& view);

/// The view `line` is drawn in: `view` itself, unless the line has a y axis
/// of its own, and then `view` with that axis's linear window in place of its
/// y. See PlotLine::ownY. The x half is never touched -- every line shares the
/// one x axis whatever its y.
[[nodiscard]] PlotView lineView(const PlotLine& line, const PlotView& view);

/// One unbroken stroke. A line with two gaps in it is three runs.
struct PlotRun
{
    int first = 0;
    int count = 0;
};

/// What projecting one line produced.
struct PlotProjected
{
    /// Strokes appended to `runs`.
    int runs = 0;
    /// Whether the envelope was used, which is to say whether a drawn point is
    /// a sample or a summary of several.
    ///
    /// Markers are the reason this is reported. A marker is punctuation on a
    /// line and it marks a *sample*; drawing one per envelope point would put
    /// two dots in every pixel column, which says nothing and is not what the
    /// setting means.
    bool decimated = false;
};

/// Where sample `at` of `line` sits along x -- or NaN when it sits nowhere,
/// which is a sample past the end of a time base or one whose x did not read.
[[nodiscard]] double xOf(const PlotLine& line, const PlotAxis& axis, qsizetype at);

/// Every drawable sample of `line`, in **data** coordinates and in drawing
/// order, with the undrawable ones simply absent.
///
/// Nothing in the renderer calls this: the whole design is that a sample
/// becomes a pixel without ever becoming a QPointF in data space, because
/// sixteen bytes a point built and copied per refill is what the old boundary
/// cost. It exists as the seam tests/test_customplot.cpp asserts the x
/// arithmetic through, and it agrees with projectLine() by construction --
/// both drop a sample that does not read.
///
/// About the data and not about a scale: there is no PlotView here, so a value
/// a logarithmic axis could not place is still reported. That is the right
/// answer to what this is asked -- where the samples are -- and it is why the
/// agreement above is an agreement over a linear view.
[[nodiscard]] std::vector<QPointF> samplesOf(const PlotLine& line, const PlotAxis& axis);

/// Where `value` sits up the pane, as a fraction from the bottom, and where
/// `x` sits along it.
///
/// The one piece of arithmetic the chrome and the curve must agree about. A
/// tick drawn at a fraction these functions did not produce is a grid line that
/// lies about where the curve is, which is why PlotItem hands them to QML
/// rather than letting QML derive the mapping a second time.
[[nodiscard]] double yFractionOf(double value, const PlotView& view);
[[nodiscard]] double xFractionOf(double x, const PlotView& view);

/// Project `line` into `points` in item coordinates, splitting the strokes at
/// gaps, and append each stroke to `runs`. Both vectors are appended to, so a
/// set of lines projects into one pair of buffers.
///
PlotProjected projectLine(const PlotLine& line, const PlotAxis& axis, const PlotView& view,
                          std::vector<QPointF>& points, std::vector<PlotRun>& runs);

/// How far a mitred join may reach past the stroke before it is given up.
///
/// An envelope turns through very nearly 180 degrees at every column -- up to
/// the bucket's high, back down to the next one's low -- and a miter at that
/// angle is a spear several hundred pixels long thrown across the pane. Four is
/// the conventional limit.
///
/// Past it the join becomes a butt: the two segments end and start on the same
/// perpendicular. *Not* a miter cut short, which is what this used to do and is
/// the wrong shape -- the bisector of two nearly opposite normals points along
/// the stroke rather than across it, so clamping its length left a whisker
/// standing out of every extreme of every envelope column. A butt at a one- or
/// two-pixel stroke is what a round join would have drawn anyway.
inline constexpr double kMiterLimit = 4.0;

/// The smallest `1 + n1.n2` a join can have and still be mitred within the
/// limit above. The miter reaches sqrt(2 / sum) times the half width, so a
/// limit of four is a sum of an eighth.
inline constexpr double kMiterFloor = 2.0 / (kMiterLimit * kMiterLimit);

/// A stroke thinner than this has no area to rasterise and would disappear
/// rather than draw faintly. Not a design decision -- Theme.plotLineWidth is
/// what says how heavy a line is -- only a floor under the arithmetic.
inline constexpr double kMinStrokeWidth = 0.25;

/// Expand `count` projected points into a triangle strip `width` pixels wide,
/// calling `place(x, y)` twice per station.
///
/// Triangles rather than a wide line because line width above 1.0 is an
/// optional RHI feature that several backends ignore without saying so, and
/// the highlight -- the one affordance for following a line through a bundle of
/// fifty -- is a line drawn at double width. A stroke that is sometimes two
/// pixels and sometimes one depending on the machine is not an affordance.
///
/// A template, and in the header, because this is the hot loop of the whole
/// plot. Measured over ten thousand lines of two thousand points, it was more
/// than half of a frame -- and half of *that* was writing the vertices into a
/// vector so the caller could copy them into the buffer it had already
/// allocated. PlotItem passes a sink that writes each vertex where it belongs
/// and the copy disappears.
template<typename Place>
void strokeRunInto(const QPointF* points, int count, double width, Place&& place)
{
    if (points == nullptr || count < 2) {
        return;
    }
    const double half = (width > kMinStrokeWidth ? width : kMinStrokeWidth) / 2.0;

    // Which side of the path the first vertex of each pair is on, carried along
    // the run.
    //
    // This is the whole reason an envelope drew as a comb of spindles rather
    // than as a band. A triangle strip pairs each station's two vertices with
    // the next station's two, and the quad between them is only a quad while
    // both pairs are the same way round. At a reversal -- which is *every*
    // station of an envelope, up to the high and straight back down -- the
    // normal flips to the other side of the path, so the two sides crossed and
    // every quad rasterised as an hourglass pinched to a point in the middle:
    // half the ink, and a black gap where the join should have been solid.
    //
    // Screen space has no memory of which side was "left", so the test is
    // simply whether this station's offset still points the way the last one
    // did. It costs a dot product per station and nothing else.
    QPointF carried;

    // The normal of the segment leaving station `k`. A segment of no length has
    // no direction, so it keeps the one before it -- which happens in an
    // envelope wherever a column held a single sample.
    //
    // std::sqrt rather than std::hypot. hypot exists to survive squaring a
    // number near the top of the range, and these are pixel deltas: projectLine
    // clamps every coordinate to ten million, whose square is 1e14 and has
    // nearly three hundred orders of magnitude of headroom. hypot was costing
    // about four nanoseconds a station for a guarantee that cannot be needed.
    const auto normalAfter = [&](int k, QPointF fallback) {
        const double sx = points[k + 1].x() - points[k].x();
        const double sy = points[k + 1].y() - points[k].y();
        const double square = sx * sx + sy * sy;
        if (!(square > 0.0)) {
            return fallback;
        }
        const double inverse = 1.0 / std::sqrt(square);
        return QPointF(-sy * inverse, sx * inverse);
    };

    QPointF entering = normalAfter(0, QPointF(0.0, -1.0));
    QPointF leaving = entering;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            entering = leaving;
            leaving = (i < count - 1) ? normalAfter(i, entering) : entering;
        }

        // The ends take the one normal they have; a join takes the bisector,
        // lengthened so the stroke stays `width` wide through the corner.
        //
        // Written without normalising the bisector, because it does not have to
        // be. Both normals are unit, so |n1 + n2|^2 is 2 + 2d for d = n1 . n2,
        // and the reach that keeps a corner square works out as 1 / (1 + d) --
        // no square root at all.
        //
        // Past the miter limit the join is a butt instead: both segments end on
        // the one perpendicular they very nearly share. The bisector of two
        // opposed normals points *along* the stroke, so a miter cut to length
        // there is not a join at all, it is a whisker standing out of the line
        // -- and at 180 degrees it is the spear kMiterLimit is named for.
        QPointF offset = leaving;
        if (i > 0 && i < count - 1) {
            const double dot = entering.x() * leaving.x() + entering.y() * leaving.y();
            const double sum = 1.0 + dot;
            if (sum >= kMiterFloor) {
                offset =
                    QPointF((entering.x() + leaving.x()) / sum, (entering.y() + leaving.y()) / sum);
            }
        }

        // ...and it goes on the side the run has been using. See `carried`.
        if (i > 0 && offset.x() * carried.x() + offset.y() * carried.y() < 0.0) {
            offset = QPointF(-offset.x(), -offset.y());
        }
        carried = offset;

        const double ox = offset.x() * half;
        const double oy = offset.y() * half;
        place(points[i].x() + ox, points[i].y() + oy);
        place(points[i].x() - ox, points[i].y() - oy);
    }
}

/// The same, collected into a vector. What the tests assert against.
void strokeRun(const QPointF* points, int count, double width, std::vector<QPointF>& out);

/// How many sides a marker is drawn with.
///
/// A marker is four pixels across and the one it replaces was a QML Rectangle
/// with a radius of half its width -- a circle. Eight sides is a circle at that
/// size and six is a visible hexagon; the count is stated here because
/// PlotItem sizes its vertex buffer from it, and a buffer sized from a
/// different number than the one that fills it is a write past the end of a
/// mapped range.
inline constexpr int kMarkerSides = 8;

/// Append one marker of `radius` at `centre`, as exactly kMarkerSides vertices
/// in triangle-strip order.
void markerAt(const QPointF& centre, double radius, std::vector<QPointF>& out);

} // namespace gui
