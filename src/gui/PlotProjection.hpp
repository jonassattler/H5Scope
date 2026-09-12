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
// Four things in here are the reason the plot is ours rather than a library's,
// and all four are cheap once the projection is:
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
// 4. A logarithmic y axis, which 2-D Qt Graphs cannot express at any price.

#include <QtCore/QPointF>
#include <QtGui/QColor>

#include <vector>

namespace gui {

/// How far below the largest value a logarithmic axis reaches when the data
/// itself gives no floor -- which it does not, because a log axis has to put
/// its bottom somewhere when the values reach zero or go negative.
///
/// Stated rather than derived so that the chrome can state the same thing.
/// Nothing in QML computes it: PlotItem::yFraction() is what the ticks ask,
/// so the grid and the curve cannot disagree about where a value sits.
inline constexpr double kLogDecades = 6.0;

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
struct PlotLine {
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
};

/// How the shared x axis turns a position into a value.
struct PlotAxis {
    /// x = start + position * step.
    double start = 0.0;
    double step = 1.0;

    /// ...unless there is a time base, and then x = values[round(position)].
    ///
    /// Borrowed on the same terms as PlotLine::values. A time base has a value
    /// at each of its own positions and nowhere in between, so a point that
    /// falls between two of them takes the nearer; interpolating would invent
    /// an x, which is the same mistake as inventing a y.
    const double* values = nullptr;
    qsizetype count = 0;

    [[nodiscard]] bool explicitX() const { return values != nullptr && count > 0; }
};

/// The window being shown and the pane it is shown in.
struct PlotView {
    double xMin = 0.0;
    double xMax = 1.0;
    double yMin = 0.0;
    double yMax = 1.0;
    bool logY = false;

    /// The pane, in item coordinates.
    double width = 0.0;
    double height = 0.0;

    /// Most envelope columns one line may spend, or 0 for one per pixel.
    ///
    /// The envelope bounds each *line* by the pane's width, which is the whole
    /// point of it -- but it does not bound their sum, and ten thousand lines
    /// at two vertices a column is forty million vertices whatever the pane
    /// measures. PlotItem divides a fixed budget between the lines and passes
    /// the share down here. See kMaxVertices in PlotItem.cpp.
    int maxColumns = 0;
};

/// One unbroken stroke. A line with two gaps in it is three runs.
struct PlotRun {
    int first = 0;
    int count = 0;
};

/// What projecting one line produced.
struct PlotProjected {
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
/// both ask xOf() and both apply drawable() below.
[[nodiscard]] std::vector<QPointF> samplesOf(const PlotLine& line, const PlotAxis& axis);

/// Whether a value can be drawn on this view at all: finite, and above zero
/// when the axis is logarithmic.
[[nodiscard]] bool drawable(double value, bool logY);

/// Where `value` sits up the pane, as a fraction from the bottom.
///
/// The one piece of arithmetic the chrome and the curve must agree about. A
/// tick drawn at a fraction this function did not produce is a grid line that
/// lies about where the curve is, which is why PlotItem hands this to QML
/// rather than letting QML derive it.
[[nodiscard]] double yFractionOf(double value, const PlotView& view);

/// Project `line` into `points` in item coordinates, splitting the strokes at
/// gaps, and append each stroke to `runs`. Both vectors are appended to, so a
/// set of lines projects into one pair of buffers.
///
PlotProjected projectLine(const PlotLine& line, const PlotAxis& axis,
                          const PlotView& view, std::vector<QPointF>& points,
                          std::vector<PlotRun>& runs);

/// Expand `count` projected points into a triangle strip `width` pixels wide,
/// appending two vertices per station to `out`.
///
/// Triangles rather than a wide line because line width above 1.0 is an
/// optional RHI feature that several backends ignore without saying so, and
/// the highlight -- the one affordance for following a line through a bundle of
/// fifty -- is a line drawn at double width. A stroke that is sometimes two
/// pixels and sometimes one depending on the machine is not an affordance.
///
/// Joins are mitred, and the miter is cut at kMiterLimit so that the near
/// reversal an envelope makes at a one-sample spike does not throw a spear
/// across the pane. At a true reversal the two normals cancel and the join
/// falls back to a butt end, which at these widths is what a round join would
/// have drawn anyway.
void strokeRun(const QPointF* points, int count, double width,
               std::vector<QPointF>& out);

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
